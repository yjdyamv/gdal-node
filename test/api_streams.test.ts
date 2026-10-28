import * as gdal from 'gdal-node'
import { Readable, Transform, finished as _finished } from 'stream'
import { promisify } from 'util'
import { assert } from 'chai'
import * as path from 'path'
import * as semver from 'semver'
const finished = promisify(_finished)

type doneCb = (err?: unknown) => void

describe('gdal.RasterReadStream', () => {
  function noDataTest(done: doneCb, file: string, convert?: boolean) {
    const ds = gdal.open(path.resolve(__dirname, 'data', file))
    const band = ds.bands.get(1)
    const noData = ds.bands.get(1).noDataValue
    let test: (v: number) => boolean
    if (convert) test = isNaN
    else test = (v) => v === noData
    const rs = band.pixels.createReadStream({
      convertNoData: convert,
      type: Float64Array
    })
    rs.once('data', (chunk) => {
      try {
        assert.instanceOf(chunk, Float64Array)
        assert.isTrue(chunk.subarray(0, 50).every(test))
        done()
      } catch (e) {
        done(e)
      }
    })
  }

  function readTest(done: doneCb, file: string, blockOptimize: boolean) {
    const ds = gdal.open(path.resolve(__dirname, 'data', file))
    const band = ds.bands.get(1)
    const expected = band.pixels.read(0, 0, band.size.x, band.size.y)
    const type = gdal.fromDataType(band.dataType)
    const actual = new type(band.size.x * band.size.y)

    const rs = band.pixels.createReadStream({ blockOptimize })
    assert.instanceOf(rs, gdal.RasterReadStream)
    let length = 0
    rs.on('data', (chunk) => {
      try {
        assert.instanceOf(chunk, type)
        actual.set(chunk, length)
        length += chunk.length
      } catch (e) {
        rs.removeAllListeners('data')
        rs.removeAllListeners('end')
        done(e)
      }
    })
    rs.on('end', () => {
      try {
        assert.equal(length, band.size.x * band.size.y)
        assert.deepEqual(actual, expected)
        done()
      } catch (e) {
        done(e)
      }
    })
  }

  const inputFiles = [
    'gfs.t00z.alnsf.nc',
    'h5ex_d_gzip.h5',
    'sample.tif',
    'a39se10.jpg'
  ]
  if (semver.gte(gdal.version, '3.0.0')) {
    inputFiles.push('gfs.t00z.pgrb2b.4p.f000.grb2')
    inputFiles.push('France_120km_WRF_WAM_210525-00.grb')
  }

  it('should accept a raster band', (done) => readTest(done, 'sample.tif', true))
  it('should accept a raster band w/o blockOptimize', (done) => readTest(done, 'sample.tif', false))
  it('should accept a raster band w/Float', (done) => readTest(done, 'AROME_T2m_10.tiff', true))
  it('should accept a raster band w/Float w/o blockOptimize', (done) => readTest(done, 'AROME_T2m_10.tiff', false))
  it('should support on the fly conversion w/ noData', (done) => noDataTest(done, 'dem_azimuth50_pa.img', undefined))
  it('should support noData conversion', (done) => noDataTest(done, 'dem_azimuth50_pa.img', true))
  for (const file of inputFiles) {
    it(`should accept various formats (${file})`, (done) => readTest(done, file, true))
  }

  it('should convert to the requested data type', async () => {
    const ds = gdal.open(path.resolve(__dirname, 'data', 'sample.tif'))
    const band = ds.bands.get(1)
    const size = band.size
    const expected = band.pixels.read(0, 0, size.x, size.y)
    // sample.tif has a single block per row, which means that it is read block
    // by block, and `readBlockAsync` refuses an array which is not of the data
    // type of the band, unlike `readAsync` which converts
    const chunks: Float32Array[] = []
    const rs = band.pixels.createReadStream({ type: Float32Array })
    for await (const chunk of rs as AsyncIterable<Float32Array>) {
      chunks.push(chunk)
    }
    ds.close()
    assert.isNotEmpty(chunks)
    const actual = new Float32Array(size.x * size.y)
    let length = 0
    for (const chunk of chunks) {
      assert.instanceOf(chunk, Float32Array)
      actual.set(chunk, length)
      length += chunk.length
    }
    assert.equal(length, size.x * size.y)
    assert.deepEqual(actual, Float32Array.from(expected))
  })

  it('should convert the 64 bit integers', async () => {
    const filename = `/vsimem/ds_rs_int64.${String(Math.random()).substring(2)}.tiff`
    const writeDs = gdal.open(filename, 'w', 'GTiff', 4, 4, 1, gdal.GDT_Int64)
    writeDs.bands.get(1).pixels.write(0, 0, 4, 4, new BigInt64Array(16).fill(7n))
    writeDs.close()

    const ds = gdal.open(filename)
    const band = ds.bands.get(1)
    // both paths are checked: a `BigInt64Array` cannot be converted with `set`
    // into another type of array, so the conversion is left to GDAL
    const readAll = async (blockOptimize: boolean): Promise<Float64Array> => {
      const chunks: Float64Array[] = []
      const rs = band.pixels.createReadStream({ type: Float64Array, blockOptimize })
      for await (const chunk of rs as AsyncIterable<Float64Array>) {
        chunks.push(chunk)
      }
      const actual = new Float64Array(16)
      let length = 0
      for (const chunk of chunks) {
        assert.instanceOf(chunk, Float64Array)
        actual.set(chunk, length)
        length += chunk.length
      }
      assert.equal(length, 16)
      return actual
    }
    const expected = new Float64Array(16).fill(7)
    assert.deepEqual(await readAll(true), expected)
    assert.deepEqual(await readAll(false), expected)
    ds.close()
    gdal.vsimem.release(filename)
  })

  it('should throw with invalid arguments', () => {
    const ds = gdal.open(path.resolve(__dirname, 'data', 'sample.tif'))
    const band = ds.bands.get(1)
    const construct = (options: unknown) => new gdal.RasterReadStream(options as gdal.RasterReadableOptions)
    assert.throws(() => construct({}), /"band" must be a gdal.RasterBand/)
    assert.throws(() => construct({ band: {} }), /"band" must be a gdal.RasterBand/)
    assert.throws(() => construct({ band, type: 5 }), /"type" must be a constructor/)
    ds.close()
  })
})

describe('gdal.RasterWriteStream', () => {
  function writeTest(done: doneCb, w: number, h: number, len: number, blockSize: number, blockOptimize: boolean, convertNoData?: boolean) {
    const filename = `/vsimem/ds_ws_test.${String(
      Math.random()
    ).substring(2)}.tmp.tiff`
    const ds = gdal.open(filename, 'w', 'GTiff', w, h, 1, gdal.GDT_Float64, { BLOCKXSIZE: w, BLOCKYSIZE: blockSize })
    const band = ds.bands.get(1)
    if (convertNoData) band.noDataValue = 1e38
    const ws = band.pixels.createWriteStream({ blockOptimize, convertNoData })
    const pattern = new Float64Array(len)
    for (let i = 0; i < len; i++) {
      if (i % 10 == 0 && convertNoData) pattern[i] = NaN
      else pattern[i] = i
    }

    let written = 0

    // this is the callback for the final write
    function endCheck() {
      try {
        ds.flush()
        ds.close()
        const testDS = gdal.open(filename)
        const testData = testDS.bands.get(1).pixels.read(0, 0, w, h)
        if (convertNoData) {
          for (let i = 0; i < pattern.length; i++) if (isNaN(pattern[i])) pattern[i] = 1e38
        }
        for (let i = 0; i < w*h; i += len) {
          assert.deepEqual(pattern, testData.subarray(i, i + len))
        }
        testDS.close()
        gdal.vsimem.release(filename)
        done()
      } catch (e) {
        // mocha has a complicated relationship with async exceptions
        // we have to explicitly signal errors
        done(e)
      }
    }

    function write() {
      while (written < w*h) {
        written += pattern.length
        // write until finished or write returns false because the buffer is full
        if (!ws.write(pattern, written == w*h ? endCheck : undefined)) break
      }
    }

    // continue writing if we had to stop because the buffer was full
    ws.on('drain', write)

    // mocha can't catch this
    ws.on('error', (e) => {
      done(e)
    })

    // start writing
    write()
  }

  function writeTestOverflow(done: doneCb, w: number, h: number, len: number, blockSize: number, blockOptimize: boolean) {
    const doneInverted = (err: Error) => {
      if (!err) done('did not throw')
      else if (err.toString().match(/beyond the end/)) done()
      else done(err)
    }

    writeTest(doneInverted as doneCb, w, h, len, blockSize, blockOptimize, undefined)
  }

  it('should write a raster band in zero-copy mode w/ edge block',
    (done) => writeTest(done, 801, 600, 1602, 2, true, undefined))
  it('should write a raster band in zero-copy mode w/o edge block',
    (done) => writeTest(done, 801, 601, 1803, 3, true, undefined))
  it('should write a raster band in block consolidation mode w/ big chunks w/ edge block',
    (done) => writeTest(done, 801, 601, 1803, 2, true, undefined))
  it('should write a raster band in block consolidation mode w/ small chunks w/ edge block',
    (done) => writeTest(done, 801, 601, 267, 2, true, undefined))
  it('should write a raster band in block consolidation mode w/ big chunks w/o edge block',
    (done) => writeTest(done, 801, 601, 1803, 6, true, undefined))
  it('should write a raster band in block consolidation mode w/ small chunks w/o edge block',
    (done) => writeTest(done, 801, 601, 267, 6, true, undefined))
  it('should write a raster band in line mode w/ big chunks',
    (done) => writeTest(done, 801, 601, 1803, 2, false, undefined))
  it('should write a raster band in line mode w/ small chunks',
    (done) => writeTest(done, 801, 601, 267, 2, false, undefined))

  it('should throw on writing beyond the end in zero-copy mode w/ edge block',
    (done) => writeTestOverflow(done, 801, 601, 1804, 2, true))
  it('should throw on writing beyond the end in zero-copy mode w/o edge block',
    (done) => writeTestOverflow(done, 801, 601, 1804, 3, true))
  it('should throw on writing beyond the end in block consolidation mode w/ big chunks w/ edge block',
    (done) => writeTestOverflow(done, 801, 601, 1804, 2, true))
  it('should throw on writing beyond the end in block consolidation mode w/ small chunks w/ edge block',
    (done) => writeTestOverflow(done, 801, 601, 268, 2, true))
  it('should throw on writing beyond the end in block consolidation mode w/ big chunks w/o edge block',
    (done) => writeTestOverflow(done, 801, 601, 1804, 6, true))
  it('should throw on writing beyond the end in block consolidation mode w/ small chunks w/o edge block',
    (done) => writeTestOverflow(done, 801, 601, 268, 6, true))
  it('should throw on writing beyond the end in line mode w/ big chunks',
    (done) => writeTestOverflow(done, 801, 601, 1804, 2, false))
  it('should throw on writing beyond the end in line mode w/ small chunks',
    (done) => writeTestOverflow(done, 801, 601, 268, 2, false))

  it('should support noData conversion', (done) => writeTest(done, 801, 601, 1803, 2, true, true))

  it('should support an ill-behaved user application', (done) => {
    const filename = `/vsimem/ds_pressure_test.${String(
      Math.random()
    ).substring(2)}.tmp.tiff`
    const ds = gdal.open(filename, 'w', 'GTiff', 64, 128, 1, gdal.GDT_Float64)
    const band = ds.bands.get(1)
    const ws = band.pixels.createWriteStream()
    const data = new Float64Array(64)
    for (let i = 0; i < 64; i++) data[i] = i
    for (let i = 0; i < 127; i++) ws.write(data)
    ws.write(data, () => {
      try {
        ds.flush()
        ds.close()
        const testDS = gdal.open(filename)
        const testData = testDS.bands.get(1).pixels.read(0, 0, 64, 128)
        for (let i = 0; i < 128; i += 128) {
          assert.deepEqual(data, testData.subarray(i * 64, (i+1) * 64))
        }
        testDS.close()
        gdal.vsimem.release(filename)
        done()
      } catch (e) {
        done(e)
      }
    })
  })

  it('should throw with an invalid band', () => {
    const construct = (options: unknown) => new gdal.RasterWriteStream(options as gdal.RasterWritableOptions)
    assert.throws(() => construct({}), /"band" must be a gdal.RasterBand/)
    assert.throws(() => construct({ band: {} }), /"band" must be a gdal.RasterBand/)
  })

  it('should fail on a write which is not a TypedArray', async () => {
    const filename = `/vsimem/ds_ws_invalid.${String(
      Math.random()
    ).substring(2)}.tmp.tiff`
    const ds = gdal.open(filename, 'w', 'GTiff', 64, 64, 1, gdal.GDT_Float64)
    const ws = ds.bands.get(1).pixels.createWriteStream()
    ws.write({} as unknown as Float64Array)
    const error = await finished(ws).then(() => undefined, (e: unknown) => e)
    assert.instanceOf(error, Error)
    assert.match((error as Error).message, /Only TypedArrays are supported/)
    ds.close()
    gdal.vsimem.release(filename)
  })

  it('should fail on mixing the data types', async () => {
    const filename = `/vsimem/ds_ws_mixing.${String(
      Math.random()
    ).substring(2)}.tmp.tiff`
    const ds = gdal.open(filename, 'w', 'GTiff', 64, 64, 1, gdal.GDT_Float64)
    const ws = ds.bands.get(1).pixels.createWriteStream()
    ws.write(new Float64Array(64))
    ws.write(new Float32Array(64))
    const error = await finished(ws).then(() => undefined, (e: unknown) => e)
    assert.instanceOf(error, Error)
    assert.match((error as Error).message, /Type mixing is not supported/)
    ds.close()
    gdal.vsimem.release(filename)
  })

  it('should fail when the stream ends with pending data', async () => {
    const filename = `/vsimem/ds_ws_pending.${String(
      Math.random()
    ).substring(2)}.tmp.tiff`
    const ds = gdal.open(filename, 'w', 'GTiff', 64, 64, 1, gdal.GDT_Float64)
    const ws = ds.bands.get(1).pixels.createWriteStream()
    ws.write(new Float64Array(64))
    ws.end()
    const error = await finished(ws).then(() => undefined, (e: unknown) => e)
    assert.instanceOf(error, Error)
    assert.match((error as Error).message, /Stream finished with pending data/)
    ds.close()
    gdal.vsimem.release(filename)
  })

  it('should fail when the stream ends before filling the raster', async () => {
    const filename = `/vsimem/ds_ws_filling.${String(
      Math.random()
    ).substring(2)}.tmp.tiff`
    const ds = gdal.open(filename, 'w', 'GTiff', 64, 128, 1, gdal.GDT_Float64, { BLOCKXSIZE: 64, BLOCKYSIZE: 64 })
    const ws = ds.bands.get(1).pixels.createWriteStream()
    // one whole block of two is written, so nothing is left pending
    ws.write(new Float64Array(64 * 64))
    ws.end()
    const error = await finished(ws).then(() => undefined, (e: unknown) => e)
    assert.instanceOf(error, Error)
    assert.match((error as Error).message, /Stream finished before filling the raster/)
    ds.close()
    gdal.vsimem.release(filename)
  })

  it('should fail when the dataset is opened in read-only mode', async () => {
    const ds = gdal.open(path.resolve(__dirname, 'data', 'sample.tif'))
    const band = ds.bands.get(1)
    const size = band.size
    const ws = band.pixels.createWriteStream()
    ws.write(new Uint8Array(size.x * size.y))
    ws.end()
    const error = await finished(ws).then(() => undefined, (e: unknown) => e)
    assert.instanceOf(error, Error)
    assert.match((error as Error).message, /read-only mode/)
    ds.close()
  })

  it('should propagate an exception thrown by the callback of the user', async () => {
    const filename = `/vsimem/ds_ws_throw.${String(
      Math.random()
    ).substring(2)}.tmp.tiff`
    const ds = gdal.open(filename, 'w', 'GTiff', 64, 64, 1, gdal.GDT_Float64)
    const ws = ds.bands.get(1).pixels.createWriteStream()
    ws.write(new Float64Array(64 * 64), () => {
      throw new Error('stir the pot')
    })
    const error = await finished(ws).then(() => undefined, (e: unknown) => e)
    assert.instanceOf(error, Error)
    assert.match((error as Error).message, /stir the pot/)
    ds.close()
    gdal.vsimem.release(filename)
  })
})

describe('gdal.RasterReadStream + gdal.RasterWriteStream', () => {
  it('should support piping', () => {
    const dsIn = gdal.open(path.resolve(__dirname, 'data', 'AROME_T2m_10.tiff'))
    const filename = `/vsimem/ds_pipe_test.${String(
      Math.random()
    ).substring(2)}.tmp.tiff`
    const dsOut = gdal.open(filename, 'w', 'GTiff', dsIn.rasterSize.x, dsIn.rasterSize.y, 1, gdal.GDT_Float64)
    const bandIn = dsIn.bands.get(1)
    const bandOut = dsOut.bands.get(1)
    const rs = bandIn.pixels.createReadStream()
    const ws = bandOut.pixels.createWriteStream()

    rs.pipe(ws)
    return assert.isFulfilled(finished(ws).then(() => {
      dsOut.close()
      const dataOrig = bandIn.pixels.read(0, 0, bandIn.size.x, bandIn.size.y)

      const dsTest = gdal.open(filename)
      const dataTest = dsTest.bands.get(1).pixels.read(0, 0, bandIn.size.x, bandIn.size.y)
      assert.deepEqual(dataOrig, dataTest)
      dsTest.close()
      gdal.vsimem.release(filename)
    }))
  })
  it('should support transforms', () => {
    const dsIn = gdal.open(path.resolve(__dirname, 'data', 'AROME_T2m_10.tiff'))
    const filename = `/vsimem/ds_transform_test.${String(
      Math.random()
    ).substring(2)}.tmp.tiff`
    const dsOut = gdal.open(filename, 'w', 'GTiff', dsIn.rasterSize.x, dsIn.rasterSize.y, 1, gdal.GDT_Float64)
    const bandIn = dsIn.bands.get(1)
    const bandOut = dsOut.bands.get(1)
    const rs = bandIn.pixels.createReadStream()
    const ws = bandOut.pixels.createWriteStream()

    const scaleTransform = new Transform({
      objectMode: true,
      transform(chunk, _, cb) {
        for (let i = 0; i < chunk.length; i++) {
          chunk[i] = Math.min(chunk[i], 6000) / 250
        }
        cb(null, chunk)
      }
    })

    rs.pipe(scaleTransform).pipe(ws)
    return assert.isFulfilled(finished(ws).then(() => {
      dsOut.close()

      const dataOrig = bandIn.pixels.read(0, 0, bandIn.size.x, bandIn.size.y)
      for (let i = 0; i < dataOrig.length; i++) {
        dataOrig[i] = Math.min(dataOrig[i], 6000) / 250
      }

      const dsTest = gdal.open(filename)
      const dataTest = dsTest.bands.get(1).pixels.read(0, 0, bandIn.size.x, bandIn.size.y)
      assert.deepEqual(dataOrig, dataTest)
      dsTest.close()
      gdal.vsimem.release(filename)
    }))
  })
})

describe('gdal.RasterMuxStream', () => {
  function testMux(blockOptimize?: boolean) {
    const dsT2m = gdal.open(path.resolve(__dirname, 'data', 'AROME_T2m_10.tiff'))
    const dsD2m = gdal.open(path.resolve(__dirname, 'data', 'AROME_D2m_10.tiff'))

    const filename = `/vsimem/ds_mux_test.${String(
      Math.random()
    ).substring(2)}.tmp.tiff`
    const dsCloudBase = gdal.open(filename, 'w', 'GTiff', dsT2m.rasterSize.x, dsD2m.rasterSize.y, 1, gdal.GDT_Float64)

    const mux = new gdal.RasterMuxStream({
      T2m: dsT2m.bands.get(1).pixels.createReadStream(),
      D2m: dsD2m.bands.get(1).pixels.createReadStream()
    }, { blockOptimize })

    const ws = dsCloudBase.bands.get(1).pixels.createWriteStream()

    // Espy's estimation for cloud base height (lifted condensation level)
    // LCL = 125 * (T2m - Td2m)
    // where T2m is the temperature at 2m and Td2m is the dew point at 2m
    const fn = (t: number, td: number) => 125 * (t - td)
    const espyEstimation = new gdal.RasterTransform({ type: Float64Array, fn })

    mux.pipe(espyEstimation).pipe(ws)
    return assert.isFulfilled(finished(ws).then(() => {
      dsCloudBase.close()

      const dataOrigT2m = dsT2m.bands.get(1).pixels.read(0, 0, dsT2m.rasterSize.x, dsT2m.rasterSize.y)
      const dataOrigD2m = dsD2m.bands.get(1).pixels.read(0, 0, dsD2m.rasterSize.x, dsD2m.rasterSize.y)
      const dataCloudBase = new Float64Array(dsD2m.rasterSize.x * dsD2m.rasterSize.y)
      for (let i = 0; i < dataOrigT2m.length; i++) {
        dataCloudBase[i] = 125 * (dataOrigT2m[i] - dataOrigD2m[i])
      }

      const dsTest = gdal.open(filename)
      const dataTest = dsTest.bands.get(1).pixels.read(0, 0, dsTest.rasterSize.x, dsTest.rasterSize.y)
      assert.deepEqual(dataCloudBase, dataTest)
      dsTest.close()
      gdal.vsimem.release(filename)
    }))
  }

  // the mux only needs streams, but the typings ask for `RasterReadStream`,
  // and the input does not have to come from a raster
  function mockInput(...chunks: Float64Array[]): gdal.RasterReadStream {
    const input = new Readable({ objectMode: true, read: () => undefined })
    for (const chunk of chunks) input.push(chunk)
    input.push(null)
    return input as unknown as gdal.RasterReadStream
  }

  async function drain(stream: AsyncIterable<unknown>): Promise<void> {
    for await (const chunk of stream) {
      // nothing, the point is to consume the stream
      void chunk
    }
  }

  it('should accept multiple inputs', () => testMux(undefined))
  it('should support different block sizes', () => testMux(false))

  // the mux synchronizes its inputs on the largest buffer which all of them can
  // fill, which means that the buffers which are smaller than that have to be
  // consolidated, and this is what an input which is not read in one piece
  // produces
  it('should consolidate the buffers which are smaller than the output block', async () => {
    const mux = new gdal.RasterMuxStream({
      A: mockInput(
        new Float64Array(500).fill(1),
        new Float64Array(500).fill(2),
        new Float64Array(500).fill(3)),
      B: mockInput(new Float64Array(1500).fill(9))
    }, { blockOptimize: false })
    const chunks: Record<string, Float64Array>[] = []
    for await (const chunk of mux as AsyncIterable<Record<string, Float64Array>>) {
      chunks.push(chunk)
    }
    assert.lengthOf(chunks, 1)
    assert.deepEqual(chunks[0].A, Float64Array.from({ length: 1500 }, (_, i) => Math.floor(i / 500) + 1))
    assert.deepEqual(chunks[0].B, new Float64Array(1500).fill(9))
  })

  it('should fail when an input ends before the others', async () => {
    const mux = new gdal.RasterMuxStream({
      short: mockInput(new Float64Array(10)),
      long: mockInput(new Float64Array(20))
    })
    const error = await drain(mux as AsyncIterable<unknown>).then(() => undefined, (e: unknown) => e)
    assert.instanceOf(error, Error)
    assert.match((error as Error).message, /premature end on short/)
  })
})
