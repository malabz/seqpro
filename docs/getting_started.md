# Getting started with SeqPro

<a href="zh-CN/getting_started.md">简体中文</a>

This guide takes a new C++17 user from a FASTA file to indexed base and interval queries. For
complete behavior, continue with the [Core API guide](core_api_guide.md). The optional
[SequenceText API guide](sequence_text_api_guide.md) is independent and is not needed for ordinary
FASTA access.

## Requirements

- x86_64, 64-bit Linux or WSL.
- GCC 9+, Clang 10+, and CMake 3.20+.
- An uncompressed, structurally valid FASTA file.
- C++17 or newer in the consuming target.

SeqPro preserves FASTA bytes exactly. It does not uppercase symbols, restrict the alphabet, or
replace symbols with `N`.

## 1. Link SeqPro with CMake

Choose one integration method.

### Source tree with `add_subdirectory`

```cmake
add_subdirectory(path/to/seqpro)
target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

### Offline `FetchContent`

```cmake
include(FetchContent)

FetchContent_Declare(
  SeqPro
  SOURCE_DIR /absolute/path/to/seqpro
)
FetchContent_MakeAvailable(SeqPro)

target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

### Installed package

Install SeqPro once:

```bash
cmake -S /path/to/seqpro -B /path/to/seqpro-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DCMAKE_INSTALL_PREFIX=/path/to/seqpro-install
cmake --build /path/to/seqpro-build --parallel
cmake --install /path/to/seqpro-build
```

Consume it:

```cmake
find_package(SeqPro 0.2 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

Configure the consumer with:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/seqpro-install
```

The exported target requests `cxx_std_17` without changing the parent project's global C++
standard.

## 2. Prepare a FASTA file

Create `reference.fa`:

```text
>chr1 example chromosome
ACGTACGTACGT
>plasmid
TTGCAACC
```

FASTA record names are the first whitespace-delimited token after `>`, so the indexed names are
`chr1` and `plasmid`.

## 3. Build and validate the index

With the command-line tool:

```bash
seqpro-index build reference.fa
seqpro-index validate reference.fa --full
seqpro-index info reference.fa
```

The default files are:

```text
reference.fa
reference.fa.fai
reference.fa.fai.seqpro.meta
```

The `.fai` is a standard Samtools-compatible five-column index. The SeqPro metadata sidecar adds
source fingerprints without changing the FAI.

You can instead build from C++ with `BuildFastaIndex()` or open and build in one explicit call with
`IndexedFasta::OpenOrBuildIndex()`.

## 4. Build a complete first program

Create `main.cc`:

```cpp
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "seqpro/seqpro.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 2) {
    std::cerr << "usage: fasta_reader FASTA\n";
    return 2;
  }

  try {
    const seqpro::IndexedFasta indexed_fasta =
        seqpro::IndexedFasta::OpenOrBuildIndex(
            std::filesystem::path(argument_values[1]));
    const seqpro::FastaSequenceView chromosome =
        indexed_fasta.SequenceByName("chr1");

    const char sequence_base = chromosome.ReadBase(4);
    const std::string sequence_region =
        chromosome.ReadSubsequence(2, 6);

    std::cout << chromosome.sequence_name() << '\t'
              << chromosome.sequence_length() << '\n'
              << "base[4]\t" << sequence_base << '\n'
              << "region[2,8)\t" << sequence_region << '\n';
    return 0;
  } catch (const seqpro::SeqProError& seqpro_error) {
    std::cerr << "SeqPro error "
              << static_cast<unsigned>(seqpro_error.error_code())
              << ": " << seqpro_error.what() << '\n';
    return 1;
  }
}
```

Create `CMakeLists.txt` for an installed SeqPro package:

```cmake
cmake_minimum_required(VERSION 3.20)
project(FastaReader LANGUAGES CXX)

find_package(SeqPro 0.2 CONFIG REQUIRED)

add_executable(fasta_reader main.cc)
target_link_libraries(fasta_reader PRIVATE SeqPro::seqpro)
target_compile_features(fasta_reader PRIVATE cxx_std_17)
```

Build and run:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/seqpro-install
cmake --build build --parallel
./build/fasta_reader reference.fa
```

Expected output:

```text
chr1    12
base[4] A
region[2,8)    GTACGT
```

## 5. Understand coordinates

All sequence positions are zero-based. Every interval is half-open:

```text
[sequence_start_position,
 sequence_start_position + subsequence_length)
```

`ReadSubsequence(2, 6)` therefore reads positions 2 through 7. SeqPro never silently truncates an
out-of-range request. A zero-length interval may start exactly at `sequence_length()`.

## 6. Choose a reading API

| Need | API | Result memory |
|---|---|---|
| One symbol | `ReadBase()` | Returns one byte |
| Convenient owned interval | `ReadSubsequence()` | Allocates one `std::string` |
| Reuse an existing buffer | `CopySubsequenceTo()` | Caller owns the buffer |
| Export a very large interval | `WriteSubsequenceTo()` | Bounded transfer buffer |
| Consume mmap spans directly | `SubsequenceChunks()` | Zero-copy `string_view` spans |

See [Choosing a reading API](core_api_guide.md#choosing-a-reading-api) for complete ownership,
lifetime, error, and complexity details.

## 7. Handle errors

SeqPro throws `SeqProError` for deterministic library failures. Use `error_code()` for program
logic and `what()` for human-readable context:

```cpp
try {
  const auto sequence = indexed_fasta.SequenceByName("missing");
  (void)sequence;
} catch (const seqpro::SeqProError& error) {
  if (error.error_code() == seqpro::ErrorCode::kSequenceNotFound) {
    // Report a missing record without treating the index as corrupt.
  }
}
```

`FindSequenceId()` is the non-throwing alternative when a missing name is expected.

## 8. mmap, lifetime, and threads

- Opening a mapping does not copy the complete FASTA into heap memory.
- Accessed file-backed pages may enter RSS; this is normal mmap/page-cache behavior.
- `IndexedFasta` and `FastaSequenceView` are immutable shared handles. Concurrent const queries are
  safe and use no hidden worker threads.
- A `FastaSequenceView` keeps the mapping alive after the originating `IndexedFasta` is destroyed.
- A `SequenceChunkRange` keeps the mapping alive while it exists. A detached `string_view` does not.
- Never replace, truncate, or modify the FASTA, FAI, or metadata while a corresponding reader or
  view is alive.

## Next steps

- [Core API guide](core_api_guide.md): every index, reader, view, chunk, and error API.
- [SequenceText API guide](sequence_text_api_guide.md): optional suffix-index text layouts.
- [FASTA, FAI, and metadata contract](fai_contract.md): authoritative file-format behavior.
- [SequenceText contract](sequence_text_layout.md): authoritative coordinate and state rules.
- <a href="../examples">Complete examples</a>: buildable Core examples.
