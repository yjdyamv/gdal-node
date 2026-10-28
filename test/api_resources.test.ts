import * as gdal from 'gdal-async'
import { assert } from 'chai'

interface Failure extends Error {
  suppressed?: Error
}

describe('the dataset scopes', () => {
  const opened = (ds: gdal.Dataset) => () => ds.rasterSize
  const temporary = (name: string) => `/vsimem/scope_${name}.${String(Math.random()).substring(2)}.tif`
  const write = (ds: gdal.Dataset, value: number) => {
    ds.bands.get(1).pixels.write(0, 0, 4, 4, new Uint8Array(16).fill(value))
  }
  const read = (ds: gdal.Dataset) =>
    Array.from(ds.bands.get(1).pixels.read(0, 0, 4, 4) as Uint8Array)

  describe('withDataset()', () => {
    it('should return what the callback returned', () => {
      const count = gdal.withDataset(`${__dirname}/data/sample.tif`, 'r', (ds) => ds.bands.count())
      assert.equal(count, 1)
    })
    it('should close the dataset when the callback returns', () => {
      let ds: gdal.Dataset
      gdal.withDataset(`${__dirname}/data/sample.tif`, 'r', (opened) => {
        ds = opened
        assert.isOk(opened.rasterSize)
      })
      assert.throws(opened(ds!), /already been destroyed/)
    })
    it('should write the data before it closes the dataset', () => {
      const file = temporary('with')
      gdal.withDataset([ file, 'w', 'GTiff', 4, 4, 1, gdal.GDT_Byte ], (ds) => write(ds, 7))
      const reopened = gdal.open(file)
      const pixels = read(reopened)
      reopened.close()
      assert.deepEqual(pixels, new Array(16).fill(7))
    })
    it('should leave the dataset which the callback returned open', () => {
      const ds = gdal.withDataset(`${__dirname}/data/sample.tif`, 'r', (ds) => ds)
      assert.isOk(ds.rasterSize)
      ds.close()
    })
    it('should close the dataset and rethrow when the callback throws', () => {
      let ds: gdal.Dataset
      assert.throws(() => {
        gdal.withDataset(`${__dirname}/data/sample.tif`, 'r', (opened) => {
          ds = opened
          throw new Error('the callback failed')
        })
      }, /the callback failed/)
      assert.throws(opened(ds!), /already been destroyed/)
    })
    it('should not close a dataset which the callback has already closed', () => {
      const result = gdal.withDataset(`${__dirname}/data/sample.tif`, 'r', (ds) => {
        ds.close()
        return 'done'
      })
      assert.equal(result, 'done')
    })
    it('should close the dataset when the promise of the callback settles', async () => {
      let ds: gdal.Dataset
      const value = await gdal.withDataset(`${__dirname}/data/sample.tif`, 'r', async (opened) => {
        ds = opened
        return 'done'
      })
      assert.equal(value, 'done')
      assert.throws(opened(ds!), /already been destroyed/)
    })
    it('should close the dataset when the promise of the callback rejects', async () => {
      let ds: gdal.Dataset
      let error: Error
      await gdal.withDataset(`${__dirname}/data/sample.tif`, 'r', async (opened) => {
        ds = opened
        throw new Error('the callback failed')
      }).catch((e: Error) => {
        error = e
      })
      assert.equal(error!.message, 'the callback failed')
      assert.throws(opened(ds!), /already been destroyed/)
    })
    it('should keep the error of the callback when the dataset does not close', () => {
      const close = gdal.Dataset.prototype.close
      gdal.Dataset.prototype.close = () => {
        throw new Error('the dataset did not close')
      }
      try {
        let error: Failure | undefined
        try {
          gdal.withDataset(`${__dirname}/data/sample.tif`, 'r', () => {
            throw new Error('the callback failed')
          })
        } catch (e) {
          error = e as Failure
        }
        assert.equal(error?.message, 'the callback failed')
        assert.equal(error?.suppressed?.message, 'the dataset did not close')
      } finally {
        gdal.Dataset.prototype.close = close
      }
    })
    it('should keep the error of the callback when it cannot be extended', () => {
      const close = gdal.Dataset.prototype.close
      gdal.Dataset.prototype.close = () => {
        throw new Error('the dataset did not close')
      }
      try {
        let error: Failure | undefined
        try {
          gdal.withDataset(`${__dirname}/data/sample.tif`, 'r', () => {
            throw Object.freeze(new Error('the callback failed'))
          })
        } catch (e) {
          error = e as Failure
        }
        assert.equal(error?.message, 'the callback failed')
        assert.isUndefined(error?.suppressed)
      } finally {
        gdal.Dataset.prototype.close = close
      }
    })
    it('should throw if the callback is not a function', () => {
      assert.throws(
        () => (gdal.withDataset as unknown as (...args: unknown[]) => unknown)(`${__dirname}/data/sample.tif`, 'r', 'not a function'),
        /callback must be a function/
      )
    })
  })
  describe('withDatasetAsync()', () => {
    it('should return what the callback resolved with', async () => {
      const count = await gdal.withDatasetAsync(
        `${__dirname}/data/sample.tif`, 'r', (ds) => ds.bands.countAsync()
      )
      assert.equal(count, 1)
    })
    it('should close the dataset when the callback returns', async () => {
      let ds: gdal.Dataset
      await gdal.withDatasetAsync(`${__dirname}/data/sample.tif`, 'r', (opened) => {
        ds = opened
        assert.isOk(opened.rasterSize)
      })
      assert.throws(opened(ds!), /already been destroyed/)
    })
    it('should write the data before it closes the dataset', async () => {
      const file = temporary('with_async')
      await gdal.withDatasetAsync([ file, 'w', 'GTiff', 4, 4, 1, gdal.GDT_Byte ], (ds) => write(ds, 9))
      const reopened = gdal.open(file)
      const pixels = read(reopened)
      reopened.close()
      assert.deepEqual(pixels, new Array(16).fill(9))
    })
    it('should leave the dataset which the callback returned open', async () => {
      const ds = await gdal.withDatasetAsync(`${__dirname}/data/sample.tif`, 'r', (ds) => ds)
      assert.isOk(ds.rasterSize)
      ds.close()
    })
    it('should close the dataset and rethrow when the callback rejects', async () => {
      let ds: gdal.Dataset
      await assert.isRejected(
        gdal.withDatasetAsync(`${__dirname}/data/sample.tif`, 'r', (opened) => {
          ds = opened
          throw new Error('the callback failed')
        }),
        /the callback failed/
      )
      assert.throws(opened(ds!), /already been destroyed/)
    })
    it('should not close a dataset which the callback has already closed', async () => {
      const result = await gdal.withDatasetAsync(`${__dirname}/data/sample.tif`, 'r', (ds) => {
        ds.close()
        return 'done'
      })
      assert.equal(result, 'done')
    })
    it('should keep the error of the callback when the dataset does not close', async () => {
      const closeAsync = gdal.Dataset.prototype.closeAsync
      gdal.Dataset.prototype.closeAsync = () => Promise.reject(new Error('the dataset did not close'))
      try {
        let error: Failure | undefined
        await gdal.withDatasetAsync(`${__dirname}/data/sample.tif`, 'r', () => {
          throw new Error('the callback failed')
        }).catch((e: Error) => {
          error = e as Failure
        })
        assert.equal(error?.message, 'the callback failed')
        assert.equal(error?.suppressed?.message, 'the dataset did not close')
      } finally {
        gdal.Dataset.prototype.closeAsync = closeAsync
      }
    })
    it('should reject if the callback is not a function', async () => {
      await assert.isRejected(
        (gdal.withDatasetAsync as unknown as (...args: unknown[]) => Promise<unknown>)(`${__dirname}/data/sample.tif`, 'r', 'not a function'),
        /callback must be a function/
      )
    })
  })
})
