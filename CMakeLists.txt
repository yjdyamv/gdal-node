# CMake.js build for the gdal-async native addon.
#
# This coexists with binding.gyp (node-gyp) while the migration is in progress:
# both produce lib/binding/<node_abi>-<platform>-<arch>/gdal.node, so nothing in
# lib/gdal.js has to change - it locates the binding through node-pre-gyp's
# binary.find().
#
# Only the system-GDAL configuration is ported so far. The vendored sources
# under deps/ (binding.gyp's SHARED_GDAL=OFF path) still need node-gyp.

cmake_minimum_required(VERSION 3.15)

project(gdal-async LANGUAGES CXX)

# Node 24's V8 headers require C++20 (cpp_17.gypi's C++17 is a NAN-era leftover)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
endif()

option(SHARED_GDAL "Link against a system GDAL instead of the vendored one under deps/" ON)
option(BUNDLED_GDAL "Define BUNDLED_GDAL: the module reports itself as shipping its own GDAL/PROJ data" ON)
option(ENABLE_LOGGING "Compile the logging API (gdal.startLogging and friends)" OFF)

if(NOT SHARED_GDAL)
  message(FATAL_ERROR
    "Building the vendored GDAL under deps/ is not ported to CMake yet.\n"
    "Configure with -DSHARED_GDAL=ON plus -DGDAL_INCLUDE_DIR=... -DGDAL_LIBRARY=..., "
    "or keep using binding.gyp/node-gyp for that configuration.")
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

# node-pre-gyp's directory name for the running runtime, e.g. node-v137-linux-x64
execute_process(
  COMMAND ${NODE_EXECUTABLE} -p
          "process.release.name + '-v' + process.versions.modules + '-' + process.platform + '-' + process.arch"
  OUTPUT_VARIABLE NODE_PRE_GYP_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# ---------------------------------------------------------------------------
# GDAL
# ---------------------------------------------------------------------------
find_path(GDAL_INCLUDE_DIR NAMES gdal.h PATH_SUFFIXES gdal)
find_library(GDAL_LIBRARY NAMES gdal)

if(NOT GDAL_INCLUDE_DIR OR NOT GDAL_LIBRARY)
  message(FATAL_ERROR
    "GDAL not found. Point CMake at it with -DGDAL_INCLUDE_DIR=<dir containing gdal.h> "
    "and -DGDAL_LIBRARY=<path to libgdal>.")
endif()

if(BUNDLED_GDAL)
  # gdal_coordinate_transformation.cpp includes <proj.h> in this configuration
  find_path(PROJ_INCLUDE_DIR NAMES proj.h)
  if(NOT PROJ_INCLUDE_DIR)
    # a system GDAL usually sits next to PROJ, e.g. .../include/gdal and .../include
    get_filename_component(GDAL_INCLUDE_ROOT ${GDAL_INCLUDE_DIR} DIRECTORY)
    find_path(PROJ_INCLUDE_DIR NAMES proj.h HINTS ${GDAL_INCLUDE_ROOT})
  endif()
  if(NOT PROJ_INCLUDE_DIR)
    message(FATAL_ERROR "proj.h not found, set -DPROJ_INCLUDE_DIR=<dir containing proj.h>")
  endif()
endif()

# ---------------------------------------------------------------------------
# The addon
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(gdal MODULE ${SOURCES})
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
  HAVE_LIBZ=1)

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
  target_link_libraries(gdal PRIVATE
    secur32 odbccp32 odbc32 crypt32 ws2_32 advapi32 wbemuuid Shlwapi)
elseif(APPLE)
  target_compile_definitions(gdal PRIVATE DARWIN)
  target_link_libraries(gdal PRIVATE "-framework Security")
else()
  target_compile_definitions(gdal PRIVATE LINUX)
  target_link_options(gdal PRIVATE "-Wl,--exclude-libs,ALL")
endif()

target_link_libraries(gdal PRIVATE ${GDAL_LIBRARY})

# cmake-js passes the Node.js import library (node.lib on Windows) in
# CMAKE_JS_LIB; without it the napi_* and uv_* symbols stay unresolved.
if(CMAKE_JS_LIB)
  target_link_libraries(gdal PRIVATE ${CMAKE_JS_LIB})
endif()

# ---------------------------------------------------------------------------
# node-pre-gyp layout: lib/gdal.js -> binary.find() ->
# lib/binding/<node_abi>-<platform>-<arch>/gdal.node
# ---------------------------------------------------------------------------
set(BINDING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/binding/${NODE_PRE_GYP_DIR})

add_custom_command(TARGET gdal POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BINDING_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gdal> ${BINDING_DIR}/gdal.node
  VERBATIM)
