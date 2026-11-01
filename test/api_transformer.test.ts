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

  beforeEach(() => {
    directory = fs.mkdtempSync(path.join(os.tmpdir(), 'gdal-transformer-'))
    filename = path.join(directory, 'test.tif')
  })

  afterEach(() => {
    try {
      ds.close()
    } catch (_e) { /* already closed */ }
    global.gc!()
    fs.rmSync(directory, { recursive: true, force: true })
  })

  describe('gdal.Transformer', () => {
    let wgs84: gdal.SpatialReference
    let webmercator: gdal.SpatialReference

    beforeEach(() => {
      wgs84 = gdal.SpatialReference.fromEPSG(4326)
      webmercator = gdal.SpatialReference.fromEPSG(3857)
    })

    describe('constructor', () => {
      it('should accept two SpatialReferences', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        assert.instanceOf(transformer, gdal.Transformer)
      })

      it('should accept a Dataset', () => {
        ds = gdal.open(filename, 'w', 'GTiff', 10, 10, 1, gdal.GDT_Byte)
        ds.srs = wgs84
        ds.geoTransform = [ 1, 1, 0, 10, 0, -1 ]
        ds.flush()
        assert.instanceOf(new gdal.Transformer(ds), gdal.Transformer)
      })

      it('should throw when "src" is missing', () => {
        assert.throws(() => {
          new (gdal.Transformer as unknown as new () => gdal.Transformer)()
        }, /src must be given/)
      })

      it('should throw when "src" is neither a SpatialReference nor a Dataset', () => {
        assert.throws(() => {
          new (gdal.Transformer as unknown as new (a: string, b: gdal.SpatialReference) => gdal.Transformer)(
            'x',
            webmercator
          )
        }, /src must be a SpatialReference or a Dataset/)
      })

      it('should throw when "dst" is missing', () => {
        assert.throws(() => {
          new (gdal.Transformer as unknown as new (a: gdal.SpatialReference) => gdal.Transformer)(wgs84)
        }, /dst must be a SpatialReference/)
      })

      it('should throw on a dataset without a geotransform', () => {
        ds = gdal.drivers.get('MEM').create('', 10, 10, 1, gdal.GDT_Byte)
        assert.throws(() => new gdal.Transformer(ds), /Unable to compute a transformation/)
      })
    })

    describe('transformPoint', () => {
      it('should transform a point', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        const pt = transformer.transformPoint(false, { x: 1, y: 1 })
        assert.closeTo(pt.x, 111319.49, 0.01)
        assert.closeTo(pt.y, 111325.14, 0.01)
        assert.equal(pt.z, 0)
      })

      it('should leave the origin at the origin', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        const pt = transformer.transformPoint(false, { x: 0, y: 0 })
        assert.equal(pt.x, 0)
        assert.equal(pt.y, 0)
      })

      it('should carry "z" through', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        assert.equal(transformer.transformPoint(false, { x: 1, y: 1, z: 5 }).z, 5)
      })

      it('should transform in the other direction when dstToSrc is true', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        const pt = transformer.transformPoint(true, { x: 111319.49079327357, y: 111325.1428663851 })
        assert.closeTo(pt.x, 1, 1e-6)
        assert.closeTo(pt.y, 1, 1e-6)
      })

      it('should use the geotransform of a Dataset', () => {
        ds = gdal.open(filename, 'w', 'GTiff', 10, 10, 1, gdal.GDT_Byte)
        ds.srs = wgs84
        ds.geoTransform = [ 1, 1, 0, 10, 0, -1 ]
        ds.flush()
        const transformer = new gdal.Transformer(ds)
        assert.deepEqual(transformer.transformPoint(false, { x: 0, y: 0 }), { x: 1, y: 10, z: 0 })
        assert.deepEqual(transformer.transformPoint(false, { x: 1, y: 1 }), { x: 2, y: 9, z: 0 })
      })

      it('should throw on a point which cannot be transformed', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        assert.throws(() => transformer.transformPoint(false, { x: 1000, y: 1000 }), /Invalid latitude/)
      })

      it('should throw when "point" is missing', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        assert.throws(() => {
          (transformer as unknown as Record<string, (a: boolean) => void>).transformPoint(false)
        }, /point must be given/)
      })

      it('should throw when "x" is missing', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        assert.throws(() => transformer.transformPoint(false, {} as unknown as gdal.Point), /x must be given/)
      })
    })

    describe('transformPoints', () => {
      it('should transform many points', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        const pts = transformer.transformPoints(false, [
          { x: 0, y: 0 },
          { x: 1, y: 1 }
        ])
        assert.lengthOf(pts, 2)
        assert.equal(pts[0].x, 0)
        assert.closeTo(pts[1].x, 111319.49, 0.01)
      })

      it('should throw when any of the points cannot be transformed', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        assert.throws(() => {
          transformer.transformPoints(false, [
            { x: 0, y: 0 },
            { x: 1000, y: 1000 }
          ])
        }, /Invalid latitude/)
      })

      it('should accept an empty array', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        assert.deepEqual(transformer.transformPoints(false, []), [])
      })

      it('should throw when the array contains something else than objects', () => {
        const transformer = new gdal.Transformer(wgs84, webmercator)
        assert.throws(() => {
          transformer.transformPoints(false, [ 1 as unknown as gdal.Point ])
        }, /points must contain only objects/)
      })
    })

    describe('the lifetime', () => {
      it('should not crash when the transformer is garbage collected', () => {
        for (let i = 0; i < 10; i++) {
          const transformer = new gdal.Transformer(wgs84, webmercator)
          transformer.transformPoint(false, { x: 0, y: 0 })
        }
        global.gc!()
        assert.isOk(true)
      })
    })
  })
})
