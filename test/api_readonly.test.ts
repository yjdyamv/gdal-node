import { assert } from 'chai'
import * as fs from 'fs'
import * as path from 'path'
import gdal from 'gdal-node'

// Every property which cannot be written is registered with the READ_ONLY_SETTER
// marker; the test parses the registration tables so that a new one cannot be
// added without being covered
const sourceFiles = (dir: string): string[] =>
  fs.readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) return sourceFiles(full)
    return entry.name.endsWith('.cpp') || entry.name.endsWith('.hpp') ? [ full ] : []
  })

const enclosing = (text: string, from: number): number => {
  let depth = 0
  for (let i = from; i < text.length; i++) {
    if (text[i] === '{') depth++
    else if (text[i] === '}' && --depth === 0) return i
  }
  throw new Error('unbalanced braces')
}

const accessor = /\b(hiddenAttr|asyncAttr|attr)\s*\(\s*"([^"]+)"([^)]*)\)/g

const readOnlyProperties = (): Map<string, Set<string>> => {
  const table = new Map<string, Set<string>>()
  for (const file of sourceFiles(path.join(__dirname, '..', 'src'))) {
    const text = fs.readFileSync(file, 'utf8')
    if (!text.includes('READ_ONLY_SETTER')) continue
    const initialize = /void\s+(\w+)::Initialize\s*\(\s*Napi::Object/g
    for (let init = initialize.exec(text); init; init = initialize.exec(text)) {
      const end = enclosing(text, init.index)
      for (const match of text.slice(init.index, end).matchAll(accessor)) {
        const [ full, , name, args ] = match
        if (!args.includes('READ_ONLY_SETTER')) continue
        const props = table.get(init[1]) ?? new Set<string>()
        props.add(name)
        // asyncAttr registers both `name` and `nameAsync` with the same setter
        if (full.startsWith('asyncAttr')) props.add(`${name}Async`)
        table.set(init[1], props)
      }
    }
  }
  return table
}

// the multidim collections share their initialization through the template in
// src/collections/group_collection.hpp, so they cannot be parsed per class
const templateCollections = [ 'GroupGroups', 'GroupArrays', 'GroupDimensions', 'GroupAttributes', 'ArrayDimensions', 'ArrayAttributes' ]
const templateProperties = [ 'ds', 'parent', 'names' ]

describe('read-only properties', () => {
  let raster: gdal.Dataset
  let vector: gdal.Dataset
  let multidim: gdal.Dataset

  const feature = () => vector.layers.get(0).features.first()!

  const instances: Record<string, () => unknown> = {
    Dataset: () => raster,
    DatasetBands: () => raster.bands,
    DatasetLayers: () => vector.layers,
    RasterBand: () => raster.bands.get(1),
    RasterBandPixels: () => raster.bands.get(1).pixels,
    Driver: () => gdal.drivers.get(0),
    ColorTable: () => new gdal.ColorTable(gdal.GPI_RGB),
    Layer: () => vector.layers.get(0),
    LayerFeatures: () => vector.layers.get(0).features,
    LayerFields: () => vector.layers.get(0).fields,
    Feature: feature,
    FeatureFields: () => feature().fields,
    FeatureDefn: () => feature().defn,
    FeatureDefnFields: () => feature().defn.fields,
    Geometry: () => new gdal.Point(1, 2),
    GeometryCollection: () => new gdal.GeometryCollection(),
    Polygon: () => new gdal.Polygon(),
    SimpleCurve: () => new gdal.LineString(),
    CompoundCurve: () => new gdal.CompoundCurve(),
    Group: () => multidim.root,
    MDArray: () => multidim.root.arrays.get(0),
    Attribute: () => multidim.root.attributes.get(0),
    Dimension: () => multidim.root.dimensions.get(0)
  }
  for (const name of templateCollections) instances[name] = () => multidim.root.groups

  const table = readOnlyProperties()
  for (const name of templateCollections) table.set(name, new Set(templateProperties))

  before(() => {
    raster = gdal.open(path.join(__dirname, 'data', 'sample.tif'))
    vector = gdal.open(path.join(__dirname, 'data', 'cutline.shp'))
    multidim = gdal.open(path.join(__dirname, 'data', 'gfs.t00z.alnsf.nc'), 'mr')
  })

  after(() => {
    raster.close()
    vector.close()
    multidim.close()
  })

  it('should cover every class with a read-only property', () => {
    const missing = Array.from(table.keys()).filter((cls) => !instances[cls])
    assert.deepEqual(missing, [], 'the test needs an instance of these classes')
    assert.isAtLeast(table.size, 20)
  })

  for (const [ cls, props ] of table) {
    describe(cls, () => {
      for (const prop of Array.from(props).sort()) {
        it(`${prop} should be read-only`, () => {
          const obj = instances[cls]() as Record<string, unknown>
          assert.throws(() => {
            obj[prop] = 42
          }, new RegExp(`^${prop} is a read-only property$`))
        })
      }
    })
  }
})
