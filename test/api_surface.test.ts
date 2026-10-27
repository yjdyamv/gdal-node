import { assert } from 'chai'
import * as path from 'path'
import gdal from 'gdal-async'
import { startServer } from './utils/http'

// The members whose only test is here: everything which the rest of the suite
// does not call at all
describe('API surface', () => {
  describe('the multidim collections', () => {
    let ds: gdal.Dataset

    before(() => {
      ds = gdal.open(path.join(__dirname, 'data', 'gfs.t00z.alnsf.nc'), 'mr')
    })
    after(() => {
      ds.close()
      void global.gc!()
    })

    // the multidim collections share the shape of the group collections, but
    // they are four different types, so they are exercised through this one
    interface MultidimCollection<T> {
      readonly ds: gdal.Dataset
      readonly parent: gdal.Group | gdal.MDArray
      readonly names: string[]
      count(): number
      countAsync(): Promise<number>
      get(id: number): T
      getAsync(id: number): Promise<T>
      forEach(cb: (item: T) => void): void
      map<V>(cb: (item: T) => V): V[]
    }
    const describe_ = (item: object) => (item as { description?: string }).description
    const collections = (): MultidimCollection<gdal.Attribute | gdal.Dimension | gdal.Group | gdal.MDArray>[] =>
      [ ds.root.groups, ds.root.arrays, ds.root.dimensions, ds.root.attributes ]

    it('should know the dataset', () => {
      for (const collection of collections()) {
        assert.strictEqual(collection.ds, ds)
      }
    })

    it('should know the parent object', () => {
      // the collections of a group belong to the group, the collections of an
      // array to the array
      const root = ds.root
      for (const collection of collections()) {
        assert.strictEqual(collection.parent, root)
      }
      const array = root.arrays.get(0)
      for (const collection of [ array.dimensions, array.attributes ]) {
        assert.strictEqual(collection.parent, array)
      }
    })

    it('should iterate with forEach()', () => {
      for (const collection of collections()) {
        let seen = 0
        collection.forEach(() => {
          ++seen
        })
        assert.equal(seen, collection.count())
      }
    })

    it('should map with map()', () => {
      for (const collection of collections()) {
        assert.lengthOf(collection.map((item) => item), collection.count())
      }
    })

    it('should count and get asynchronously', async () => {
      for (const collection of collections()) {
        // eslint-disable-next-line no-await-in-loop
        assert.equal(await collection.countAsync(), collection.count())
        if (collection.count() > 0) {
          // the multidim objects are not cached, so they are compared by value
          // eslint-disable-next-line no-await-in-loop
          assert.equal(describe_(await collection.getAsync(0)), describe_(collection.get(0)))
        }
      }
    })

    it('should have the collections of an array', async () => {
      const array = ds.root.arrays.get(0)
      assert.instanceOf(array.dimensions, gdal.ArrayDimensions)
      assert.instanceOf(array.attributes, gdal.ArrayAttributes)
      assert.deepEqual(array.dimensions.names, [ 'time' ])
      assert.isAbove(array.attributes.names.length, 0)
      assert.equal(array.dimensions.get(0).description, '/time')

      for (const collection of [ array.dimensions, array.attributes ]) {
        assert.strictEqual(collection.ds, ds)
        assert.strictEqual(collection.parent, array)
        // eslint-disable-next-line no-await-in-loop
        assert.equal(await collection.countAsync(), collection.count())
        if (collection.count() > 0) {
          // eslint-disable-next-line no-await-in-loop
          assert.equal(describe_(await collection.getAsync(0)), describe_(collection.get(0)))
        }
        let seen = 0
        collection.forEach(() => {
          ++seen
        })
        assert.equal(seen, collection.count())
        assert.lengthOf(collection.map((item) => item), collection.count())
      }
    })

    it('should throw when getting a group which does not exist', async () => {
      assert.equal(ds.root.groups.count(), 0)
      assert.throws(() => ds.root.groups.get(0), /Element not found: index 0/)
      await assert.isRejected(ds.root.groups.getAsync(0), /Element not found: index 0/)
    })

    it('should name the element which is not there', () => {
      // the message is the same for every collection: which one was being
      // asked is not something the caller can have got wrong
      const array = ds.root.arrays.get(0)
      assert.throws(() => ds.root.groups.get('bogus'), /Element not found: bogus/)
      assert.throws(() => ds.root.arrays.get('bogus'), /Element not found: bogus/)
      assert.throws(() => ds.root.dimensions.get('bogus'), /Element not found: bogus/)
      assert.throws(() => array.dimensions.get('bogus'), /Element not found: bogus/)
      assert.throws(() => array.dimensions.get(99), /Element not found: index 99/)
      assert.throws(() => array.attributes.get('bogus'), /Element not found: bogus/)
    })
  })

  describe('the collections', () => {
    it('should iterate a ColorTable with forEach() and map()', () => {
      const table = new gdal.ColorTable(gdal.GPI_RGB)
      table.set(0, { c1: 1, c2: 2, c3: 3, c4: 4 })
      table.set(1, { c1: 5, c2: 6, c3: 7, c4: 8 })
      const seen: { c1: number }[] = []
      table.forEach((entry) => {
        seen.push(entry)
      })
      assert.lengthOf(seen, 2)
      assert.equal(seen[0].c1, 1)
      assert.deepEqual(table.map((entry) => entry.c1), [ 1, 5 ])
    })

    it('should iterate the fields with forEach() and map()', () => {
      const ds = gdal.open(path.join(__dirname, 'data', 'cutline.shp'))
      const feature = ds.layers.get(0).features.first()!
      const defn_fields = feature.defn.fields
      assert.lengthOf(defn_fields.map((field) => field.name), defn_fields.count())
      const seen: gdal.FieldDefn[] = []
      defn_fields.forEach((field) => {
        seen.push(field)
      })
      assert.lengthOf(seen, defn_fields.count())

      const fields = feature.fields
      assert.deepEqual(fields.map((value) => value), Object.values(fields.toObject()))
      ds.close()
    })

    it('should iterate the children of a GeometryCollection with forEach() and map()', () => {
      const collection = new gdal.GeometryCollection()
      collection.children.add(new gdal.Point(1, 2))
      collection.children.add(new gdal.Point(3, 4))
      const seen: gdal.Geometry[] = []
      collection.children.forEach((child) => {
        seen.push(child)
      })
      assert.lengthOf(seen, 2)
      assert.lengthOf(collection.children.map((child) => child), 2)
    })

    it('should require a callback for forEach() and map()', () => {
      const ds = gdal.open(path.join(__dirname, 'data', 'sample.tif'))
      const lds = gdal.open(path.join(__dirname, 'data', 'cutline.shp'))
      const layer = lds.layers.get(0)
      const mds = gdal.open(path.join(__dirname, 'data', 'gfs.t00z.alnsf.nc'), 'mr')
      const array = mds.root.arrays.get(0)
      // a callback which is not a function used to be reported by V8 when the
      // first element was reached (`callback is not a function`), which says
      // nothing about the call which was made
      const collections = {
        bands: ds.bands,
        layers: ds.layers,
        features: layer.features,
        fields: layer.fields,
        feature_fields: layer.features.first()!.fields,
        defn_fields: layer.features.first()!.defn.fields,
        children: new gdal.GeometryCollection().children,
        points: new gdal.LineString().points,
        curves: new gdal.CompoundCurve().curves,
        groups: mds.root.groups,
        arrays: mds.root.arrays,
        dimensions: array.dimensions,
        attributes: array.attributes
      } as unknown as Record<string, {
        forEach: (cb?: unknown) => void
        map: (cb?: unknown) => unknown[]
      }>
      // the third argument of assert.throws() is the message of the assertion and
      // not the expected message, so the error is caught here to check both its
      // class and its text
      for (const [ name, collection ] of Object.entries(collections)) {
        for (const method of [ 'forEach', 'map' ]) {
          let error: unknown
          try {
            (collection as unknown as Record<string, () => void>)[method]()
          } catch (e) {
            error = e
          }
          assert.instanceOf(error, TypeError, `${name}.${method}()`)
          assert.equal((error as Error).message, 'callback must be a function', `${name}.${method}()`)
        }
      }
      ds.close()
      lds.close()
      mds.close()
    })
  })

  describe('the geometry statics', () => {
    it('should build the multi geometries', () => {
      // the constructors of the multi geometries take no arguments
      const point = new gdal.MultiPoint()
      point.children.add(new gdal.Point(1, 2))
      assert.instanceOf(point, gdal.MultiPoint)
      assert.equal(point.children.count(), 1)
      const line = new gdal.MultiLineString()
      line.children.add(new gdal.LineString())
      assert.instanceOf(line, gdal.MultiLineString)
      const ring = new gdal.LinearRing()
      ring.points.add({ x: 0, y: 0 })
      ring.points.add({ x: 1, y: 0 })
      ring.points.add({ x: 1, y: 1 })
      ring.closeRings()
      const polygon = new gdal.MultiPolygon()
      polygon.children.add(new gdal.Polygon())
      assert.instanceOf(polygon, gdal.MultiPolygon)
      // the aliases of the constructors are the constructors themselves
      assert.instanceOf(new gdal.Point.Multi(), gdal.MultiPoint)
      assert.instanceOf(new gdal.LineString.Multi(), gdal.MultiLineString)
      assert.instanceOf(new gdal.LinearRing.Multi(), gdal.MultiLineString)
      assert.instanceOf(new gdal.Polygon.Multi(), gdal.MultiPolygon)
    })

    it('should polygonize a MultiLineString', () => {
      const segment = (points: number[][]) => {
        const line = new gdal.LineString()
        for (const [ x, y ] of points) {
          line.points.add({ x, y })
        }
        return line
      }
      const multi = new gdal.MultiLineString()
      for (const side of [
        [ [ 0, 0 ], [ 10, 0 ] ],
        [ [ 10, 0 ], [ 10, 10 ] ],
        [ [ 10, 10 ], [ 0, 10 ] ],
        [ [ 0, 10 ], [ 0, 0 ] ]
      ]) {
        multi.children.add(segment(side))
      }
      const polygons = multi.polygonize()
      assert.instanceOf(polygons, gdal.GeometryCollection)
      assert.equal(polygons.children.count(), 1)
      assert.instanceOf(polygons.children.get(0), gdal.Polygon)
      assert.closeTo((polygons.children.get(0) as gdal.Polygon).getArea(), 100, 1e-6)

      // the lines which do not close do not form a polygon
      const open = new gdal.MultiCurve()
      open.children.add(segment([ [ 0, 0 ], [ 10, 0 ] ]))
      assert.isNull(open.polygonize())
    })

    it('should tell the name of a geometry type', () => {
      assert.equal(gdal.Geometry.getName(gdal.wkbPoint), 'Point')
      assert.equal(gdal.Geometry.getName(gdal.wkbPolygon), 'Polygon')
    })
  })

  describe('the geometry members', () => {
    it('should interpolate a point with value()', () => {
      const line = new gdal.LineString()
      line.points.add({ x: 0, y: 0 })
      line.points.add({ x: 10, y: 0 })
      const middle = line.value(5)
      assert.closeTo(middle.x, 5, 1e-6)
      assert.closeTo(middle.y, 0, 1e-6)
      // the distance is clamped to the curve, as it is in GDAL
      assert.closeTo(line.value(-1).x, 0, 1e-6)
      assert.closeTo(line.value(1e9).x, 10, 1e-6)
    })

    it('should refuse to interpolate on an empty curve', () => {
      // the curve of a geometry which has no vertices has no first and no last
      // vertex, and the interpolation of GDAL reads both of them without
      // looking: asking for the point of an empty curve segfaulted the process
      assert.throws(() => new gdal.LineString().value(0), /Cannot get the value of an empty curve/)
      assert.throws(() => new gdal.LinearRing().value(0), /Cannot get the value of an empty curve/)
      assert.throws(() => new gdal.LineString().value(NaN), /Cannot get the value of an empty curve/)
    })

    it('should give the length of a GeometryCollection', () => {
      const collection = new gdal.GeometryCollection()
      collection.children.add(new gdal.Point(0, 0))
      collection.children.add(new gdal.Point(10, 0))
      assert.closeTo(collection.getLength(), 0, 1e-6)
      const lines = new gdal.MultiLineString()
      const line = new gdal.LineString()
      line.points.add({ x: 0, y: 0 })
      line.points.add({ x: 10, y: 0 })
      lines.children.add(line)
      assert.closeTo(lines.getLength(), 10, 1e-6)
    })

    it('should convert the curves to an array', () => {
      const curve = new gdal.CompoundCurve()
      const line = new gdal.LineString()
      line.points.add({ x: 0, y: 0 })
      line.points.add({ x: 10, y: 0 })
      curve.curves.add(line)
      const array = curve.curves.toArray()
      assert.lengthOf(array, 1)
      assert.instanceOf(array[0], gdal.SimpleCurve)
      assert.lengthOf(Array.from(curve.curves), 1)
    })

    it('should transform to another spatial reference', () => {
      const point = new gdal.Point(10, 20)
      point.srs = gdal.SpatialReference.fromProj4('+init=epsg:4326')
      point.transformTo(gdal.SpatialReference.fromProj4('+init=epsg:3857'))
      assert.closeTo(point.x, 1113194.9079327357, 1e-3)
      assert.closeTo(point.y, 2273030.926987689, 1e-3)
    })
  })

  describe('the raster band members', () => {
    let ds: gdal.Dataset
    let band: gdal.RasterBand

    before(() => {
      ds = gdal.open(path.join(__dirname, 'data', 'sample.tif'))
      band = ds.bands.get(1)
    })
    after(() => {
      ds.close()
      void global.gc!()
    })

    it('should compute the element-wise statistics against another band', () => {
      assert.isNumber(band.max(band).pixels.read(0, 0, 1, 1)[0])
      assert.isNumber(band.min(band).pixels.read(0, 0, 1, 1)[0])
      assert.isNumber(band.mean(band).pixels.read(0, 0, 1, 1)[0])
    })

    it('should count and get the overviews asynchronously', async () => {
      assert.equal(await band.overviews.countAsync(), band.overviews.count())
      // the overviews are built in memory, the fixture is never written to
      const source = gdal.open(path.join(__dirname, 'data', 'sample.tif'))
      const ds2 = gdal.open('/vsimem/overviews.tif', 'w', 'GTiff',
        source.rasterSize.x, source.rasterSize.y, source.bands.count(), gdal.GDT_Byte)
      ds2.bands.get(1).pixels.write(0, 0, source.rasterSize.x, source.rasterSize.y,
        source.bands.get(1).pixels.read(0, 0, source.rasterSize.x, source.rasterSize.y))
      source.close()
      const band2 = ds2.bands.get(1)
      assert.equal(band2.overviews.count(), 0)
      await ds2.buildOverviewsAsync('AVERAGE', [ 2 ])
      assert.equal(band2.overviews.count(), 1)
      const overview = await band2.overviews.getAsync(0)
      assert.instanceOf(overview, gdal.RasterBand)
      assert.equal(overview.size.x, Math.floor(band2.size.x / 2))
      ds2.close()
    })

    it('should flush a layer asynchronously', async () => {
      const vector = gdal.open(path.join(__dirname, 'data', 'cutline.shp'), 'r+')
      await vector.layers.get(0).flushAsync()
      assert.isTrue(vector.layers.get(0).flushAsync() instanceof Promise)
      vector.close()
    })
  })

  describe('the asynchronous algebra', () => {
    let ds: gdal.Dataset
    let band: gdal.RasterBand

    before(() => {
      ds = gdal.open('temp', 'w', 'MEM', 4, 4, 2, gdal.GDT_Float32)
      band = ds.bands.get(1)
      band.pixels.write(0, 0, 4, 4, new Float32Array(16).fill(2))
      ds.bands.get(2).pixels.write(0, 0, 4, 4, new Float32Array(16).fill(4))
    })
    after(() => {
      ds.close()
      void global.gc!()
    })

    const unary = [ 'abs', 'sqrt', 'log', 'log10', 'not' ] as const
    const binary = [
      'add', 'sub', 'mul', 'div', 'pow', 'lt', 'lte', 'gt', 'gte', 'eq', 'notEq', 'and', 'or',
      'min', 'max', 'mean'
    ] as const

    for (const name of unary) {
      it(`${name}Async() should accept a band`, async () => {
        const result = await gdal.algebra[`${name}Async`](band)
        assert.instanceOf(result, gdal.RasterBand)
      })
    }

    for (const name of binary) {
      it(`${name}Async() should accept two bands`, async () => {
        const result = await gdal.algebra[`${name}Async`](band, ds.bands.get(2))
        assert.instanceOf(result, gdal.RasterBand)
      })
    }

    it('ifThenElseAsync() should accept three bands', async () => {
      const condition = gdal.algebra.gt(band, ds.bands.get(2))
      const result = await gdal.algebra.ifThenElseAsync(condition, band, ds.bands.get(2))
      assert.instanceOf(result, gdal.RasterBand)
    })

    it('asTypeAsync() should change the data type', async () => {
      const result = await gdal.algebra.asTypeAsync(band, gdal.GDT_Int32)
      assert.equal(result.dataType, gdal.GDT_Int32)
    })
  })

  describe('the spatial reference statics', () => {
    let server: { base: string, stop: () => void }

    before(async () => {
      // the synchronous imports block the event loop, so the files are served
      // by a server which runs in its own process
      server = await startServer()
    })
    after(() => {
      server.stop()
    })

    it('should import a spatial reference from a CRS URL', async () => {
      const srs = gdal.SpatialReference.fromCRSURL('http://www.opengis.net/def/crs/EPSG/0/4326')
      assert.equal(srs.getAuthorityCode('GEOGCS'), '4326')
      const async_ = await gdal.SpatialReference.fromCRSURLAsync('http://www.opengis.net/def/crs/EPSG/0/3857')
      assert.isTrue(async_.isSameGeogCS(srs))
      assert.include(async_.toProj4(), '+proj=merc')
    })

    it('should import a spatial reference from a URL', async () => {
      // cutline.prj is a WKT string, which is what importFromUrl downloads and
      // parses
      const srs = gdal.SpatialReference.fromURL(`${server.base}/cutline.prj`)
      assert.equal(srs.toProj4(), '+proj=longlat +datum=WGS84 +no_defs')
      const async_ = await gdal.SpatialReference.fromURLAsync(`${server.base}/cutline.prj`)
      assert.equal(async_.toProj4(), srs.toProj4())
    })

    it('should fail to import a URL which is not there', async () => {
      assert.throws(() => gdal.SpatialReference.fromURL(`${server.base}/missing.prj`), /HTTP error code : 404/)
      await assert.isRejected(gdal.SpatialReference.fromURLAsync(`${server.base}/missing.prj`), /HTTP error code : 404/)
    })
  })

  // the tests which need the PROJ database have to come before the module level
  // functions: `setPROJSearchPaths` replaces the search path of the process and
  // cannot be undone
  describe('the module-level functions', () => {
    it('should have a Driver with openAsync()', async () => {
      const driver = gdal.drivers.get('GTiff')
      const ds = await driver.openAsync(path.join(__dirname, 'data', 'sample.tif'))
      assert.instanceOf(ds, gdal.Dataset)
      ds.close()
    })

    it('should polygonize asynchronously', async () => {
      const src = gdal.open(path.join(__dirname, 'data', 'sample.tif'))
      const band = src.bands.get(1)
      const dst = gdal.open('temp', 'w', 'Memory')
      const layer = dst.layers.create('polygons', null, gdal.Polygon)
      layer.fields.add(new gdal.FieldDefn('val', gdal.OFTInteger))
      await gdal.polygonizeAsync({ src: band, dst: layer, pixValField: 0 })
      assert.isAbove(layer.features.count(), 0)
      src.close()
      dst.close()
    })

    it('should switch the logging on and off', () => {
      gdal.verbose()
      gdal.quiet()
      gdal.stopLogging()
    })

    it('should set the PROJ search paths', () => {
      // this leaves PROJ without a usable database for the rest of the process
      gdal.setPROJSearchPaths(path.join(__dirname, 'data'))
      assert.isUndefined(gdal.setPROJSearchPaths('/nonexistent'))
      // the name which this function had before it was registered under the
      // name given in its documentation
      gdal.setPROJSearchPath(path.join(__dirname, 'data'))
    })

    it('should acquire the locks of three datasets', async () => {
      // the entry point of the stress test, which is the only other user
      const locks = gdal as unknown as {
        _acquireLocks: (...datasets: gdal.Dataset[]) => void
        _acquireLocksAsync: (...datasets: gdal.Dataset[]) => Promise<void>
      }
      const ds1 = gdal.open(path.join(__dirname, 'data', 'sample.tif'))
      const ds2 = gdal.open(path.join(__dirname, 'data', 'multiband.tif'))
      const ds3 = gdal.open(path.join(__dirname, 'data', 'gfs.t00z.alnsf.nc'), 'mr')
      assert.isUndefined(locks._acquireLocks(ds1, ds2, ds3))
      assert.isUndefined(await locks._acquireLocksAsync(ds1, ds2, ds3))
      assert.throws(() => locks._acquireLocks(ds1, ds2), /ds3 must be given/)
      ds1.close()
      ds2.close()
      ds3.close()
    })
  })

  describe('the error classes', () => {
    const classes = [
      [ 'GDALError', 'GDALError', null ],
      [ 'GDALOpenError', 'GDALOpenError', 'CPLE_OpenFailed' ],
      [ 'GDALOutOfMemoryError', 'GDALOutOfMemoryError', 'CPLE_OutOfMemory' ],
      [ 'GDALFileIOError', 'GDALFileIOError', 'CPLE_FileIO' ],
      [ 'GDALIllegalArgumentError', 'GDALIllegalArgumentError', 'CPLE_IllegalArg' ],
      [ 'GDALUnsupportedError', 'GDALUnsupportedError', 'CPLE_NotSupported' ],
      [ 'GDALAssertionError', 'GDALAssertionError', 'CPLE_AssertionFailed' ],
      [ 'GDALNoWriteAccessError', 'GDALNoWriteAccessError', 'CPLE_NoWriteAccess' ],
      [ 'GDALUserInterruptError', 'GDALUserInterruptError', 'CPLE_UserInterrupt' ],
      [ 'GDALObjectNullError', 'GDALObjectNullError', 'CPLE_ObjectNull' ],
      [ 'GDALHttpError', 'GDALHttpError', 'CPLE_HttpResponse' ],
      [ 'GDALAWSError', 'GDALAWSError', 'CPLE_AWSError' ],
      [ 'AbortError', 'AbortError', 'ABORT_ERR' ]
    ] as const

    for (const [ name, expectedName, expectedCode ] of classes) {
      it(`${name} should have a name and a code`, () => {
        const klass = (gdal as unknown as Record<string, typeof gdal.GDALError>)[name]
        assert.strictEqual(klass.prototype.name, expectedName)
        if (expectedCode) {
          assert.strictEqual(
            (klass.prototype as unknown as { code: string }).code,
            expectedCode
          )
        }
        const error = new klass('boom')
        assert.equal(error.message, 'boom')
        assert.equal(error.name, expectedName)
        assert.instanceOf(error, gdal.GDALError)
        assert.isString(error.stack)
      })
    }

    it('should expose the size of a Float16', () => {
      assert.strictEqual(gdal.Float16Array.BYTES_PER_ELEMENT, 2)
    })
  })
})
