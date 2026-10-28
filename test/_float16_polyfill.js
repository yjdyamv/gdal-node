// A child process of test/api_float16.test.ts: the polyfill of Float16Array is
// used only when the global constructor is missing, which is the case only on
// Node.js < 24, so the constructor is deleted here before the module is loaded.
// The script does the work and prints a JSON report of it; the assertions are in
// the test, which is the only place where a failure has a name.

delete global.Float16Array

const gdal = require('../lib/gdal.js')

const values = [ 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5, 10.5, 11.5, 12.5 ]
const report = { global: typeof global.Float16Array }

const ds = gdal.open('float16-polyfill', 'w', 'MEM', 4, 3, 1, gdal.GDT_Float16)
const band = ds.bands.get(1)

report.dataType = band.dataType === gdal.GDT_Float16
report.write = band.pixels.write(0, 0, 4, 3, new gdal.Float16Array(values)) instanceof gdal.Float16Array

const given = new gdal.Float16Array(values.length)
report.given = band.pixels.read(0, 0, 4, 3, given) === given
report.givenValues = Array.from(given)

const allocated = band.pixels.read(0, 0, 4, 3)
report.allocated = allocated instanceof gdal.Float16Array
report.allocatedValues = Array.from(allocated)

const block = band.pixels.readBlock(0, 0)
report.block = block instanceof gdal.Float16Array
report.blockValues = Array.from(block)

// the methods of a band of any other type must be called as they were before
const mem = gdal.open('float32-polyfill', 'w', 'MEM', 2, 2, 1, gdal.GDT_Float32)
const other = mem.bands.get(1)
other.pixels.write(0, 0, 2, 2, new Float32Array([ 1, 2, 3, 4 ]))
const otherRead = other.pixels.read(0, 0, 2, 2)
report.otherType = otherRead.constructor.name
report.otherValues = Array.from(otherRead)

const collect = async () => {
  // the chunks of a stream of a Float16 band used to be refused by the native
  // code, because lib/readable.js does not name the array of a block read
  const chunks = await new Promise((resolve, reject) => {
    const collected = []
    const stream = band.pixels.createReadStream()
    stream.on('data', (chunk) => collected.push(chunk))
    stream.on('error', reject)
    stream.on('end', () => resolve(collected))
  })
  report.stream = chunks.every((chunk) => chunk instanceof gdal.Float16Array)
  report.streamValues = chunks.reduce((all, chunk) => all.concat(Array.from(chunk)), [])

  const read = await band.pixels.readAsync(0, 0, 4, 3)
  report.asyncRead = read instanceof gdal.Float16Array
  report.asyncReadValues = Array.from(read)

  const readBlock = await band.pixels.readBlockAsync(0, 0)
  report.asyncBlock = readBlock instanceof gdal.Float16Array
  report.asyncBlockValues = Array.from(readBlock)

  await band.pixels.writeAsync(0, 0, 4, 3, new gdal.Float16Array(values.map((v) => v * 2)))
  report.asyncWriteValues = Array.from(band.pixels.read(0, 0, 4, 3))

  const filled = new gdal.Float16Array(Array.from({ length: block.length }, () => 7))
  await band.pixels.writeBlockAsync(0, 0, filled)
  report.asyncWriteBlockValues = Array.from(band.pixels.read(0, 0, 4, 3))

  // a destroyed band must reject the promise of the asynchronous methods: the
  // polyfill decides whether it has to make the array which the caller did not
  // give by asking the band for its data type, and that question has to be left
  // to the native method when the band is gone
  const pixels = band.pixels
  ds.close()
  mem.close()
  report.closedBlock = await pixels.readBlockAsync(0, 0).then(
    () => 'resolved',
    (e) => e.message
  )
  report.closedRead = await pixels.readAsync(0, 0, 4, 3).then(
    () => 'resolved',
    (e) => e.message
  )

  process.stdout.write(JSON.stringify(report))
}

collect().catch((e) => {
  console.error(e)
  process.exitCode = 1
})
