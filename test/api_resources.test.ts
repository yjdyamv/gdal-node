import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'
import * as cp from 'node:child_process'
import * as gdal from 'gdal-async'
import { assert } from 'chai'

interface Failure extends Error {
  suppressed?: Error
}

describe('the dataset scopes', () => {
  const sample = `${__dirname}/data/sample.tif`
  const multiband = `${__dirname}/data/multiband.tif`
  const opened = (ds: gdal.Dataset) => () => ds.rasterSize
  const opened2 = (ds: gdal.Dataset) => () => ds.rasterSize
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
  describe('using()', () => {
    it('should close the datasets which the callback opened', () => {
      const opened: gdal.Dataset[] = []
      const helper = () => {
        const ds = gdal.open(sample)
        opened.push(ds)
        return ds
      }
      const count = gdal.using((scope) => {
        const ds = gdal.open(sample)
        opened.push(ds)
        helper()
        assert.equal(scope.size, 2)
        return ds.bands.count()
      })
      assert.equal(count, 1)
      for (const ds of opened) assert.throws(opened2(ds), /already been destroyed/)
    })
    it('should close the datasets which were opened after an await', async () => {
      let ds: gdal.Dataset
      const value = await gdal.using(async () => {
        await new Promise((resolve) => setTimeout(resolve, 1))
        ds = gdal.open(sample)
        return 'done'
      })
      assert.equal(value, 'done')
      assert.throws(opened2(ds!), /already been destroyed/)
    })
    it('should close the datasets in the reverse order of their opening', () => {
      const order: string[] = []
      const close = gdal.Dataset.prototype.close
      gdal.Dataset.prototype.close = function () {
        order.push(this.description)
        return close.call(this)
      }
      try {
        gdal.using(() => {
          gdal.open(sample)
          gdal.open(multiband)
        })
      } finally {
        gdal.Dataset.prototype.close = close
      }
      assert.deepEqual(order, [ multiband, sample ])
    })
    it('should leave the dataset which the callback returned open', () => {
      const ds = gdal.using(() => gdal.open(sample))
      assert.isOk(ds.rasterSize)
      ds.close()
    })
    it('should keep a dataset out of the scope', () => {
      const kept = gdal.using((scope) => {
        const ds = gdal.open(sample)
        scope.keep(ds)
        assert.equal(scope.size, 0)
        return ds
      })
      assert.isOk(kept.rasterSize)
      kept.close()
    })
    it('should close a dataset before the end of the scope', () => {
      gdal.using((scope) => {
        const ds = gdal.open(sample)
        gdal.open(multiband)
        assert.equal(scope.size, 2)
        scope.close(ds)
        assert.equal(scope.size, 1)
        assert.throws(opened2(ds), /already been destroyed/)
      })
    })
    it('should not touch a dataset which was opened outside the callback', () => {
      const outer = gdal.open(sample)
      gdal.using(() => gdal.open(multiband))
      assert.isOk(outer.rasterSize)
      outer.close()
    })
    it('should close the datasets and rethrow when the callback throws', () => {
      let ds: gdal.Dataset
      assert.throws(() => {
        gdal.using(() => {
          ds = gdal.open(sample)
          throw new Error('the callback failed')
        })
      }, /the callback failed/)
      assert.throws(opened2(ds!), /already been destroyed/)
    })
    it('should attach a failure of a close to the failure of the callback', () => {
      const close = gdal.Dataset.prototype.close
      gdal.Dataset.prototype.close = () => {
        throw new Error('the dataset did not close')
      }
      try {
        let error: Failure | undefined
        try {
          gdal.using(() => {
            gdal.open(sample)
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
    it('should throw the failure of a close when the callback succeeded', () => {
      const close = gdal.Dataset.prototype.close
      gdal.Dataset.prototype.close = () => {
        throw new Error('the dataset did not close')
      }
      try {
        assert.throws(() => gdal.using(() => {
          gdal.open(sample)
          return 'done'
        }), /the dataset did not close/)
      } finally {
        gdal.Dataset.prototype.close = close
      }
    })
    it('should report every failure of a close', () => {
      const close = gdal.Dataset.prototype.close
      gdal.Dataset.prototype.close = () => {
        throw new Error('the dataset did not close')
      }
      try {
        let error: Error | undefined
        try {
          gdal.using(() => {
            gdal.open(sample)
            gdal.open(multiband)
            return 'done'
          })
        } catch (e) {
          error = e as Error
        }
        assert.equal(error?.name, 'AggregateError')
        assert.equal(error?.message, 'failed to close 2 datasets')
      } finally {
        gdal.Dataset.prototype.close = close
      }
    })
    it('should close the datasets when the promise of the callback rejects', async () => {
      let ds: gdal.Dataset
      await assert.isRejected(
        gdal.using(async () => {
          ds = gdal.open(sample)
          throw new Error('the callback failed')
        }),
        /the callback failed/
      )
      assert.throws(opened2(ds!), /already been destroyed/)
    })
    it('should throw if the callback is not a function', () => {
      assert.throws(
        () => (gdal.using as unknown as (...args: unknown[]) => unknown)('not a function'),
        /callback must be a function/
      )
    })
  })
  describe('usingAsync()', () => {
    it('should close the datasets which the callback opened', async () => {
      let ds: gdal.Dataset
      const count = await gdal.usingAsync(async (scope) => {
        await new Promise((resolve) => setTimeout(resolve, 1))
        ds = gdal.open(sample)
        assert.equal(scope.size, 1)
        return ds.bands.count()
      })
      assert.equal(count, 1)
      assert.throws(opened2(ds!), /already been destroyed/)
    })
    it('should close them with closeAsync()', async () => {
      const close = gdal.Dataset.prototype.close
      const closeAsync = gdal.Dataset.prototype.closeAsync
      let synchronous = 0
      let asynchronous = 0
      gdal.Dataset.prototype.close = () => {
        synchronous++
      }
      gdal.Dataset.prototype.closeAsync = async function () {
        asynchronous++
        close.call(this)
      }
      try {
        await gdal.usingAsync(async () => {
          await gdal.openAsync(sample)
        })
      } finally {
        gdal.Dataset.prototype.close = close
        gdal.Dataset.prototype.closeAsync = closeAsync
      }
      assert.equal(asynchronous, 1)
      assert.equal(synchronous, 0)
    })
    it('should leave the dataset which the callback returned open', async () => {
      const ds = await gdal.usingAsync(() => gdal.openAsync(sample))
      assert.isOk(ds.rasterSize)
      await ds.closeAsync()
    })
    it('should close the datasets and rethrow when the callback rejects', async () => {
      let ds: gdal.Dataset
      await assert.isRejected(
        gdal.usingAsync(async () => {
          ds = await gdal.openAsync(sample)
          throw new Error('the callback failed')
        }),
        /the callback failed/
      )
      assert.throws(opened2(ds!), /already been destroyed/)
    })
    it('should keep the error of the callback when the dataset does not close', async () => {
      const closeAsync = gdal.Dataset.prototype.closeAsync
      gdal.Dataset.prototype.closeAsync = () => Promise.reject(new Error('the dataset did not close'))
      try {
        let error: Failure | undefined
        await gdal.usingAsync(async () => {
          await gdal.openAsync(sample)
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
    it('should track the dataset which the callback form opens', async () => {
      await gdal.usingAsync(async (scope) => {
        await new Promise<void>((resolve, reject) => {
          gdal.openAsync(sample, (error, ds) => {
            if (error) {
              reject(error); return
            }
            assert.isOk(ds.rasterSize)
            resolve()
          })
        })
        assert.equal(scope.size, 1)
      })
    })
    it('should not track a dataset which is opened after the callback returned', async () => {
      let ds: gdal.Dataset | undefined
      await new Promise<void>((resolve) => {
        gdal.using((scope) => {
          gdal.openAsync(sample, (error, opened) => {
            ds = opened
            assert.equal(scope.size, 0)
            resolve()
          })
        })
      })
      assert.isOk(ds!.rasterSize)
      ds!.close()
    })
    it('should reject if the callback is not a function', async () => {
      await assert.isRejected(
        (gdal.usingAsync as unknown as (...args: unknown[]) => Promise<unknown>)('not a function'),
        /callback must be a function/
      )
    })
  })
})

describe('the process exit', () => {
  const script = path.join(__dirname, '_process_exit.js')
  const run = (style: 'exit' | 'natural' | 'keep' | 'broken'): string => {
    const file = path.join(os.tmpdir(), `gdal_exit_${style}_${process.pid}.tif`)
    cp.execFileSync(process.execPath, [ script, file, style ])
    return file
  }
  const values = (file: string): number[] => {
    const ds = gdal.open(file)
    try {
      return Array.from(new Set(ds.bands.get(1).pixels.read(0, 0, 64, 64) as Uint8Array))
    } finally {
      ds.close()
    }
  }

  // a GeoTIFF writes the directory which describes its blocks only when it is
  // closed, so a script which still holds the dataset when it ends loses what
  // it has written unless something closes it for the script
  it('should close the dataset of a script which forgets it and calls process.exit()', () => {
    const file = run('exit')
    try {
      assert.deepEqual(values(file), [ 42 ])
    } finally {
      fs.rmSync(file, { force: true })
    }
  })

  it('should close the dataset of a script which forgets it and falls off the end', () => {
    const file = run('natural')
    try {
      assert.deepEqual(values(file), [ 42 ])
    } finally {
      fs.rmSync(file, { force: true })
    }
  })

  it('should not let a dataset which refuses to close stop the exit', () => {
    const file = run('broken')
    try {
      assert.deepEqual(values(file), [ 0 ])
    } finally {
      fs.rmSync(file, { force: true })
    }
  })

  it('should not close the datasets of a caller which owns them itself', () => {
    const file = run('keep')
    try {
      assert.deepEqual(values(file), [ 0 ])
    } finally {
      fs.rmSync(file, { force: true })
    }
  })
})
