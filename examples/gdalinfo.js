const gdal = require('../lib/gdal.js')
const util = require('util')

console.log(`GDAL Version: ${gdal.version}, source: ${gdal.bundled ? 'bundled with gdal-node' : 'system-installed library'}`)

const filename = process.argv[2]
if (!filename) {
  console.error('Filename must be provided')
  process.exit(1)
}

const ds = gdal.open(filename)

const driver = ds.driver
const driver_metadata = driver.getMetadata()
if (driver_metadata.DCAP_RASTER !== 'YES') {
  console.error('Source file is not a raster')
  process.exit(1)
}

console.log(`Driver: ${driver.description}`)

// raster dimensions
const size = ds.rasterSize
console.log(`Size is ${size.x}, ${size.y}`)

// spatial reference
// a raster may have none at all, as is the case with a plain image
const srs = ds.srs
console.log('Coordinate System is: ')
console.log(srs === null ? 'null' : srs.toPrettyWKT())

// geotransform
// a raster which is georeferenced with ground control points, or not at all,
// has none
const geotransform = ds.geoTransform

if (geotransform === null) {
  console.log('No geotransform')
} else {
  console.log(`Origin = (${geotransform[0]}, ${geotransform[3]})`)
  console.log(`Pixel Size = (${geotransform[1]}, ${geotransform[5]})`)
  console.log('GeoTransform =')
  console.log(geotransform)

  // corners
  // the corners are placed by the geotransform and then transformed to WGS84,
  // so they need the spatial reference of the raster as well as its geotransform
  if (srs !== null) {
    const corners = {
      'Upper Left  ': { x: 0, y: 0 },
      'Upper Right ': { x: size.x, y: 0 },
      'Bottom Right': { x: size.x, y: size.y },
      'Bottom Left ': { x: 0, y: size.y },
      'Center      ': { x: size.x / 2, y: size.y / 2 }
    }

    const wgs84 = gdal.SpatialReference.fromEPSG(4326)
    const coord_transform = new gdal.CoordinateTransformation(srs, wgs84)

    console.log('Corner Coordinates:')
    const corner_names = Object.keys(corners)
    corner_names.forEach((corner_name) => {
      // convert pixel x,y to the coordinate system of the raster
      // then transform it to WGS84
      const corner = corners[corner_name]
      const pt_orig = {
        x:
          geotransform[0] + corner.x * geotransform[1] + corner.y * geotransform[2],
        y: geotransform[3] + corner.x * geotransform[4] + corner.y * geotransform[5]
      }
      const pt_wgs84 = coord_transform.transformPoint(pt_orig)
      const description = util.format(
        '%s (%d, %d) (%s, %s)',
        corner_name,
        Math.floor(pt_orig.x * 100) / 100,
        Math.floor(pt_orig.y * 100) / 100,
        gdal.decToDMS(pt_wgs84.x, 'Long'),
        gdal.decToDMS(pt_wgs84.y, 'Lat')
      )
      console.log(description)
    })
  }
}

// bands
ds.bands.forEach((band) => {
  const description = util.format(
    'Band %d Block=%dx%d Type=%s, ColorInterp=%s',
    band.id,
    band.blockSize.x,
    band.blockSize.y,
    band.dataType,
    band.colorInterpretation
  )
  console.log(description)

  if (band.description) {
    console.log(`  Description = ${band.description}`)
  }
  // the statistics are null until something computes them
  if (band.minimum !== null) {
    console.log(`  Min=${Math.floor(band.minimum * 1000) / 1000}`)
  }
  if (band.maximum !== null) {
    console.log(`  Max=${Math.floor(band.maximum * 1000) / 1000}`)
  }
  // a band without a nodata value reports it as NaN
  if (band.noDataValue !== null && !Number.isNaN(band.noDataValue)) {
    console.log(`  NoData Value=${band.noDataValue}`)
  }

  // band overviews
  const overview_info = []
  band.overviews.forEach((overview) => {
    let overview_description = `${overview.size.x}x${overview.size.y}`

    const metadata = overview.getMetadata()
    if (metadata.RESAMPLING === 'AVERAGE_BIT2') {
      overview_description += '*'
    }

    overview_info.push(overview_description)
  })

  if (overview_info.length > 0) {
    console.log(`  Overviews: ${overview_info.join(', ')}`)
  }
  if (band.hasArbitraryOverviews) {
    console.log('  Overviews: arbitrary')
  }
  if (band.unitType) {
    console.log(`  Unit Type: ${band.unitType}`)
  }

  // category names
  const category_names = band.categoryNames
  if (category_names.length > 0) {
    console.log('  Category Names: ')
    for (let i = 0; i < category_names.length; i++) {
      console.log(`    ${i}: ${category_names[i]}`)
    }
  }

  // the scale and the offset are null unless the band has them, the identity
  // value of a band which has none being 1 and 0
  if (band.scale !== null || band.offset !== null) {
    const scale = band.scale === null ? 1 : band.scale
    const offset = band.offset === null ? 0 : band.offset
    console.log(`  Offset: ${offset},   Scale: ${scale}`)
  }

  // band metadata
  let metadata = band.getMetadata()
  let keys = Object.keys(metadata)
  if (keys.length > 0) {
    console.log('  Metadata:')
    keys.forEach((key) => {
      console.log(`    ${key}=${metadata[key]}`)
    })
  }

  metadata = band.getMetadata('IMAGE_STRUCTURE')
  keys = Object.keys(metadata)
  if (keys.length > 0) {
    console.log('  Image Structure Metadata:')
    keys.forEach((key) => {
      console.log(`    ${key}=${metadata[key]}`)
    })
  }
})
