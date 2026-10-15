/**
 * Packs the addon that was just built into the tarball that is attached to the
 * GitHub release and that `scripts/install.mjs` downloads:
 *
 *     build/stage/v<version>/napi-v8-<platform>-<arch>.tar.gz
 *     +- lib/binding/napi-v8-<platform>-<arch>/gdal.node
 *
 * Packing the binary is the only thing that used to require node-pre-gyp.
 * Usage: node scripts/package.mjs
 */
import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const pkg = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'))

const expand = (template) => template
  .replace('{platform}', process.platform)
  .replace('{arch}', process.arch)
  .replace('{version}', pkg.version)

const binding = path.resolve(root, expand(pkg.binary.module_path))
const addon = path.join(binding, `${pkg.binary.module_name}.node`)
if (!fs.existsSync(addon))
  throw new Error(`${addon} does not exist, run \`npm run build:cmake\` first`)

const destination = path.join(root, 'build', 'stage', expand(pkg.binary.remote_path))
fs.mkdirSync(destination, { recursive: true })

const tarball = path.join(destination, expand(pkg.binary.package_name))
fs.rmSync(tarball, { force: true })

// tar must be given paths relative to the working directory: it reads
// `C:/...` as a remote host and Windows separators are not portable
const posix = (p) => path.relative(root, p).split(path.sep).join('/')
execFileSync('tar', ['-czf', posix(tarball), posix(binding)], { cwd: root, stdio: 'inherit' })

console.log(`${posix(tarball)}: ${fs.statSync(tarball).size} bytes`)
