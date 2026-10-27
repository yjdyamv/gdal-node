import { assert } from 'chai'
import * as fs from 'fs'
import * as path from 'path'
import * as ts from 'typescript'
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
 *
 * The typings are checked against the module itself as well, because agreeing with
 * the comments is not the same thing as describing the module: a comment which names
 * the wrong class, or a member which the JS layer installs on a class of its own,
 * gives a declaration which the compiler accepts and the runtime does not have.
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
  /(?<![\w.])(METHOD_ASYNCABLE_AS|METHOD_ASYNCABLE|METHOD_HIDDEN|METHOD_AS|METHOD|ATTR_ASYNCABLE|ATTR_DONT_ENUM|ATTR|asyncMethod|asyncAttr|hiddenAttr|method|attr)\s*\(([^)]*)\)/g

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

/** The code without its comments, so that a commented-out descriptor is ignored. */
const uncommented = (code: string): string => {
  let out = ''
  for (let i = 0; i < code.length; ++i) {
    const char = code[i]
    if (char === '/' && code[i + 1] === '/') {
      const end = code.indexOf('\n', i)
      i = end < 0 ? code.length : end - 1
      continue
    }
    if (char === '/' && code[i + 1] === '*') {
      const end = code.indexOf('*/', i + 2)
      i = end < 0 ? code.length : end + 1
      continue
    }
    if (char === '"' || char === "'") {
      const from = i
      for (++i; i < code.length && code[i] !== char; ++i) if (code[i] === '\\') ++i
      out += code.slice(from, i + 1)
      continue
    }
    out += char
  }
  return out
}

/** The JS name the descriptor registers, or null if it cannot be read statically. */
const registeredName = (descriptor: Descriptor): string | null => {
  // the macros pass the object first, the helpers take the name first
  if (/^ATTR/.test(descriptor.macro)) return literal(descriptor.args[1])
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

        // the descriptors are the arrays of the registry, which may precede the call
        for (const descriptor of descriptors(uncommented(init))) {
          const name = registeredName(descriptor)
          if (name === null || !isPublic(name)) continue
          members.add(name)
          const isAsync =
            descriptor.macro.includes('ASYNCABLE') ||
            descriptor.macro === 'asyncMethod' ||
            descriptor.macro === 'asyncAttr'
          if (isAsync) members.add(`${name}Async`)
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

// ------------------------------------------------------------------ the runtime

/**
 * The doc comments are written by hand and `npm run yatag` believes them, so nothing
 * makes sure that the typings describe the module: a comment which names the wrong
 * class, or a member which the JS layer installs on a class of its own, produces a
 * declaration which the compiler accepts and which the module does not have.
 *
 * `points` was documented as a member of `Geometry` while it is registered only on
 * `SimpleCurve`, so the typings promised it on every geometry, and the static
 * `wkbType` was declared on `Geometry` while only the concrete classes have it.
 *
 * These tests read the declarations with the compiler API and compare them with the
 * runtime object. A member may be installed by the constructor instead of being an
 * accessor of the prototype (`Envelope.minX`, `GDALError.code`), and the module has
 * three kinds of objects which are declared as classes: the real classes, the
 * collections exposed as plain objects (`gdal.fs`, `gdal.vsimem`) and the namespaces
 * of the geometry classes (`gdal.Point.Multi`), so all of them are checked.
 */

/** The declarations of lib/index.d.ts, or null if `npm run yatag` has not run. */
const declaration = (): ts.SourceFile | null => {
  const file = path.join(root, 'lib/index.d.ts')
  if (!fs.existsSync(file)) return null
  return ts.createSourceFile('index.d.ts', fs.readFileSync(file, 'utf8'), ts.ScriptTarget.ESNext, true)
}

const runtime = gdal as unknown as Record<string, unknown>

/** `name`, or `Symbol.x` for a computed name, or null if it cannot be read. */
const declaredName = (node: ts.Node): string | null => {
  const name = (node as ts.NamedDeclaration).name
  if (name === undefined) return null
  if (ts.isIdentifier(name) || ts.isStringLiteral(name)) return name.text
  if (ts.isComputedPropertyName(name)) return name.expression.getText()
  return null
}

const isMember = (node: ts.Node): boolean =>
  ts.isPropertySignature(node) ||
  ts.isMethodSignature(node) ||
  ts.isPropertyDeclaration(node) ||
  ts.isMethodDeclaration(node) ||
  ts.isGetAccessorDeclaration(node) ||
  ts.isSetAccessorDeclaration(node)

const isStatic = (node: ts.Node): boolean =>
  ts.canHaveModifiers(node) &&
  (ts.getModifiers(node) ?? []).some((modifier) => modifier.kind === ts.SyntaxKind.StaticKeyword)

/** `Symbol.iterator` -> the symbol itself, since it is not a string key. */
const wellKnown = (name: string): symbol | null => {
  const match = /^Symbol\.([\w$]+)$/.exec(name)
  if (match === null) return null
  return (Symbol as unknown as Record<string, symbol>)[match[1]] ?? null
}

const hasMember = (owner: object, name: string): boolean => {
  const symbol = wellKnown(name)
  if (symbol === null) return name in owner
  return (owner as Record<symbol, unknown>)[symbol] !== undefined
}

/** A class of the module, as the declarations see it. */
interface Constructor {
  prototype: object
  new (...args: unknown[]): object
}

/**
 * The objects of a class which could be constructed, for the members which the
 * constructor installs on them rather than exposing them from the prototype.
 * A constructor may accept any of the forms, so every one of them is tried.
 */
const instances = (constructor: Constructor): object[] => {
  const attempts = [
    () => new constructor(),
    () => new constructor('test'),
    () => new constructor('test', 'CPLE_Test')
  ]
  const made: object[] = []
  for (const attempt of attempts) {
    try {
      made.push(attempt())
    } catch {
      // this constructor needs other arguments
    }
  }
  return made
}

/** The classes which really have the member, for the messages. */
const owners = (name: string, isStaticMember: boolean): string[] =>
  Object.keys(gdal).filter((other) => {
    const found = ctor(other)
    if (found === undefined) return false
    const owner = isStaticMember ? found : found.prototype
    return owner !== undefined && Object.prototype.hasOwnProperty.call(owner, name)
  })

/**
 * class name -> its declarations, since a class may be declared twice: `Dataset` is
 * both a class and an interface which adds the `Symbol.dispose` of the typings.
 */
const declarations = (file: ts.SourceFile): Map<string, ts.Node[]> => {
  const classes = new Map<string, ts.Node[]>()
  for (const node of file.statements) {
    if (!ts.isClassDeclaration(node) && !ts.isInterfaceDeclaration(node)) continue
    if (node.name === undefined) continue
    classes.set(node.name.text, [ ...(classes.get(node.name.text) ?? []), node ])
  }
  return classes
}

/** The names which are declared as a class - the interfaces are types only. */
const declaredClasses = (file: ts.SourceFile): Set<string> =>
  new Set(file.statements.filter(ts.isClassDeclaration).map((node) => node.name?.text).filter((name) => name !== undefined))

describe('the typings', () => {
  const file = declaration()
  const declared = file === null ? null : declarations(file)

  it('declares only classes which the module has', () => {
    if (file === null) return // `npm run yatag` has not generated them

    const missing = Array.from(declaredClasses(file)).filter((name) => !(name in runtime))
    assert.deepEqual(missing, [], `declared but not in the module: ${missing.join(', ')}`)
  })

  it('declares the members which the module has', () => {
    if (declared === null) return

    const missing: string[] = []
    for (const [ name, nodes ] of declared) {
      const value = runtime[name]
      if (value === undefined) continue // the case above
      const isClass = typeof value === 'function'
      const object = isClass ? instances(value as unknown as Constructor) : [ value as object ]

      for (const node of nodes) {
        for (const member of (node as ts.ClassDeclaration).members) {
          if (!isMember(member)) continue
          const memberName = declaredName(member)
          if (memberName === null) continue

          // the statics and the members of the plain objects are properties of the
          // value itself, everything else belongs to the prototype
          const target = isClass && !isStatic(member) ? (value as unknown as Constructor).prototype : (value as object)
          if (hasMember(target, memberName)) continue
          if (!isStatic(member) && object.some((made) => hasMember(made, memberName))) continue

          const owner = owners(memberName, isStatic(member))
          missing.push(owner.length === 0 ? `${name}.${memberName}` : `${name}.${memberName} (only ${owner.join(', ')} has it)`)
        }
      }
    }

    assert.deepEqual(missing, [], `declared but not in the module: ${missing.join(', ')}`)
  })

  it('declares the functions and the constants which the module has', () => {
    if (file === null) return

    const missing: string[] = []
    for (const node of file.statements) {
      if (ts.isFunctionDeclaration(node) && node.name !== undefined) {
        if (!(node.name.text in runtime)) missing.push(`${node.name.text}()`)
        continue
      }
      if (!ts.isVariableStatement(node)) continue
      for (const variable of node.declarationList.declarations) {
        const name = declaredName(variable)
        if (name !== null && !(name in runtime)) missing.push(name)
      }
    }

    assert.deepEqual(missing, [], `declared but not in the module: ${missing.join(', ')}`)
  })

  it('declares the namespaces which the module has', () => {
    if (file === null) return

    const missing: string[] = []
    for (const node of file.statements) {
      if (!ts.isModuleDeclaration(node) || !ts.isIdentifier(node.name)) continue
      const object = runtime[node.name.text]
      if (object === undefined) {
        missing.push(node.name.text)
        continue
      }

      let body = node.body
      while (body !== undefined && ts.isModuleDeclaration(body)) body = body.body
      for (const member of body === undefined || !ts.isModuleBlock(body) ? [] : body.statements) {
        if (!ts.isVariableStatement(member)) continue
        for (const variable of member.declarationList.declarations) {
          const name = declaredName(variable)
          if (name === null) continue
          if (!(name in (object as object))) missing.push(`${node.name.text}.${name}`)
        }
      }
    }

    assert.deepEqual(missing, [], `declared but not in the module: ${missing.join(', ')}`)
  })

  // `unknown` is the type which yatag writes when it does not know the type of a
  // member, which happens with the members which are declared by a tag of their
  // own and not by a `@type`: the compiler accepts them, but the user of the
  // typings knows nothing about the value
  it('declares the type of every member', () => {
    if (file === null) return

    const untyped: string[] = []
    const visit = (node: ts.Node): void => {
      if (node.kind === ts.SyntaxKind.UnknownKeyword) {
        let owner: ts.Node | undefined = node.parent
        while (owner !== undefined && !isMember(owner) && !ts.isVariableDeclaration(owner)) owner = owner.parent
        untyped.push(owner === undefined ? node.getText() : declaredName(owner) ?? node.getText())
      }
      ts.forEachChild(node, visit)
    }
    visit(file)

    assert.deepEqual(untyped, [], `declared without a type: ${untyped.join(', ')}`)
  })
})
