import * as fs from 'node:fs'
import * as path from 'node:path'
import * as vm from 'node:vm'
import * as ts from 'typescript'
import { assert } from 'chai'

// An example which does not parse costs more than no example at all, because a
// reader believes it and pastes it: the documentation of `gdal.info()` declared
// `const output` twice in the same block, the one of
// `lineStringPoints.add()` was missing a comma between two arguments and two
// collections used `[ring1, ...]`, the ellipsis of the prose, inside an array
// literal.  Every block is expected to be a program: the `@example` blocks of
// the sources and the `js` blocks of the chapters of the README.

const root = path.join(__dirname, '..')

interface Example {
  readonly where: string
  readonly code: string
}

const files = (directory: string, extension: RegExp): string[] => {
  const found: string[] = []
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const full = path.join(directory, entry.name)
    if (entry.isDirectory()) found.push(...files(full, extension))
    else if (extension.test(entry.name)) found.push(full)
  }
  return found
}

// the body of every `@example` tag of a documentation comment
const annotated = (file: string): Example[] => {
  const examples: Example[] = []
  const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/)
  let current: Example | undefined
  let code: string[] = []
  const finish = (): void => {
    if (current !== undefined) examples.push({ where: current.where, code: code.join('\n').trim() })
    current = undefined
    code = []
  }
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    if (/^\s*(\*|\/\/)?\s*@example\b/.test(line)) {
      finish()
      current = { where: `${path.relative(root, file).replace(/\\/g, '/')}:${i + 1}`, code: '' }
      continue
    }
    if (current === undefined) continue
    // the next tag, or the end of the comment, ends the example
    if (/^\s*(\*|\/\/)?\s*@[a-zA-Z]/.test(line) || /^\s*\*\/\s*$/.test(line) || /^\s*\/\*\*/.test(line)) {
      finish()
      continue
    }
    code.push(line.replace(/^\s*(\*|\/\/)/, '').replace(/^\s/, ''))
  }
  finish()
  return examples.filter((example) => example.code.length > 0)
}

// the body of every ```js block of a chapter
const fenced = (file: string): Example[] => {
  const chapters = fs.readFileSync(file, 'utf8').split(/\r?\n/)
  const examples: Example[] = []
  let start = -1
  let code: string[] = []
  for (let i = 0; i < chapters.length; i++) {
    const line = chapters[i]
    if (start < 0 && /^\s*```js\s*$/.test(line)) {
      start = i
      code = []
      continue
    }
    if (start >= 0 && /^\s*```\s*$/.test(line)) {
      examples.push({ where: `${path.basename(file)}:${start + 1}`, code: code.join('\n').trim() })
      start = -1
      continue
    }
    if (start >= 0) code.push(line)
  }
  return examples
}

const documented = (): Example[] => {
  const sources = files(path.join(root, 'src'), /\.(cpp|hpp)$/).flatMap((file) => annotated(file))
  const chapters = [ 'README.md', 'ASYNCIO.md' ]
    .map((name) => path.join(root, name))
    .flatMap((file) => fenced(file))
  return [ ...sources, ...chapters ]
}

// an example which writes the type of an argument is TypeScript, and an
// example which imports is a module: neither is a script
const typed = (file: ts.SourceFile): boolean => {
  let found = false
  const walk = (node: ts.Node): void => {
    if (found) return
    const type = (node as unknown as { type?: ts.Node }).type
    if ((type !== undefined && ts.isTypeNode(type)) || ts.isImportDeclaration(node) || ts.isExportAssignment(node)) {
      found = true
      return
    }
    ts.forEachChild(node, walk)
  }
  walk(file)
  return found
}

// `using` and `await using` are declarations which only Node.js 24 and later
// accept at runtime, while every compiler which this test uses knows them: the
// blocks which use them fall back to the compiler when the engine cannot run
// them, so that they are still checked and not silently skipped
const explicitResourceManagement = ((): boolean => {
  try {
    new vm.Script('{\n  using resource = null\n  resource\n}')
    return true
  } catch (_e) {
    return false
  }
})()

// `await` is legitimate in an example, but not at the top level of a script
const parses = (code: string): boolean => {
  for (const source of [ code, `(async () => {\n${code}\n})()` ]) {
    try {
      new vm.Script(source)
      return true
    } catch (_e) { /* not a script */ }
  }
  // every other block must be plain JavaScript, which is what a reader pastes
  // into a script: TypeScript would accept the block which declared `const`
  // twice, because a redeclaration is one of its semantic errors and not one
  // of its syntax errors, so a block without any type is not allowed through -
  // unless it uses a declaration which the engine of this version does not have
  // yet, in which case the compiler is the only thing which can check it
  const file = ts.createSourceFile('example.ts', code, ts.ScriptTarget.ESNext, true, ts.ScriptKind.TS)
  const diagnostics = (file as unknown as { parseDiagnostics: readonly ts.Diagnostic[] }).parseDiagnostics
  if (diagnostics.length !== 0) {
    return false
  }
  return typed(file) || (!explicitResourceManagement && /\busing\s+[\w$]+/.test(code))
}

describe('the examples of the documentation', () => {
  it('should be found', () => {
    // the extractor must not quietly find nothing
    assert.isAbove(documented().length, 60)
  })
  it('should be valid programs', () => {
    const broken = documented()
      .filter((example) => !parses(example.code))
      .map((example) => `${example.where}:\n${example.code.split('\n').map((line) => `    ${line}`).join('\n')}`)
    assert.deepEqual(broken, [], `examples which do not parse:\n${broken.join('\n')}`)
  })
})
