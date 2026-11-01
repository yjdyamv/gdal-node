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

  // The Zarr driver is used because it is the only multidim driver which is
  // always built and which can be closed and reopened on every platform.
  // netCDF on Windows keeps the file locked after close, so the dataset cannot
  // be reopened in the same process and the round-trip tests would not work.
  let directory: string
  let filename: string

  beforeEach(() => {
    directory = fs.mkdtempSync(path.join(os.tmpdir(), 'gdal-multidim-'))
    filename = path.join(directory, 'test.zarr')
  })

  afterEach(() => {
    fs.rmSync(directory, { recursive: true, force: true })
  })

  const createDataset = (): gdal.Dataset => gdal.drivers.get('Zarr').createMultidim(filename)

  describe('gdal.Driver', () => {
    describe('createMultidim', () => {
      it('should create a multidimensional dataset with a root group', () => {
        const ds = createDataset()
        assert.instanceOf(ds, gdal.Dataset)
        assert.instanceOf(ds.root, gdal.Group)
        assert.equal(ds.root.description, '/')
        assert.isNull(ds.bands)
        ds.close()
      })

      it('should return a dataset which can be reopened in multidim mode', () => {
        createDataset().close()
        const ds = gdal.open(filename, 'mr')
        assert.instanceOf(ds.root, gdal.Group)
        assert.deepEqual(ds.root.arrays.names, [])
        ds.close()
      })

      it('should throw on an invalid filename', () => {
        assert.throws(() => {
          gdal.drivers.get('Zarr').createMultidim(path.join(directory, 'no', 'such', 'dir', 'test.zarr'))
        })
      })

      it('should throw on a driver which does not support multidimensional datasets', () => {
        assert.throws(() => {
          gdal.drivers.get('GTiff').createMultidim(path.join(directory, 'test.tif'))
        }, /CreateMultiDimensional/)
      })

      it('should accept root group options', () => {
        const ds = gdal.drivers.get('Zarr').createMultidim(filename, { DIMENSION_SEPARATOR: '/' })
        assert.instanceOf(ds.root, gdal.Group)
        ds.close()
      })

      it('createMultidimAsync() should create a multidimensional dataset', () =>
        assert.isFulfilled(gdal.drivers.get('Zarr').createMultidimAsync(filename)
          .then((ds: gdal.Dataset) => {
            assert.instanceOf(ds.root, gdal.Group)
            ds.close()
          })))
    })
  })

  describe('gdal.Group', () => {
    let ds: gdal.Dataset
    beforeEach(() => {
      ds = createDataset()
    })

    afterEach(() => {
      // some tests close the dataset themselves in order to reopen it
      try {
        ds.close()
      } catch (_e) { /* already closed */ }
      global.gc!()
    })

    describe('createDimension', () => {
      it('should create a dimension', () => {
        const dim = ds.root.createDimension('x', 'HORIZONTAL_X', 'EAST', 10)
        assert.instanceOf(dim, gdal.Dimension)
        assert.equal(dim.description, '/x')
        assert.equal(dim.size, 10)
        assert.equal(dim.type, 'HORIZONTAL_X')
        assert.equal(dim.direction, 'EAST')
      })

      it('should make the dimension visible in "dimensions"', () => {
        ds.root.createDimension('x', 'HORIZONTAL_X', 'EAST', 10)
        assert.deepEqual(ds.root.dimensions.names, [ 'x' ])
        assert.equal(ds.root.dimensions.count(), 1)
        assert.equal(ds.root.dimensions.get('x').size, 10)
      })

      it('should accept an empty type and direction', () => {
        const dim = ds.root.createDimension('x', '', '', 4)
        assert.equal(dim.type, '')
        assert.equal(dim.direction, '')
        assert.equal(dim.size, 4)
      })

      it('should throw on a missing name', () => {
        assert.throws(() => {
          (ds.root as unknown as Record<string, (n: unknown, t: string, d: string, s: number) => gdal.Dimension>).createDimension(undefined, '', '', 4)
        }, /name must be a string/)
      })

      it('should throw on a negative size', () => {
        assert.throws(() => {
          ds.root.createDimension('x', '', '', -1)
        }, /size must not be negative/)
      })

      it('createDimensionAsync() should create a dimension', () =>
        assert.isFulfilled(ds.root.createDimensionAsync('x', 'HORIZONTAL_X', 'EAST', 10)
          .then((dim: gdal.Dimension) => {
            assert.equal(dim.size, 10)
          })))
    })

    describe('createMDArray', () => {
      let x: gdal.Dimension, y: gdal.Dimension
      beforeEach(() => {
        x = ds.root.createDimension('x', 'HORIZONTAL_X', 'EAST', 4)
        y = ds.root.createDimension('y', 'HORIZONTAL_Y', 'NORTH', 3)
      })

      it('should create an array', () => {
        const arr = ds.root.createMDArray('data', [ x, y ], 'Float64')
        assert.instanceOf(arr, gdal.MDArray)
        assert.equal(arr.description, '/data')
        assert.equal(arr.dataType, 'Float64')
        assert.equal(arr.length, 12)
        assert.deepEqual(arr.dimensions.names, [ 'x', 'y' ])
        assert.isTrue(arr.isWritable)
      })

      it('should make the array visible in "arrays"', () => {
        ds.root.createMDArray('data', [ x, y ], 'Float64')
        assert.deepEqual(ds.root.arrays.names, [ 'data' ])
        assert.equal(ds.root.arrays.count(), 1)
      })

      it('should create a 0-dimensional array', () => {
        const arr = ds.root.createMDArray('scalar', [], 'Int32')
        assert.equal(arr.length, 1)
        assert.deepEqual(arr.dimensions.names, [])
      })

      it('should create an array of every numeric data type', () => {
        for (const type of [ 'Byte', 'Int16', 'UInt16', 'Int32', 'UInt32', 'Float32', 'Float64' ]) {
          const arr = ds.root.createMDArray(`data_${type}`, [ x ], type)
          assert.equal(arr.dataType, type)
        }
      })

      it('should throw on an invalid data type', () => {
        assert.throws(() => {
          ds.root.createMDArray('data', [ x, y ], 'NotAType')
        }, /Invalid data type/)
      })

      it('should throw when dimensions contains something else than Dimension', () => {
        assert.throws(() => {
          ds.root.createMDArray('data', [ x, 'y' as unknown as gdal.Dimension ], 'Float64')
        }, /dimensions must contain only Dimension objects/)
      })

      it('createMDArrayAsync() should create an array', () =>
        assert.isFulfilled(ds.root.createMDArrayAsync('data', [ x, y ], 'Float64')
          .then((arr: gdal.MDArray) => {
            assert.equal(arr.length, 12)
            assert.equal(arr.dataType, 'Float64')
          })))
    })

    describe('createGroup', () => {
      it('should create a subgroup', () => {
        const group = ds.root.createGroup('sub')
        assert.instanceOf(group, gdal.Group)
        assert.equal(group.description, '/sub')
      })

      it('should make the subgroup visible in "groups"', () => {
        ds.root.createGroup('sub')
        assert.deepEqual(ds.root.groups.names, [ 'sub' ])
      })

      it('should allow creating dimensions and arrays in the subgroup', () => {
        const group = ds.root.createGroup('sub')
        const dim = group.createDimension('x', 'HORIZONTAL_X', 'EAST', 2)
        const arr = group.createMDArray('data', [ dim ], 'Float32')
        assert.equal(arr.description, '/sub/data')
        assert.deepEqual(group.dimensions.names, [ 'x' ])
      })

      it('createGroupAsync() should create a subgroup', () =>
        assert.isFulfilled(ds.root.createGroupAsync('sub')
          .then((group: gdal.Group) => {
            assert.equal(group.description, '/sub')
          })))
    })

    describe('createAttribute', () => {
      it('should create a string attribute', () => {
        const attr = ds.root.createAttribute('title')
        assert.instanceOf(attr, gdal.Attribute)
        assert.equal(attr.dataType, 'String')
      })

      it('should create a numeric attribute', () => {
        const attr = ds.root.createAttribute('version', [], 'Int32')
        assert.equal(attr.dataType, 'Int32')
      })

      it('should make the attribute visible in "attributes"', () => {
        ds.root.createAttribute('title')
        assert.include(ds.root.attributes.names, 'title')
      })

      it('should throw on an invalid data type', () => {
        assert.throws(() => {
          ds.root.createAttribute('title', [], 'NotAType')
        }, /Invalid data type/)
      })

      it('createAttributeAsync() should create an attribute', () =>
        assert.isFulfilled(ds.root.createAttributeAsync('title')
          .then((attr: gdal.Attribute) => {
            assert.equal(attr.dataType, 'String')
          })))
    })

    describe('deleteGroup', () => {
      it('should delete a subgroup', () => {
        ds.root.createGroup('sub')
        assert.isTrue(ds.root.deleteGroup('sub'))
        assert.deepEqual(ds.root.groups.names, [])
      })

      it('should throw on a nonexistent subgroup', () => {
        assert.throws(() => {
          ds.root.deleteGroup('nonexistent')
        })
      })

      it('deleteGroupAsync() should delete a subgroup', () =>
        assert.isFulfilled(ds.root.createGroupAsync('sub')
          .then(() => ds.root.deleteGroupAsync('sub'))
          .then((r: boolean) => {
            assert.isTrue(r)
            assert.deepEqual(ds.root.groups.names, [])
          })))
    })

    describe('deleteMDArray', () => {
      it('should delete an array', () => {
        const dim = ds.root.createDimension('x', 'HORIZONTAL_X', 'EAST', 2)
        ds.root.createMDArray('data', [ dim ], 'Float64')
        assert.isTrue(ds.root.deleteMDArray('data'))
        assert.deepEqual(ds.root.arrays.names, [])
      })

      it('should throw on a nonexistent array', () => {
        assert.throws(() => {
          ds.root.deleteMDArray('nonexistent')
        })
      })
    })

    describe('deleteAttribute', () => {
      it('should delete an attribute', () => {
        ds.root.createAttribute('title')
        assert.isTrue(ds.root.deleteAttribute('title'))
        assert.notInclude(ds.root.attributes.names, 'title')
      })

      it('should throw on a nonexistent attribute', () => {
        assert.throws(() => {
          ds.root.deleteAttribute('nonexistent')
        })
      })
    })

    describe('rename', () => {
      it('should rename a subgroup', () => {
        const group = ds.root.createGroup('sub')
        group.rename('renamed')
        assert.deepEqual(ds.root.groups.names, [ 'renamed' ])
      })

      it('renameAsync() should rename a subgroup', () =>
        assert.isFulfilled(ds.root.createGroupAsync('sub')
          .then((group: gdal.Group) => group.renameAsync('renamed'))
          .then(() => {
            assert.deepEqual(ds.root.groups.names, [ 'renamed' ])
          })))
    })
  })

  describe('gdal.MDArray', () => {
    let ds: gdal.Dataset
    let mdarray: gdal.MDArray
    let x: gdal.Dimension, y: gdal.Dimension

    beforeEach(() => {
      ds = createDataset()
      x = ds.root.createDimension('x', 'HORIZONTAL_X', 'EAST', 4)
      y = ds.root.createDimension('y', 'HORIZONTAL_Y', 'NORTH', 3)
      mdarray = ds.root.createMDArray('data', [ x, y ], 'Float64')
    })

    afterEach(() => {
      // some tests close the dataset themselves in order to reopen it
      try {
        ds.close()
      } catch (_e) { /* already closed */ }
      global.gc!()
    })

    describe('"isWritable" property', () => {
      it('should be true for a freshly created array', () => {
        assert.isTrue(mdarray.isWritable)
      })

      it('should be false for an array opened read-only', () => {
        mdarray.write({ origin: [ 0, 0 ], span: [ 4, 3 ], data: Float64Array.from({ length: 12 }) })
        ds.close()
        const rds = gdal.open(filename, 'mr')
        assert.isFalse(rds.root.arrays.get('data').isWritable)
        rds.close()
      })
    })

    describe('write', () => {
      it('should write the whole array', () => {
        const data = Float64Array.from({ length: 12 }, (_, i) => i)
        mdarray.write({ origin: [ 0, 0 ], span: [ 4, 3 ], data })
        assert.deepEqual(Array.from(mdarray.read({ origin: [ 0, 0 ], span: [ 4, 3 ] })), Array.from(data))
      })

      it('should write a partial array', () => {
        // the values fill the buffer in the order GDAL addresses it: the last
        // dimension varies fastest, and the write starts at the origin
        mdarray.write({ origin: [ 1, 1 ], span: [ 2, 2 ], data: Float64Array.from([ 1, 2, 3, 4 ]) })
        const all = mdarray.read({ origin: [ 0, 0 ], span: [ 4, 3 ] })
        assert.deepEqual(Array.from(all), [ 0, 0, 0, 0, 1, 2, 0, 3, 4, 0, 0, 0 ])
      })

      it('should convert the data to the type of the array', () => {
        const int = ds.root.createMDArray('int', [ x ], 'Int32')
        // GDAL rounds rather than truncates when converting
        int.write({ origin: [ 0 ], span: [ 4 ], data: Float64Array.from([ 1.7, 2.2, 3.9, 4.5 ]) })
        assert.deepEqual(Array.from(int.read({ origin: [ 0 ], span: [ 4 ] })), [ 2, 2, 4, 5 ])
      })

      it('should accept a "data_type" which restates the type of the data', () => {
        mdarray.write({
          origin: [ 0, 0 ],
          span: [ 4, 3 ],
          data: Float64Array.from({ length: 12 }, () => 7),
          data_type: 'Float64'
        })
        assert.deepEqual(
          Array.from(mdarray.read({ origin: [ 0, 0 ], span: [ 4, 3 ] })),
          [ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7 ]
        )
      })

      it('should throw when "data_type" disagrees with the type of the data', () => {
        assert.throws(() => {
          mdarray.write({
            origin: [ 0, 0 ],
            span: [ 4, 3 ],
            data: Float64Array.from({ length: 12 }),
            data_type: 'Float32'
          })
        }, /data_type does not match the type of the data array/)
      })

      it('should throw on an invalid "data_type"', () => {
        assert.throws(() => {
          mdarray.write({
            origin: [ 0, 0 ],
            span: [ 4, 3 ],
            data: Float64Array.from({ length: 12 }),
            data_type: 'NotAType'
          })
        }, /Invalid data type/)
      })

      it('should support buffer strides when writing', () => {
        // the values are scattered in the buffer in the order GDAL addresses
        // them: the first dimension varies slowest, the last one fastest
        mdarray.write({
          origin: [ 0, 0 ],
          span: [ 2, 3 ],
          stride: [ 3, 1 ],
          data: Float64Array.from([ 1, 2, 3, 4, 5, 6 ])
        })
        assert.deepEqual(
          Array.from(mdarray.read({ origin: [ 0, 0 ], span: [ 4, 3 ] })),
          [ 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0 ]
        )
      })

      it('should be the exact inverse of read with the same stride', () => {
        mdarray.write({ origin: [ 0, 0 ], span: [ 4, 3 ], data: Float64Array.from({ length: 12 }, (_, i) => i + 1) })
        const slice = mdarray.read({ origin: [ 0, 0 ], span: [ 2, 3 ], stride: [ 3, 1 ] })
        assert.deepEqual(Array.from(slice), [ 1, 2, 3, 4, 5, 6 ])
        mdarray.write({ origin: [ 0, 0 ], span: [ 4, 3 ], data: Float64Array.from({ length: 12 }, () => 0) })
        mdarray.write({ origin: [ 0, 0 ], span: [ 2, 3 ], stride: [ 3, 1 ], data: slice })
        assert.deepEqual(
          Array.from(mdarray.read({ origin: [ 0, 0 ], span: [ 4, 3 ] })),
          [ 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0 ]
        )
      })

      it('should write a 0-dimensional array', () => {
        const scalar = ds.root.createMDArray('scalar', [], 'Int32')
        scalar.write({ origin: [], span: [], data: Int32Array.from([ 42 ]) })
        assert.deepEqual(Array.from(scalar.read({ origin: [], span: [] })), [ 42 ])
      })

      it('should throw when "data" is missing', () => {
        assert.throws(() => {
          mdarray.write.bind(mdarray)({ origin: [ 0, 0 ], span: [ 4, 3 ] })
        }, /data must be a TypedArray/)
      })

      it('should throw when "data" is not a TypedArray', () => {
        assert.throws(() => {
          mdarray.write.bind(mdarray)({ origin: [ 0, 0 ], span: [ 4, 3 ], data: [ 1, 2, 3 ] } as unknown as gdal.MDArrayOptions<Float64Array>)
        }, /data must be a TypedArray/)
      })

      it('should throw when "origin" is missing', () => {
        const without = (options: Record<string, unknown>) => options as unknown as gdal.MDArrayOptions<Float64Array>
        assert.throws(() => {
          mdarray.write(without({ span: [ 4, 3 ], data: Float64Array.from({ length: 12 }) }))
        }, /origin is required/)
      })

      it('should throw when "span" is missing', () => {
        const without = (options: Record<string, unknown>) => options as unknown as gdal.MDArrayOptions<Float64Array>
        assert.throws(() => {
          mdarray.write(without({ origin: [ 0, 0 ], data: Float64Array.from({ length: 12 }) }))
        }, /span is required/)
      })

      it('should throw on an out of bounds write', () => {
        assert.throws(() => {
          mdarray.write({ origin: [ 0, 0 ], span: [ 8, 8 ], data: Float64Array.from({ length: 64 }) })
        })
      })

      it('writeAsync() should write the array', () =>
        assert.isFulfilled(mdarray.writeAsync({
          origin: [ 0, 0 ],
          span: [ 4, 3 ],
          data: Float64Array.from({ length: 12 }, (_, i) => i)
        }).then(() => {
          assert.deepEqual(
            Array.from(mdarray.read({ origin: [ 0, 0 ], span: [ 4, 3 ] })),
            [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 ]
          )
        })))

      it('writeAsync() should reject when "data" is missing', () =>
        assert.isRejected(mdarray.writeAsync({
          origin: [ 0, 0 ],
          span: [ 4, 3 ]
        } as unknown as gdal.MDArrayOptions<Float64Array>), /data must be a TypedArray/))

      it('should survive a close and reopen', () => {
        mdarray.write({ origin: [ 0, 0 ], span: [ 4, 3 ], data: Float64Array.from({ length: 12 }, (_, i) => i + 1) })
        ds.close()
        const rds = gdal.open(filename, 'mr')
        assert.deepEqual(
          Array.from(rds.root.arrays.get('data').read({ origin: [ 0, 0 ], span: [ 4, 3 ] })),
          [ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 ]
        )
        rds.close()
      })
    })

    describe('the writable metadata properties', () => {
      it('should have a writable "unitType"', () => {
        mdarray.unitType = 'm'
        assert.equal(mdarray.unitType, 'm')
      })

      it('should throw when "unitType" is not a string', () => {
        assert.throws(() => {
          (mdarray as unknown as Record<string, unknown>).unitType = 42
        }, /Unit type must be a string/)
      })

      it('should have a writable "scale"', () => {
        mdarray.scale = 2.5
        assert.equal(mdarray.scale, 2.5)
      })

      it('should throw when "scale" is not a number', () => {
        assert.throws(() => {
          (mdarray as unknown as Record<string, unknown>).scale = 'x'
        }, /Scale must be a number/)
      })

      it('should have a writable "offset"', () => {
        mdarray.offset = -1.5
        assert.equal(mdarray.offset, -1.5)
      })

      it('should throw when "offset" is not a number', () => {
        assert.throws(() => {
          (mdarray as unknown as Record<string, unknown>).offset = 'x'
        }, /Offset must be a number/)
      })

      it('should have a writable "noDataValue"', () => {
        assert.isNull(mdarray.noDataValue)
        mdarray.noDataValue = -9999
        assert.equal(mdarray.noDataValue, -9999)
        mdarray.noDataValue = null
        assert.isNull(mdarray.noDataValue)
      })

      it('should throw when "noDataValue" is not a number', () => {
        assert.throws(() => {
          (mdarray as unknown as Record<string, unknown>).noDataValue = 'x'
        }, /No data value must be a number/)
      })

      it('should have a writable "srs"', () => {
        const srs = gdal.SpatialReference.fromEPSG(4326)
        assert.isNull(mdarray.srs)
        mdarray.srs = srs
        assert.instanceOf(mdarray.srs, gdal.SpatialReference)
        assert.equal(mdarray.srs!.toWKT(), srs.toWKT())
        mdarray.srs = null
        assert.isNull(mdarray.srs)
      })

      it('should throw when "srs" is not a SpatialReference', () => {
        assert.throws(() => {
          (mdarray as unknown as Record<string, unknown>).srs = 'EPSG:4326'
        }, /srs must be SpatialReference object/)
      })
    })
  })

  describe('gdal.Attribute', () => {
    let ds: gdal.Dataset
    beforeEach(() => {
      ds = createDataset()
    })

    afterEach(() => {
      // some tests close the dataset themselves in order to reopen it
      try {
        ds.close()
      } catch (_e) { /* already closed */ }
      global.gc!()
    })

    describe('"value" property', () => {
      it('should be writable for a string attribute', () => {
        const attr = ds.root.createAttribute('title')
        attr.value = 'hello'
        assert.equal(attr.value, 'hello')
      })

      it('should be writable for a numeric attribute', () => {
        const attr = ds.root.createAttribute('version', [], 'Int32')
        attr.value = 42
        assert.equal(attr.value, 42)
      })

      it('should throw when a string attribute is assigned a number', () => {
        const attr = ds.root.createAttribute('title')
        assert.throws(() => {
          (attr as unknown as Record<string, unknown>).value = 42
        }, /String attributes must be assigned a string/)
      })

      it('should throw when a numeric attribute is assigned a string', () => {
        const attr = ds.root.createAttribute('version', [], 'Int32')
        assert.throws(() => {
          (attr as unknown as Record<string, unknown>).value = 'x'
        }, /Numeric attributes must be assigned a number/)
      })

      it('should survive a close and reopen', () => {
        ds.root.createAttribute('title').value = 'hello'
        ds.close()
        const rds = gdal.open(filename, 'mr')
        assert.equal(rds.root.attributes.get('title').value, 'hello')
        rds.close()
      })
    })
  })
})
