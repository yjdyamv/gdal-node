# CMake build for the gdal-async native addon.
#
# Generator: Ninja (see CMakePresets.json, or -G Ninja). cmake-js also prefers
# Ninja automatically as soon as it is on PATH.
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
# Without Conan it falls back to a GDAL pointed at with -DGDAL_INCLUDE_DIR=<dir
# with gdal.h> and -DGDAL_LIBRARY=<libgdal>.
#
# The binding still lands in lib/binding/napi-v8-<platform>-<arch>/gdal.node,
# so nothing in lib/gdal.js has to change - it locates the binding through
# node-pre-gyp's binary.find().
#
# The vendored sources under deps/ (binding.gyp's SHARED_GDAL=OFF path) are not
# ported to CMake and remain node-gyp-only.

cmake_minimum_required(VERSION 3.15)

project(gdal-async LANGUAGES CXX)

# Node 24's V8 headers require C++20 (cpp_17.gypi's C++17 is a NAN-era leftover)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
endif()

# ---------------------------------------------------------------------------
# Node.js headers and node-addon-api, resolved the same way binding.gyp does
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
# package.json and with NAPI_VERSION in binding.gyp.
set(NAPI_BUILD_VERSION 8)
execute_process(
  COMMAND ${NODE_EXECUTABLE} -p "process.platform + '-' + process.arch"
  OUTPUT_VARIABLE NODE_PLATFORM_ARCH
  OUTPUT_STRIP_TRAILING_WHITESPACE)
set(NODE_PRE_GYP_DIR "napi-v${NAPI_BUILD_VERSION}-${NODE_PLATFORM_ARCH}")

# ---------------------------------------------------------------------------
# GDAL
#
# GDAL_INCLUDE_DIR/GDAL_LIBRARY win when given; otherwise the package from
# Conan's CMakeDeps generator (which conan_toolchain.cmake puts on
# CMAKE_PREFIX_PATH) or a distro GDAL CMake package is used.
# ---------------------------------------------------------------------------
# An explicit GDAL_INCLUDE_DIR/GDAL_LIBRARY wins, so a caller pointing at a
# specific GDAL (a toolchain, a sysroot) always gets exactly that one.
if(GDAL_INCLUDE_DIR AND GDAL_LIBRARY)
  message(STATUS "GDAL: using GDAL_INCLUDE_DIR/GDAL_LIBRARY")
  set(GDAL_TARGET ${GDAL_LIBRARY})
  set(GDAL_FROM_PACKAGE FALSE)
else()
  # Conan's CMakeDeps generates gdal-config.cmake with a gdal::gdal target; a
  # distro GDAL ships a gdal-config.cmake with GDAL::GDAL. The two files have the
  # same name, so accept whichever target actually turns up.
  find_package(gdal CONFIG QUIET)

  if(TARGET gdal::gdal)
    message(STATUS "GDAL: using the Conan package (gdal::gdal)")
    set(GDAL_TARGET gdal::gdal)
    set(GDAL_FROM_PACKAGE TRUE)
  elseif(TARGET GDAL::GDAL)
    message(STATUS "GDAL: using the GDAL CMake package (GDAL::GDAL)")
    set(GDAL_TARGET GDAL::GDAL)
    set(GDAL_FROM_PACKAGE TRUE)
  else()
    message(STATUS "GDAL: no CMake package, searching for gdal.h and libgdal")
    find_path(GDAL_INCLUDE_DIR NAMES gdal.h PATH_SUFFIXES gdal)
    find_library(GDAL_LIBRARY NAMES gdal)

    if(NOT GDAL_INCLUDE_DIR OR NOT GDAL_LIBRARY)
      message(FATAL_ERROR
        "GDAL not found. Run 'conan install .' to provide it, or point CMake at it "
        "with -DGDAL_INCLUDE_DIR=<dir containing gdal.h> and -DGDAL_LIBRARY=<path to libgdal>.")
    endif()

    set(GDAL_TARGET ${GDAL_LIBRARY})
    set(GDAL_FROM_PACKAGE FALSE)
  endif()
endif()

# A GDAL coming from a package carries its own data files, so the module must not
# claim to be self-contained: lib/gdal.js would point GDAL_DATA/PROJ_LIB at deps/.
if(DEFINED BUNDLED_GDAL)
  set(BUNDLED_GDAL_DEFAULT ${BUNDLED_GDAL})
elseif(GDAL_FROM_PACKAGE)
  set(BUNDLED_GDAL_DEFAULT OFF)
else()
  set(BUNDLED_GDAL_DEFAULT ON)
endif()

option(BUNDLED_GDAL
  "Define BUNDLED_GDAL: the module reports itself as shipping its own GDAL/PROJ data"
  ${BUNDLED_GDAL_DEFAULT})
option(ENABLE_LOGGING "Compile the logging API (gdal.startLogging and friends)" OFF)

# gdal_coordinate_transformation.cpp includes <proj.h> in every configuration, but
# PROJ is a private dependency of GDAL: Conan links it as $<LINK_ONLY:PROJ::proj>,
# so the library ends up on the link line while its include directories do not.
# Conan's PROJ package also declares no include directories of its own (its
# locations only end up in CMAKE_INCLUDE_PATH), so look for the header first,
# then for a PROJ CMake package that does carry them, then next to GDAL.
find_package(proj CONFIG QUIET)
find_path(PROJ_INCLUDE_DIR NAMES proj.h)
if(NOT PROJ_INCLUDE_DIR AND GDAL_INCLUDE_DIR)
  # a system GDAL usually sits next to PROJ, e.g. .../include/gdal and .../include
  get_filename_component(GDAL_INCLUDE_ROOT ${GDAL_INCLUDE_DIR} DIRECTORY)
  find_path(PROJ_INCLUDE_DIR NAMES proj.h HINTS ${GDAL_INCLUDE_ROOT})
endif()
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
  ${GDAL_INCLUDE_DIR}
  ${PROJ_INCLUDE_DIR})

target_compile_definitions(gdal PRIVATE
  NODE_GYP_MODULE_NAME=gdal
  BUILDING_NODE_EXTENSION
  _LARGEFILE_SOURCE
  _FILE_OFFSET_BITS=64
  NOGDI=1
  HAVE_LIBZ=1
  NAPI_VERSION=${NAPI_BUILD_VERSION})

if(BUNDLED_GDAL)
  target_compile_definitions(gdal PRIVATE BUNDLED_GDAL=1)
endif()

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
    # differs between the two MSVC configurations - see common.gypi
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
  VERBATIM)
