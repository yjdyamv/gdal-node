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
 * --shared-gdal builds against the GDAL already installed on the system and
 * skips Conan entirely; the build then defaults to GDAL's own data files.
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
  // CMake 4 refuses the cmake_minimum_required() older than 3.5 that several
  // of the dependencies still declare when Conan has to build them.  This has
  // to be an environment variable: a variable set by the generated toolchain
  // is set too late for the projects that need it
  env.CMAKE_POLICY_VERSION_MINIMUM = '3.5'
  if (!sharedGdal) {
    log('resolving the dependencies with Conan')
    // conan/recipes/gdal is a fork of the recipe of conan-center: it also
    // carries the ExprTk and muparser options the addon needs.  Exporting it
    // here makes the CI and the developers use the very same recipe
    if (!run('conan', [ 'export', path.join(root, 'conan', 'recipes', 'gdal') ])) {
      return false
    }

    // --build=missing so that a toolchain without prebuilt dependencies
    // (a distribution that Conan has no binaries for) still works
    // C++20 because the addon needs it for the V8 headers and the profile
    // written by `conan profile detect` may default to a lower standard
    // In the CI the pinned profiles of conan/profiles are used instead: they
    // name the compilers that conan-center has published the binaries with,
    // while the compilers of a runner are usually newer - Conan would then
    // rebuild the whole dependency tree from source on every run.  They also
    // select Ninja over the MSBuild generator that Conan picks by default on
    // Windows.  Keep them in sync with the runners pinned by
    // .github/workflows/test-dev.yml
    // The architecture is part of the name for every platform that has a
    // profile of each: a profile that pins an architecture the machine does
    // not have cross-compiles the dependencies, and the executables of the
    // build context - the sqlite3 that generates proj.db, for one - are then
    // built for a CPU that cannot run them
    const arm64 = process.arch === 'arm64'
    const pinnedPlatform = {
      darwin: `macos-${arm64 ? 'arm64' : 'x64'}`,
      linux: 'linux',
      win32: 'windows'
    }[process.platform]
    const pinned = pinnedPlatform ? path.join(root, 'conan', 'profiles', pinnedPlatform) : null
    const profiles = process.env.CI && pinned && existsSync(pinned) ? [ `-pr:h=${pinned}`, `-pr:b=${pinned}` ] : []
    if (!run('conan', [ 'install', '.', '-of', 'build-conan', '--build=missing',
      '-s', `build_type=${debug ? 'Debug' : 'Release'}`, '-s', 'compiler.cppstd=20',
      ...profiles ])) {
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
  return run(process.execPath, args, env)
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exit(buildFromSource(process.argv.includes('--debug'), process.argv.includes('--shared-gdal')) ? 0 : 1)
}
