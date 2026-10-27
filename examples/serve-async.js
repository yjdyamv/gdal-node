// This example implements a simple HTTP server
// supporting multiple concurrent connections
// serving raster data from a GDAL dataset
//
// This is the same as serve.js but using ES2017 async/await

const gdal = require('..')
const http = require('http')
const path = require('path')

const datasetFile = path.join(__dirname, '..', 'test', 'data', 'sample.tif')
// the port to listen on, 0 lets the system pick a free one
const port = +(process.argv[2] || 8010)
const wgs84 = gdal.SpatialReference.fromEPSG(4326)

gdal.openAsync(datasetFile).then((ds) => {
  const xform = new gdal.CoordinateTransformation(wgs84, ds)
  console.log(`Opened ${datasetFile}`)

  const server = http.createServer(async (req, res) => {
    try {
      const params = req.url.split('/')
      if (params.length > 3 && params[1].length && params[2].length
        && !isNaN(params[2]) && ! isNaN(params[3])) {

        let x, y
        switch (params[1]) {
          case 'pixels':
            x = +params[2]
            y = +params[3]
            break
          case 'WGS84':
            {
              const coords = xform.transformPoint(+params[2], +params[3])
              x = Math.floor(coords.x)
              y = Math.floor(coords.y)
            }
            break
          default: {
            const error = new RangeError('Unsupported method')
            error.httpCode = 404
            throw error
          }
        }

        const band = await ds.bands.getAsync(1)
        const value = await band.pixels.getAsync(x, y)

        res.writeHead(200)
        res.end(`Value at position ${x}:${y} is ${value}\n`)
      } else {
        const error = new TypeError('No or invalid coordinates specified')
        error.httpCode = 400
        throw error
      }
    } catch (e) {
      // the same status codes which serve.js answers with
      res.writeHead(e.httpCode || 500)
      res.end(e.message)
    }
  })

  server.listen(port, 'localhost', () => {
    // the port which was bound, which is not the one which was asked for when
    // it is 0
    const address = server.address()
    console.log(`Server up and running on http://localhost:${address.port}`)
    console.log('Try:')
    console.log(`curl http://localhost:${address.port}/pixels/120/100`)
    console.log(`curl http://localhost:${address.port}/WGS84/-110.3/44.55`)
  })
})
