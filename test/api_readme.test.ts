import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'
import * as cp from 'node:child_process'
import * as vm from 'node:vm'
import { assert } from 'chai'

// The examples of the README are the first thing a new user reads and the only
// part of the documentation which nobody compiles, so they are the part which
// rots first: the chapter shows `layer.extent` while the API has
// `layer.getExtent()`, and one of the blocks was not even valid JavaScript
// (`catch (e => console.error(e))`).  Every block which brings its own `gdal`
// is a complete program - the test runs it, with the name of the package
// replaced by this build and the names of the sample files replaced by the
// fixtures, and compares the output with what the block promises.

const root = path.join(__dirname, '..')
const gdalPath = path.join(root, 'lib', 'gdal.js')

// `using` and `await using` do not exist before Node.js 24, and an example which
// uses them can only be run by the versions which have them: the syntax of it is
// checked by test/api_docs.test.ts on every version
const explicitResourceManagement = ((): boolean => {
  try {
    new vm.Script('{\n  using resource = null\n  resource\n}')
    return true
  } catch (_e) {
    return false
  }
})()

const fixtures: Record<string, string> = {
  'sample.tif': path.join(__dirname, 'data', 'sample.tif'),
  'sample.shp': path.join(__dirname, 'data', 'shp', 'sample.shp')
}

const blocks = (): string[] => {
  const readme = fs.readFileSync(path.join(root, 'README.md'), 'utf8')
  return Array.from(readme.matchAll(/```js\r?\n([^]*?)```/g), (match) => match[1])
    .filter((code) => code.includes('require(') && code.includes('gdal-node'))
}

// The block as a program which node can run: the package is this build and the
// files which it opens are the fixtures.
const program = (marker: string): string => {
  const block = blocks().find((code) => code.includes(marker))
  assert.isDefined(block, `the README has no example with ${marker}`)
  let source = (block as string)
    .replace(/require\((['"])gdal-node\1\)/g, `require(${JSON.stringify(gdalPath)})`)
  for (const [ name, file ] of Object.entries(fixtures)) {
    source = source.split(`'${name}'`).join(JSON.stringify(file))
    source = source.split(`"${name}"`).join(JSON.stringify(file))
  }
  // `await` is only valid in a module or in an async function
  if (source.includes('await ')) {
    return `(async () => {\n${source}\n})()`
  }
  return source
}

const run = (marker: string): string => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'gdal-readme-'))
  const file = path.join(directory, 'example.js')
  fs.writeFileSync(file, program(marker))
  // the examples which print a raster print about 800 000 numbers
  return cp.execFileSync(process.execPath, [ file ], { encoding: 'utf8', maxBuffer: 128 * 1024 * 1024 })
}

const lines = (output: string): string[] => output.split(/\r?\n/).filter((line) => line.length > 0)

// An example which prints a value of the API prints `undefined` when the
// member which it names does not exist - which is exactly what the vector
// example did with `layer.extent`, a member of the API of GDAL1 which is called
// `getExtent()` today - and `NaN` when it names one which the file does not have.
const meaningful = (output: string): void => {
  for (const line of lines(output)) {
    assert.notInclude(line, 'undefined', `the example prints undefined: ${line}`)
    assert.notInclude(line, 'NaN', `the example prints NaN: ${line}`)
  }
}

describe('the examples of the README', () => {
  it('should read a raster synchronously', () => {
    const output = run('number of bands')
    assert.include(lines(output), 'number of bands: 1')
    assert.include(lines(output), 'width: 984')
    assert.include(lines(output), 'height: 804')
    assert.match(output, /^geotransform: -?[\d.]+(?:,-?[\d.]+){5}$/m)
    assert.match(output, /^srs: PROJCS/m)
    meaningful(output)
  })
  it('should read a vector synchronously', () => {
    const output = run('number of features')
    assert.include(lines(output), 'number of features: 23')
    assert.include(lines(output), 'fields: path,name,type,long_name,fips_num,fips,state_fips,state_abbr')
    assert.match(output, /^extent: \{"minX":-111\.\d+,"minY":40\.\d+,"maxX":-104\.\d+,"maxY":45\.\d+\}$/m)
    assert.match(output, /^srs: GEOGCS\["NAD83"/m)
    meaningful(output)
  })
  it('should read a raster with callbacks', () => {
    const output = run('pixels.readAsync(0, 0, dataset.rasterSize.x')
    assert.match(output, /Uint8Array\(791136\)/)
  })
  it('should read a raster with promises', () => {
    const output = run('.then((dataset) => dataset.bands.getAsync(1))')
    assert.match(output, /Uint8Array\(791136\)/)
  })
  it('should read a raster with async/await', () => {
    // this one prints nothing, it only has to run
    assert.equal(run('await ds.rasterSizeAsync'), '')
  })
  it('should close the dataset at the end of the block', function (this: Mocha.Context) {
    if (!explicitResourceManagement) {
      console.log('the example requires `using`, which Node.js 24 introduced')
      this.skip()
      return
    }
    // this one prints nothing either, the `using` blocks close the datasets
    assert.equal(run('using ds = gdal.open('), '')
  })
})
