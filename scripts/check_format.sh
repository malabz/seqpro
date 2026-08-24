#!/usr/bin/env bash

set -euo pipefail

clang_format_executable=${1:-clang-format}
repository_path=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

source_directories=(
  "${repository_path}/include"
  "${repository_path}/src"
  "${repository_path}/tools"
  "${repository_path}/examples"
  "${repository_path}/benchmarks"
  "${repository_path}/tests"
  "${repository_path}/extensions"
  "${repository_path}/fuzz"
)

mapfile -d '' source_files < <(
  find "${source_directories[@]}" -type f \
    \( -name '*.h' -o -name '*.cc' \) \
    ! -path '*/third_party/*' -print0 |
    sort -z
)

if [[ ${#source_files[@]} -eq 0 ]]; then
  echo "no SeqPro-owned C++ source files were found" >&2
  exit 3
fi

"${clang_format_executable}" --dry-run --Werror "${source_files[@]}"
