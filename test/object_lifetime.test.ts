// These objects are not public

import * as gdal from 'gdal-async'
import { assert } from 'chai'
import * as path from 'path'

describe('object cache', () => {
  it('should return same object if pointer is same', () => {
    for (let i = 0; i < 10; i++) {
      // @ts-expect-error not a public API
      gdal.log(`Object Cache test run #${i}`)
      const ds = gdal.open('temp', 'w', 'MEM', 4, 4, 1)
      const band1 = ds.bands.get(1)
      const band2 = ds.bands.get(1)
      assert.instanceOf(band1, gdal.RasterBand)
      assert.equal(band1, band2)
      assert.equal(band1.size.x, 4)
      assert.equal(band2.size.x, 4)
          global.gc!()
    }
  })
})
describe('object lifetimes', () => {
  it('datasets should stay alive until all bands go out of scope', () => {
    let ds: gdal.Dataset | null = gdal.open('temp', 'w', 'MEM', 4, 4, 1)
    let band: gdal.RasterBand | null = ds.bands.get(1)

    // @ts-expect-error not a public API
    const ds_uid = ds._uid
    // @ts-expect-error not a public API
    const band_uid = band._uid

    // this allows to dereference the object for GC
    // eslint-disable-next-line no-useless-assignment
    ds = null
      global.gc!()

      // @ts-expect-error not a public API
      assert.isTrue(gdal._isAlive(ds_uid))
      // @ts-expect-error not a public API
      assert.isTrue(gdal._isAlive(band_uid))

      // eslint-disable-next-line no-useless-assignment
      band = null
      global.gc!()

      // @ts-expect-error not a public API
      assert.isFalse(gdal._isAlive(ds_uid))
      // @ts-expect-error not a public API
      assert.isFalse(gdal._isAlive(band_uid))
  })
  it('bands should immediately be garbage collected as they go out of scope', () => {
    const ds = gdal.open('temp', 'w', 'MEM', 4, 4, 1)
    let band: gdal.RasterBand | null = ds.bands.get(1)

    // @ts-expect-error not a public API
    const ds_uid = ds._uid
    // @ts-expect-error not a public API
    const band_uid = band._uid

    // eslint-disable-next-line no-useless-assignment
    band = null
      global.gc!()

      // @ts-expect-error not a public API
      assert.isTrue(gdal._isAlive(ds_uid))
      // @ts-expect-error not a public API
      assert.isFalse(gdal._isAlive(band_uid))
  })
  it('datasets should stay alive until all layers go out of scope', () => {
    let ds: gdal.Dataset | null = gdal.open(path.join(__dirname, 'data/shp/sample.shp'))
    let layer: gdal.Layer | null = ds.layers.get(0)

    // @ts-expect-error not a public API
    const ds_uid = ds._uid
    // @ts-expect-error not a public API
    const layer_uid = layer._uid

    // eslint-disable-next-line no-useless-assignment
    ds = null
      global.gc!()

      // @ts-expect-error not a public API
      assert.isTrue(gdal._isAlive(ds_uid))
      // @ts-expect-error not a public API
      assert.isTrue(gdal._isAlive(layer_uid))

      // eslint-disable-next-line no-useless-assignment
      layer = null
      global.gc!()

      // @ts-expect-error not a public API
      assert.isFalse(gdal._isAlive(ds_uid))
      // @ts-expect-error not a public API
      assert.isFalse(gdal._isAlive(layer_uid))
  })
  it('layers should immediately be garbage collected as they go out of scope', () => {
    const ds: gdal.Dataset | null = gdal.open(path.join(__dirname, 'data/shp/sample.shp'))
    let layer: gdal.Layer | null = ds.layers.get(0)

    // @ts-expect-error not a public API
    const ds_uid = ds._uid
    // @ts-expect-error not a public API
    const layer_uid = layer._uid

    // eslint-disable-next-line no-useless-assignment
    layer = null
      global.gc!()

      // @ts-expect-error not a public API
      assert.isTrue(gdal._isAlive(ds_uid))
      // @ts-expect-error not a public API
      assert.isFalse(gdal._isAlive(layer_uid))
  })
})
describe('object ownership', () => {
  // The addon has to free the objects it creates itself - the bands the algebra
  // operators return and the copies of the field definitions - when their
  // wrappers are garbage collected. What is checked here is the lifetime of the
  // wrappers; the native objects are checked by the leak detector of the
  // AddressSanitizer build of the test suite.
  it('should free the bands created by the algebra operators', () => {
    const ds = gdal.open('temp', 'w', 'MEM', 16, 16, 1)
    const band = ds.bands.get(1)

    let computed: gdal.RasterBand | null = gdal.algebra.abs(band)
    // @ts-expect-error not a public API
    const uid = computed._uid
    // @ts-expect-error not a public API
    const ds_uid = computed.ds._uid

    // @ts-expect-error not a public API
    assert.isTrue(gdal._isAlive(uid))
    assert.equal(computed.size.x, 16)

    // eslint-disable-next-line no-useless-assignment
    computed = null
      global.gc!()

      // @ts-expect-error not a public API
      assert.isFalse(gdal._isAlive(uid))
      // the dataset the computed band owns goes away with it
      // @ts-expect-error not a public API
      assert.isFalse(gdal._isAlive(ds_uid))

      ds.close()
  })
  it('should not leak the objects it creates', () => {
    const ds = gdal.open('temp', 'w', 'MEM', 16, 16, 1)
    const band = ds.bands.get(1)
    const vds = gdal.open(path.join(__dirname, 'data/shp/sample.shp'))
    const layer = vds.layers.get(0)

    for (let i = 0; i < 200; i++) {
      // owns its dataset
      assert.instanceOf(gdal.algebra.add(band, i), gdal.RasterBand)
      // owns a copy of the OGRFieldDefn
      assert.instanceOf(layer.fields.get(0), gdal.FieldDefn)
    }
      global.gc!()

      // everything still works after the collected objects went away
      const computed = gdal.algebra.add(band, 1)
      assert.equal(computed.size.x, 16)
      // @ts-expect-error not a public API
      assert.isTrue(gdal._isAlive(computed._uid))
      assert.equal(layer.fields.get(0).name, 'path')

      ds.close()
      vds.close()
  })
})
