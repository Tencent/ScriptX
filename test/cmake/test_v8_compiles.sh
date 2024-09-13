#! /bin/bash

# USAGE:
# <script> # no param, test all supported versions, abort on failure
# <script> continure # ignore errors and go on
# <script> version # test for given version

ABORT_ON_FAILURE=True

if [[ $1 == "continue" ]]; then
  echo "continure on failure"
else
  set -e
fi

if echo $1 | egrep -q -e '^\d+\.\d+.\d+$'; then
  echo "test for version $1"
  TARGET_VERSION=$1
fi

BUILD_DIR=$(realpath $(dirname "$0"))/../cmake-build-v8
V8_INCLUDES_DIR=${BUILD_DIR}/ScriptXTestLibs/v8/includes
V8_SUPPORTED_VERSIONS=${BUILD_DIR}/ScriptXTestLibs/v8/supported_versions.txt

if [[ $(uname) == "Darwin" ]]; then
  NPROC=$(sysctl -n hw.ncpu)
else
  NPROC=$(nproc)
fi

mkdir -p $BUILD_DIR
cd $BUILD_DIR

SUPPORTED_VERSIONS=($(< ${V8_SUPPORTED_VERSIONS}))
CMAKE="cmake --log-level=ERROR -Wno-dev -Wno-deprecated .. -DSCRIPTX_BACKEND=V8 -DSCRIPTX_TEST_BUILD_ONLY=ON"

echo "supported v8 version: ${SUPPORTED_VERSIONS[@]}"

echo STEP 1. initial configure to download depedencies
rm -rf CMakeCache.txt
$CMAKE

echo STEP 2. test compile for each version

PASSED_VERSIONS=()
FAILED_VERSIONS=()

function compile() {
  version=$1
  FILE="${V8_INCLUDES_DIR}/$version"
  echo v8 version ${version}
  rm -rf CMakeCache.txt
  $CMAKE -DSCRIPTX_V8_INCLUDES=${FILE}
  make -j${NPROC} clean ScriptX
  if [[ $? -eq 0 ]]; then
    PASSED_VERSIONS+=($version)
  else
    FAILED_VERSIONS+=($version)
  fi
}

if [[ -n $TARGET_VERSION ]] ; then
  compile $TARGET_VERSION
else
  for version in "${SUPPORTED_VERSIONS[@]}"; do
    compile $version
  done
fi

echo "passed versions: [${PASSED_VERSIONS[@]}]"
echo "failed versions: [${FAILED_VERSIONS[@]}]"

