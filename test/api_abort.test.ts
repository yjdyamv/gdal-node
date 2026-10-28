import * as gdal from 'gdal-node'
import * as path from 'path'
import { assert } from 'chai'

// A deterministic abort: an already aborted signal
const aborted = () => {
  const controller = new AbortController()
  controller.abort()
  return controller.signal
}

// Aborts an operation the first time it reports progress.  GDAL can only be
// interrupted from its progress function, which is where the AbortSignal flag is
// read, so the first notification is the earliest moment at which an abort can
// be requested, and this is what a program which lets its user press 'cancel'
// does.  In synchronous mode the flag is read again right after the callback
// returns, so the operation stops at that very notification; in asynchronous
// mode the notification travels to the main thread while the auxiliary thread
// keeps working, so the operation stops at the next checkpoint - which is why
// the operations below are big enough to report hundreds or thousands of
// notifications while they run.  Unlike a timer, none of this assumes anything
// about the speed of the machine
const abortAfterFirstProgress = (controller: AbortController): () => void => {
  let aborted = false
  return () => {
    if (aborted) return
    aborted = true
    controller.abort()
  }
}

// Waits for an operation which was supposed to be aborted: it has to reject with
// an AbortError, and nothing else.  The abort is requested from the progress
// callback of the operation itself, so there is no race to work around: an
// operation which succeeds here is an operation which ignored the abort
const assertAborted = async function (operation: Promise<unknown>): Promise<void> {
  const result = await operation.then(
    (value) => ({ value, error: null as Error | null }),
    (error: Error) => ({ value: null, error })
  )
  assert.isNotNull(result.error, 'should have been aborted')
  assert.equal((result.error as Error).name, 'AbortError')
}

describe('AbortController', () => {
  describe('synchronous operations', () => {
    let ds: gdal.Dataset

    beforeEach(() => {
      ds = gdal.open('/vsimem/abort_sync.tif', 'w', 'GTiff', 32, 32, 1)
    })
    afterEach(() => {
      ds.close()
      gdal.vsimem.release('/vsimem/abort_sync.tif')
      void global.gc!()
    })

    it('should throw when the signal is already aborted', () => {
      assert.throws(() => gdal.translate('/vsimem/abort_out.tif', ds, [], { abort_signal: aborted() }),
        'The operation was aborted')
    })

    it('should throw an AbortError', () => {
      try {
        gdal.translate('/vsimem/abort_out.tif', ds, [], { abort_signal: aborted() })
        assert.fail('should have thrown')
      } catch (e) {
        assert.instanceOf(e, Error)
        assert.equal((e as Error).name, 'AbortError')
        assert.equal((e as Error & { code: string }).code, 'ABORT_ERR')
      }
    })

    it('should throw an AbortError when aborted from its progress callback', () => {
      const big = gdal.open('/vsimem/abort_sync_big.tif', 'w', 'GTiff', 1024, 1024, 1)
      let calls = 0
      const controller = new AbortController()
      try {
        big.bands.get(1).pixels.write(0, 0, 1024, 1024, new Float32Array(1024 * 1024))
        gdal.translate('/vsimem/abort_out.tif', big, [ '-r', 'lanczos' ], {
          abort_signal: controller.signal,
          progress_cb: () => {
            if (calls++ === 0) controller.abort()
          }
        })
        assert.fail('should have thrown')
      } catch (e) {
        // Nothing can abort the signal from another task while the event loop is
        // blocked, but the progress callback runs on this very thread
        assert.equal((e as Error).name, 'AbortError')
        assert.isAbove(calls, 0, 'should have received progress before being aborted')
      } finally {
        big.close()
        gdal.vsimem.release('/vsimem/abort_sync_big.tif')
      }
    })

    it('should throw a TypeError on something which is not an AbortSignal', () => {
      // @ts-expect-error not an AbortSignal
      assert.throws(() => gdal.translate('/vsimem/abort_out.tif', ds, [], { abort_signal: {} }),
        TypeError, 'abort_signal must be an AbortSignal')
    })

    it('should not do anything when there is no abort_signal', () => {
      const out = gdal.translate('/vsimem/abort_out.tif', ds, [])
      assert.instanceOf(out, gdal.Dataset)
      out.close()
    })

    it('should not do anything when the signal is not aborted', () => {
      const controller = new AbortController()
      const out = gdal.translate('/vsimem/abort_out.tif', ds, [], { abort_signal: controller.signal })
      assert.instanceOf(out, gdal.Dataset)
      out.close()
    })
  })

  describe('asynchronous operations', () => {
    let ds: gdal.Dataset
    let src: gdal.Dataset

    before(() => {
      // Written and closed before it is read, so that the operations below read
      // it from the storage instead of from the block cache of the dataset which
      // wrote it: a translation of a cached raster reports next to no progress
      // and finishes before an asynchronous abort can be requested, which is
      // what made these tests depend on the speed of the machine.  A translation
      // of this one takes about 0.7 s and reports about 2000 notifications
      const write = gdal.open('/vsimem/abort_src.tif', 'w', 'GTiff', 2048, 2048, 1, gdal.GDT_Float32)
      const buf = new Float32Array(2048 * 2048)
      for (let i = 0; i < buf.length; i++) buf[i] = Math.sin(i * 0.001) * 100
      write.bands.get(1).pixels.write(0, 0, 2048, 2048, buf)
      write.close()
      src = gdal.open('/vsimem/abort_src.tif')
    })
    after(() => {
      src.close()
      gdal.vsimem.release('/vsimem/abort_src.tif')
      void global.gc!()
    })

    beforeEach(() => {
      ds = gdal.open('/vsimem/abort_async.tif', 'w', 'GTiff', 32, 32, 1)
    })
    afterEach(() => {
      ds.close()
      gdal.vsimem.release('/vsimem/abort_async.tif')
      void global.gc!()
    })

    it('should reject when the signal is already aborted', () =>
      assert.isRejected(gdal.translateAsync('/vsimem/abort_out.tif', ds, [], { abort_signal: aborted() }),
        'The operation was aborted'))

    it('should reject with an AbortError', async () => {
      try {
        await gdal.translateAsync('/vsimem/abort_out.tif', ds, [], { abort_signal: aborted() })
        assert.fail('should have rejected')
      } catch (e) {
        assert.equal((e as Error).name, 'AbortError')
        assert.equal((e as Error & { code: string }).code, 'ABORT_ERR')
      }
    })

    it('should reject when the operation is aborted while running', async () => {
      const controller = new AbortController()
      await assertAborted(gdal.translateAsync('/vsimem/abort_out.tif', src,
        [ '-r', 'lanczos', '-co', 'COMPRESS=DEFLATE' ], {
          abort_signal: controller.signal,
          progress_cb: abortAfterFirstProgress(controller)
        }))
    })

    it('should stop calling the progress callback after being aborted', async () => {
      const controller = new AbortController()
      const abort = abortAfterFirstProgress(controller)
      let progress = 0
      await assertAborted(gdal.translateAsync('/vsimem/abort_out.tif', src,
        [ '-r', 'lanczos', '-co', 'COMPRESS=DEFLATE' ], {
          abort_signal: controller.signal,
          progress_cb: () => {
            progress++
            abort()
          }
        }))
      // The abort is requested from the callback itself, so it has to have been
      // called at least once, otherwise this test would pass without testing
      // anything
      const calls = progress
      assert.isAbove(calls, 0, 'should have received progress before being aborted')
      // Let the notifications which were already on their way when the abort was
      // requested drain: the asynchronous dispatcher queues them for the main
      // thread, so a few of them are delivered after the request - but once the
      // operation has finished, there is nothing left to report
      await new Promise((resolve) => setImmediate(resolve))
      assert.equal(progress, calls, 'should not receive progress after being aborted')
    })

    it('should not do anything when the signal is not aborted', async () => {
      const controller = new AbortController()
      const out = await gdal.translateAsync('/vsimem/abort_out.tif', ds, [], { abort_signal: controller.signal })
      assert.instanceOf(out, gdal.Dataset)
      out.close()
    })

    it('should not do anything when the signal is aborted after the operation has finished', async () => {
      const controller = new AbortController()
      const out = await gdal.translateAsync('/vsimem/abort_out.tif', ds, [], { abort_signal: controller.signal })
      out.close()
      controller.abort()
      await new Promise((resolve) => setImmediate(resolve))
      assert.isTrue(controller.signal.aborted)
    })

    it('should work with the callback API', (done) => {
      const controller = new AbortController()
      gdal.translateAsync('/vsimem/abort_out.tif', ds, [], { abort_signal: controller.signal },
        (err: Error | null, out?: gdal.Dataset) => {
          try {
            assert.instanceOf(out, gdal.Dataset)
            assert.isNull(err)
            out!.close()
            controller.abort()
            done()
          } catch (e) {
            done(e as Error)
          }
        })
    })

    it('should abort the callback API', (done) => {
      gdal.translateAsync('/vsimem/abort_out.tif', ds, [], { abort_signal: aborted() },
        (err: Error | null, out?: gdal.Dataset) => {
          try {
            assert.isUndefined(out)
            assert.equal(err!.name, 'AbortError')
            done()
          } catch (e) {
            done(e as Error)
          }
        })
    })
  })

  describe('other operations', () => {
    it('should abort Dataset.buildOverviews', async () => {
      const ds = gdal.open('/vsimem/abort_ovr.tif', 'w', 'GTiff', 512, 512, 1)
      try {
        await assert.isRejected(ds.buildOverviewsAsync('NEAREST', [ 2, 4 ], [], { abort_signal: aborted() }),
          'The operation was aborted')
      } finally {
        ds.close()
        gdal.vsimem.release('/vsimem/abort_ovr.tif')
      }
    })

    it('should abort Driver.createCopy', async () => {
      const ds = gdal.open('/vsimem/abort_copy.tif', 'w', 'GTiff', 32, 32, 1)
      try {
        const driver = gdal.drivers.get('GTiff')
        await assert.isRejected(driver.createCopyAsync('/vsimem/abort_copy_out.tif', ds, [], false,
          { abort_signal: aborted() }), 'The operation was aborted')
      } finally {
        ds.close()
        gdal.vsimem.release('/vsimem/abort_copy.tif')
      }
    })

    it('should abort gdal.warp', async () => {
      const ds = gdal.open('/vsimem/abort_warp_src.tif', 'w', 'GTiff', 32, 32, 1)
      ds.geoTransform = [ 0, 1, 0, 0, 0, -1 ]
      ds.srs = gdal.SpatialReference.fromEPSG(4326)
      try {
        await assert.isRejected(gdal.warpAsync('/vsimem/abort_warp_out.tif', null, [ ds ], [], { abort_signal: aborted() }),
          'The operation was aborted')
      } finally {
        ds.close()
        gdal.vsimem.release('/vsimem/abort_warp_src.tif')
      }
    })

    it('should abort gdal.dem', async () => {
      const ds = gdal.open('/vsimem/abort_dem_src.tif', 'w', 'GTiff', 32, 32, 1)
      ds.bands.get(1).noDataValue = -9999
      try {
        await assert.isRejected(gdal.demAsync('/vsimem/abort_dem_out.tif', ds, 'hillshade', [], undefined, { abort_signal: aborted() }),
          'The operation was aborted')
      } finally {
        ds.close()
        gdal.vsimem.release('/vsimem/abort_dem_src.tif')
      }
    })
  })

  describe('RasterBandPixels', () => {
    let ds: gdal.Dataset
    let band: gdal.RasterBand
    const buf = new Float32Array(64 * 64)

    beforeEach(() => {
      ds = gdal.open('/vsimem/abort_pixels.tif', 'w', 'GTiff', 64, 64, 1)
      band = ds.bands.get(1)
    })
    afterEach(() => {
      ds.close()
      gdal.vsimem.release('/vsimem/abort_pixels.tif')
      void global.gc!()
    })

    it('should abort a synchronous read', () => {
      assert.throws(() => band.pixels.read(0, 0, 64, 64, buf, { abort_signal: aborted() }),
        'The operation was aborted')
    })

    it('should abort a synchronous write', () => {
      assert.throws(() => band.pixels.write(0, 0, 64, 64, buf, { abort_signal: aborted() }),
        'The operation was aborted')
    })

    it('should abort an asynchronous read', () =>
      assert.isRejected(band.pixels.readAsync(0, 0, 64, 64, undefined, { abort_signal: aborted() }),
        'The operation was aborted'))

    it('should abort an asynchronous write', () =>
      assert.isRejected(band.pixels.writeAsync(0, 0, 64, 64, buf, { abort_signal: aborted() }),
        'The operation was aborted'))

    // A synchronous read runs the progress callback inline, on the same thread
    // as the read itself, so an abort requested from it is seen by the check
    // which follows the callback - there is nothing to wait for and nothing to
    // race against.  The asynchronous case cannot be tested this way: there the
    // notification is queued for the main thread while the auxiliary thread
    // keeps reading, so the abort is only seen if the read has work left, and a
    // read is too short for that unless it is made artificially slow.  The
    // asynchronous abort is covered by the translations above, which report
    // thousands of notifications while they run
    it('should abort a synchronous read from its progress callback', () => {
      const controller = new AbortController()
      let progress = 0
      assert.throws(() => band.pixels.read(0, 0, 64, 64, new Uint8Array(64 * 64), {
        abort_signal: controller.signal,
        progress_cb: () => {
          progress++
          abortAfterFirstProgress(controller)()
        }
      }), 'The operation was aborted')
      // The read has to have reported progress, otherwise nothing was aborted
      assert.isAbove(progress, 0, 'should have received progress before being aborted')
    })

    it('should not do anything when the signal is not aborted', async () => {
      const controller = new AbortController()
      const data = await band.pixels.readAsync(0, 0, 64, 64, undefined, { abort_signal: controller.signal })
      assert.instanceOf(data, Uint8Array)
      assert.equal(data.length, 64 * 64)
    })

    it('should still work without any options', async () => {
      const data = await band.pixels.readAsync(0, 0, 64, 64)
      assert.instanceOf(data, Uint8Array)
      await band.pixels.writeAsync(0, 0, 64, 64, data)
      assert.isTrue(true)
    })
  })

  describe('MDArray', () => {
    let ds: gdal.Dataset
    let mdarray: gdal.MDArray

    before(function () {
      if (!gdal.MDArray) this.skip()
      ds = gdal.open(path.resolve(__dirname, 'data', 'gfs.t00z.alnsf.nc'), 'mr')
      mdarray = ds.root.arrays.get('alnsf')
    })
    after(() => {
      ds?.close()
      void global.gc!()
    })

    const options = { origin: [ 0, 7, 10 ], span: [ 1, 5, 5 ], data_type: gdal.GDT_Float32 }

    it('should abort a synchronous read', () => {
      assert.throws(() => mdarray.read({ ...options, abort_signal: aborted() }),
        'The operation was aborted')
    })

    it('should abort an asynchronous read', () =>
      assert.isRejected(mdarray.readAsync({ ...options, abort_signal: aborted() }),
        'The operation was aborted'))

    it('should not do anything when the signal is not aborted', () => {
      const controller = new AbortController()
      const data = mdarray.read({ ...options, abort_signal: controller.signal })
      assert.instanceOf(data, Float32Array)
    })
  })
})
