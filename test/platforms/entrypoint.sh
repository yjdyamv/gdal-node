#!/bin/bash

# A failing step aborts the test run: a failed build must never be followed
# by the test suite, which would then only report the confusing secondary
# errors caused by the missing binding
set -e

export HOME=/target
SEP="\n=======================================================\n"
echo -e "${SEP}Preparing source tree${SEP}"

if [ -n "${CCACHE_DIR}" ]; then
  echo -e "${SEP}Checking for ccache"
  if which ccache; then
    if [ -z "${CC}" ]; then
      export CC="ccache gcc"
    fi
    if [ -z "${CXX}" ]; then
      export CXX="ccache g++"
    fi
    echo -e "Using ccache in ${CCACHE_DIR} CC=\"${CC}\" CXX=\"${CXX}\"${SEP}"
  else
    echo -e "Container has no ccache installed${SEP}"
  fi
else
  echo -e "${SEP}Host has no ccache installed${SEP}"
fi
rsync -lr /src/ /target/src --exclude=build --exclude=test/data --exclude node_modules
cd /target/src
rm -rf lib/binding package-lock.json
git checkout test/data
npm install --ignore-scripts

echo -e "${SEP}Preparing Conan${SEP}"
# Conan takes CC and CXX for the name of the compiler executable: the two-word
# "ccache gcc" of the masquerade above ends up detected as no compiler at all,
# and the profile it writes then rejects the compiler.cppstd=20 of the install
# with "'settings.compiler' value not defined"
env -u CC -u CXX conan profile detect --force

# "ccache gcc" is not a compiler CMake can use: CMake keeps ccache as the
# compiler and gcc as its first, permanent argument, and a recipe that calls the
# compiler itself - libpng preprocesses its configuration with it - ends up
# running "ccache -E".  A real compiler with ccache as its launcher is the way
# CMake expects both the compiler and ccache to be given
CONFS=""
if [ -x /usr/bin/gcc ] && which ccache >/dev/null; then
  CONFS="${CONFS}tools.build:compiler_executables={\"c\": \"gcc\", \"cpp\": \"g++\"}\n"
  CONFS="${CONFS}tools.cmake.cmaketoolchain:extra_variables={\"CMAKE_C_COMPILER_LAUNCHER\": \"ccache\", \"CMAKE_CXX_COMPILER_LAUNCHER\": \"ccache\"}\n"
fi
if [ -n "${CONFS}" ]; then
  printf '[conf]\n%b' "${CONFS}" >> ${HOME}/.conan2/profiles/default
fi
case ${1} in
  DEV)
    echo -e "${SEP}Testing the current version against the bundled GDAL${SEP}"
    node scripts/build.mjs
    npm test
    R=0
    ;;

  RELEASE)
    echo -e "${SEP}Testing pre-built binary${SEP}"
    npm install --ignore-scripts
    node scripts/install.mjs
    npx yatag
    npm test
    R=0
    ;;

  PUBLISH)
    echo -e "${SEP}Publishing pre-built binary${SEP}"
    node scripts/build.mjs
    npm test
    npm run package
    npx node-pre-gyp-github publish
    R=0
    ;;

  ASAN)
    echo -e "${SEP}Testing the current version with ASAN${SEP}"
    export CXXFLAGS="-fsanitize=address -fno-omit-frame-pointer -g"
    export LDFLAGS="-fsanitize=address"
    node scripts/build.mjs --debug
    LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libasan.so.5.0.0 npm test 2> asan.output
    if egrep -q "node_gdal|GDAL" asan.output; then
      echo ${SEP}${SEP}Memory leaks detected${SEP}${SEP}
      cat asan.output
      exit 1
    fi
    R=0
    ;;
esac

# Launch docker -it ... /bin/bash to get a shell after running the test
if [ ! -z "${2}" ]; then
  shift
  exec $@
fi

exit ${R:-0}
