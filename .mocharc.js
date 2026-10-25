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
  timeout: 20000,
  'v8-expose-gc': true
}
