#!/usr/bin/env node
/*
 * Builds the native addon with Conan and cmake-js.
 *
 * The dependencies are not vendored anymore: `conan install` resolves them
 * (GDAL, PROJ, GEOS, ...) and generates build-conan/conan_toolchain.cmake,
 * which cmake-js passes to CMake. CMakeLists.txt copies the resulting addon
 * to lib/binding/<platform>-<arch>.
 *
 * Usage: node scripts/build.mjs [--debug] [--shared-gdal]
 *
 * --shared-gdal builds against the GDAL already installed on the system (or one
 * pointed at with -DGDAL_INCLUDE_DIR/-DGDAL_LIBRARY) and skips Conan entirely.
 */
import { execFileSync } from 'node:child_process'
import { existsSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

const log = (...args) => console.log('gdal-async:', ...args)

/**
 * Runs a command, printing its output
 *
 * @param {string} command
 * @param {string[]} args
 * @param {object} [env]
 * @returns {boolean} true when the command succeeded
 */
const run = (command, args, env) => {
  try {
    execFileSync(command, args, { cwd: root, stdio: 'inherit', env: env || process.env })
    return true
  } catch (e) {
    if (e.code === 'ENOENT') {
      log(`'${command}' was not found in PATH`)
    } else {
      log(`'${command} ${args.join(' ')}' failed with exit code ${e.status}`)
    }
    return false
  }
}

/**
 * Builds the addon, reporting whether it was built
 *
 * @param {boolean} [debug] build a debug addon
 * @param {boolean} [sharedGdal] use the GDAL of the system, without Conan
 * @returns {boolean}
 */
export const buildFromSource = (debug = false, sharedGdal = false) => {
  const toolchain = path.join(root, 'build-conan', 'conan_toolchain.cmake')
  const env = { ...process.env }
  if (!sharedGdal) {
    log('resolving the dependencies with Conan')
    // --build=missing so that a toolchain without prebuilt dependencies
    // (a distribution that Conan has no binaries for) still works
    if (!run('conan', [ 'install', '.', '-of', 'build-conan', '--build=missing',
      '-s', `build_type=${debug ? 'Debug' : 'Release'}` ])) {
      log('install Conan from https://conan.io/downloads and try again')
      return false
    }
    if (!existsSync(toolchain)) {
      log(`Conan did not generate ${path.relative(root, toolchain)}`)
      return false
    }
    env.CMAKE_TOOLCHAIN_FILE = toolchain
  } else {
    log('using the GDAL installed on this system')
  }
  const cmakejs = path.join(root, 'node_modules', 'cmake-js', 'bin', 'cmake-js')
  if (!existsSync(cmakejs)) {
    log('cmake-js is missing, run `npm install` first')
    return false
  }
  log('building the addon with cmake-js')
  const args = [ cmakejs, 'build' ]
  if (debug) {
    args.push('--debug')
  }
  if (sharedGdal) {
    args.push('--cmake-options=-DBUNDLED_GDAL=OFF')
  }
  return run(process.execPath, args, env)
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exit(buildFromSource(process.argv.includes('--debug'), process.argv.includes('--shared-gdal')) ? 0 : 1)
}
