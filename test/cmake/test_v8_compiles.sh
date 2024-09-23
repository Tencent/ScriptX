#! /bin/bash

# USAGE:
# <script> # no param, test all supported versions, abort on failure
# <script> continure # ignore errors and go on
# <script> version # test for given version

function log() {
  #echo -e "\e[1;32m${1}\e[0m"
  echo -e "\033[1;31m${*}\033[0m"
}

if [[ $1 == "continue" ]]; then
  log "continure on failure"
else
  set -e
fi

if echo "$1" | grep -q -w -E '[0-9]+\.[0-9]+.*'; then
  log "test for version $1"
  TARGET_VERSION=$1
fi

BUILD_DIR=$(realpath $(dirname "$0"))/../cmake-build-v8
V8_INCLUDES_DIR=${BUILD_DIR}/ScriptXTestLibs/v8/includes
V8_SUPPORTED_VERSIONS=${BUILD_DIR}/ScriptXTestLibs/v8/supported_versions.txt
CMAKE="cmake --log-level=ERROR -Wno-dev -Wno-deprecated .. -DSCRIPTX_BACKEND=V8 -DSCRIPTX_TEST_BUILD_ONLY=ON"

log "STEP 1. initial configure to download depedencies"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
rm -rf CMakeCache.txt
$CMAKE

log "STEP 2. test compile for each version"

SUPPORTED_VERSIONS=($(< "$V8_SUPPORTED_VERSIONS"))
PASSED_VERSIONS=()
FAILED_VERSIONS=()

log "supported v8 version:" "${SUPPORTED_VERSIONS[@]}"

if [[ $(uname) == "Darwin" ]]; then
  NPROC=$(sysctl -n hw.ncpu)
else
  NPROC=$(nproc)
fi

log "CPU cores $NPROC"

function compile() {
  version=$1
  FILE="${V8_INCLUDES_DIR}/$version"
  log "v8 version ${version}"
  rm -rf CMakeCache.txt
  $CMAKE -DSCRIPTX_V8_INCLUDES="${FILE}"
  make -j${NPROC} clean ScriptX UnitTests
  if [[ $? -eq 0 ]]; then
    PASSED_VERSIONS+=($version)
  else
    FAILED_VERSIONS+=($version)
  fi
}

if [[ -n $TARGET_VERSION ]] ; then
  compile $TARGET_VERSION
else
  if echo "$SCRIPTX_TEST_V8_JOB_SPLIT_CONFIG" | grep -q -w -E '[0-9]+\/[0-9]+'; then
    TOTAL=$(echo ${SCRIPTX_TEST_V8_JOB_SPLIT_CONFIG} | cut -d/ -f 2)
    INDEX=$(echo ${SCRIPTX_TEST_V8_JOB_SPLIT_CONFIG} | cut -d/ -f 1)
    COUNT=$((${#SUPPORTED_VERSIONS[@]} - 1))

    START=$(((COUNT * INDEX + TOTAL - 1) / TOTAL))  # upper(COUNT * INDEX / TOTAL)
    END=$((COUNT * (INDEX + 1) / TOTAL))            # lower(COUNT * (INDEX+1) / TOTAL)

    log "split job=[$START, $END] total=$COUNT"

    for ((i = START; i <= END; i++)); do
      compile ${SUPPORTED_VERSIONS[$i]}
    done
  else
    for version in "${SUPPORTED_VERSIONS[@]}"; do
      compile $version
    done
  fi
fi

log "passed versions: [" "${PASSED_VERSIONS[@]}" "]"
log "failed versions: [" "${FAILED_VERSIONS[@]}" "]"

exit "${#FAILED_VERSIONS[@]}"

