// A child process of test/api_resources.test.ts: it opens a dataset for
// writing, writes into it and then ends without ever closing it, either by
// falling off the end of the script or by calling process.exit(), which is the
// case where the runtime does not run the finalizers of the addon objects.
// The test reopens the file to see what the addon did with the data.

const gdal = require('../lib/gdal.js')

const file = process.argv[2]
const style = process.argv[3]
const size = 64
const value = 42

const ds = gdal.open(file, 'w', 'GTiff', size, size, 1, gdal.GDT_Byte)
ds.bands.get(1).pixels.write(0, 0, size, size, new Uint8Array(size * size).fill(value))

if (style === 'keep') {
  gdal.closeAllOnExit = false
}

if (style === 'broken') {
  // a dataset which refuses to be closed must not stop the closing of the
  // other ones, nor make the exit fail
  ds.close = () => {
    throw new Error('this dataset cannot be closed')
  }
}

if (style !== 'natural') {
  process.exit(0)
}
