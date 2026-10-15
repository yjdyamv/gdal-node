#!/usr/bin/env node
/*
 * Installs the native addon.
 *
 * It either downloads the prebuilt binary published with the GitHub release of
 * the current version, or it builds the addon from the sources with Conan and
 * cmake-js. A local build is attempted when the download is not possible, when
 * the platform has no prebuilt binary, or when it is explicitly requested with
 * `npm install --build-from-source`.
 *
 * Nothing is done when the binding has already been built - which is the usual
 * case while working on the sources.
 *
 * Usage: node scripts/install.mjs [--build-from-source] [--debug] [--verbose]
 */
import { copyFileSync, createWriteStream, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync } from 'node:fs'
import { execFileSync } from 'node:child_process'
import http from 'node:http'
import https from 'node:https'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const pkg = JSON.parse(readFileSync(path.join(root, 'package.json'), 'utf8'))

const bindingDir = path.resolve(root, pkg.binary.module_path
  .replace('{platform}', process.platform)
  .replace('{arch}', process.arch))
const bindingFile = path.join(bindingDir, `${pkg.binary.module_name}.node`)

const verbose = process.argv.includes('--verbose')

const log = (...args) => console.log('gdal-async:', ...args)
const debug = (...args) => {
  if (verbose) {
    log(...args)
  }
}

/**
 * Substitutes the placeholders of a binary configuration value
 *
 * @param {string} value
 * @returns {string}
 */
const expand = (value) => value
  .replace('{platform}', process.platform)
  .replace('{arch}', process.arch)
  .replace('{version}', pkg.version)

/**
 * Downloads an URL, following the redirects
 *
 * @param {string} url
 * @param {string} dest
 * @param {number} [redirects]
 * @returns {Promise<string>} the path of the downloaded file
 */
const download = (url, dest, redirects = 0) => new Promise((resolve, reject) => {
  if (redirects > 5) {
    return reject(new Error(`${url} redirects too many times`))
  }
  const client = url.startsWith('https:') ? https : http
  client.get(url, { headers: { 'User-Agent': `gdal-async/${pkg.version}` } }, (res) => {
    if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
      res.resume()
      return resolve(download(new URL(res.headers.location, url).toString(), dest, redirects + 1))
    }
    if (res.statusCode !== 200) {
      res.resume()
      return reject(new Error(`${url} responded with ${res.statusCode}`))
    }
    const file = createWriteStream(dest)
    res.pipe(file)
    file.on('finish', () => file.close(() => resolve(dest)))
    file.on('error', reject)
  }).on('error', reject)
})

/**
 * Finds the binding inside a directory tree
 *
 * @param {string} dir
 * @returns {?string}
 */
const findBinding = (dir) => {
  for (const entry of readdirSync(dir)) {
    const full = path.join(dir, entry)
    if (entry === `${pkg.binary.module_name}.node`) {
      return full
    }
    if (statSync(full).isDirectory()) {
      const found = findBinding(full)
      if (found) {
        return found
      }
    }
  }
  return null
}

/**
 * Downloads and unpacks the prebuilt binary of the current version
 *
 * @returns {Promise<boolean>} true when the binding has been installed
 */
const downloadPrebuilt = async () => {
  const asset = expand(pkg.binary.package_name)
  const url = new URL(`${expand(pkg.binary.remote_path)}/${asset}`, pkg.binary.host).toString()
  const tmp = mkdtempSync(path.join(os.tmpdir(), 'gdal-async-'))
  const tarball = path.join(tmp, asset)
  try {
    log(`downloading the prebuilt binary from ${url}`)
    await download(url, tarball)
    // tar must be given a relative path: it reads `C:/...` as a remote host
    execFileSync('tar', [ '-xzf', asset ], { cwd: tmp, stdio: verbose ? 'inherit' : 'ignore' })
    const found = findBinding(tmp)
    if (!found) {
      throw new Error(`no ${pkg.binary.module_name}.node was found in ${asset}`)
    }
    mkdirSync(bindingDir, { recursive: true })
    copyFileSync(found, bindingFile)
    return true
  } catch (e) {
    log(`the prebuilt binary is not available: ${e.message}`)
    return false
  } finally {
    rmSync(tmp, { recursive: true, force: true })
  }
}

/**
 * Installs the addon
 *
 * @returns {Promise<number>} the exit code
 */
const main = async () => {
  if (existsSync(bindingFile)) {
    debug(`the ${process.platform}-${process.arch} binding is already built, nothing to do`)
    return 0
  }
  const fromSource = process.argv.includes('--build-from-source') ||
    process.env.GDAL_ASYNC_BUILD_FROM_SOURCE === '1' ||
    process.env.npm_config_build_from_source === 'true'
  if (!fromSource && await downloadPrebuilt()) {
    log(`installed the prebuilt binding in ${path.relative(root, bindingFile)}`)
    return 0
  }
  log('building the addon from the sources, this requires Conan and a C++20 compiler')
  const { buildFromSource } = await import('./build.mjs')
  if (buildFromSource(process.argv.includes('--debug')) && existsSync(bindingFile)) {
    log(`built the binding in ${path.relative(root, bindingFile)}`)
    return 0
  }
  console.error('gdal-async: there is no prebuilt binary for this platform and the build from the sources failed')
  console.error('gdal-async: install Conan and a C++20 compiler, then re-run `npm install --build-from-source`')
  return 1
}

process.exit(await main())
