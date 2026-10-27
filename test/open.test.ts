import * as gdal from 'gdal-async'
import * as path from 'path'
import * as assert from 'assert'
import * as semver from 'semver'

describe('Open', () => {
  afterEach(() => void global.gc!())

  it('should throw when invalid file', () => {
    const filename = path.join(__dirname, 'data/invalid')
    assert.throws(() => {
      gdal.open(filename)
    }, semver.gte(gdal.version, '3.0.0') ? /not recognized.*supported file format/ : /Error/)
  })
  it('should throw when non-existing file', () => {
    const filename = path.join(__dirname, 'data/inexisting')
    assert.throws(() => {
      gdal.open(filename)
    }, semver.gte(gdal.version, '3.0.0') ? /No such file or directory/ : /Error/)
  })

  // the third argument of gdal.open() is the GDAL2-style way of naming the
  // driver, or the drivers, which may open the file
  describe('with a list of drivers', () => {
    const filename = path.join(__dirname, 'data/sample.tif')

    it('should use the driver which can open the file', () => {
      const ds = gdal.open(filename, 'r', [ 'GTiff' ])
      assert.equal(ds.driver.description, 'GTiff')
      ds.close()
    })
    it('should try the drivers in order', () => {
      // the PNG driver cannot open a TIFF
      const ds = gdal.open(filename, 'r', [ 'PNG', 'GTiff' ])
      assert.equal(ds.driver.description, 'GTiff')
      ds.close()
    })
    it('should throw when none of the drivers can open the file', () => {
      assert.throws(() => {
        gdal.open(filename, 'r', [ 'PNG', 'JPEG' ])
      }, /Error opening dataset/)
    })
    it('should throw when a driver does not exist', () => {
      assert.throws(() => {
        gdal.open(filename, 'r', [ 'awfag' ])
      }, /Cannot find driver: awfag/)
    })
    it('should throw when the argument is not a driver name or a list', () => {
      assert.throws(() => {
        gdal.open(filename, 'r', 42 as unknown as string[])
      }, /driver\(s\) must be a string or list of strings/)
    })
    it('should create a dataset with a single driver', () => {
      const ds = gdal.open('/vsimem/open_list.tif', 'w', [ 'GTiff' ], 4, 4, 1, gdal.GDT_Byte)
      assert.ok(ds instanceof gdal.Dataset)
      ds.close()
      gdal.vsimem.release('/vsimem/open_list.tif')
    })
    it('should refuse to create a dataset with a list of drivers', () => {
      assert.throws(() => {
        gdal.open('/vsimem/open_list.tif', 'w', [ 'GTiff', 'MEM' ], 4, 4, 1, gdal.GDT_Byte)
      }, /Only one driver can be used to create a file/)
    })
    it('should refuse to create a dataset without a driver', () => {
      assert.throws(() => {
        gdal.open('/vsimem/open_list.tif', 'w')
      }, /Driver must be specified/)
    })
    it('should throw when the driver of a new dataset does not exist', () => {
      assert.throws(() => {
        gdal.open('/vsimem/open_list.tif', 'w', 'awfag', 4, 4, 1, gdal.GDT_Byte)
      }, /Cannot find driver: awfag/)
    })
  })

  describe('with a list of drivers asynchronously', () => {
    const filename = path.join(__dirname, 'data/sample.tif')

    it('should use the driver which can open the file', async () => {
      const ds = await gdal.openAsync(filename, 'r', [ 'GTiff' ])
      assert.equal(ds.driver.description, 'GTiff')
      ds.close()
    })
    it('should try the drivers in order', async () => {
      const ds = await gdal.openAsync(filename, 'r', [ 'PNG', 'GTiff' ])
      assert.equal(ds.driver.description, 'GTiff')
      ds.close()
    })
    it('should throw when none of the drivers can open the file', async () => {
      const error = await gdal.openAsync(filename, 'r', [ 'PNG', 'JPEG' ])
        .then(() => undefined, (e: unknown) => e)
      assert.ok(error instanceof Error)
      assert.match((error as Error).message, /Error opening dataset/)
    })
    it('should throw when a driver does not exist', () => {
      // the drivers are looked up before the promises are created, so this
      // throws instead of rejecting
      assert.throws(() => {
        void gdal.openAsync(filename, 'r', [ 'awfag' ])
      }, /Cannot find driver: awfag/)
    })
    it('should open a buffer', async () => {
      const fs = await import('node:fs')
      const ds = await gdal.openAsync(fs.readFileSync(filename))
      assert.equal(ds.driver.description, 'GTiff')
      ds.close()
    })
    it('should create a dataset with a single driver', async () => {
      const ds = await gdal.openAsync('/vsimem/open_list_async.tif', 'w', [ 'GTiff' ], 4, 4, 1, gdal.GDT_Byte)
      assert.ok(ds instanceof gdal.Dataset)
      ds.close()
      gdal.vsimem.release('/vsimem/open_list_async.tif')
    })
    it('should throw when the argument is not a driver name or a list', () => {
      assert.throws(() => {
        void gdal.openAsync(filename, 'r', 42 as unknown as string[])
      }, /driver\(s\) must be a string or list of strings/)
    })
    it('should refuse to create a dataset without a driver', () => {
      assert.throws(() => {
        void gdal.openAsync('/vsimem/open_list_async.tif', 'w')
      }, /Driver must be specified/)
    })
    it('should refuse to create a dataset with a list of drivers', () => {
      assert.throws(() => {
        gdal.openAsync('/vsimem/open_list_async.tif', 'w', [ 'GTiff', 'MEM' ], 4, 4, 1, gdal.GDT_Byte)
      }, /Only one driver can be used to create a file/)
    })
    it('should throw when the driver of a new dataset does not exist', () => {
      assert.throws(() => {
        gdal.openAsync('/vsimem/open_list_async.tif', 'w', 'awfag', 4, 4, 1, gdal.GDT_Byte)
      }, /Cannot find driver: awfag/)
    })
  })
})
