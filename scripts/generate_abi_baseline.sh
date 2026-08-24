#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 ABIDW_EXECUTABLE CORE_LIBRARY SEQUENCE_TEXT_LIBRARY" >&2
  exit 2
fi

abidw_executable=$(realpath "$1")
core_library_path=$(realpath "$2")
sequence_text_library_path=$(realpath "$3")
repository_path=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
baseline_directory="${repository_path}/abi/v0.2.0"

mkdir -p "${baseline_directory}"

common_abidw_options=(
  --no-corpus-path
  --no-comp-dir-path
  --short-locs
  --no-parameter-names
  --drop-private-types
  --drop-undefined-syms
  --exported-interfaces-only
  --type-id-style hash
)

"${abidw_executable}" "${common_abidw_options[@]}" \
  --out-file "${baseline_directory}/seqpro.abi" \
  "${core_library_path}"
"${abidw_executable}" "${common_abidw_options[@]}" \
  --out-file "${baseline_directory}/sequence_text.abi" \
  "${sequence_text_library_path}"

if grep -Eq '/mnt/|/tmp/|/home/' "${baseline_directory}"/*.abi; then
  echo "ABI baseline contains an absolute build path" >&2
  exit 3
fi
