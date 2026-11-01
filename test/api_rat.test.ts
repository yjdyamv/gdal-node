import { assert } from 'chai'
import * as fs from 'fs'
import * as os from 'os'
import * as path from 'path'
import * as gdal from 'gdal-node'
import * as semver from 'semver'

describe('gdal', () => {
  if (!semver.gte(gdal.version, '3.1.0')) {
    return
  }

  let directory: string
  let filename: string
  let ds: gdal.Dataset
  let band: gdal.RasterBand

  beforeEach(() => {
    directory = fs.mkdtempSync(path.join(os.tmpdir(), 'gdal-rat-'))
    filename = path.join(directory, 'test.tif')
    ds = gdal.open(filename, 'w', 'GTiff', 4, 1, 1, gdal.GDT_Byte)
    band = ds.bands.get(1)
  })

  afterEach(() => {
    try {
      ds.close()
    } catch (_e) { /* already closed */ }
    global.gc!()
    fs.rmSync(directory, { recursive: true, force: true })
  })

  const filled = (): gdal.RasterAttributeTable => {
    const rat = new gdal.RasterAttributeTable()
    rat.setRowCount(3)
    const value = rat.createColumn('Value', 'Integer', 'MinMax')
    rat.createColumn('Name', 'String', 'Name')
    rat.createColumn('Count', 'Real', 'PixelCount')
    rat.setValues(value, [ 10, 20, 30 ])
    rat.setValues(1, [ 'a', 'b', 'c' ])
    rat.setValues(2, [ 1.5, 2.5, 3.5 ])
    return rat
  }

  describe('gdal.RasterBand', () => {
    describe('"rasterAttributeTable" property', () => {
      it('should be null when the band has no table', () => {
        assert.isNull(band.rasterAttributeTable)
      })

      it('should be assignable and readable', () => {
        band.rasterAttributeTable = filled()
        const rat = band.rasterAttributeTable!
        assert.instanceOf(rat, gdal.RasterAttributeTable)
        assert.equal(rat.rowCount, 3)
        assert.deepEqual(rat.columnNames, [ 'Value', 'Name', 'Count' ])
      })

      it('should be resettable to null', () => {
        band.rasterAttributeTable = filled()
        band.rasterAttributeTable = null
        assert.isNull(band.rasterAttributeTable)
      })

      it('should throw when assigned something else', () => {
        assert.throws(() => {
          (band as unknown as Record<string, unknown>).rasterAttributeTable = 'x'
        }, /raster attribute table must be a gdal.RasterAttributeTable object or null/)
      })

      it('should hand out a table which is read-only', () => {
        band.rasterAttributeTable = filled()
        const rat = band.rasterAttributeTable!
        assert.throws(() => {
          rat.setRowCount(10)
        }, /read-only/)
      })

      it('should survive a close and reopen', () => {
        band.rasterAttributeTable = filled()
        ds.close()
        ds = gdal.open(filename, 'r+')
        const rat = ds.bands.get(1).rasterAttributeTable!
        assert.equal(rat.rowCount, 3)
        assert.deepEqual(rat.getValues(1), [ 'a', 'b', 'c' ])
      })
    })
  })

  describe('gdal.RasterAttributeTable', () => {
    let rat: gdal.RasterAttributeTable

    beforeEach(() => {
      rat = new gdal.RasterAttributeTable()
    })

    describe('constructor', () => {
      it('should create an empty table', () => {
        assert.instanceOf(rat, gdal.RasterAttributeTable)
        assert.equal(rat.rowCount, 0)
        assert.equal(rat.columnCount, 0)
        assert.deepEqual(rat.columnNames, [])
        assert.isFalse(rat.changesAreWrittenToFile)
      })

      it('should not accept arguments', () => {
        assert.throws(() => {
          new (gdal.RasterAttributeTable as unknown as new (x: string) => gdal.RasterAttributeTable)('x')
        }, /cannot be created from JavaScript/)
      })
    })

    describe('setRowCount', () => {
      it('should resize the table', () => {
        rat.setRowCount(5)
        assert.equal(rat.rowCount, 5)
        rat.setRowCount(2)
        assert.equal(rat.rowCount, 2)
      })

      it('should throw on a negative count', () => {
        assert.throws(() => {
          rat.setRowCount(-1)
        }, /count must not be negative/)
      })

      it('should throw on a missing count', () => {
        assert.throws(() => {
          (rat as unknown as Record<string, () => void>).setRowCount()
        }, /count must be given/)
      })
    })

    describe('createColumn', () => {
      it('should create a column of every type', () => {
        assert.equal(rat.createColumn('i', 'Integer'), 0)
        assert.equal(rat.createColumn('r', 'Real'), 1)
        assert.equal(rat.createColumn('s', 'String'), 2)
        assert.deepEqual(rat.columnTypes, [ 'Integer', 'Real', 'String' ])
      })

      it('should return the index of the new column', () => {
        rat.createColumn('a', 'Integer')
        assert.equal(rat.createColumn('b', 'Integer'), 1)
        assert.equal(rat.columnCount, 2)
      })

      it('should default the usage to Generic', () => {
        rat.createColumn('a', 'Integer')
        assert.deepEqual(rat.columnUsages, [ 'Generic' ])
      })

      it('should accept a usage', () => {
        rat.createColumn('a', 'Integer', 'Min')
        rat.createColumn('b', 'String', 'Name')
        assert.deepEqual(rat.columnUsages, [ 'Min', 'Name' ])
      })

      it('should throw on an invalid type', () => {
        assert.throws(() => {
          rat.createColumn('a', 'NotAType')
        }, /Invalid column type/)
      })

      it('should throw on an invalid usage', () => {
        assert.throws(() => {
          rat.createColumn('a', 'Integer', 'NotAUsage')
        }, /Invalid column usage/)
      })
    })

    describe('getValues', () => {
      it('should read an integer column', () => {
        rat.setRowCount(3)
        rat.createColumn('a', 'Integer')
        rat.setValues(0, [ 1, 2, 3 ])
        assert.deepEqual(rat.getValues(0), [ 1, 2, 3 ])
      })

      it('should read a real column', () => {
        rat.setRowCount(2)
        rat.createColumn('a', 'Real')
        rat.setValues(0, [ 1.5, 2.5 ])
        assert.deepEqual(rat.getValues(0), [ 1.5, 2.5 ])
      })

      it('should read a string column', () => {
        rat.setRowCount(2)
        rat.createColumn('a', 'String')
        rat.setValues(0, [ 'x', 'y' ])
        assert.deepEqual(rat.getValues(0), [ 'x', 'y' ])
      })

      it('should support a start and a length', () => {
        rat.setRowCount(5)
        rat.createColumn('a', 'Integer')
        rat.setValues(0, [ 1, 2, 3, 4, 5 ])
        assert.deepEqual(rat.getValues(0, 1, 2), [ 2, 3 ])
      })

      it('should throw on an invalid column', () => {
        assert.throws(() => {
          rat.getValues(0)
        }, /column must be a valid column index/)
      })

      it('should throw on an invalid start', () => {
        rat.createColumn('a', 'Integer')
        assert.throws(() => {
          rat.getValues(0, 1)
        }, /start must be a valid row index/)
      })

      it('should throw when the range does not fit', () => {
        rat.setRowCount(2)
        rat.createColumn('a', 'Integer')
        assert.throws(() => {
          rat.getValues(0, 0, 5)
        }, /length must fit in the table/)
      })
    })

    describe('setValues', () => {
      it('should write a subset of the rows', () => {
        rat.setRowCount(4)
        rat.createColumn('a', 'Integer')
        rat.setValues(0, [ 9, 9 ], 2)
        assert.deepEqual(rat.getValues(0), [ 0, 0, 9, 9 ])
      })

      it('should throw when the values do not fit', () => {
        rat.setRowCount(2)
        rat.createColumn('a', 'Integer')
        assert.throws(() => {
          rat.setValues(0, [ 1, 2, 3 ])
        }, /values must fit in the table/)
      })

      it('should throw when a string column gets a number', () => {
        rat.setRowCount(1)
        rat.createColumn('a', 'String')
        assert.throws(() => {
          rat.setValues(0, [ 1 as unknown as string ])
        }, /String columns must be assigned strings/)
      })

      it('should throw when a real column gets a string', () => {
        rat.setRowCount(1)
        rat.createColumn('a', 'Real')
        assert.throws(() => {
          rat.setValues(0, [ 'x' as unknown as number ])
        }, /Real columns must be assigned numbers/)
      })

      it('should throw when an integer column gets a string', () => {
        rat.setRowCount(1)
        rat.createColumn('a', 'Integer')
        assert.throws(() => {
          rat.setValues(0, [ 'x' as unknown as number ])
        }, /Integer columns must be assigned numbers/)
      })
    })

    describe('getColumnOfUsage', () => {
      it('should return the index of the column', () => {
        rat.createColumn('a', 'Integer', 'Min')
        rat.createColumn('b', 'String', 'Name')
        assert.equal(rat.getColumnOfUsage('Name'), 1)
      })

      it('should return -1 when there is no such column', () => {
        rat.createColumn('a', 'Integer')
        assert.equal(rat.getColumnOfUsage('Name'), -1)
      })

      it('should throw on an invalid usage', () => {
        assert.throws(() => {
          rat.getColumnOfUsage('Nope')
        }, /Invalid column usage/)
      })
    })

    describe('getRowOfValue', () => {
      it('should return the row of the value', () => {
        rat.setRowCount(3)
        rat.createColumn('a', 'Integer', 'MinMax')
        rat.setValues(0, [ 10, 20, 30 ])
        assert.equal(rat.getRowOfValue(20), 1)
      })

      it('should return -1 for a value which is not in the table', () => {
        rat.setRowCount(3)
        rat.createColumn('a', 'Integer', 'MinMax')
        rat.setValues(0, [ 10, 20, 30 ])
        assert.equal(rat.getRowOfValue(999), -1)
      })
    })

    describe('"tableType" property', () => {
      it('should default to Thematic', () => {
        assert.equal(rat.tableType, 'Thematic')
      })

      it('should be writable', () => {
        rat.tableType = 'Athematic'
        assert.equal(rat.tableType, 'Athematic')
        rat.tableType = 'Thematic'
        assert.equal(rat.tableType, 'Thematic')
      })

      it('should throw on an invalid value', () => {
        assert.throws(() => {
          rat.tableType = 'Nope'
        }, /Table type must be either Thematic or Athematic/)
      })

      it('should throw when it is not a string', () => {
        assert.throws(() => {
          (rat as unknown as Record<string, unknown>).tableType = 42
        }, /Table type must be a string/)
      })
    })

    describe('clone', () => {
      it('should return an independent copy', () => {
        rat.setRowCount(3)
        rat.createColumn('a', 'String')
        rat.setValues(0, [ 'a', 'b', 'c' ])

        const copy = rat.clone()
        assert.instanceOf(copy, gdal.RasterAttributeTable)
        assert.deepEqual(copy.getValues(0), [ 'a', 'b', 'c' ])

        copy.setValues(0, [ 'x', 'y', 'z' ])
        assert.deepEqual(rat.getValues(0), [ 'a', 'b', 'c' ])
        assert.deepEqual(copy.getValues(0), [ 'x', 'y', 'z' ])
      })

      it('should produce a writable copy of a band table', () => {
        band.rasterAttributeTable = filled()
        const copy = band.rasterAttributeTable!.clone()
        copy.setRowCount(5)
        assert.equal(copy.rowCount, 5)
        assert.equal(band.rasterAttributeTable!.rowCount, 3)
      })
    })

    describe('the table of a destroyed band', () => {
      it('should throw when used', () => {
        band.rasterAttributeTable = filled()
        const attached = band.rasterAttributeTable!
        ds.close()
        assert.throws(() => attached.rowCount, /already (been )?destroyed/)
      })
    })
  })
})
