#!/usr/bin/env node
// Expands lib/default_iterators.gpp into lib/default_iterators.js
//
// The template uses a tiny subset of the "gpp" preprocessor: <#define name|body>
// blocks which are instantiated with <#name arg1|arg2|arg3|arg4>, where the
// arguments are substituted for #1..#4 in the body, including in the arguments
// of the macros which the body itself instantiates. gpp is a C program which is
// not available on every platform we build on, so the fifty lines which it
// takes to expand the seven macros of the template live here instead.
//
// Usage: node scripts/generate-default-iterators.js [--check]
//   --check  exit with 1 when lib/default_iterators.js is not up to date

const fs = require('fs')
const path = require('path')
const { ESLint } = require('eslint')

const root = path.join(__dirname, '..')
const source = path.join(root, 'lib', 'default_iterators.gpp')
const target = path.join(root, 'lib', 'default_iterators.js')

// <#define name|\n body \n> - the body cannot contain a line which is a single ">"
const DEFINE = /<#define (\w+)\|([\s\S]*?)\n>[ \t]*\n?/g
// <#name arg1|arg2|...> - "#2" inside of a type, as in {forEachCb<#2>}, is not a call
const CALL = /<#([A-Za-z_]\w*)([^>]*)>/g

const expandTemplate = (template) => {
  const defines = new Map()
  const segments = []
  let consumed = 0
  for (const match of template.matchAll(DEFINE)) {
    segments.push(template.slice(consumed, match.index))
    let body = match[2]
    // the newline before the closing ">" belongs to the block, not to the body
    if (body.endsWith('\n')) body = body.slice(0, -1)
    defines.set(match[1], body)
    consumed = match.index + match[0].length
  }
  segments.push(template.slice(consumed))

  const expand = (name, args, depth = 0) => {
    const body = defines.get(name)
    if (body === undefined) throw new Error(`<#${name}> is not defined`)
    if (depth > 16) throw new Error(`<#${name}> expands into itself`)
    // gpp keeps the newline which ends the line of the invocation
    return body
      .replace(/#(\d+)/g, (_, index) => args[Number(index) - 1] ?? '')
      .replace(CALL, (_, nested, rest) => expand(nested, rest.trim().split('|'), depth + 1)) + '\n'
  }

  return segments.join('').replace(CALL, (_, name, rest) => expand(name, rest.trim().split('|')))
}

// the result is formatted by the same eslint which formats the rest of lib/
const format = async (text) => {
  const [result] = await new ESLint({ cwd: root, fix: true }).lintText(text, { filePath: target })
  return result.output ?? text
}

const main = async () => {
  const expanded = expandTemplate(fs.readFileSync(source, 'utf8').replace(/\r\n/g, '\n'))
  const formatted = await format(expanded)

  if (!process.argv.includes('--check')) {
    fs.writeFileSync(target, formatted)
    return
  }

  // the checkout may have rewritten the line endings of the committed file
  const current = fs.readFileSync(target, 'utf8').replace(/\r\n/g, '\n')
  if (current !== formatted) {
    console.error(`${path.relative(root, target)} is not up to date: run "npm run iterators"`)
    process.exitCode = 1
  }
}

main()
