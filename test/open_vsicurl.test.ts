import * as gdal from 'gdal-node'
import { assert } from 'chai'
import * as path from 'path'
import { startServer } from './utils/http'

// The files are served by a local HTTP server instead of being downloaded from
// S3: what these tests check is the /vsicurl driver and its chaining with
// /vsigzip, not the availability of an object store, and a 25 MB download which
// the rest of the suite is competing for is how they used to become flaky.

describe('Open', () => {
  afterEach(() => void global.gc!())

  describe('vsicurl', () => {
    let base: string
    let stop: () => void
    let expected: string | undefined
    let ds: gdal.Dataset

    before(async () => {
      const local = gdal.open(path.join(__dirname, 'data', 'sample.tif'))
      expected = local.srs?.toWKT()
      local.close()
      ;({ base, stop } = await startServer())
    })

    after(() => {
      if (ds) ds.close()
      stop()
    })

    it('should not throw', () => {
      ds = gdal.open(`/vsicurl/${base}/sample.tif`)
    })

    it('should be able to read raster size', () => {
      assert.equal(ds.rasterSize.x, 984)
      assert.equal(ds.rasterSize.y, 804)
      assert.equal(ds.bands.count(), 1)
    })

    it('should have projection', () => {
      assert.equal(ds.srs?.toWKT(), expected)
    })

    it('should be able to read pixels', () => {
      assert.equal(ds.bands.get(1).pixels.read(0, 0, 4, 4).length, 16)
    })
  })

  describe('vsicurl/Async', () => {
    let base: string
    let stop: () => void
    let ds: Promise<gdal.Dataset>

    before(async () => ({ base, stop } = await startServer()))

    after(() => {
      if (ds) ds.then((r) => r.close())
      stop()
    })

    it('should not throw', () => {
      ds = gdal.openAsync(`/vsicurl/${base}/sample.tif`)
      return assert.isFulfilled(ds)
    })

    it('should be able to read raster size', () =>
      assert.isFulfilled(
        Promise.all([
          assert.eventually.equal(
            ds.then((r) => r.rasterSize.x),
            984
          ),
          assert.eventually.equal(
            ds.then((r) => r.rasterSize.y),
            804
          ),
          assert.eventually.equal(
            ds.then((r) => r.bands.count()),
            1
          )
        ])
      ))
  })

  describe('vsigzip/vsicurl chaining', () => {
    let base: string
    let stop: () => void
    let ds: gdal.Dataset

    before(async () => ({ base, stop } = await startServer()))

    after(() => {
      if (ds) ds.close()
      stop()
    })

    it('should not throw', () => {
      ds = gdal.open(`/vsigzip//vsicurl/${base}/sample.tif.gz`)
    })

    it('should be able to read raster size', () => {
      assert.equal(ds.rasterSize.x, 984)
      assert.equal(ds.rasterSize.y, 804)
      assert.equal(ds.bands.count(), 1)
    })

    it('should have projection', () => {
      assert.isNotNull(ds.srs)
    })
  })
})
