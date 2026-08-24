#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 RELEASE_ARCHIVE" >&2
  exit 2
fi

release_archive=$(realpath "$1")
validation_root=$(mktemp -d "/tmp/seqpro-release-validation-XXXXXX")
cleanup() {
  if [[ -n ${validation_root:-} && -d ${validation_root} ]]; then
    cmake -E remove_directory "${validation_root}"
  fi
}
trap cleanup EXIT

mkdir -p "${validation_root}/source tree"
tar -xzf "${release_archive}" -C "${validation_root}/source tree" --strip-components=1
source_tree="${validation_root}/source tree"

cmake -S "${source_tree}" -B "${validation_root}/core static" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=OFF \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build "${validation_root}/core static" --parallel
ctest --test-dir "${validation_root}/core static" --output-on-failure

cmake -S "${source_tree}" -B "${validation_root}/extension shared" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build "${validation_root}/extension shared" --parallel
ctest --test-dir "${validation_root}/extension shared" --output-on-failure

initial_prefix="${validation_root}/initial prefix"
relocated_prefix="${validation_root}/relocated prefix"
cmake --install "${validation_root}/extension shared" --prefix "${initial_prefix}"
mv "${initial_prefix}" "${relocated_prefix}"

mkdir -p "${validation_root}/consumer source"
cp "${source_tree}/extensions/sequence_text/tests/consumer/find_package.CMakeLists.txt" \
  "${validation_root}/consumer source/CMakeLists.txt"
cmake -S "${validation_root}/consumer source" \
  -B "${validation_root}/consumer build" -G Ninja \
  -DSEQPRO_SEQUENCE_TEXT_DIR="${source_tree}/extensions/sequence_text" \
  -DCMAKE_PREFIX_PATH="${relocated_prefix}"
cmake --build "${validation_root}/consumer build" --parallel
"${validation_root}/consumer build/seqpro-sequence-text-consumer"
"${validation_root}/consumer build/seqpro-sequence-text-consumer-cxx20"
