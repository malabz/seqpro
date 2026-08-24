# Local release-candidate and release procedure

SeqPro release validation is performed explicitly on a maintainer-controlled Linux or WSL
environment. Validation and publishing remain separate: the commands below create evidence and
candidate assets, but they never create a tag or GitHub Release.

## Compatibility contract

- The root `project(SeqPro VERSION ...)` declaration is the single version source.
- Generated `seqpro/version.h`, the CLI, package config, and library filenames must agree.
- 0.2.x patches use SONAME `0.2` and CMake `SameMinorVersion` compatibility.
- ABI checks compare libraries built with the same fixed GCC/libstdc++ toolchain.
- Compatibility is not claimed across libc++, libstdc++ ABI modes, architectures, or minor
  versions.
- The core FAI and metadata schema 1 remain independent from SequenceText.

## Prepare an exact candidate commit

Start from the intended release branch and require a clean worktree:

```bash
git status --short --branch
git diff --check
git rev-parse HEAD
```

Confirm that `CHANGELOG.md` contains one `0.2.0 - Unreleased` entry and that all public names are
final. Record the full commit hash with the validation results; every archive and ABI result must
refer to that same commit.

## Core and extension build matrix

Run a core-only static Release build:

```bash
cmake -S . -B build/release-core -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DSEQPRO_BUILD_EXAMPLES=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=OFF \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build build/release-core --parallel
ctest --test-dir build/release-core --output-on-failure
```

Run a SequenceText-enabled shared RelWithDebInfo build. This configuration also checks SONAME,
dynamic exports, cross-DSO exceptions, installation, relocation, consumers, and the frozen ABI:

```bash
cmake -S . -B build/release-shared -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DSEQPRO_BUILD_EXAMPLES=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_ENABLE_ABI_CHECKS=ON \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build build/release-shared --parallel
ctest --test-dir build/release-shared --output-on-failure
```

Repeat the supported compiler boundary with GCC 9+ and Clang 10+ when preparing a public release.
Use separate build directories and record compiler, CMake, glibc, and libstdc++ versions.

## Formatting, documentation, and static analysis

Run the repository-owned checks without rewriting source files:

```bash
bash scripts/check_format.sh clang-format
cmake -DSEQPRO_SOURCE_DIR="${PWD}" -P cmake/CheckSourceConventions.cmake
cmake -DSEQPRO_SOURCE_DIR="${PWD}" -P cmake/CheckDocumentation.cmake
shellcheck scripts/*.sh
git diff --check
```

Generate strict API documentation:

```bash
cmake -S . -B build/release-docs -G Ninja \
  -DSEQPRO_BUILD_TOOLS=OFF \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_BUILD_DOCUMENTATION=ON
cmake --build build/release-docs --target seqpro-docs
```

Run clang-tidy on all owned targets:

```bash
CC=clang CXX=clang++ cmake -S . -B build/release-tidy -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DSEQPRO_BUILD_EXAMPLES=ON \
  -DSEQPRO_BUILD_BENCHMARKS=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_ENABLE_CLANG_TIDY=ON \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build build/release-tidy --parallel
```

Run GCC's analyzer. The two existing libstdc++ false-positive suppressions remain narrowly scoped;
all other warnings are fatal:

```bash
CC=gcc CXX=g++ cmake -S . -B build/release-gcc-analyzer -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fanalyzer -Wno-analyzer-malloc-leak -Wno-analyzer-use-of-uninitialized-value" \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DSEQPRO_BUILD_EXAMPLES=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build build/release-gcc-analyzer --parallel
```

## Sanitizers

Run AddressSanitizer, UndefinedBehaviorSanitizer, and LeakSanitizer with Clang:

```bash
sanitizer_flags="-fsanitize=address,undefined,leak -fno-omit-frame-pointer"
CC=clang CXX=clang++ cmake -S . -B build/release-sanitizers -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="${sanitizer_flags}" \
  -DCMAKE_EXE_LINKER_FLAGS="${sanitizer_flags}" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DSEQPRO_BUILD_EXAMPLES=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build build/release-sanitizers --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/release-sanitizers --output-on-failure \
  -R '^seqpro\.(unit|sequence_text\.unit)$'
```

Run ThreadSanitizer separately with GCC:

```bash
thread_sanitizer_flags="-fsanitize=thread -fno-omit-frame-pointer"
CC=gcc CXX=g++ cmake -S . -B build/release-thread-sanitizer -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="${thread_sanitizer_flags}" \
  -DCMAKE_EXE_LINKER_FLAGS="${thread_sanitizer_flags}" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=OFF \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build build/release-thread-sanitizer --parallel
TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 \
ctest --test-dir build/release-thread-sanitizer --output-on-failure \
  -R '^seqpro\.(unit|sequence_text\.unit)$'
```

## Samtools interoperability

Install Samtools 1.24, ensure `samtools` is on `PATH`, and run the bidirectional FAI integration
test from a core build:

```bash
cmake -S . -B build/release-samtools -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=OFF \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build build/release-samtools --parallel
ctest --test-dir build/release-samtools --output-on-failure \
  -R '^seqpro\.integration\.samtools$'
```

## Four bounded fuzz campaigns

Build the non-installed Clang fuzz targets:

```bash
CC=clang CXX=clang++ cmake -S . -B build/release-fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DSEQPRO_BUILD_TOOLS=OFF \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_BUILD_FUZZERS=ON \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build build/release-fuzz --parallel
```

Run each target for at least 300 seconds. Corpora and failures remain under `fuzz-results/` for
inspection and archival:

```bash
fuzz_duration_seconds=300
mkdir -p fuzz-results
set -o pipefail
declare -A corpus_directories=(
  [seqpro-fuzz-fasta-index]=fasta_index
  [seqpro-fuzz-metadata]=metadata
  [seqpro-fuzz-fasta-scanner]=fasta_scanner
  [seqpro-fuzz-sequence-text]=sequence_text
)

for fuzz_target in \
  seqpro-fuzz-fasta-index \
  seqpro-fuzz-metadata \
  seqpro-fuzz-fasta-scanner \
  seqpro-fuzz-sequence-text; do
  result_directory="fuzz-results/${fuzz_target}"
  corpus_directory="${result_directory}/corpus"
  artifact_directory="${result_directory}/artifacts"
  mkdir -p "${corpus_directory}" "${artifact_directory}"
  cp -a "fuzz/corpus/${corpus_directories[${fuzz_target}]}/." \
    "${corpus_directory}/"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "build/release-fuzz/fuzz/${fuzz_target}" \
    -artifact_prefix="${artifact_directory}/" \
    -max_len=65536 \
    -max_total_time="${fuzz_duration_seconds}" \
    -rss_limit_mb=4096 \
    -timeout=5 \
    "${corpus_directory}" \
    2>&1 | tee "${result_directory}/fuzzer.log"
done
```

Any crash, timeout, sanitizer report, assertion failure, or unexpected exception blocks release.
Preserve the final corpus, logs, and every generated artifact with the candidate evidence.

## Reproducible candidate assets

Generate a source candidate twice from the exact clean commit and validate the extracted package:

```bash
candidate_commit=$(git rev-parse HEAD)
bash scripts/build_release_candidate.sh \
  . release-artifacts "${candidate_commit}"
source_archive=$(find release-artifacts -maxdepth 1 \
  -name 'seqpro-0.2.0-rc.*.tar.gz' -print -quit)
test -n "${source_archive}"
bash scripts/validate_release_archive.sh "${source_archive}"
```

Package the already generated Doxygen HTML reproducibly:

```bash
source_date_epoch=$(git show -s --format=%ct "${candidate_commit}")
tar --sort=name \
  --mtime="@${source_date_epoch}" \
  --owner=0 --group=0 --numeric-owner \
  -C build/release-docs/api-docs \
  -cf - html \
  | gzip -n -9 \
  > release-artifacts/seqpro-0.2.0-api-docs.tar.gz
sha256sum release-artifacts/seqpro-0.2.0-api-docs.tar.gz \
  > release-artifacts/seqpro-0.2.0-api-docs.tar.gz.sha256
```

Retain compiler and system versions, test logs, sanitizer results, fuzz corpora, ABI reports,
source archive, checksum, manifest, and API documentation together. Do not publish generic
precompiled Linux binaries because their glibc, libstdc++, and compiler ABI would imply unsupported
compatibility.

## ABI baseline

`abi/v0.2.0/seqpro.abi` and `abi/v0.2.0/sequence_text.abi` are generated from DWARF-enabled shared
libraries using a fixed GCC/libstdc++ toolchain. Do not regenerate a baseline merely to make a
failing diff disappear.

For a compatible 0.2.x change, inspect `abidiff`, preserve public layouts and exported behavior,
and update tests without changing the baseline. For an intentional incompatible release, choose a
new minor version, update the SONAME and source consumers, then generate and review a new baseline.

## Publish the final 0.2.0 release

Publishing remains a separate, explicitly authorized operation:

1. Replace `Unreleased` with the release date and add a new top-level `Unreleased` section.
2. Rerun every local release-candidate check on the exact intended commit.
3. Create an annotated `v0.2.0` tag and verify its target commit.
4. Create a GitHub Release using `docs/releases/v0.2.0.md`.
5. Upload the validated source archive, checksum, manifest, and API documentation.
6. Verify public checksums and install from the downloaded archive in a clean directory.

No repository command in this document creates a tag or publishes a release implicitly.
