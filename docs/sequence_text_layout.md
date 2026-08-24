# SequenceTextLayout contract

This document defines the byte layout, coordinate systems, state machine, error semantics, and
concurrency boundary of the optional SeqPro 0.2.x `SequenceText` component. The core
[FASTA/FAI contract](fai_contract.md) remains independent.

## Component boundary

`SequenceTextLayout` uses only the public `IndexedFasta` API. It does not parse interval files,
persist excluded intervals, implement a suffix array, BWT, or FM-index, or add state to the FAI.

The component is not built by default:

```cmake
set(SEQPRO_BUILD_SEQUENCE_TEXT ON)
add_subdirectory(path/to/seqpro)
target_link_libraries(app PRIVATE SeqPro::sequence_text)
```

An installed component must be requested explicitly:

```cmake
find_package(SeqPro 0.2 CONFIG REQUIRED COMPONENTS SequenceText)
target_link_libraries(app PRIVATE SeqPro::sequence_text)
```

The core aggregation header `seqpro/seqpro.h` does not include
`seqpro/sequence_text_layout.h`.

## Coordinate systems

- `SequencePosition`: zero-based position in the original FASTA sequence.
- `ActiveSequencePosition`: zero-based position in that sequence after excluded intervals are
  removed; control bytes are not counted.
- `SequenceTextPosition`: zero-based position in the assembled text; separators and the terminator
  are counted.

Original intervals are half-open:

```text
[sequence_start_position, sequence_end_position)
```

They require:

```text
sequence_start_position < sequence_end_position <= sequence_length
```

Text intervals are represented by `text_start_position` and `text_length`, require
`text_length > 0`, and must not overflow.

## Byte layout

Every non-empty active run is followed by one separator byte `0x01`. A unique terminator byte
`0x00` follows all runs:

```text
run0 0x01 run1 0x01 ... runR-1 0x01 0x00
```

For `B` active sequence bytes and `R` active runs:

```text
text_size = B + R + 1
```

The last run still has a separator. A fully excluded sequence emits neither an empty run nor a
separator. If every selected sequence is fully excluded, the complete text is the single
terminator byte.

For example, excluding `[2, 4)` from `ABCDEFG` produces active runs `AB` and `EFG`:

```text
AB 0x01 EFG 0x01 0x00
```

The separator prevents a suffix match from crossing the excluded `CD` interval.

## Tagged text locations

`LocateTextPosition()` returns one alternative of `SequenceTextLocation`:

- `SequenceTextBaseLocation` provides the sequence ID, active-run index, original sequence
  position, and compressed active position;
- `SequenceTextSeparatorLocation` identifies the sequence and run immediately preceding the
  separator;
- `SequenceTextTerminatorLocation` has no fabricated sequence ID.

A separator is never mapped to the following run. The terminator is never mapped to a real base.

`LocateTextInterval()` returns `LocatedSequenceInterval` only when a non-empty interval lies
entirely inside one active run. It returns `std::nullopt` when the interval begins on a control
byte, crosses a separator, crosses two active runs, or exceeds the text. A zero-length interval is
an invalid argument and raises `SeqProError`.

The final field names are:

- `LocatedSequenceInterval::original_sequence_start_position`;
- `LocatedSequenceInterval::active_sequence_start_position`;
- `LocatedSequenceInterval::interval_length`.

## Selection and ordering

An empty `selected_sequence_order` constructor argument selects every FAI entry in FAI order. A
non-empty vector selects only those IDs and fixes their layout order. Every ID must exist and must
occur exactly once.

Unselected sequences do not emit text and cannot receive excluded intervals. Construction performs
the initial no-exclusion finalization, so `layout_generation() == 1` immediately after successful
construction.

## Mutation and Finalize

`ExcludeInterval()` and `ExcludeIntervals()` accept original FASTA coordinates. Batch calls
validate every interval before modifying logical state; one invalid item rejects the whole batch.

`ExcludeTextIntervals()` converts results from one current suffix-index generation:

1. the layout must be finalized;
2. `source_generation` must equal `layout_generation()`;
3. every text interval must lie fully inside one active run;
4. all text intervals are converted to original coordinates before any are appended;
5. success marks the layout dirty.

Pass all matches from one text generation in one batch. Once the layout is dirty, old text
coordinates cannot be appended one at a time.

`Finalize()`:

1. validates the pending intervals;
2. sorts them by start and end position;
3. merges overlapping and adjacent intervals;
4. computes non-empty complement runs;
5. computes per-sequence active prefixes;
6. computes global text starts and separator positions;
7. checks every count, position, prefix, generation, and container-size conversion;
8. publishes the fully built temporary state in one swap.

A failed finalization leaves the object dirty and never exposes a partially rebuilt index. A
successful dirty finalization advances the generation, even when the new interval ultimately merges
into an existing exclusion. Calling `Finalize()` on an already clean layout is an idempotent no-op
and does not advance the generation.

`ClearExcludedIntervals()` clears one selected sequence; `ClearAllExcludedIntervals()` clears all
selected sequences. Clearing an already empty set is a no-op. SeqPro 0.2.x intentionally does not
provide partial restoration: clear one sequence and append the exclusions that should remain.

## Dirty-state rules

While dirty, callers may use:

- `is_finalized()` and `layout_generation()`;
- original-coordinate `ExcludeInterval()` and `ExcludeIntervals()`;
- `ClearExcludedIntervals()` and `ClearAllExcludedIntervals()`;
- `Finalize()`;
- immutable identity accessors `indexed_fasta()` and `sequence_order()`.

Coordinate lookup, interval lookup, text reads, materialization, copy, and streaming output raise:

```text
SeqProError(ErrorCode::kInvalidArgument, ...)
```

The message states that `Finalize()` is required. The library does not finalize implicitly.

## Text access

- `ReadTextByte()` returns one active FASTA byte, separator, or terminator.
- `Materialize()` allocates exactly `text_size()` bytes and does not retain a hidden text cache.
- `CopyTextTo()` requires a non-null destination buffer whose size exactly equals `text_size()`.
- `WriteTo()` streams through bounded working memory.

`MaterializedSequenceText::sequence_text_bytes` contains the unique final NUL. Consumers must use
`sequence_text_bytes.size()`; C string functions such as `strlen()` are invalid for this data.
`layout_generation` binds suffix-array or FM-index coordinates to the layout that created them.

The FASTA core already rejects NUL, but the component also checks active output bytes for both
reserved values:

- active `0x00` raises `kUnsupportedFileFormat`;
- active `0x01` raises `kUnsupportedFileFormat`.

The component does not escape or replace either value. Finalization does not scan sequence bodies,
so the check occurs in byte access, copy, materialization, and streaming paths. Reserved bytes in
fully excluded regions are not read.

## Lifetime and concurrency

`SequenceTextLayout` retains an `IndexedFasta` value and therefore shares ownership of the
read-only mapping. It is move-only and does not implicitly copy interval metadata.

Concurrency is phase-based:

```text
mutation phase:
  one thread calls Exclude*, Clear*, and Finalize

query phase:
  after successful Finalize, multiple threads call const methods
```

The class contains no mutex, hidden thread pool, or mutable materialized-text cache. Finalized const
queries, coordinate conversions, byte reads, materializations, and independent stream writes may
run concurrently. Concurrent mutation, two simultaneous mutation threads, or overlapping mutation
and query operations are outside the thread-safety contract.

## Complexity

Let:

- `S` be the number of selected sequences;
- `M` be the number of excluded intervals;
- `R` be the number of active runs.

Then:

- append one exclusion: amortized O(1);
- append a batch of `k` exclusions: O(k), plus bounded reservation;
- finalization: O(M log M + S + R);
- original/active conversion: O(log runs in that sequence);
- text-position and text-interval lookup: O(log R);
- materialization or output: O(active base count + R);
- metadata memory: O(S + M + R), independent of total FASTA base count.

Default FAI order uses contiguous sequence IDs directly. Explicit subsets or reordered IDs use a
hash table containing only selected sequences. Bulk reserved-byte detection uses `memchr` while
preserving the exact original coordinate of the first invalid byte.

## Failure behavior

SeqPro rejects invalid or duplicate sequence IDs, unselected sequences, empty or out-of-range
intervals, stale generations, dirty queries, arithmetic overflow, incorrect destination sizes,
reserved active bytes, and failed output streams. It does not truncate intervals, skip invalid
sequences, reinterpret control bytes, auto-read interval formats, persist exclusions, or swallow
errors.
