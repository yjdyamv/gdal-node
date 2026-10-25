import gdal from 'gdal-async'
import * as os from 'os'

import * as chai from 'chai'
import chaiAsPromised from 'chai-as-promised'
chai.use(chaiAsPromised)

let noFailNet = function (this: Mocha.Context) {
  let test: Mocha.Suite | Mocha.Test | undefined = this.currentTest
  while (test) {
    if (test.title.match(/w\/Net/)) {
      this.retries(3)
    }
    test = test.parent
  }
}

if (!process.env.MOCHA_TEST_NETWORK || +process.env.MOCHA_TEST_NETWORK === 0) {
  noFailNet = function (this: Mocha.Context) {
    let test: Mocha.Suite | Mocha.Test | undefined = this.currentTest
    while (test) {
      if (test.title.match(/w\/Net/)) {
        console.log('test requires networking, run with MOCHA_TEST_NETWORK=1 npm test to enable:', test.title)
        this.skip()
      }
      test = test.parent
    }
  }
}

const platformSkip = function (this: Mocha.Context) {
  let test: Mocha.Suite | Mocha.Test | undefined = this.currentTest
  while (test) {
    if (test.title.match(/on Linux/) && os.platform() != 'linux') {
      console.log('test requires Linux: ', test.title)
      this.skip()
    }
    test = test.parent
  }
}

const bundledSkip = function (this: Mocha.Context) {
  let test: Mocha.Suite | Mocha.Test | undefined = this.currentTest
  while (test) {
    if (test.title.match(/with bundled GDAL/) && !gdal.bundled) {
      console.log('test requires bundled version of GDAL: ', test.title)
      this.skip()
    }
    test = test.parent
  }
}

// The driver collection is the last thing holding on to the native driver
// objects, so dropping it and running the GC right before the process exits is
// what lets the ASAN leak check see them being freed. This has to happen at the
// exit of the process and not in a root `afterAll` hook: mocha calls the root
// hooks once per file in parallel mode, so every file of a worker after the
// first one would find `gdal.drivers` deleted
const cleanup = () => {
  // @ts-expect-error not a public API
  delete gdal.drivers
  global.gc!()
}
process.once('exit', cleanup)

exports.mochaHooks = {
  beforeEach: function (done: () => void) {
    platformSkip.call(this)
    noFailNet.call(this)
    bundledSkip.call(this)
    done()
  }
}
