import { assert } from 'chai'
import * as fs from 'fs'
import * as path from 'path'
import gdal from 'gdal-async'

/**
 * The `Initialize` of every class is the only place which decides what the module
 * exposes: the `GDALDefineClass<>` call with its descriptor list registers the
 * methods and the accessors, and `GDAL_SetMethod` / `GDAL_SetAsyncableMethod`
 * register the statics.
 *
 * The documentation is a second copy of that list - every member needs a yatag
 * comment with its own `@method` or `@name` and the `@memberof` of its class - and
 * `npm run yatag` generates `lib/index.d.ts` from those comments, so the typings are
 * a third copy which is only as complete as the comments are.
 *
 * These tests parse the registration and the comments out of the sources and check
 * that they agree, so that a method cannot be registered without being documented
 * and a doc comment cannot survive a method which is no longer there.
 */

const root = path.join(__dirname, '..')

// members which every object has and which are not part of the published API
const ignored = new Set([ 'toString' ])
const isPublic = (name: string): boolean => !name.startsWith('_') && !ignored.has(name)

const sourceFiles = (dir: string): string[] =>
  fs.readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const file = path.join(dir, entry.name)
    if (entry.isDirectory()) return sourceFiles(file)
    if (!/\.(cpp|hpp)$/.test(entry.name)) return []
    return [ file ]
  })

// ---------------------------------------------------------------- registration

const DESCRIPTOR =
  /\b(METHOD_ASYNCABLE_AS|METHOD_ASYNCABLE|METHOD_HIDDEN|METHOD_AS|METHOD|ATTR_ASYNCABLE|ATTR_DONT_ENUM|ATTR)\s*\(([^)]*)\)/g

interface Descriptor {
  macro: string
  args: string[]
}

const descriptors = (code: string): Descriptor[] =>
  Array.from(code.matchAll(DESCRIPTOR), (match) => ({
    macro: match[1],
    args: match[2].split(',').map((arg) => arg.trim())
  }))

const literal = (arg: string | undefined): string | null => {
  if (arg === undefined) return null
  const quoted = /^"([\w$]+)"$/.exec(arg)
  if (quoted) return quoted[1]
  return /^[\w$]+$/.test(arg) ? arg : null
}

/** The JS name the descriptor registers, or null if it cannot be read statically. */
const registeredName = (descriptor: Descriptor): string | null => {
  if (descriptor.macro.startsWith('ATTR')) return literal(descriptor.args[1])
  return literal(descriptor.args[0])
}

/** The body of the block which starts at `from` (a `{`), skipping strings and comments. */
const block = (code: string, from: number): string => {
  let depth = 0
  for (let i = from; i < code.length; ++i) {
    const char = code[i]
    if (char === '/' && code[i + 1] === '/') {
      i = code.indexOf('\n', i)
      if (i < 0) break
      continue
    }
    if (char === '"' || char === "'") {
      for (++i; i < code.length && code[i] !== char; ++i) if (code[i] === '\\') ++i
      continue
    }
    if (char === '{') {
      ++depth
    } else if (char === '}') {
      --depth
      if (depth === 0) return code.slice(from + 1, i)
    }
  }
  return code.slice(from + 1)
}

/** class name -> the JS names its `Initialize` registers */
const registered = (): Map<string, Set<string>> => {
  const classes = new Map<string, Set<string>>()

  for (const file of sourceFiles(path.join(root, 'src'))) {
    const code = fs.readFileSync(file, 'utf8')
    const starts = Array.from(code.matchAll(/::Initialize\s*\(/g), (match) => match.index as number)

    for (const [ i, start ] of starts.entries()) {
      const init = code.slice(start, starts[i + 1] ?? code.length)

      const define = /GDALDefineClass<[^>]*>\s*\(\s*env\s*,\s*"([\w$]+)"/.exec(init)
      if (define !== null) {
        const members = classes.get(define[1]) ?? new Set<string>()
        classes.set(define[1], members)

        const list = init.indexOf('{', define.index)
        for (const descriptor of descriptors(block(init, list))) {
          const name = registeredName(descriptor)
          if (name === null || !isPublic(name)) continue
          members.add(name)
          if (descriptor.macro.includes('ASYNCABLE')) members.add(`${name}Async`)
        }
      }

      // the statics are registered from the same Initialize, but they are not in the
      // descriptor list; the module level functions are not members of a class
      if (define === null) continue
      const statics = /GDAL_Set(Asyncable)?Method\s*\(\s*env\s*,\s*[\w.]+\s*,\s*"([\w$]+)"/g
      for (const match of init.matchAll(statics)) {
        if (!isPublic(match[2])) continue
        ;(classes.get(define[1]) as Set<string>).add(match[2])
        if (match[1] !== undefined) (classes.get(define[1]) as Set<string>).add(`${match[2]}Async`)
      }
    }
  }

  return classes
}

// --------------------------------------------------------------------- the docs

/** "Class.member" -> the files which document it */
const documented = (): Map<string, string[]> => {
  const comments = new Map<string, string[]>()

  for (const file of sourceFiles(path.join(root, 'src'))) {
    const code = fs.readFileSync(file, 'utf8')
    for (const match of code.matchAll(/\/\*\*([\s\S]*?)\*\//g)) {
      const owner = /@memberof\s+([\w$.]+)/.exec(match[1])
      const name = /@(?:method|name)\s+([\w$]+)/.exec(match[1])
      if (owner === null || name === null) continue
      const key = `${owner[1]}.${name[1]}`
      comments.set(key, [ ...(comments.get(key) ?? []), path.relative(root, file) ])
    }
  }

  return comments
}

// ------------------------------------------------------------------- the typings

/** class name -> the members declared in its `export class` block of lib/index.d.ts */
const typings = (): Map<string, Set<string>> | null => {
  // the typings are generated by `npm run yatag`, they are not in the repository
  const file = path.join(root, 'lib/index.d.ts')
  if (!fs.existsSync(file)) return null

  const code = fs.readFileSync(file, 'utf8')
  const classes = new Map<string, Set<string>>()

  for (const match of code.matchAll(/^export class ([\w$]+)/gm)) {
    const members = new Set<string>()
    for (const line of block(code, code.indexOf('{', match.index)).split('\n')) {
      const declared = /^\s*(?:static\s+|readonly\s+|get\s+|set\s+)*([A-Za-z_$][\w$]*)\s*[(:<]/.exec(line)
      if (declared !== null) members.add(declared[1])
    }
    classes.set(match[1], members)
  }

  return classes
}

// ------------------------------------------------------------------- the checks

const classes = registered()
const comments = documented()

const ctor = (name: string): { prototype?: object } | undefined => {
  const value = (gdal as unknown as Record<string, unknown>)[name]
  return typeof value === 'function' ? (value as { prototype?: object }) : undefined
}

const prototypes = new Map<object, string>()
for (const name of Object.keys(gdal)) {
  const prototype = ctor(name)?.prototype
  if (prototype) prototypes.set(prototype, name)
}

const ancestors = (name: string): string[] => {
  const chain = [ name ]
  let proto = ctor(name)?.prototype
  while (proto) {
    proto = Object.getPrototypeOf(proto) as object | undefined
    const ancestor = proto === undefined ? undefined : prototypes.get(proto)
    if (ancestor !== undefined) chain.push(ancestor)
  }
  return chain
}

/** The class, its ancestors and its descendants - the classes which may document a member. */
const lineage = (name: string): string[] => {
  const chain = ancestors(name)
  for (const other of classes.keys()) {
    if (other !== name && ancestors(other).includes(name)) chain.push(other)
  }
  return chain
}

const has = (name: string, member: string): boolean =>
  lineage(name).some((owner) => classes.get(owner)?.has(member) === true)

describe('registration', () => {
  it('finds the classes and their members in the sources', () => {
    assert.isAbove(classes.size, 30, 'the registration parser found too few classes')
    assert.isAbove(classes.get('SpatialReference')?.size ?? 0, 20, 'too few SpatialReference members')
  })

  it('documents every registered member', () => {
    const missing: string[] = []
    for (const [ name, members ] of classes) {
      for (const member of members) {
        if (!lineage(name).some((owner) => comments.has(`${owner}.${member}`))) missing.push(`${name}.${member}`)
      }
    }
    assert.deepEqual(missing, [], `registered but not documented: ${missing.join(', ')}`)
  })

  it('registers every documented member', () => {
    const stale: string[] = []
    for (const key of comments.keys()) {
      const [ owner, member ] = key.split('.')
      if (!classes.has(owner) || !isPublic(member)) continue
      if (!has(owner, member)) stale.push(`${key} (${(comments.get(key) ?? []).join(', ')})`)
    }
    assert.deepEqual(stale, [], `documented but not registered: ${stale.join(', ')}`)
  })

  it('declares every registered member in the typings', () => {
    const declarations = typings()
    if (declarations === null) return // `npm run yatag` has not generated them

    const missing: string[] = []
    for (const [ name, members ] of classes) {
      for (const member of members) {
        if (!ancestors(name).some((owner) => declarations.get(owner)?.has(member) === true)) {
          missing.push(`${name}.${member}`)
        }
      }
    }
    assert.deepEqual(missing, [], `registered but not declared in lib/index.d.ts: ${missing.join(', ')}`)
  })
})
