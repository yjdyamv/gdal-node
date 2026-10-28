import * as gdal from 'gdal-async'
import * as path from 'path'
import { assert } from 'chai'
import * as fileUtils from './utils/file'
import * as semver from 'semver'

if (Symbol.asyncIterator) {
  describe('gdal.Dataset', () => {
    describe('instance', () => {
      describe('"bands" propertry', () => {
        describe('@@asyncIterator()', () => {
          it('should support iterating over the values', async () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'multiband.tif'))
            let count = 0
            for await (const band of ds.bands) {
              assert.instanceOf(band, gdal.RasterBand)
              count++
            }
            assert.equal(count, ds.bands.count())
          })
          it('should throw error if dataset is destroyed', () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'multiband.tif'))
            ds.close()
            return assert.isRejected((async () => {
              for await (const band of ds.bands) void band
            })(), /already been destroyed/)
          })
        })
      })
      describe('"layers" propertry', () => {
        describe('@@iterator()', () => {
          it('should support iterating over the values', () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'cutline.shp'))
            let count = 0
            for (const layer of ds.layers) {
              assert.instanceOf(layer, gdal.Layer)
              count++
            }
            assert.equal(count, ds.layers.count())
            ds.close()
          })
        })
        describe('@@asyncIterator()', () => {
          it('should support iterating over the values', async () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'cutline.shp'))
            let count = 0
            for await (const layer of ds.layers) {
              assert.instanceOf(layer, gdal.Layer)
              count++
            }
            assert.equal(count, ds.layers.count())
            ds.close()
          })
        })
      })
    })
  })
  describe('gdal.Layer', () => {
    describe('instance', () => {
      describe('"fields" propertry', () => {
        describe('@@iterator()', () => {
          it('should support iterating over the values', () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'cutline.shp'))
            const fields = ds.layers.get(0).fields
            let count = 0
            for (const field of fields) {
              assert.instanceOf(field, gdal.FieldDefn)
              count++
            }
            assert.equal(count, fields.count())
            ds.close()
          })
        })
        // the field definitions are already in memory, the async iterator
        // resolves the synchronous accessors
        describe('@@asyncIterator()', () => {
          it('should support iterating over the values', async () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'cutline.shp'))
            const fields = ds.layers.get(0).fields
            let count = 0
            for await (const field of fields) {
              assert.instanceOf(field, gdal.FieldDefn)
              count++
            }
            assert.equal(count, fields.count())
            ds.close()
          })
          it('should reject if the dataset is closed', () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'cutline.shp'))
            const fields = ds.layers.get(0).fields
            ds.close()
            return assert.isRejected((async () => {
              for await (const field of fields) void field
            })(), /already (been )?destroyed/)
          })
        })
      })
      describe('"features" propertry', () => {
        describe('@@asyncIterator()', () => {
          it('should support iterating over the values', async () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'park.geo.json'))
            const layer = ds.layers.get(0)
            let count = 0
            for await (const feature of layer.features) {
              assert.instanceOf(feature, gdal.Feature)
              count++
            }
            assert.equal(count, layer.features.count())
          })
          it('should throw error if dataset is destroyed', () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'park.geo.json'))
            const layer = ds.layers.get(0)
            ds.close()
            return assert.isRejected((async () => {
              for (const l of layer.features) void l
            })(), /already (been )?destroyed/)
          })
        })
      })
    })
  })
  describe('gdal.FeatureDefn', () => {
    describe('instance', () => {
      describe('"fields" propertry', () => {
        describe('@@iterator()', () => {
          it('should support iterating over the values', () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'cutline.shp'))
            const defn = ds.layers.get(0).features.first()!.defn
            let count = 0
            for (const field of defn.fields) {
              assert.instanceOf(field, gdal.FieldDefn)
              count++
            }
            assert.equal(count, defn.fields.count())
            ds.close()
          })
        })
        describe('@@asyncIterator()', () => {
          it('should support iterating over the values', async () => {
            const ds = gdal.open(path.resolve(__dirname, 'data', 'cutline.shp'))
            const defn = ds.layers.get(0).features.first()!.defn
            let count = 0
            for await (const field of defn.fields) {
              assert.instanceOf(field, gdal.FieldDefn)
              count++
            }
            assert.equal(count, defn.fields.count())
            ds.close()
          })
        })
      })
    })
  })
  describe('gdal.GeometryCollection', () => {
    describe('instance', () => {
      describe('"children" propertry', () => {
        describe('@@iterator()', () => {
          it('should support iterating over the values', () => {
            const collection = new gdal.GeometryCollection()
            collection.children.add(new gdal.Point(1, 2))
            collection.children.add(new gdal.Point(3, 4))
            let count = 0
            for (const child of collection.children) {
              assert.instanceOf(child, gdal.Point)
              count++
            }
            assert.equal(count, collection.children.count())
          })
        })
      })
    })
  })
  describe('gdal.CompoundCurve', () => {
    describe('instance', () => {
      describe('"curves" propertry', () => {
        describe('@@asyncIterator()', () => {
          it('should support iterating over the values', async () => {
            const curve = new gdal.CompoundCurve()
            const line = new gdal.LineString()
            line.points.add({ x: 0, y: 0 })
            line.points.add({ x: 10, y: 0 })
            curve.curves.add(line)
            let count = 0
            for await (const part of curve.curves) {
              assert.instanceOf(part, gdal.SimpleCurve)
              count++
            }
            assert.equal(count, curve.curves.count())
          })
        })
      })
    })
  })
  describe('gdal.RasterBand', () => {
    describe('"overviews" property', () => {
      describe('@@asyncIterator()', () => {
        if (!semver.satisfies(gdal.version, '~3.1.4')) {
          // Work around https://github.com/OSGeo/gdal/issues/3746
          // on Fedora 33, which shipped GDAL 3.1
          it('should iterate through the overviews', async () => {
            const tempFile = fileUtils.clone(`${__dirname}/data/sample.tif`)
            const ds = gdal.open(tempFile, 'r+')
            const band = ds.bands.get(1)
            ds.buildOverviews('NEAREST', [ 2, 4 ])
            const w = []
            for await (const overview of band.overviews) {
              w.push(overview.size.x)
            }
            assert.sameMembers(w, [ ds.rasterSize.x / 2, ds.rasterSize.x / 4 ])
            ds.close()
            gdal.vsimem.release(tempFile)
          })
          it('should throw error if dataset already closed', () => {
            const tempFile = fileUtils.clone(`${__dirname}/data/sample.tif`)
            const ds = gdal.open(tempFile, 'r+')
            const band = ds.bands.get(1)
            ds.buildOverviews('NEAREST', [ 2 ])
            ds.close()
            gdal.vsimem.release(tempFile)
            return assert.isRejected((async () => {
              for await (const overview of band.overviews) void overview
            })(), /already been destroyed/)
          })
        }
      })
    })
  })
  if (semver.gte(gdal.version, '3.1.0')) {
    const tests = {
      Group: { groups: gdal.Group, arrays: gdal.MDArray, dimensions: gdal.Dimension, attributes: gdal.Attribute },
      MDArray: { dimensions: gdal.Dimension, attributes: gdal.Attribute }
    } as Record<string, Record<string, new () => gdal.Attribute | gdal.Dimension | gdal.MDArray | gdal.Group>>
    // the MDArray collections belong to an array, not to the root group
    interface Collection {
      count: () => number
      [Symbol.iterator]: () => Iterator<unknown>
      [Symbol.asyncIterator]: () => AsyncIterator<unknown>
    }
    const owner = (ds: gdal.Dataset, tested: 'Group' | 'MDArray') =>
      (tested === 'Group' ? ds.root : ds.root.arrays.get(0)) as unknown as Record<string, Collection>
    for (const tested of Object.keys(tests) as ('Group' | 'MDArray')[]) {
      describe(`gdal.${tested}`, () => {
        for (const prop of Object.keys(tests[tested]) as ('groups' | 'arrays' | 'dimensions' | 'attributes')[]) {
          describe(`"${prop}" property`, () => {
            describe('@@asyncIterator()', () => {
              it('should iterate over the values', () => {
                const ds = gdal.open(path.resolve(__dirname, 'data', 'gfs.t00z.alnsf.nc'), 'mr')
                const collection = owner(ds, tested)[prop]
                let count = 0
                for (const i of collection) {
                  assert.instanceOf(i, tests[tested][prop])
                  count++
                }
                assert.equal(count, collection.count())
                ds.close()
              })
              it('should iterate over the values asynchronously', async () => {
                const ds = gdal.open(path.resolve(__dirname, 'data', 'gfs.t00z.alnsf.nc'), 'mr')
                const collection = owner(ds, tested)[prop]
                let count = 0
                for await (const i of collection) {
                  assert.instanceOf(i, tests[tested][prop])
                  count++
                }
                assert.equal(count, collection.count())
                ds.close()
              })
              it('should reject if the dataset is closed', () => {
                const ds = gdal.open(path.resolve(__dirname, 'data', 'gfs.t00z.alnsf.nc'), 'mr')
                // the collection is taken before the dataset is closed, otherwise
                // it is the getter which throws and the iterator is never called
                const collection = owner(ds, tested)[prop]
                ds.close()
                return assert.isRejected((async () => {
                  for await (const i of collection) void i
                })(), /already been destroyed/)
              })
            })
          })
        }
      })
    }
  }
}
