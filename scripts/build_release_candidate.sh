#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: $0 REPOSITORY OUTPUT_DIRECTORY [GIT_REF]" >&2
  exit 2
fi

repository_path=$(realpath "$1")
output_directory=$(realpath -m "$2")
release_ref=${3:-HEAD}

if [[ -n $(git -C "${repository_path}" status --porcelain) ]]; then
  echo "release candidate archive requires a clean working tree" >&2
  exit 3
fi

release_commit=$(git -C "${repository_path}" rev-parse "${release_ref}^{commit}")
release_version=$(
  git -C "${repository_path}" show "${release_commit}:CMakeLists.txt" |
    sed -n 's/^project(SeqPro VERSION \([0-9][0-9.]*\) LANGUAGES.*$/\1/p'
)
if [[ -z ${release_version} ]]; then
  echo "cannot determine SeqPro version from ${release_commit}:CMakeLists.txt" >&2
  exit 3
fi

short_commit=${release_commit:0:12}
source_date_epoch=$(git -C "${repository_path}" show -s --format=%ct "${release_commit}")
archive_stem="seqpro-${release_version}-rc.${short_commit}"

mkdir -p "${output_directory}"
temporary_directory=$(mktemp -d "${output_directory}/.seqpro-release-XXXXXX")
cleanup() {
  if [[ -n ${temporary_directory:-} && -d ${temporary_directory} ]]; then
    cmake -E remove_directory "${temporary_directory}"
  fi
}
trap cleanup EXIT

for archive_iteration in first second; do
  git -C "${repository_path}" archive \
    --format=tar \
    --prefix="seqpro-${release_version}/" \
    "${release_commit}" >"${temporary_directory}/${archive_iteration}.tar"
  gzip -n -9 -c "${temporary_directory}/${archive_iteration}.tar" \
    >"${temporary_directory}/${archive_iteration}.tar.gz"
done

if ! cmp -s "${temporary_directory}/first.tar.gz" \
              "${temporary_directory}/second.tar.gz"; then
  echo "release candidate archive is not reproducible" >&2
  exit 3
fi

archive_path="${output_directory}/${archive_stem}.tar.gz"
cp "${temporary_directory}/first.tar.gz" "${archive_path}"
archive_sha256=$(sha256sum "${archive_path}" | awk '{print $1}')
printf '%s  %s\n' "${archive_sha256}" "$(basename "${archive_path}")" \
  >"${archive_path}.sha256"

manifest_path="${output_directory}/${archive_stem}.manifest.json"
printf '%s\n' \
  '{' \
  "  \"project\": \"SeqPro\"," \
  "  \"version\": \"${release_version}\"," \
  "  \"release_kind\": \"candidate\"," \
  "  \"commit\": \"${release_commit}\"," \
  "  \"source_date_epoch\": ${source_date_epoch}," \
  "  \"archive\": \"$(basename "${archive_path}")\"," \
  "  \"sha256\": \"${archive_sha256}\"," \
  '  "supported_platform": "x86_64 Linux/WSL",' \
  '  "minimum_cmake": "3.20",' \
  '  "minimum_compilers": ["GCC 9", "Clang 10 with libstdc++"],' \
  '  "bundled_dependencies": [' \
  '    {"name": "xxHash", "version": "0.8.3", "license": "BSD-2-Clause"}' \
  '  ]' \
  '}' >"${manifest_path}"

echo "${archive_path}"
echo "${archive_path}.sha256"
echo "${manifest_path}"
