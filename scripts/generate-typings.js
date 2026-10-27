#!/usr/bin/env node
// Generates lib/index.d.ts out of the doc comments of the sources
//
// Almost every declaration of the typings is derived from the `@class`,
// `@method`, `@param` and `@return` comments of src/, which yatag reads, so that
// a method is declared once and documented once. The two exceptions have no
// comment which could be derived from:
//
//   * the members which are named by a symbol (`[Symbol.dispose]`), because
//     yatag drops the brackets and produces `Symbol.dispose()`
//   * the classes of the failures (`gdal.GDALError` and its subclasses), which
//     are plain properties of the module and not methods of a class
//
// They are written by hand in lib/types-extra.d.ts and appended here, so that
// lib/index.d.ts stays a single generated file which typedoc can read.
//
// The result is committed, so `--check` compares it with what the sources say
// without writing anything: that is what the tests and the CI run.
//
// Usage: node scripts/generate-typings.js [--check]

const fs = require('fs')
const path = require('path')
const { execFileSync } = require('child_process')

const root = path.join(__dirname, '..')
const target = path.join(root, 'lib', 'index.d.ts')
const extra = path.join(root, 'lib', 'types-extra.d.ts')
const generated = path.join(root, 'lib', 'index.generated.d.ts')

// yatag writes the declarations which come from the comments
execFileSync(process.execPath, [require.resolve('yatag/yatag.js')], {
  cwd: root,
  env: { ...process.env, YATAG_OUTPUT: generated },
  stdio: 'inherit'
})

// the line endings are not a part of the comparison: the file is committed with
// `eol=crlf` (see .gitattributes) and written with CRLF, but a checkout or an
// editor which disagrees is not a drift of the declarations
const read = (file) => fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n').replace(/\s*$/, '')
const declarations = read(generated) + '\n\n' + read(extra) + '\n'
fs.rmSync(generated)

if (process.argv.includes('--check')) {
  const current = fs.existsSync(target) ? read(target) : ''
  if (current !== declarations.replace(/\s*$/, '')) {
    console.error(
      path.relative(root, target) + ' is not up to date: run "npm run yatag" and commit the result'
    )
    process.exitCode = 1
  } else {
    console.log(path.relative(root, target) + ' is up to date')
  }
} else {
  fs.writeFileSync(target, declarations.replace(/\n/g, '\r\n'))
  console.log(path.relative(root, target) + ' updated')
}
