# SequenceText API guide

<a href="zh-CN/sequence_text_api_guide.md">简体中文</a> · [Getting started](getting_started.md) ·
[SequenceText contract](sequence_text_layout.md)

`SequenceTextLayout` is an optional, separately linked component for suffix arrays, FM-indexes, and
iterative workflows that remove already-processed regions. Ordinary FASTA users need only
`SeqPro::seqpro` and can ignore this guide.

## Enable and link the component

Source tree or offline FetchContent:

```cmake
set(SEQPRO_BUILD_SEQUENCE_TEXT ON)
add_subdirectory(path/to/seqpro)
target_link_libraries(my_app PRIVATE SeqPro::sequence_text)
```

Installed package:

```cmake
find_package(SeqPro 0.2 CONFIG REQUIRED COMPONENTS SequenceText)
target_link_libraries(my_app PRIVATE SeqPro::sequence_text)
```

Include the extension explicitly:

```cpp
#include "seqpro/indexed_fasta.h"
#include "seqpro/sequence_text_layout.h"
```

The Core aggregate header `seqpro/seqpro.h` intentionally does not include SequenceText.

## Mental model

The component uses three zero-based coordinate systems:

| Type | Meaning |
|---|---|
| `SequencePosition` | Position in the original FASTA record |
| `ActiveSequencePosition` | Position after excluded bases are removed from one record |
| `SequenceTextPosition` | Position in the assembled text, including control bytes |

Original intervals use `[sequence_start_position, sequence_end_position)`. Text intervals use
`text_start_position` and `text_length`. Empty excluded/text intervals are invalid.

Each non-empty active run is followed by `kSeparatorByte` (`0x01`). One `kTerminatorByte` (`0x00`)
ends the complete text:

```text
run0 0x01 run1 0x01 ... runN 0x01 0x00
```

If `B` bases remain in `R` runs, `text_size() == B + R + 1`. A fully excluded sequence contributes
no empty run or separator. If everything is excluded, the text is one terminator byte.

Public interval and location fields:

| Type | Fields |
|---|---|
| `OriginalSequenceInterval` | `sequence_start_position`, `sequence_end_position` |
| `ExcludedSequenceInterval` | `sequence_id`, `sequence_start_position`, `sequence_end_position` |
| `SequenceTextInterval` | `text_start_position`, `text_length` |
| `SequenceTextBaseLocation` | `sequence_id`, `sequence_run_index`, `original_sequence_position`, `active_sequence_position` |
| `SequenceTextSeparatorLocation` | `preceding_sequence_id`, `preceding_run_index` |
| `SequenceTextTerminatorLocation` | No fields |
| `LocatedSequenceInterval` | `sequence_id`, `sequence_run_index`, `original_sequence_start_position`, `active_sequence_start_position`, `interval_length` |
| `MaterializedSequenceText` | `sequence_text_bytes`, `layout_generation` |

`SequenceTextLength` is a 64-bit text byte count, `SequenceTextGeneration` is a 64-bit layout
version, and `SequenceRunIndex` is a 32-bit run index within one sequence.

## Constructing a layout

```cpp
seqpro::SequenceTextLayout layout(indexed_fasta);
```

An empty `selected_sequence_order` selects every FAI record in FAI order. A non-empty vector selects
a unique subset and fixes its order:

```cpp
const auto chr1_id = *indexed_fasta.FindSequenceId("chr1");
const auto chr2_id = *indexed_fasta.FindSequenceId("chr2");

seqpro::SequenceTextLayout layout(
    indexed_fasta, {chr2_id, chr1_id});
```

Invalid or duplicate IDs throw. Unselected sequences cannot later receive excluded intervals.
Construction performs the initial no-exclusion finalize, so:

```cpp
layout.is_finalized() == true;
layout.layout_generation() == 1;
```

The class is move-only. `indexed_fasta()` returns the retained immutable reader;
`sequence_order()` returns selected IDs in layout order.

## State and generation

```text
constructed/finalized
        |
        | Exclude* or effective Clear*
        v
      dirty
        |
        | Finalize()
        v
finalized at the next generation
```

While dirty, only these operations are valid:

- `is_finalized()` and `layout_generation()`.
- Original-coordinate `ExcludeInterval()` and `ExcludeIntervals()`.
- `ClearExcludedIntervals()` and `ClearAllExcludedIntervals()`.
- `Finalize()`.

Coordinate queries, text reads, materialization, and text-coordinate exclusion throw
`ErrorCode::kInvalidArgument` until `Finalize()` succeeds. Clean `Finalize()` is a no-op. A dirty
finalize advances generation even if merging produces the same effective intervals. Old text
coordinates must never be reused with a new generation.

## Excluding original-coordinate intervals

### `ExcludeInterval()`

Two overloads select by ID or name:

```cpp
layout.ExcludeInterval(sequence_id, 1000, 1500);
layout.ExcludeInterval("chr2", 200, 900);
```

The interval is `[start, end)`, must be non-empty, and must lie within a selected sequence. The
method appends validation-safe input and marks the layout dirty. It does not automatically call
`Finalize()`.

### `ExcludeIntervals()`

```cpp
layout.ExcludeIntervals({
    {chr1_id, 1000, 1500},
    {chr1_id, 3000, 3500},
    {chr2_id, 200, 900},
});
layout.Finalize();
```

The complete batch is validated before mutation; one invalid interval rejects the entire call.
`Finalize()` sorts and merges overlapping, adjacent, nested, and duplicate intervals.

### Clearing exclusions

`ClearExcludedIntervals(sequence_id)` and `ClearExcludedIntervals(sequence_name)` remove every
exclusion for one selected record. `ClearAllExcludedIntervals()` removes all exclusions. Clearing an
already empty set is a no-op. There is no partial restore API: clear one sequence and re-add the
intervals that should remain.

## Excluding current text coordinates

`ExcludeTextIntervals()` converts hits from the current text generation back to original FASTA
intervals atomically:

```cpp
const seqpro::MaterializedSequenceText text = layout.Materialize();

std::vector<seqpro::SequenceTextInterval> hits = {
    {1000, 250},
    {5000, 100},
};

layout.ExcludeTextIntervals(text.layout_generation, hits);
layout.Finalize();
```

Requirements:

- The layout must currently be finalized.
- `source_generation` must equal `layout_generation()`.
- Every interval must be non-empty and fully inside one active run.
- A separator, terminator, cross-run, or out-of-range interval rejects the complete batch.

Submit all hits from one suffix-index generation in one call. After success the layout is dirty, so
old text coordinates cannot be added one by one.

## Finalized interval and count queries

| Method | Result |
|---|---|
| `text_size()` | Active bases + one separator per run + one terminator |
| `active_base_count()` | Total remaining FASTA symbols |
| `active_run_count()` | Number of non-empty active runs |
| `ActiveSequenceLength(id)` | Remaining compressed length for one sequence |
| `ExcludedBaseCount(id)` | Excluded original bases for one sequence |
| `ActiveIntervalsById(id)` | Active original-coordinate runs |
| `ExcludedIntervalsById(id)` | Sorted, merged excluded intervals |

IDs must be selected by this layout. Returned interval vectors are owned copies; modifying them does
not modify the layout.

## Original and active coordinate conversion

### `FindActiveSequencePosition()`

Converts an original position to the compressed active position. Returns `std::nullopt` when the
original base is excluded. Invalid sequence IDs or out-of-range original positions throw.

### `OriginalSequencePosition()`

Converts a valid active position back to the original FASTA position. An active position must be
strictly less than `ActiveSequenceLength(id)`.

```cpp
const auto active_position =
    layout.FindActiveSequencePosition(chr1_id, original_position);
if (active_position) {
  const seqpro::SequencePosition round_trip =
      layout.OriginalSequencePosition(chr1_id, *active_position);
}
```

For every non-excluded base, original → active → original is an identity.

## Conversion to sequence-text coordinates

### `FindTextPosition()`

Converts an original FASTA position to assembled-text position. Returns `std::nullopt` for an
excluded base.

### `TextPositionFromActive()`

Converts a valid active position to text position. It never returns a separator or terminator.

### `LocateTextPosition()`

Returns the tagged `SequenceTextLocation` variant:

- `SequenceTextBaseLocation`: sequence ID, run index, original position, and active position.
- `SequenceTextSeparatorLocation`: preceding sequence ID and run index.
- `SequenceTextTerminatorLocation`: no invented sequence ID.

```cpp
const seqpro::SequenceTextLocation location =
    layout.LocateTextPosition(text_position);

std::visit(
    [](const auto& tagged_location) {
      using Location = std::decay_t<decltype(tagged_location)>;
      if constexpr (std::is_same_v<Location,
                                   seqpro::SequenceTextBaseLocation>) {
        ConsumeBase(tagged_location.sequence_id,
                    tagged_location.original_sequence_position);
      } else if constexpr (std::is_same_v<
                               Location,
                               seqpro::SequenceTextSeparatorLocation>) {
        ConsumeSeparator(tagged_location.preceding_sequence_id,
                         tagged_location.preceding_run_index);
      } else {
        ConsumeTerminator();
      }
    },
    location);
```

### `LocateTextInterval()`

Returns `LocatedSequenceInterval` only when a non-empty text interval lies completely inside one
active run. The result contains sequence ID, run index, original start, active start, and length.
Starting on a control byte, crossing a separator/run, or exceeding text end returns `std::nullopt`;
a zero length is an invalid argument and throws.

## Reading and exporting text

### `ReadTextByte()`

Returns one active FASTA byte, separator, or terminator. Position must be less than `text_size()`.
Active FASTA bytes `0x00` and `0x01` are rejected as reserved.

### `Materialize()`

Allocates exactly `text_size()` bytes and returns `MaterializedSequenceText` containing
`sequence_text_bytes` and `layout_generation`. The string includes the final NUL. Always use
`sequence_text_bytes.size()`; never use `strlen()` or APIs that infer length from NUL.

### `CopyTextTo()`

Copies the complete text into a caller-owned buffer. `destination_size_bytes` must equal
`text_size()` exactly and a non-empty buffer must be non-null.

```cpp
std::vector<char> destination_buffer(
    static_cast<std::size_t>(layout.text_size()));
layout.CopyTextTo(destination_buffer.data(), destination_buffer.size());
```

### `WriteTo()`

Streams the complete text with bounded working memory. `transfer_buffer_size_bytes` must be
positive. Output failures throw `ErrorCode::kIoError`. The stream receives raw separator and
terminator bytes, not a printable FASTA representation.

## Complete iterative workflow

```cpp
seqpro::IndexedFasta indexed_fasta =
    seqpro::IndexedFasta::Open("reference.fa");
seqpro::SequenceTextLayout layout(indexed_fasta);

for (;;) {
  const seqpro::MaterializedSequenceText text = layout.Materialize();

  // The application builds a suffix array/FM-index and returns hits that
  // belong to text.layout_generation.
  const std::vector<seqpro::SequenceTextInterval> hits =
      FindNewMatches(text.sequence_text_bytes);
  if (hits.empty()) {
    break;
  }

  layout.ExcludeTextIntervals(text.layout_generation, hits);
  layout.Finalize();
}
```

SequenceText does not implement the suffix array, BWT, or FM-index itself.

## Concurrency

Use phase-based concurrency:

```text
single-thread mutation: Exclude* / Clear* / Finalize
multi-thread query:     finalized const operations
```

The class contains no mutex. Concurrent const queries on one finalized layout are safe and
lock-free. Concurrent mutation, mutation/query overlap, and two simultaneous mutators are not
supported. Each `Materialize()` owns its own output string; each caller-owned output buffer or
stream still requires normal external synchronization.

For exact overflow behavior, reserved-byte rules, dirty-state failures, and control-byte layout,
see the [SequenceText contract](sequence_text_layout.md).

Buildable C++17 examples:

- <a href="../extensions/sequence_text/examples/sequence_text_usage.cc">Complete exclusion and
  finalization workflow</a>
- <a href="../extensions/sequence_text/examples/sequence_text_coordinates.cc">Coordinate and
  tagged-location queries</a>
