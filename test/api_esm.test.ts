import gdal from 'gdal-node'
import { assert } from 'chai'
import * as path from 'path'

type Module = typeof import('gdal-node')

const fixture = (name: string): string => path.join(__dirname, 'data', name)

// The module is CommonJS, so an ES module which imports it gets the whole module
// as the default export, plus the classes as named exports which lib/gdal.mjs
// re-exports for the "import" condition of the exports map
const importModule = async (): Promise<Module & { default: Module }> =>
  (await import('gdal-node')) as Module & { default: Module }

describe('ES modules', () => {
  it('should import the module by its name', async () => {
    const module = await importModule()
    assert.equal(module.default, gdal)
    assert.isFunction(module.default.open)
  })

  it('should have the classes as named exports', async () => {
    const module = await importModule()
    assert.equal(module.Dataset, gdal.Dataset)
    assert.equal(module.RasterBand, gdal.RasterBand)
    assert.equal(module.Geometry, gdal.Geometry)
    assert.equal(module.Envelope, gdal.Envelope)
    assert.equal(module.GDALOpenError, gdal.GDALOpenError)
    assert.equal(module.AbortError, gdal.AbortError)
  })

  it('should build the objects of the named exports', async () => {
    const module = await importModule()
    const dataset = module.default.open(fixture('sample.tif'))
    assert.instanceOf(dataset, module.Dataset)
    assert.instanceOf(dataset.bands.get(1), module.RasterBand)
    dataset.close()
  })

  it('should report the failures of the named exports', async () => {
    const module = await importModule()
    assert.throws(() => module.default.open(fixture('does_not_exist.tif')), module.GDALOpenError)
  })
})
