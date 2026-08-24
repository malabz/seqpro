# SeqPro

[![Quality](https://github.com/malabz/seqpro/actions/workflows/quality.yml/badge.svg)](https://github.com/malabz/seqpro/actions/workflows/quality.yml)

[简体中文](README.zh-CN.md)

SeqPro is a C++17 library for indexed random access to uncompressed FASTA files. The core library
creates or reads standard five-column FAI files and uses a read-only memory mapping to retrieve
bases without loading complete sequences into heap memory. Opening an existing index scales with
the number of FASTA records rather than the number of bases.

SeqPro 0.2.0 is the first public release candidate. The default build provides only the general
FASTA access library. An independent, opt-in `SequenceTextLayout` component can assemble selected
active sequence runs into suffix-array or FM-index input text. Applications that only need indexed
FASTA access do not build, install, or link that component.

## Supported environment

- x86_64, 64-bit Linux or WSL.
- GCC 9 or newer, or Clang 10 or newer with the system libstdc++ ABI.
- CMake 3.20 or newer.
- Uncompressed, structurally valid FASTA input.

Patch releases in the 0.2.x line are intended to remain source- and binary-compatible when built
for the same platform, compiler ABI, C++ runtime ABI, and build mode. Compatibility is not promised
between different minor versions, different C++ runtime ABIs, or different
`_GLIBCXX_USE_CXX11_ABI` configurations. Rebuild consumers when crossing those boundaries.

## Build and test

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON

cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

SeqPro does not overwrite a parent project's C++ standard, warning policy, build type, RPATH, or
installation prefix.

## Build and validate an index

```bash
build/seqpro-index build reference.fa
build/seqpro-index validate reference.fa --full
build/seqpro-index info reference.fa
```

The default files are:

```text
reference.fa
reference.fa.fai
reference.fa.fai.seqpro.meta
```

The `.fai` is always the standard Samtools/HTSlib-compatible five-column format. The optional
`.seqpro.meta` sidecar records a versioned source fingerprint, file metadata, record count, and
total base count without extending or changing the FAI.

`build` is an explicit write operation. `validate` and `IndexedFasta::Open()` are read-only. A
valid external FAI without a SeqPro sidecar can be opened directly. Running `build` later validates
and preserves that FAI before adding the sidecar.

## Link with CMake

From a source tree:

```cmake
add_subdirectory(path/to/seqpro)
target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

From an installation:

```cmake
find_package(SeqPro 0.2 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

The imported target declares the required `cxx_std_17` feature without setting the global
`CMAKE_CXX_STANDARD`.

## Random-access example

```cpp
#include <iostream>
#include <string>
#include <vector>

#include "seqpro/seqpro.h"

int main() {
  const seqpro::IndexedFasta reference =
      seqpro::IndexedFasta::Open("reference.fa");
  const seqpro::FastaSequenceView chromosome =
      reference.SequenceByName("chr1");

  const char sequence_base = chromosome.ReadBase(42);
  const std::string sequence_region =
      chromosome.ReadSubsequence(1'000, 500);

  std::vector<char> destination_buffer(1'024);
  chromosome.CopySubsequenceTo(
      1'000, destination_buffer.data(), destination_buffer.size());
  chromosome.WriteSubsequenceTo(1'000, 1'000'000, std::cout);

  std::cout << sequence_base << '\n' << sequence_region << '\n';
}
```

`SubsequenceChunks()` exposes physically contiguous `std::string_view` spans that point directly
into the mapping and exclude FASTA line endings:

```cpp
for (const seqpro::SequenceChunk sequence_chunk :
     chromosome.SubsequenceChunks(1'000, 500)) {
  Consume(sequence_chunk.sequence_start_position,
          sequence_chunk.sequence_bases);
}
```

The chunk range keeps the mapping alive. A detached `sequence_bases` view does not independently
extend that lifetime.

## Coordinate and error semantics

Sequence positions are zero-based and intervals are half-open:

```text
[sequence_start_position,
 sequence_start_position + subsequence_length)
```

- `ReadBase(position)` requires `position < sequence_length()`.
- A zero-length interval may start exactly at the sequence end.
- Requests are never silently truncated.
- Invalid IDs, missing names, malformed input, stale indexes, overflow, and out-of-range requests
  raise `seqpro::SeqProError` with a specific `ErrorCode`.
- Returned bytes preserve FASTA case and symbols; SeqPro does not normalize alphabets or replace
  characters with `N`.

## Optional SequenceText component

Enable the extension explicitly in a source-tree or FetchContent build:

```cmake
set(SEQPRO_BUILD_SEQUENCE_TEXT ON)
add_subdirectory(path/to/seqpro)
target_link_libraries(my_app PRIVATE SeqPro::sequence_text)
```

Request it explicitly from an installation:

```cmake
find_package(SeqPro 0.2 CONFIG REQUIRED COMPONENTS SequenceText)
target_link_libraries(my_app PRIVATE SeqPro::sequence_text)
```

A normal `find_package(SeqPro CONFIG REQUIRED)` defines only `SeqPro::seqpro`. If the installed
package was built without the optional component, requesting `SequenceText` fails during CMake
configuration.

Typical iterative use:

```cpp
#include "seqpro/sequence_text_layout.h"

seqpro::IndexedFasta indexed_fasta =
    seqpro::IndexedFasta::Open("reference.fa");
seqpro::SequenceTextLayout sequence_text_layout(indexed_fasta);

// Construction creates generation 1 with no excluded intervals.
seqpro::MaterializedSequenceText materialized_text =
    sequence_text_layout.Materialize();

sequence_text_layout.ExcludeTextIntervals(
    materialized_text.layout_generation,
    {{1'000, 250}, {5'000, 100}});
sequence_text_layout.Finalize();

seqpro::MaterializedSequenceText next_generation_text =
    sequence_text_layout.Materialize();
```

Every non-empty active run is followed by `0x01`; one `0x00` terminates the complete text.
`MaterializedSequenceText::sequence_text_bytes` therefore contains an embedded final NUL and must
always be consumed with `size()`, never `strlen()`. Tagged locations distinguish bases,
separators, and the terminator.

Mutation and `Finalize()` are single-threaded phases. After successful finalization, const
coordinate queries and text access on one layout are lock-free and may run concurrently. See the
[SequenceText contract](docs/sequence_text_layout.md) for the complete state, generation, and
coordinate rules.

## mmap, memory, and concurrency

Mapping a file does not read the complete file into RAM. The mapping first reserves virtual address
space; pages normally enter resident memory or the page cache when accessed. Consequently:

- VIRT may approach the FASTA file size and is not the heap footprint.
- Heap use after opening grows with record count and total sequence-name length, not total bases.
- Accessed pages appearing in RSS are normal operating-system cache behavior.
- A 64-bit address space is an explicit requirement.

`IndexedFasta`, `FastaSequenceView`, and finalized SequenceText queries are immutable and do not
use hidden worker threads or query locks. Sequence views and chunk ranges share ownership of the
mapping. Do not modify, replace, or truncate the FASTA, FAI, or metadata while any corresponding
reader or view remains alive.

On WSL, large Linux workloads are often faster on the WSL Linux filesystem than under `/mnt/c` or
`/mnt/d`; both layouts are supported.

## Optional project targets

All non-core targets are opt-in except the top-level command-line tool:

| CMake option | Default | Purpose |
|---|---:|---|
| `SEQPRO_BUILD_TOOLS` | top-level ON | Build `seqpro-index` |
| `SEQPRO_BUILD_SEQUENCE_TEXT` | OFF | Build the independent extension |
| `SEQPRO_BUILD_EXAMPLES` | OFF | Build examples |
| `SEQPRO_BUILD_BENCHMARKS` | OFF | Build microbenchmarks |
| `SEQPRO_BUILD_DOCUMENTATION` | OFF | Build strict Doxygen HTML |
| `SEQPRO_BUILD_FUZZERS` | OFF | Build non-installed Clang libFuzzer targets |
| `SEQPRO_WARNINGS_AS_ERRORS` | OFF | Promote SeqPro-owned warnings to errors |

The benchmark targets are never run by CTest and make no hardware-independent throughput promise.

## Documentation

- [Getting started](docs/getting_started.md): a complete 10-minute C++17 and CMake walkthrough
- [Core API guide](docs/core_api_guide.md): indexing, opening, lookup, five reading modes, errors,
  lifetimes, and concurrency
- [SequenceText API guide](docs/sequence_text_api_guide.md): optional text layout, exclusions,
  coordinate conversion, generations, and suffix-index iteration
- [FASTA, FAI, and metadata contract](docs/fai_contract.md)
- [SequenceText byte-layout and coordinate contract](docs/sequence_text_layout.md)
- Local Doxygen HTML: configure with `-DSEQPRO_BUILD_DOCUMENTATION=ON`, then open
  `build/api-docs/html/index.html`
- [Contributing and compatibility policy](CONTRIBUTING.md)
- [Security policy](SECURITY.md)
- [Release-candidate and release procedure](docs/releasing.md)
- [Changelog](CHANGELOG.md)

SeqPro is available under the MIT License. The bundled xxHash implementation retains its
BSD-2-Clause license; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
