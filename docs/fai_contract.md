# FASTA, FAI, and metadata contract

This document defines the on-disk and coordinate contract implemented by the SeqPro 0.2.x core
library. The standard FAI remains externally interoperable; all SeqPro-specific state is stored in
a separate metadata sidecar.

## Standard five-column FAI

Each index record contains exactly five TAB-separated fields:

```text
NAME    LENGTH    OFFSET    LINEBASES    LINEWIDTH
```

The fields follow the Samtools/HTSlib faidx meaning:

- `NAME`: the first non-empty ASCII-whitespace-delimited token after `>`.
- `LENGTH`: total number of sequence symbols.
- `OFFSET`: zero-based byte offset of the first sequence symbol in the FASTA.
- `LINEBASES`: number of sequence symbols in a complete physical sequence line.
- `LINEWIDTH`: number of bytes in a complete physical line, including LF or CRLF.

All numeric fields are unsigned decimal 64-bit integers. Parsing rejects signs, empty values,
trailing characters, overflow, fewer or more than five fields, comments, status rows, and extension
columns.

For HTSlib compatibility, when the final record has exactly one sequence line and the file has no
final line ending, `LINEWIDTH` is serialized as `LINEBASES + 1`. The virtual line-ending byte is
never accessed because that record has no second physical line.

The historical RaMAx private format beginning with `YES` or `NO` and followed by six-column rows
is not a FAI. SeqPro detects and rejects it with guidance to rebuild from the original FASTA.

## FASTA input rules

SeqPro 0.2.x supports uncompressed FASTA with:

- LF or CRLF line endings;
- different legal line widths between records;
- a final sequence line shorter than earlier complete lines;
- a file with or without a final line ending;
- arbitrary non-whitespace sequence bytes other than NUL;
- original case, IUPAC symbols, protein symbols, and punctuation preserved byte-for-byte.

Within one record, every non-final sequence line must have the same number of symbols and the same
line-ending width. Different records may use different widths and line-ending styles.

Index construction rejects:

- an empty file or a directory path;
- gzip or BGZF magic bytes;
- sequence content before the first header;
- an empty name token;
- duplicate sequence names;
- an empty sequence;
- blank lines inside a sequence;
- ASCII whitespace or NUL inside a sequence line;
- additional sequence data after a short final line;
- mixed LF and CRLF line endings inside one sequence;
- malformed headers, widths, offsets, or integer arithmetic overflow.

SeqPro intentionally does not validate against a DNA or protein alphabet and does not uppercase,
clean, escape, or replace sequence bytes.

## SeqPro metadata schema 1

The default metadata path is `<fai-path>.seqpro.meta`. Its strict TAB-separated text schema is:

```text
SEQPRO_META\t1
fasta_size_bytes\t<UINT64>
fasta_mtime_ns\t<UINT64>
fasta_xxh3_128\t<32 lowercase hexadecimal digits>
fai_xxh3_128\t<32 lowercase hexadecimal digits>
record_count\t<UINT64>
total_bases\t<UINT64>
```

The sidecar does not contain an absolute path, so the FASTA, FAI, and metadata can be moved
together. Unknown schema versions, missing fields, duplicate or extra lines, malformed numbers,
non-canonical hashes, and mismatched hashes are rejected.

A standard FAI without a sidecar remains valid as an external index. Set
`IndexedFastaOptions::require_seqpro_metadata` to reject external indexes in pipelines that require
SeqPro provenance.

Schema 1 is unchanged by the optional SequenceText component. Excluded intervals, active runs, and
sequence-text coordinates are never written to the FAI or metadata sidecar.

## Index construction and publication

`BuildFastaIndex()` performs one sequential FASTA scan without materializing complete sequences.
It computes the FASTA XXH3-128 fingerprint during that scan. Before publishing, it compares file
identity, size, and modification time from `fstat` calls on the same open descriptor before and
after scanning. A source that changes during construction is rejected.

The FAI and metadata are serialized to unique temporary files in the destination directory,
flushed, synchronized, closed, reparsed, and then atomically renamed. The standard FAI is published
first. The metadata is published second and is cryptographically bound to the final FAI bytes. If a
process stops between those operations, the remaining standard FAI can still be validated as an
external index.

Existing behavior is explicit:

- a matching FAI and sidecar are reused;
- a valid external FAI is preserved and may be adopted by adding metadata;
- a malformed or stale index is rejected by default;
- replacement requires `FastaIndexBuildOptions::force_rebuild = true`.

`ValidateFastaIndex()` never creates, repairs, or replaces files.

## Verification modes

`IndexVerificationMode::kFast` validates:

- strict FAI syntax and unique names;
- record count and aggregate base count;
- sequence/header order and selected physical offsets;
- physical line and record boundaries;
- metadata size, modification time, and FAI fingerprint when metadata exists.

It does not hash every FASTA byte.

`IndexVerificationMode::kFull` additionally scans and validates the complete FASTA and recomputes
its XXH3-128 fingerprint. This mode can confirm content identity after a copy changes only the
modification time. Validation is read-only, so it does not rewrite the metadata timestamp;
subsequent fast verification will continue to report that stale timestamp until the index is
explicitly rebuilt or adopted.

`FastaIndexValidationReport::is_fasta_fingerprint_current` is true only when the current operation
actually recomputed and matched the complete FASTA fingerprint. Fast verification does not claim a
full-content hash.

## Query coordinates

All sequence coordinates are zero-based. Intervals are half-open:

```text
[sequence_start_position,
 sequence_start_position + subsequence_length)
```

For a non-empty interval:

```text
sequence_start_position < sequence_length
subsequence_length <= sequence_length - sequence_start_position
```

For an empty interval, `sequence_start_position <= sequence_length`, including an empty interval at
the end. Validation uses subtraction before addition to avoid unsigned wraparound. Out-of-range
requests throw `SeqProError`; SeqPro never silently truncates them.

The FAI byte offset for one valid base is:

```text
physical_line_index =
    sequence_position / bases_per_line

base_offset_within_line =
    sequence_position % bases_per_line

base_file_offset =
    first_base_offset_bytes
    + physical_line_index * bytes_per_line
    + base_offset_within_line
```

Every multiplication and addition is checked for 64-bit overflow and against the mapped file size.

## Mapping and file lifetime

The reader uses `mmap(PROT_READ, MAP_PRIVATE)`; successful mapping does not copy the complete file
into heap memory. The mapping and parsed index are immutable and may be read concurrently without
internal query locks.

The FASTA, FAI, and metadata must not be modified, replaced, or truncated while any corresponding
`IndexedFasta`, `FastaSequenceView`, or `SequenceChunkRange` remains alive. Destroy all readers
before replacing those files, then explicitly reopen the new generation.
