import gdal from 'gdal-async'
import { assert } from 'chai'
import * as cp from 'child_process'
import * as http from 'http'
import * as path from 'path'

const example = (name: string): string => path.join(__dirname, '..', 'examples', name)
const fixture = (name: string): string => path.join(__dirname, 'data', name)

interface Run {
  status: number
  stdout: string
  stderr: string
}

// The examples are what a user copies to start a program of their own, so they
// are run the way a user runs them: as a program of its own, with the module
// loaded from the source tree
const run = (name: string, ...args: string[]): Run => {
  try {
    return {
      status: 0,
      stdout: cp.execFileSync(process.execPath, [ example(name), ...args ], {
        encoding: 'utf8',
        stdio: [ 'ignore', 'pipe', 'pipe' ]
      }),
      stderr: ''
    }
  } catch (e) {
    const error = e as { status?: number | null, stdout?: string, stderr?: string }
    return { status: error.status ?? -1, stdout: error.stdout ?? '', stderr: error.stderr ?? '' }
  }
}

interface Result {
  status: number
  body: string
}

interface Server {
  base: string
  get: (path: string) => Promise<Result>
  stop: () => void
}

const get = (url: string): Promise<Result> =>
  new Promise((resolve, reject) => {
    http.get(url, (res) => {
      let body = ''
      res.setEncoding('utf8')
      res.on('data', (chunk: string) => {
        body += chunk
      })
      res.on('end', () => resolve({ status: res.statusCode ?? 0, body }))
    }).on('error', reject)
  })

// The servers of the examples listen on the port which they are given with 0
// letting the system pick a free one, so that any number of them can run at once
const serve = (name: string): Promise<Server> =>
  new Promise((resolve, reject) => {
    const child = cp.spawn(process.execPath, [ example(name), '0' ], { stdio: [ 'ignore', 'pipe', 'pipe' ] })
    let output = ''
    let errors = ''
    let started = false

    child.stdout.setEncoding('utf8')
    child.stdout.on('data', (data: string) => {
      output += data
      const address = /Server up and running on (http:\/\/\S+)/.exec(output)
      if (address !== null && !started) {
        started = true
        resolve({ base: address[1], get, stop: () => child.kill() })
      }
    })
    child.stderr.setEncoding('utf8')
    child.stderr.on('data', (data: string) => {
      errors += data
    })
    child.on('error', reject)
    child.on('exit', (code) => {
      if (!started) reject(new Error(`${name} exited with code ${code}: ${errors}`))
    })
  })

describe('the examples', () => {
  describe('gdalinfo.js', () => {
    it('should describe a raster which is georeferenced', () => {
      const { status, stdout } = run('gdalinfo.js', fixture('sample.tif'))
      assert.equal(status, 0)
      assert.match(stdout, /^GDAL Version: \d+\.\d+\.\d+/)
      assert.include(stdout, 'Driver: GTiff')
      assert.include(stdout, 'Size is 984, 804')
      assert.include(stdout, 'Coordinate System is: ')
      assert.include(stdout, 'PROJCS["unnamed"')
      assert.include(stdout, 'Origin = (-1134675.2952829634, 2485710.4658232867)')
      assert.include(stdout, 'Pixel Size = (7.502071930146189, -7.502071930145942)')
      assert.include(stdout, 'Corner Coordinates:')
      assert.include(stdout, 'Upper Left ')
      assert.include(stdout, 'Center      ')
      assert.include(stdout, 'Band 1 Block=984x8 Type=Byte, ColorInterp=Gray')
    })

    it('should describe a raster without a spatial reference', () => {
      // the world file of this raster gives it a geotransform, so the corners can
      // be placed, but there is no coordinate system to describe them in
      const { status, stdout } = run('gdalinfo.js', fixture('CM13ct.png'))
      assert.equal(status, 0)
      assert.include(stdout, 'Driver: PNG')
      assert.include(stdout, 'Size is 1951, 3109')
      assert.include(stdout, 'Coordinate System is: \nnull\n')
      assert.include(stdout, 'Origin = (674425.2950174398, 6462458.631486551)')
      assert.notInclude(stdout, 'Corner Coordinates:')
    })

    it('should describe a raster without a geotransform', () => {
      // this raster is georeferenced by ground control points, which the example
      // does not report, and it has neither a coordinate system nor a geotransform
      const { status, stdout } = run('gdalinfo.js', fixture('12_791_1476.jpg'))
      assert.equal(status, 0)
      assert.include(stdout, 'Driver: JPEG')
      assert.include(stdout, 'No geotransform')
      assert.notInclude(stdout, 'Origin =')
      assert.notInclude(stdout, 'Corner Coordinates:')
    })

    it('should report the statistics which the raster has', () => {
      const { stdout } = run('gdalinfo.js', fixture('CM13ct.png'))
      assert.include(stdout, 'Min=1')
      assert.include(stdout, 'Max=253')
      assert.include(stdout, 'NoData Value=0')
    })

    it('should not report what the raster does not have', () => {
      // no statistics, no scale, no offset and no nodata value: all of them are
      // null, and the example used to print them as if they were zero
      const { stdout } = run('gdalinfo.js', fixture('sample.tif'))
      assert.notInclude(stdout, 'Min=')
      assert.notInclude(stdout, 'Max=')
      assert.notInclude(stdout, 'NoData Value')
      assert.notInclude(stdout, 'Offset:')
    })

    it('should refuse to run without a filename', () => {
      const { status, stderr } = run('gdalinfo.js')
      assert.equal(status, 1)
      assert.include(stderr, 'Filename must be provided')
    })

    it('should refuse a vector file', () => {
      const { status, stderr } = run('gdalinfo.js', fixture('cutline.shp'))
      assert.equal(status, 1)
      assert.include(stderr, 'Source file is not a raster')
    })
  })

  describe('ogrinfo.js', () => {
    it('should describe a vector file', () => {
      const { status, stdout } = run('ogrinfo.js', fixture('cutline.shp'))
      assert.equal(status, 0)
      assert.include(stdout, 'Driver = ESRI Shapefile')
      assert.include(stdout, 'Layers: ')
      assert.include(stdout, '0: cutline')
      assert.include(stdout, 'Geometry Type = Polygon')
      assert.include(stdout, 'Spatial Reference = GEOGCS["WGS 84"')
      assert.include(stdout, 'minX = -110.35323516675017')
      assert.include(stdout, 'maxY = 44.58461535851358')
      assert.include(stdout, '-id (integer64)')
      assert.include(stdout, 'Feature Count = 1')
    })

    it('should refuse to run without a filename', () => {
      const { status, stderr } = run('ogrinfo.js')
      assert.equal(status, 1)
      assert.include(stderr, 'Filename must be provided')
    })

    it('should refuse a raster file', () => {
      const { status, stderr } = run('ogrinfo.js', fixture('sample.tif'))
      assert.equal(status, 1)
      assert.include(stderr, 'Source file is not a vector')
    })
  })

  // the two servers serve the same values, the second one with the async API
  for (const name of [ 'serve.js', 'serve-async.js' ]) {
    describe(name, () => {
      let server: Server

      before(async function () {
        this.timeout(30000)
        server = await serve(name)
      })

      after(() => {
        server.stop()
      })

      it('should serve the value of a pixel', async () => {
        const dataset = gdal.open(fixture('sample.tif'))
        const value = dataset.bands.get(1).pixels.get(120, 100)
        dataset.close()

        const result = await server.get(`${server.base}/pixels/120/100`)
        assert.equal(result.status, 200)
        assert.equal(result.body, `Value at position 120:100 is ${value}\n`)
      })

      it('should serve the value of a pixel given in WGS84', async () => {
        const dataset = gdal.open(fixture('sample.tif'))
        const point = new gdal.CoordinateTransformation(
          gdal.SpatialReference.fromEPSG(4326), dataset
        ).transformPoint(-110.3, 44.55)
        const value = dataset.bands.get(1).pixels.get(Math.floor(point.x), Math.floor(point.y))
        dataset.close()

        const result = await server.get(`${server.base}/WGS84/-110.3/44.55`)
        assert.equal(result.status, 200)
        assert.equal(
          result.body,
          `Value at position ${Math.floor(point.x)}:${Math.floor(point.y)} is ${value}\n`
        )
      })

      it('should refuse invalid coordinates', async () => {
        const result = await server.get(`${server.base}/pixels/abc/100`)
        assert.equal(result.status, 400)
        assert.equal(result.body, 'No or invalid coordinates specified')
      })

      it('should refuse an unsupported method', async () => {
        const result = await server.get(`${server.base}/EPSG/1/2`)
        assert.equal(result.status, 404)
        assert.equal(result.body, 'Unsupported method')
      })
    })
  }
})
