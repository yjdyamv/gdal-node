import { assert } from 'chai'
import gdal from 'gdal-node'

describe('geometry async', () => {
  let ring: gdal.LinearRing
  let square: gdal.Polygon
  let point_inner: gdal.Point
  let point_outer: gdal.Point
  let line_cross: gdal.LineString
  let line_nocross: gdal.LineString
  let arc: gdal.CircularString

  before(() => {
    ring = new gdal.LinearRing()
    ring.points.add({ x: 0, y: 0 })
    ring.points.add({ x: 10, y: 0 })
    ring.points.add({ x: 10, y: 10 })
    ring.points.add({ x: 0, y: 10 })
    ring.closeRings()

    square = new gdal.Polygon()
    square.rings.add(ring)

    point_inner = new gdal.Point(5, 5)
    point_outer = new gdal.Point(0, 20)

    line_cross = new gdal.LineString()
    line_cross.points.add({ x: -5, y: 5 })
    line_cross.points.add({ x: 15, y: 5 })

    line_nocross = new gdal.LineString()
    line_nocross.points.add({ x: -5, y: 20 })
    line_nocross.points.add({ x: 15, y: 20 })

    arc = new gdal.CircularString()
    arc.points.add({ x: 0, y: 5 })
    arc.points.add({ x: 10, y: 0 })
    arc.points.add({ x: 0, y: -5 })
  })

  describe('containsAsync()', () => {
    it('should return the correct result', async () => {
      assert.isTrue(await square.containsAsync(point_inner))
      assert.isFalse(await square.containsAsync(point_outer))
    })
  })

  describe('withinAsync()', () => {
    it('should return the correct result', async () => {
      assert.isTrue(await point_inner.withinAsync(square))
      assert.isFalse(await point_outer.withinAsync(square))
    })
  })

  describe('intersectsAsync()', () => {
    it('should return the correct result', async () => {
      assert.isTrue(await point_inner.intersectsAsync(square))
      assert.isFalse(await point_outer.intersectsAsync(square))
    })
  })

  describe('crossesAsync()', () => {
    it('should return the correct result', async () => {
      assert.isFalse(await point_inner.crossesAsync(square))
      assert.isTrue(await line_cross.crossesAsync(square))
      assert.isFalse(await line_nocross.crossesAsync(square))
    })
  })

  describe('touchesAsync()', () => {
    it('should return the correct result', async () => {
      assert.isFalse(await point_inner.touchesAsync(square))
      assert.isTrue(await new gdal.Point(0, 5).touchesAsync(square))
    })
  })

  describe('equalsAsync()', () => {
    it('should return the correct result', async () => {
      assert.isTrue(await point_inner.equalsAsync(new gdal.Point(5, 5)))
      assert.isFalse(await point_inner.equalsAsync(point_outer))
      assert.isFalse(await point_inner.equalsAsync(square))
    })
  })

  describe('isEmptyAsync()', () => {
    it('should return the correct result', async () => {
      assert.isFalse(await ring.isEmptyAsync())
      assert.isTrue(await new gdal.LinearRing().isEmptyAsync())
    })
  })

  describe('emptyAsync()', () => {
    it('should empty the geometry', async () => {
      const line = new gdal.LineString()
      line.points.add({ x: 1, y: 1 })
      await line.emptyAsync()
      assert.isTrue(line.isEmpty())
      assert.equal(line.points.count(), 0)
    })
  })

  describe('closeRingsAsync()', () => {
    it('should close the ring', async () => {
      const open = new gdal.LinearRing()
      open.points.add({ x: 0, y: 0 })
      open.points.add({ x: 1, y: 0 })
      open.points.add({ x: 1, y: 1 })
      await open.closeRingsAsync()
      assert.equal(open.points.count(), 4)
      const first = open.points.get(0)
      const last = open.points.get(3)
      assert.deepEqual([ first.x, first.y ], [ last.x, last.y ])
    })
  })

  describe('isRingAsync()', () => {
    it('should return the same result as isRing()', async () => {
      const open = new gdal.LinearRing()
      open.points.add({ x: 0, y: 0 })
      open.points.add({ x: 1, y: 0 })
      open.points.add({ x: 1, y: 1 })
      const closed = new gdal.LineString()
      closed.points.add({ x: 1, y: 0 })
      closed.points.add({ x: 11, y: 0 })
      closed.points.add({ x: 11, y: 10 })
      closed.points.add({ x: 1, y: 0 })
      for (const geometry of [ ring, square, open, closed ]) {
        // eslint-disable-next-line no-await-in-loop
        assert.equal(await geometry.isRingAsync(), geometry.isRing())
      }
      assert.isTrue(await closed.isRingAsync())
      assert.isFalse(await square.isRingAsync())
    })
  })

  describe('isSimpleAsync()', () => {
    it('should return the same result as isSimple()', async () => {
      const bowtie = new gdal.LineString()
      bowtie.points.add({ x: 0, y: 0 })
      bowtie.points.add({ x: 10, y: 10 })
      bowtie.points.add({ x: 0, y: 10 })
      bowtie.points.add({ x: 10, y: 0 })
      for (const geometry of [ ring, square, line_cross, line_nocross, bowtie ]) {
        // eslint-disable-next-line no-await-in-loop
        assert.equal(await geometry.isSimpleAsync(), geometry.isSimple())
      }
      assert.isTrue(await line_cross.isSimpleAsync())
      assert.isFalse(await bowtie.isSimpleAsync())
    })
  })

  describe('isValidAsync()', () => {
    it('should return the correct result', async () => {
      assert.isTrue(await square.isValidAsync())
      const invalid = new gdal.Polygon()
      const bowtie = new gdal.LinearRing()
      bowtie.points.add({ x: 0, y: 0 })
      bowtie.points.add({ x: 10, y: 10 })
      bowtie.points.add({ x: 0, y: 10 })
      bowtie.points.add({ x: 10, y: 0 })
      bowtie.points.add({ x: 0, y: 0 })
      invalid.rings.add(bowtie)
      assert.isFalse(await invalid.isValidAsync())
    })
  })

  describe('swapXYAsync()', () => {
    it('should swap the coordinates', async () => {
      const point = new gdal.Point(1, 2)
      await point.swapXYAsync()
      assert.equal(point.x, 2)
      assert.equal(point.y, 1)
    })
  })

  describe('symDifferenceAsync()', () => {
    it('should return the symmetric difference', async () => {
      const other = new gdal.Polygon()
      const other_ring = new gdal.LinearRing()
      other_ring.points.add({ x: 20, y: 0 })
      other_ring.points.add({ x: 30, y: 0 })
      other_ring.points.add({ x: 30, y: 10 })
      other_ring.points.add({ x: 20, y: 10 })
      other_ring.closeRings()
      other.rings.add(other_ring)

      const result = await square.symDifferenceAsync(other)
      assert.instanceOf(result, gdal.Geometry)
      assert.equal((result as gdal.Polygon).getArea(), 200)

      const same = await square.symDifferenceAsync(square)
      assert.isTrue(await same.isEmptyAsync())
    })
  })

  describe('transformAsync()', () => {
    it('should transform the geometry', async () => {
      const wgs84 = gdal.SpatialReference.fromProj4('+init=epsg:4326')
      const mercator = gdal.SpatialReference.fromProj4('+init=epsg:3857')
      const transformation = new gdal.CoordinateTransformation(wgs84, mercator)
      const point = new gdal.Point(10, 20)
      await point.transformAsync(transformation)
      assert.closeTo(point.x, 1113194.9079327357, 1e-3)
      assert.closeTo(point.y, 2273030.926987689, 1e-3)
    })
  })

  describe('transformToAsync()', () => {
    it('should transform the geometry to the given spatial reference', async () => {
      const point = new gdal.Point(10, 20)
      point.srs = gdal.SpatialReference.fromProj4('+init=epsg:4326')
      await point.transformToAsync(gdal.SpatialReference.fromProj4('+init=epsg:3857'))
      assert.closeTo(point.x, 1113194.9079327357, 1e-3)
      assert.closeTo(point.y, 2273030.926987689, 1e-3)
    })
  })
})
