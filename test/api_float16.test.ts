import * as gdal from 'gdal-node'
import { assert } from 'chai'
import * as cp from 'child_process'
import * as path from 'path'

describe('the Float16 rasters', () => {
  // A Float16Array is the only typed array which the addon cannot hand over to
  // the native code as it is: on Node.js < 24 it does not exist and the polyfill
  // is not an ArrayBufferView. The arrays of the caller travel as tagged
  // DataViews, and the arrays which the native code makes for itself are made by
  // lib/gdal.js, so both of those are checked here - the second one only in a
  // child process, which can pretend to be an old Node.js
  const values = [ 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5, 10.5, 11.5, 12.5 ]
  let ds: gdal.Dataset
  let band: gdal.RasterBand

  beforeEach(() => {
    ds = gdal.open('float16', 'w', 'MEM', 4, 3, 1, gdal.GDT_Float16)
    band = ds.bands.get(1)
  })

  afterEach(() => {
    ds.close()
  })

  it('should keep the data type of the band', () => {
    assert.equal(band.dataType, gdal.GDT_Float16)
  })

  it('should write and read the array of the caller', () => {
    const given = new gdal.Float16Array(values)
    assert.instanceOf(band.pixels.write(0, 0, 4, 3, given), gdal.Float16Array)
    const back = new gdal.Float16Array(values.length)
    assert.strictEqual(band.pixels.read(0, 0, 4, 3, back), back)
    assert.deepEqual(Array.from(back), values)
    assert.deepEqual(Array.from(given), values)
  })

  it('should make the array of a read which does not name one', () => {
    band.pixels.write(0, 0, 4, 3, new gdal.Float16Array(values))
    const data = band.pixels.read(0, 0, 4, 3)
    assert.instanceOf(data, gdal.Float16Array)
    assert.deepEqual(Array.from(data), values)
  })

  it('should make the array of a block read which does not name one', () => {
    band.pixels.write(0, 0, 4, 3, new gdal.Float16Array(values))
    const data = band.pixels.readBlock(0, 0)
    assert.instanceOf(data, gdal.Float16Array)
    assert.deepEqual(Array.from(data), values.slice(0, data.length))
  })

  it('should write and read asynchronously', async () => {
    assert.instanceOf(
      await band.pixels.writeAsync(0, 0, 4, 3, new gdal.Float16Array(values)),
      gdal.Float16Array
    )
    const data = await band.pixels.readAsync(0, 0, 4, 3)
    assert.instanceOf(data, gdal.Float16Array)
    assert.deepEqual(Array.from(data), values)

    const back = new gdal.Float16Array(values.length)
    assert.strictEqual(await band.pixels.readAsync(0, 0, 4, 3, back), back)
    assert.deepEqual(Array.from(back), values)
  })

  it('should write and read blocks asynchronously', async () => {
    const block = band.pixels.readBlock(0, 0)
    const filled = new gdal.Float16Array(
      Array.from({ length: block.length }, () => 7)
    )
    assert.isUndefined(await band.pixels.writeBlockAsync(0, 0, filled))
    const data = await band.pixels.readBlockAsync(0, 0)
    assert.instanceOf(data, gdal.Float16Array)
    assert.deepEqual(Array.from(data), Array.from(filled))
  })

  it('should stream the band', async () => {
    band.pixels.write(0, 0, 4, 3, new gdal.Float16Array(values))
    const chunks = await new Promise<gdal.TypedArray[]>((resolve, reject) => {
      const collected: gdal.TypedArray[] = []
      const stream = band.pixels.createReadStream()
      stream.on('data', (chunk) => collected.push(chunk))
      stream.on('error', reject)
      stream.on('end', () => resolve(collected))
    })
    assert.isNotEmpty(chunks)
    for (const chunk of chunks) {
      assert.instanceOf(chunk, gdal.Float16Array)
    }
    const all = chunks.reduce<number[]>((acc, chunk) => acc.concat(Array.from(chunk)), [])
    assert.deepEqual(all, values)
  })

  describe('the polyfill', () => {
    // the global constructor has to be deleted before the module is loaded,
    // which cannot be done in the process which runs the rest of the suite
    type Report = {
      global: string
      dataType: boolean
      write: boolean
      given: boolean
      givenValues: number[]
      allocated: boolean
      allocatedValues: number[]
      block: boolean
      blockValues: number[]
      otherType: string
      otherValues: number[]
      asyncRead: boolean
      asyncReadValues: number[]
      asyncBlock: boolean
      asyncBlockValues: number[]
      asyncWriteValues: number[]
      asyncWriteBlockValues: number[]
      stream: boolean
      streamValues: number[]
      closedBlock: string
      closedRead: string
    }
    let report: Report

    before(function () {
      this.timeout(60000)
      const script = path.join(__dirname, '_float16_polyfill.js')
      report = JSON.parse(cp.execFileSync(process.execPath, [ script ], { encoding: 'utf8' })) as Report
    })

    it('should not define a global constructor', () => {
      assert.equal(report.global, 'undefined')
    })

    it('should write and read the array of the caller', () => {
      assert.isTrue(report.dataType)
      assert.isTrue(report.write)
      assert.isTrue(report.given)
      assert.deepEqual(report.givenValues, values)
    })

    it('should make the array of a read which does not name one', () => {
      assert.isTrue(report.allocated)
      assert.deepEqual(report.allocatedValues, values)
    })

    it('should make the array of a block read which does not name one', () => {
      assert.isTrue(report.block)
      assert.deepEqual(report.blockValues, values.slice(0, report.blockValues.length))
    })

    it('should not change the other data types', () => {
      assert.equal(report.otherType, 'Float32Array')
      assert.deepEqual(report.otherValues, [ 1, 2, 3, 4 ])
    })

    it('should stream the band', () => {
      assert.isTrue(report.stream)
      assert.deepEqual(report.streamValues, values)
    })

    it('should write and read asynchronously', () => {
      assert.isTrue(report.asyncRead)
      assert.deepEqual(report.asyncReadValues, values)
      assert.deepEqual(report.asyncWriteValues, values.map((v) => v * 2))
    })

    it('should write and read blocks asynchronously', () => {
      assert.isTrue(report.asyncBlock)
      assert.deepEqual(report.asyncBlockValues, values.slice(0, report.asyncBlockValues.length))
      assert.deepEqual(
        report.asyncWriteBlockValues,
        report.asyncWriteBlockValues.map((v, i) => (i < report.asyncBlockValues.length ? 7 : values[i] * 2))
      )
    })

    it('should reject instead of throwing when the band is destroyed', () => {
      // the polyfill asks the band for its data type before the native method is
      // called, which is where the destroyed band is reported; it must leave
      // that report to the native method, which rejects the promise
      assert.equal(report.closedBlock, 'RasterBand object has already been destroyed')
      assert.equal(report.closedRead, 'RasterBand object has already been destroyed')
    })
  })
})
