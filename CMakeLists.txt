# CMake build for the gdal-async native addon.
#
# Generator: CMake's default for the platform (Visual Studio on Windows).
# CMakePresets.json contains Ninja presets for a manual command-line build.
#
# GDAL comes from Conan:
#
#   conan install . -of build-conan -s build_type=Release
#   cmake --preset conan-release          # or -DCMAKE_TOOLCHAIN_FILE=... below
#   cmake --build --preset conan-release
#
# Plain CMake, equivalent:
#
#   cmake -G Ninja -S . -B build -DCMAKE_TOOLCHAIN_FILE=build-conan/conan_toolchain.cmake
#   cmake --build build
#
# The binding lands in lib/binding/napi-v8-<platform>-<arch>/gdal.node, the
# directory that lib/gdal.js and scripts/install.mjs look in.  The dependencies
# are not vendored anymore, they come from Conan.

cmake_minimum_required(VERSION 3.15)

project(gdal-async LANGUAGES CXX)

# Node 24's V8 headers require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
endif()

# ---------------------------------------------------------------------------
# Node.js headers and node-addon-api, resolved through node-addon-api itself
# ---------------------------------------------------------------------------
find_program(NODE_EXECUTABLE NAMES node REQUIRED)

execute_process(
  COMMAND ${NODE_EXECUTABLE} -p "require('node-addon-api').include_dir"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE NODE_ADDON_API_INCLUDE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT NODE_ADDON_API_INCLUDE_DIR)
  message(FATAL_ERROR "node-addon-api is not installed (run npm install first)")
endif()

# node-pre-gyp's directory name for the N-API build, e.g. napi-v8-linux-x64.
# Keep NAPI_BUILD_VERSION in sync with the napi-v8 in binary.module_path in
# package.json.
set(NAPI_BUILD_VERSION 8)
execute_process(
  COMMAND ${NODE_EXECUTABLE} -p "process.platform + '-' + process.arch"
  OUTPUT_VARIABLE NODE_PLATFORM_ARCH
  OUTPUT_STRIP_TRAILING_WHITESPACE)
set(NODE_PRE_GYP_DIR "napi-v${NAPI_BUILD_VERSION}-${NODE_PLATFORM_ARCH}")

# ---------------------------------------------------------------------------
# GDAL
#
# The package from Conan's CMakeDeps generator, which conan_toolchain.cmake
# puts on CMAKE_PREFIX_PATH.  The addon always ships the GDAL it was built
# with: linking against the GDAL of the system is not supported anymore.
# ---------------------------------------------------------------------------
find_package(gdal CONFIG REQUIRED)

if(NOT TARGET gdal::gdal)
  message(FATAL_ERROR
    "gdal::gdal is missing. Run 'conan install . -of build-conan' to provide it.")
endif()

message(STATUS "GDAL: using the Conan package (gdal::gdal)")
set(GDAL_TARGET gdal::gdal)

# The GDAL of Conan keeps its data files in the Conan cache, which is not
# shipped, so the addon always carries the GDAL and PROJ data files of deps/:
# BUNDLED_GDAL makes the module report that and lib/gdal.js points GDAL_DATA,
# PROJ_LIB and CURL_CA_BUNDLE at them.
option(ENABLE_LOGGING "Compile the logging API (gdal.startLogging and friends)" OFF)

# gdal_coordinate_transformation.cpp includes <proj.h> in every configuration, but
# PROJ is a private dependency of GDAL: Conan links it as $<LINK_ONLY:PROJ::proj>,
# so the library ends up on the link line while its include directories do not.
# Conan's PROJ package also declares no include directories of its own (its
# locations only end up in CMAKE_INCLUDE_PATH), so look for the header first,
# then for a PROJ CMake package that does carry them.
find_package(proj CONFIG QUIET)
find_path(PROJ_INCLUDE_DIR NAMES proj.h)
if(NOT PROJ_INCLUDE_DIR)
  # a PROJ whose CMake package carries the include directories itself
  if(TARGET proj::proj)
    set(PROJ_TARGET proj::proj)
  elseif(TARGET PROJ::proj)
    set(PROJ_TARGET PROJ::proj)
  endif()
endif()
if(NOT PROJ_INCLUDE_DIR AND NOT PROJ_TARGET)
  message(FATAL_ERROR "proj.h not found, set -DPROJ_INCLUDE_DIR=<dir containing proj.h>")
endif()

# ---------------------------------------------------------------------------
# The addon
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(gdal MODULE ${SOURCES})

# node-addon-api throws an exception when it cannot deliver one - which happens
# when the environment is terminating (worker.terminate() with an operation in
# flight). Unwinding through node's C callback aborts the whole process.
target_compile_definitions(gdal PRIVATE NODE_API_SWALLOW_UNTHROWABLE_EXCEPTIONS)

set_target_properties(gdal PROPERTIES PREFIX "" SUFFIX ".node")

target_include_directories(gdal PRIVATE
  ${CMAKE_JS_INC}
  ${NODE_ADDON_API_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${PROJ_INCLUDE_DIR})

target_compile_definitions(gdal PRIVATE
  NODE_GYP_MODULE_NAME=gdal
  BUILDING_NODE_EXTENSION
  _LARGEFILE_SOURCE
  _FILE_OFFSET_BITS=64
  NOGDI=1
  HAVE_LIBZ=1
  NAPI_VERSION=${NAPI_BUILD_VERSION})

# The addon always ships the GDAL it was built with and its PROJ data
target_compile_definitions(gdal PRIVATE BUNDLED_GDAL=1)

if(ENABLE_LOGGING)
  target_compile_definitions(gdal PRIVATE ENABLE_LOGGING=1)
endif()

if(WIN32)
  target_compile_definitions(gdal PRIVATE
    NOMINMAX
    WIN32
    CURL_STATICLIB
    PROJ_DLL=
    OPJ_EXPORTS
    # exceptions must be on for all translation units, sizeof(std::exception)
    # differs between the two MSVC configurations
    _HAS_EXCEPTIONS=1)
  target_compile_options(gdal PRIVATE /MP /GR /EHsc /permissive-)
  # node.lib - the import library of node.exe - and the static zlib coming from
  # Conan both define the zlib symbols that node.exe exports, which MSVC reports
  # as LNK2005.  The definitions of the library that comes first on the command
  # line win, which is the zlib the addon should be using.
  target_link_options(gdal PRIVATE /FORCE:MULTIPLE)
  target_link_libraries(gdal PRIVATE
    secur32 odbccp32 odbc32 crypt32 ws2_32 advapi32 wbemuuid Shlwapi)
elseif(APPLE)
  target_compile_definitions(gdal PRIVATE DARWIN)
  target_link_libraries(gdal PRIVATE "-framework Security")
  # a bundle on macOS refuses undefined symbols: the symbols of Node.js are
  # resolved at load time from the executable, exactly as node-gyp does it
  target_link_options(gdal PRIVATE "-Wl,-undefined,dynamic_lookup")
else()
  target_compile_definitions(gdal PRIVATE LINUX)
  target_link_options(gdal PRIVATE "-Wl,--exclude-libs,ALL")
endif()

target_link_libraries(gdal PRIVATE ${GDAL_TARGET})

# PROJ has to be linked directly for its include directories to be expanded
# (see the PROJ detection above).
if(PROJ_TARGET)
  target_link_libraries(gdal PRIVATE ${PROJ_TARGET})
endif()

# cmake-js passes the Node.js import library (node.lib on Windows) in
# CMAKE_JS_LIB; without it the napi_* and uv_* symbols stay unresolved.
if(CMAKE_JS_LIB)
  target_link_libraries(gdal PRIVATE ${CMAKE_JS_LIB})
endif()

# ---------------------------------------------------------------------------
# The addon is loaded from lib/binding/<binary.module_path>/gdal.node, the
# layout that lib/gdal.js and scripts/install.mjs both compute from package.json
# ---------------------------------------------------------------------------
set(BINDING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/binding/${NODE_PRE_GYP_DIR})

add_custom_command(TARGET gdal POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BINDING_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gdal> ${BINDING_DIR}/gdal.node
  # lib/index.d.ts is the TypeScript declaration of the addon.  It is generated
  # from the JSDoc comments of the sources, it is not committed (it is listed in
  # .gitignore) and the tests do not compile without it.
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}
          ${NODE_EXECUTABLE} node_modules/yatag/yatag.js
  VERBATIM)
