import gdal from 'gdal-node'
import { assert } from 'chai'
import * as path from 'path'
import * as util from 'util'

const fixture = (name: string): string => path.join(__dirname, 'data', name)

describe('util.inspect', () => {
  it('should describe a raster dataset', () => {
    const dataset = gdal.open(fixture('sample.tif'))
    // a long path is shortened, so only the end of the description is checked
    assert.isTrue(util.inspect(dataset).endsWith('sample.tif", size: [984, 804], bands: 1 }'))
    dataset.close()
  })

  it('should describe a vector dataset', () => {
    const dataset = gdal.open(fixture('cutline.shp'))
    assert.isTrue(util.inspect(dataset).endsWith('cutline.shp", layers: 1 }'))
    dataset.close()
  })

  it('should describe a dataset which has been closed', () => {
    const dataset = gdal.open(fixture('sample.tif'))
    dataset.close()
    assert.equal(util.inspect(dataset), '<closed Dataset>')
  })

  it('should describe a raster band', () => {
    const dataset = gdal.open(fixture('sample.tif'))
    const band = dataset.bands.get(1)
    assert.equal(util.inspect(band), 'RasterBand { id: 1, description: "", size: [984, 804], dataType: "Byte" }')
    dataset.close()
  })

  it('should describe a band which has been closed', () => {
    const dataset = gdal.open(fixture('sample.tif'))
    const band = dataset.bands.get(1)
    dataset.close()
    assert.equal(util.inspect(band), '<closed RasterBand>')
  })

  it('should describe a geometry', () => {
    const geometry = new gdal.LineString()
    assert.equal(util.inspect(geometry), 'LineString { wkbType: 2, empty }')
    geometry.points.add(1, 2)
    assert.equal(util.inspect(geometry), 'LineString { wkbType: 2 }')
  })

  it('should describe a feature', () => {
    const dataset = gdal.open(fixture('cutline.shp'))
    const feature = dataset.layers.get(0).features.get(0)
    const description = util.inspect(feature)
    assert.isTrue(description.startsWith('Feature { fid: 0, fields: {'), description)
    dataset.close()
  })

  it('should be used when the object is nested', () => {
    const dataset = gdal.open(fixture('sample.tif'))
    assert.isTrue(util.inspect([ dataset ]).includes('bands: 1'))
    dataset.close()
  })
})
