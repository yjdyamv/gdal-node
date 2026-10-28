// mocha always merges the `spec` of the config file with the files given on the
// command line, so `npx mocha test/api_abort.test.ts` used to run the whole
// suite. Replace the default glob when the command line selects files itself.
// The paths of the hooks are absolute: this config is found from the test
// directory too, where a relative 'test/_common.ts' does not resolve.
const fs = require('fs')
const path = require('path')

const root = __dirname
const files = process.argv
  .slice(2)
  .filter((arg) => !arg.startsWith('-') && fs.existsSync(arg))

module.exports = {
  spec: files.length ? files : [ path.join(root, 'test', '*.test.ts').replace(/\\/g, '/') ],
  require: [ 'tsx', 'tsconfig-paths/register', path.join(root, 'test', '_common.ts'), path.join(root, 'test', '_hooks.ts') ],
  reporter: 'tap',
  // This is a report, not a watchdog: a synchronous test cannot be interrupted
  // and an asynchronous test which really hangs is stopped by the job timeout
  // of GitHub, so what is measured here is the cost of the test on a shared
  // runner which is 2-3x slower than a developer machine and which can be
  // loaded by a concurrent build. The local cost of the slowest test is 1.4s.
  timeout: 60000,
  'v8-expose-gc': true
}
