# Contributing to SeqPro

SeqPro is a general-purpose sequence library. Changes should preserve format interoperability,
bounded memory use, explicit ownership, and predictable coordinate semantics rather than optimize
for one downstream application.

## Supported development baseline

- x86_64, 64-bit Linux or WSL.
- CMake 3.20 or newer.
- GCC 9 or newer, or Clang 10 or newer with libstdc++.
- C++17 public interfaces.

The default build must remain a dependency-light core library. The optional SequenceText component
must remain independently selectable with `SEQPRO_BUILD_SEQUENCE_TEXT`.

## Configure, build, and test

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_BUILD_EXAMPLES=ON \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON

cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Before proposing a change, also run:

```bash
git diff --check

clang-format --dry-run --Werror \
  $(find include src tools examples benchmarks tests extensions fuzz \
    -type f \( -name '*.h' -o -name '*.cc' \) \
    ! -path '*/third_party/*')

cmake -S . -B build-docs \
  -DSEQPRO_BUILD_DOCUMENTATION=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON
cmake --build build-docs --target seqpro-docs

shellcheck scripts/*.sh
```

When clang-tidy is available:

```bash
cmake -S . -B build-tidy \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_BUILD_EXAMPLES=ON \
  -DSEQPRO_ENABLE_CLANG_TIDY=ON \
  -DSEQPRO_WARNINGS_AS_ERRORS=ON
cmake --build build-tidy --parallel
```

Fuzzers are Clang-only, top-level-only, non-installed targets. They are disabled in normal builds:

```bash
cmake -S . -B build-fuzz \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DSEQPRO_BUILD_TOOLS=OFF \
  -DSEQPRO_BUILD_SEQUENCE_TEXT=ON \
  -DSEQPRO_BUILD_FUZZERS=ON
cmake --build build-fuzz --parallel
```

## Naming and formatting

SeqPro follows the Google C++ naming and formatting baseline with a 100-column limit:

- types: `UpperCamelCase`;
- functions and non-trivial methods: `UpperCamelCase`;
- simple property accessors: `snake_case`;
- variables and parameters: descriptive `lower_snake_case`;
- private data members: `lower_snake_case_`;
- enum values and constants: `kUpperCamelCase`;
- files: `lower_snake_case.h` and `lower_snake_case.cc`;
- macros: `SEQPRO_UPPER_SNAKE_CASE`.

Coordinates end in `_position`; half-open boundaries use `_start_position` and
`_end_position`; logical lengths use `_length`; counts use `_count`; byte sizes and offsets use
`_size_bytes` and `_offset_bytes`. Avoid context-free names such as `data`, `info`, `state`,
`bytes`, `line`, `output`, `options`, `report`, and non-idiomatic abbreviations.

Do not reformat or rename symbols in `third_party/xxhash`.

## Public API and format compatibility

- Standard five-column FAI and metadata schema 1 changes require an explicit format review.
- Public headers must not expose POSIX, xxHash, parser, or implementation-only types.
- Core API additions belong in `include/seqpro`.
- SequenceText API additions belong in its extension and must not enter `seqpro/seqpro.h`.
- Every public symbol requires Doxygen documentation covering coordinates, ownership, failures,
  lifetime, thread safety, and complexity when relevant.
- Source-compatibility consumers must exercise every public field and method.

Within 0.2.x, preserve source and ABI compatibility for the same compiler and C++ runtime ABI. A
public ABI change requires an updated source consumer, libabigail review, changelog entry, and
normally a new minor version and SONAME. Binary compatibility is not claimed across compiler
runtimes or `_GLIBCXX_USE_CXX11_ABI` settings.

## Tests

Every behavior change should include focused boundary tests. Parser changes should cover malformed
inputs and in-memory fuzz entry points. Coordinate changes should include a simple oracle and
round-trip properties. Concurrency changes must retain immutable, lock-free const-query phases and
run under the appropriate thread sanitizer.

Do not make the default local test suite depend on multi-gigabyte genomes or hardware-specific
throughput thresholds. Microbenchmarks report regressions; correctness tests determine acceptance.

## Commits and releases

Keep changes focused and explain user-visible behavior. Do not include generated build trees,
benchmark output, fuzz crashes, credentials, or project-specific data. Creating tags, GitHub
Releases, and release assets follows [docs/releasing.md](docs/releasing.md) and is intentionally
separate from normal development.
