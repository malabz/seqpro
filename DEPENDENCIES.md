# Dependency inventory

SeqPro's installed core library has no mandatory third-party runtime library beyond the platform C
and C++ runtimes.

## Bundled production dependency

| Dependency | Version | Use | Linkage | License |
|---|---|---|---|---|
| xxHash | 0.8.3 | XXH3-128 FASTA and FAI fingerprints | compiled privately into core | BSD-2-Clause |

The bundled source is under `third_party/xxhash`. Its notice is preserved in
`THIRD_PARTY_NOTICES.md`; its headers are not installed and are not part of the public API.

## Build dependencies

- CMake 3.20 or newer.
- A C++17 compiler: GCC 9+ or Clang 10+.
- POSIX/Linux system interfaces for `open`, `read`, `fstat`, `mmap`, `madvise`, `fsync`,
  and atomic rename.

The optional `SeqPro::sequence_text` component adds no third-party dependency and links publicly
only to `SeqPro::seqpro`.

## Optional verification tools

- CTest and Threads for tests.
- Samtools 1.24 for FAI interoperability tests.
- Doxygen 1.9+ for API documentation.
- clang-format and clang-tidy for style and static analysis.
- ShellCheck for local shell and release-script validation.
- Clang libFuzzer plus AddressSanitizer and UndefinedBehaviorSanitizer for fuzz targets.
- GCC ThreadSanitizer and `-fanalyzer` for additional analysis.
- libabigail (`abidw` and `abidiff`) for the 0.2.x ABI gate.
- GNU `readelf`, `nm`, and `c++filt` for shared-library boundary checks.

These verification tools are not required by installed consumers.
