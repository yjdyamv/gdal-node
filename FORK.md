# gdal-node - a maintenance fork of node-gdal-async

`gdal-node` is a production maintenance fork of
[`mmomtchev/node-gdal-async`](https://github.com/mmomtchev/node-gdal-async), a
native asynchronous [GDAL](https://gdal.org/) binding for Node.js. It is not a
competitor of the upstream project: it exists so that a code base which is used
in production keeps receiving fixes and keeps tracking GDAL and Node.js, with a
conservative release policy.

The upstream project was written by [Momtchil Momtchev
(@mmomtchev)](https://github.com/mmomtchev), on top of
[`node-gdal-next`](https://github.com/contra/node-gdal-next) by
[@contra](https://github.com/contra), which in turn descends from
[`node-gdal`](https://github.com/naturalatlas/node-gdal) by Natural Atlas. All
of that work is Apache-2.0 and all of it is still here: this fork adds to it, it
does not replace it. If you use this software, consider supporting the upstream
author - his situation is described in [the upstream
README](https://github.com/mmomtchev/node-gdal-async#long-term-support-of-this-project).

## Provenance

| | |
| --- | --- |
| Upstream | [`mmomtchev/node-gdal-async`](https://github.com/mmomtchev/node-gdal-async) |
| Upstream release this fork starts from | `3.13.0` (`upstreamVersion` in `package.json`) |
| Fork point | `6ee86c0e8` "Fix Node.js 26 support (#376)", the parent of the upstream `3.13.0` tag |
| First fork release | `1.0.0` |
| License | Apache-2.0, unchanged |

## Versioning

`gdal-node` is versioned independently of the upstream, using [Semantic
Versioning](https://semver.org/), and its version is `<MAJOR>.<MINOR>.<PATCH>`
with nothing else in it - the GDAL release it is built against is *not* part of
it, in any form.

| Where | What it holds | Example |
| --- | --- | --- |
| Git tag, and the name of the GitHub release | `v` + the version | `v1.0.0` |
| `package.json` `version` | the version, without the `v` | `1.0.0` |
| `package.json` `upstreamVersion` | the release of the upstream `node-gdal-async` this one is based on | `3.13.0` |
| Release download URL | `.../releases/download/v<version>/` | `.../download/v1.0.0/` |

* `MAJOR` is the fork's own: a breaking change of the binding API bumps it. It
  is not tied to the upstream major, which is why the fork starts at `1.0.0`
  although it carries the upstream `3.13.0`.
* `MINOR` adds API, `PATCH` fixes bugs. Both stay compatible.
* `upstreamVersion` is the single place that says which GDAL a given `gdal-node`
  is built against - `3.13.0` is the upstream `node-gdal-async` release, whose
  number is the version of the GDAL it bundles. It appears in the release notes
  too, but never in the tag and never in the version number, so that there is
  exactly one authority to read and one string to compare.

The `v` prefix is not cosmetic: `package.json` `binary.remote_path` is
`v{version}`, which is what makes the prebuilt binaries of a release load from
`https://github.com/yjdyamv/gdal-node/releases/download/v<version>/`, and the
`test-npm.yml` workflow downloads the sources from `refs/tags/v<version>`. A
version that has been published and then withdrawn cannot be reused.

## History

The repository history is a single linear line of development:

* `8063cd0d vendor: gdal-node as of 6ee86c0e8` - one commit holding the
  complete upstream tree, including the vendored `deps/` of that release, so
  that every upstream file has a real commit behind it.
* `b452b19 build: remove the vendored dependencies` - the vendored GDAL, PROJ,
  curl and Node-API sources are gone (`deps/` keeps only the GDAL and PROJ data
  files that the package ships), because they are now resolved by Conan at build
  time.
* the 62 commits below, one per theme, in dependency order.

The pre-rewrite history (`node-gdal-async` as it was, 2127 commits, 71 tags,
192.67 MiB) is preserved outside the repository in a git bundle. Commits which
touch only the vendored `deps/` were dropped together with the vendor drop;
nothing else was rewritten.

## Sending a patch upstream

Being a fork does not prevent upstreaming: the fork's `main` and the upstream's
`master` share the `6ee86c0e8` history, so a self-contained commit can be
cherry-picked onto a branch of a pristine upstream clone and pushed as a pull
request:

```sh
git clone https://github.com/mmomtchev/node-gdal-async upstream-pr
cd upstream-pr
git checkout -b fix/some-bug master
git cherry-pick <sha>        # a commit marked `upstreamable` below
git commit -s --amend --no-edit   # the upstream project requires -s
git push git@github.com:yjdyamv/node-gdal-async.git fix/some-bug
```

Only the commits marked `upstreamable` are candidates: the `fork-only` ones
depend on this fork's build system, its object store or its new API and would
not apply to the upstream tree.

## Syncing with upstream

```sh
git remote add upstream https://github.com/mmomtchev/node-gdal-async.git  # once
git fetch upstream master
git log --oneline HEAD..upstream/master        # what is new upstream
git cherry-pick <sha>...                       # or: git rebase upstream/master
```

The vendored `deps/` is the only structural difference that can conflict:
upstream still vendors its dependencies, this fork does not. When a rebase
conflicts in `deps/`, keep the file dropped (`git rm`) and rebuild with Conan.
After a rebase, update `upstreamVersion` in `package.json` and add a
`CHANGELOG.md` entry.

## The patches

The 63 commits of the fork, in chronological order within each area. `upstreamable`
means that the commit is self-contained and can be offered to the upstream
project as a pull request; `fork-only` means that it depends on this fork's
build system, object store or API.

| Commit | Description | Class | Upstream PR |
| --- | --- | --- | --- |
| `6d45adb` | Architecture: register the members of a class from a table | fork-only | not submitted |
| `cbcb62a` | Architecture: write every method once, as a body which generates both of its forms | fork-only | not submitted |
| `f3c1031` | Architecture: check the arguments of the methods which a macro defines | fork-only | not submitted |
| `3758806` | Architecture: check the arguments of the methods with Args | fork-only | not submitted |
| `58bbb44` | Architecture: hold the wrapped objects in a handle which knows how to free them | fork-only | not submitted |
| `3ccc34c` | Architecture: carry the failures in a GDALError instead of a bare pointer | fork-only | not submitted |
| `9256947` | Architecture: keep everything an environment owns in one context | fork-only | not submitted |
| `b408506` | Architecture: port the addon from NAN to N-API | fork-only | not submitted |
| `7460b18` | Build: generate the typings and check them against the source | fork-only | not submitted |
| `d06a6ab` | Build: build with Ninja into build/ and export a compilation database | fork-only | not submitted |
| `539de2d` | Build: drop the shared GDAL build mode | fork-only | not submitted |
| `406c37c` | Build: drop the wrapper recipe of a prebuilt GDAL | fork-only | not submitted |
| `a4fe502` | Build: bundle the shipped data only with the GDAL of Conan | fork-only | not submitted |
| `b452b19` | Build: remove the vendored dependencies | fork-only | not submitted |
| `d5fa6af` | Build: remove the gyp build files | fork-only | not submitted |
| `67fb166` | Build: build, pack and install the binaries without node-gyp | fork-only | not submitted |
| `6e7e97e` | Build: upload the prebuilt binaries without node-pre-gyp | fork-only | not submitted |
| `d26c6c5` | Build: build with cmake-js and Conan instead of node-gyp | fork-only | not submitted |
| `0b4cfb0` | Build: build against the GDAL versions of older distributions | upstreamable | not submitted |
| `a5f0972` | Build: publish N-API binaries as napi-v8-<platform>-<arch> | fork-only | not submitted |
| `f0b00bd` | Build: package a prebuilt GDAL and build the addon from it | fork-only | not submitted |
| `844a940` | Build: obtain the dependencies through Conan and default to Ninja | fork-only | not submitted |
| `e6e7f71` | Build: add a CMake.js build alongside binding.gyp | fork-only | not submitted |
| `9e1fa9e` | CI and the AddressSanitizer runs: run the AddressSanitizer tests on the runner | fork-only | not submitted |
| `fc2f1b0` | CI and the AddressSanitizer runs: pin Conan and keep a runaway job from burning a runner | fork-only | not submitted |
| `ef6e984` | CI and the AddressSanitizer runs: preload the AddressSanitizer library of the container | fork-only | not submitted |
| `d51e9d2` | CI and the AddressSanitizer runs: build, test and publish the addon on every architecture | fork-only | not submitted |
| `1fb9fb3` | CI and the AddressSanitizer runs: build and test the addon with Conan on Linux, Windows and macOS | fork-only | not submitted |
| `4d73218` | Public API: close what nobody closed when the process exits | fork-only | not submitted |
| `42f78ab` | Public API: track the datasets which a scope opens implicitly | fork-only | not submitted |
| `b129f10` | Public API: give the callers a scope which closes the dataset | fork-only | not submitted |
| `cae6dfe` | Public API: close a dataset without blocking the event loop | fork-only | not submitted |
| `5f446d7` | Public API: remove a ring from a polygon | upstreamable | not submitted |
| `19916de` | Public API: give the module a modern surface | fork-only | not submitted |
| `0b26d93` | Public API: let the band statistics report progress and be interrupted | upstreamable | not submitted |
| `35bf803` | Public API: catch up with the modern Node.js surface | upstreamable | not submitted |
| `daf973d` | Public API: support aborting the operations with an AbortController | upstreamable | not submitted |
| `f360688` | Public API: support worker_threads | fork-only | not submitted |
| `a934db8` | Bug fixes: reject instead of throwing from the polyfill of a destroyed band | upstreamable | not submitted |
| `708684b` | Bug fixes: hand out a collection only while its parent is alive | upstreamable | not submitted |
| `6269fe2` | Bug fixes: honour the abort which a synchronous progress callback sets | upstreamable | not submitted |
| `b641ce3` | Bug fixes: refuse an index which is not an integer | upstreamable | not submitted |
| `abd096a` | Bug fixes: refuse an object which only claims to be an array | upstreamable | not submitted |
| `350182e` | Bug fixes: refuse a driver index which cannot name a driver | upstreamable | not submitted |
| `f1be6bd` | Bug fixes: check the callback of forEach() and of map() | upstreamable | not submitted |
| `1572adc` | Bug fixes: name what a lookup or an operation could not do | upstreamable | not submitted |
| `16cd604` | Bug fixes: name the driver which does not exist, and set the list fields | upstreamable | not submitted |
| `dfbc31e` | Bug fixes: repair the multiplexer and the streams, and support the new types | upstreamable | not submitted |
| `cf81e05` | Bug fixes: stop leaking the objects which outlive their environment | upstreamable | not submitted |
| `fe516c9` | Bug fixes: the Windows exit access violation in the memfile | fork-only | not submitted |
| `9a3a6ae` | Documentation: plan the automatic closing of the datasets | fork-only | not submitted |
| `23112fe` | Documentation: name the arguments of copyFiles in the right order | upstreamable | not submitted |
| `97d5f6c` | Documentation: plan the refactoring which removes the remaining structural debt | fork-only | not submitted |
| `d6e8f5a` | Documentation: document the Feature and FieldDefn constructors | upstreamable | not submitted |
| `d87d08a` | Tests and the test harness: run the examples of `using` only where the engine has it | upstreamable | not submitted |
| `f43d309` | Tests and the test harness: abort the asynchronous operations while they are running | upstreamable | not submitted |
| `a39ef61` | Tests and the test harness: do not depend on the platform or on a local sidecar | upstreamable | not submitted |
| `644d3c6` | Tests and the test harness: check that every documented example is a program | upstreamable | not submitted |
| `484aae5` | Tests and the test harness: run the examples of the README | upstreamable | not submitted |
| `dbb1006` | Tests and the test harness: cover the whole surface, and check the typings against it | fork-only | not submitted |
| `9f83039` | Tests and the test harness: serve the vsicurl fixtures from a local server | fork-only | not submitted |
| `125856f` | Tests and the test harness: make the abort tests independent of the machine speed | fork-only | not submitted |
| `6f5a707` | Tests and the test harness: run the test files in parallel, and one of them on its own | fork-only | not submitted |

## What the fork adds on top of the upstream 3.13.0

* **Automatic closing of the datasets** - `Dataset.closeAsync()`,
  `gdal.withDataset()`/`gdal.withDatasetAsync()`, `gdal.using()`/`gdal.usingAsync()`
  (`AsyncLocalStorage`-based scope tracking) and closing whatever the process
  forgot to close when it exits, so that a `process.exit()` cannot lose the
  buffered writes of a dataset opened for update.
* **A modern build** - CMake.js and Conan instead of node-gyp and vendored
  sources; N-API instead of NAN; the typings generated and checked against the
  source; the test suite run in parallel.
* **`worker_threads` support**, `AbortController` support and error classes.
* A surface hardening campaign: every documented example is executed by the
  test suite, every member of every class is called at least once, the typings
  are checked against the source, and the failure messages name what failed.
