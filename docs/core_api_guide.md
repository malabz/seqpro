# SeqPro Core API guide

<a href="zh-CN/core_api_guide.md">简体中文</a> · [Getting started](getting_started.md) ·
[FASTA/FAI contract](fai_contract.md)

The Core API builds and validates standard FAI indexes, opens immutable mmap-backed FASTA readers,
and exposes five reading modes. All sequence coordinates are zero-based and intervals are
half-open. Functions throw `SeqProError` unless this guide explicitly describes an `optional`
result.

## Coordinate and identifier types

| Type | Representation | Meaning |
|---|---:|---|
| `SequencePosition` | `uint64_t` | Zero-based position inside one FASTA record |
| `SequenceLength` | `uint64_t` | Number of sequence symbols |
| `SequenceId` | `uint32_t` | Contiguous identifier assigned in FAI order |

`FastaIndexEntry` represents one five-column FAI row plus its `sequence_id`:

| Field | Unit and meaning |
|---|---|
| `sequence_id` | Zero-based FAI record order |
| `sequence_name` | First whitespace-delimited FASTA header token |
| `sequence_length` | Sequence symbols |
| `first_base_offset_bytes` | Byte offset in the FASTA file |
| `bases_per_line` | Symbols in each complete sequence line |
| `bytes_per_line` | Bytes in each complete line, including LF or CRLF |

Applications normally read these fields but do not construct physical offsets themselves.

## Building and validating an index

### `BuildFastaIndex()`

```cpp
seqpro::FastaIndexBuildReport BuildFastaIndex(
    const std::filesystem::path& fasta_path,
    const seqpro::FastaIndexBuildOptions& build_options = {});
```

- **Use when:** an application explicitly permits creating or updating index files.
- **Input:** an uncompressed FASTA and optional destination/options.
- **Returns:** paths, sequence/base counts, and the performed `FastaIndexBuildAction`.
- **Writes:** a standard FAI and, by default, `<fai>.seqpro.meta`.
- **Throws:** I/O, malformed FASTA/FAI, stale index, duplicate-name, unsupported-format, and overflow
  errors.
- **Complexity:** one sequential FASTA scan when creating/rebuilding; a current index is reused.
- **Threading:** serialize simultaneous builders targeting the same files.

```cpp
seqpro::FastaIndexBuildOptions build_options;
build_options.fasta_index_path = "indexes/reference.fai";
build_options.force_rebuild = false;
build_options.write_seqpro_metadata = true;

const seqpro::FastaIndexBuildReport build_report =
    seqpro::BuildFastaIndex("reference.fa", build_options);
```

`FastaIndexBuildAction` describes the actual operation:

| Value | Meaning |
|---|---|
| `kCreated` | No index existed; SeqPro created one |
| `kReused` | FAI and metadata were already current |
| `kAdoptedExternalIndex` | A valid external FAI was preserved and given metadata |
| `kRebuilt` | `force_rebuild` allowed replacement |

`force_rebuild=false` is the safe default: malformed or stale files are reported, not silently
overwritten. `write_seqpro_metadata=false` suppresses only the SeqPro sidecar; the FAI remains
standard.

`FastaIndexBuildReport` fields:

| Field | Meaning |
|---|---|
| `build_action` | One `FastaIndexBuildAction` value |
| `fasta_path` | Source path supplied by the caller |
| `fasta_index_path` | Published standard FAI path |
| `metadata_path` | Sidecar path, or empty when metadata is disabled |
| `sequence_count` | Number of indexed records |
| `total_base_count` | Sum of all sequence lengths |

### `ValidateFastaIndex()`

```cpp
seqpro::FastaIndexValidationReport ValidateFastaIndex(
    const std::filesystem::path& fasta_path,
    const std::filesystem::path& fasta_index_path = {},
    seqpro::IndexVerificationMode verification_mode =
        seqpro::IndexVerificationMode::kFast);
```

- **Use when:** validation must be read-only.
- **Returns:** provenance, strongest completed verification, counts, and fingerprint state.
- **Writes:** nothing; this function never repairs or adopts an index.
- **Complexity:** `kFast` scales with record count; `kFull` scans and hashes the complete FASTA.

```cpp
const seqpro::FastaIndexValidationReport validation_report =
    seqpro::ValidateFastaIndex(
        "reference.fa", {}, seqpro::IndexVerificationMode::kFull);
```

`IndexVerificationMode`:

- `kFast`: validates metadata, FAI structure, counts, hashes already present in metadata, and
  selected physical offsets without hashing the complete FASTA.
- `kFull`: additionally scans the complete FASTA and recomputes its XXH3-128 fingerprint.

`FastaIndexOrigin`:

- `kSeqProVerified`: a matching SeqPro metadata sidecar was validated.
- `kExternalStandardFai`: a structurally valid standard FAI has no SeqPro metadata.

`IndexVerificationStatus` is the strongest completed level:

- `kStructureValidated`.
- `kMetadataValidated`.
- `kFullContentValidated`.

`FastaIndexValidationReport::has_seqpro_metadata` reports sidecar presence.
`is_fasta_fingerprint_current` is true only when the operation actually established a current
content fingerprint; see the [verification contract](fai_contract.md#validation-levels).

The other report fields are `index_origin`, `verification_status`, `sequence_count`, and
`total_base_count`; they correspond directly to the enums and counts described above.

## Opening an indexed FASTA

### `IndexedFasta::Open()`

```cpp
static seqpro::IndexedFasta Open(
    const std::filesystem::path& fasta_path,
    const seqpro::IndexedFastaOptions& open_options = {});
```

- **Use when:** an index must already exist and opening must be read-only.
- **Returns:** a copyable immutable handle sharing the mmap and index metadata.
- **Throws:** missing/stale/malformed index, unsupported input, I/O, or overflow errors.
- **Complexity:** O(number of records), not O(number of bases), for an existing index.
- **Threading:** after opening, all const operations are safe to call concurrently.

### `IndexedFasta::OpenOrBuildIndex()`

```cpp
static seqpro::IndexedFasta OpenOrBuildIndex(
    const std::filesystem::path& fasta_path,
    const seqpro::FastaIndexBuildOptions& build_options = {},
    const seqpro::IndexedFastaOptions& open_options = {});
```

This is the explicit convenience workflow `BuildFastaIndex()` followed by `Open()`. It may write
index files. Libraries that must remain read-only should expose `Open()` instead.

### `IndexedFastaOptions`

| Field | Default | Use |
|---|---|---|
| `fasta_index_path` | `<fasta>.fai` | Select a custom FAI path |
| `file_access_pattern` | `kOperatingSystemDefault` | mmap page-access advice |
| `index_verification_mode` | `kFast` | Fast or full validation while opening |
| `require_seqpro_metadata` | `false` | Reject a valid external FAI without metadata |

`FileAccessPattern::kRandom` is suitable for sparse random lookups;
`FileAccessPattern::kSequential` is suitable for bulk exports. These values are operating-system
advice, not correctness or caching guarantees.

```cpp
seqpro::IndexedFastaOptions open_options;
open_options.fasta_index_path = "indexes/reference.fai";
open_options.file_access_pattern = seqpro::FileAccessPattern::kRandom;
open_options.index_verification_mode = seqpro::IndexVerificationMode::kFast;
open_options.require_seqpro_metadata = true;

const seqpro::IndexedFasta indexed_fasta =
    seqpro::IndexedFasta::Open("reference.fa", open_options);
```

## Inspecting an `IndexedFasta`

These accessors allocate no sequence data:

| Method | Return and lifetime |
|---|---|
| `fasta_path()` | Reference to the opened FASTA path; valid while the handle state lives |
| `fasta_index_path()` | Reference to the selected FAI path |
| `fasta_index_origin()` | `FastaIndexOrigin` |
| `index_verification_status()` | Strongest completed validation |
| `sequence_count()` | Number of FAI records |
| `fasta_index_entries()` | Reference to immutable entries in FAI order |

```cpp
for (const seqpro::FastaIndexEntry& fasta_index_entry :
     indexed_fasta.fasta_index_entries()) {
  std::cout << fasta_index_entry.sequence_id << '\t'
            << fasta_index_entry.sequence_name << '\t'
            << fasta_index_entry.sequence_length << '\n';
}
```

Do not retain references returned by these accessors after every corresponding `IndexedFasta`
handle has been destroyed.

## Finding records and creating views

### `FindSequenceId()`

```cpp
std::optional<seqpro::SequenceId> FindSequenceId(
    std::string_view sequence_name) const noexcept;
```

- Returns `std::nullopt` when the name is absent.
- Does not allocate a temporary name and does not throw.
- Average lookup complexity is O(1).

Use it when an absent sequence is expected control flow.

### `IndexEntryById()` and `IndexEntryByName()`

Both return an immutable `FastaIndexEntry&`. They throw
`ErrorCode::kSequenceNotFound` for an invalid ID or missing name. ID lookup is O(1); name lookup is
average O(1).

### `SequenceById()` and `SequenceByName()`

Both return an owning `FastaSequenceView` that shares the mapping lifetime. Name lookup is average
O(1); ID lookup is O(1). For repeated queries, resolve the name once and reuse the view:

```cpp
const seqpro::FastaSequenceView chromosome =
    indexed_fasta.SequenceByName("chr1");

for (seqpro::SequencePosition position : query_positions) {
  Consume(chromosome.ReadBase(position));
}
```

## `FastaSequenceView` metadata

| Method | Meaning |
|---|---|
| `sequence_id()` | Contiguous FAI-order ID |
| `sequence_name()` | Non-owning name view backed by immutable reader state |
| `sequence_length()` | Number of sequence symbols |
| `fasta_index_entry()` | Immutable FAI entry reference |

The view remains valid after its original `IndexedFasta` variable is destroyed because it shares
ownership of the mapping state.

## Choosing a reading API

| Method | Allocates result? | Best use | Complexity for length k |
|---|---:|---|---|
| `ReadBase()` | No | One random symbol | O(1) |
| `ReadSubsequence()` | One string | Convenient owned interval | O(k + crossed lines) |
| `CopySubsequenceTo()` | No | Reusable caller buffer | O(k + crossed lines) |
| `WriteSubsequenceTo()` | Bounded buffer | Huge interval or stream export | O(k + crossed lines) |
| `SubsequenceChunks()` | No sequence copy | Direct mmap span processing | O(crossed lines) iteration |

All methods preserve original FASTA bytes and reject out-of-range requests without truncation.

### `ReadBase()`

```cpp
char ReadBase(seqpro::SequencePosition sequence_position) const;
```

Requires `sequence_position < sequence_length()`. Returns one original FASTA byte. Throws
`kSequenceRangeOutOfBounds` for an invalid coordinate and checked arithmetic errors if a physical
offset cannot be represented. Concurrent calls are safe.

### `ReadSubsequence()`

```cpp
std::string ReadSubsequence(
    seqpro::SequencePosition sequence_start_position,
    seqpro::SequenceLength subsequence_length) const;
```

Allocates exactly the requested logical sequence length and removes physical FASTA line endings.
An empty interval is legal when its start is at most `sequence_length()`.

### `CopySubsequenceTo()`

```cpp
void CopySubsequenceTo(
    seqpro::SequencePosition sequence_start_position,
    char* destination_buffer,
    std::size_t destination_size_bytes) const;
```

The destination size is also the requested sequence length. A non-empty destination must be
non-null. Each thread must provide its own writable buffer.

```cpp
std::vector<char> destination_buffer(4096);
chromosome.CopySubsequenceTo(
    1000, destination_buffer.data(), destination_buffer.size());
```

### `WriteSubsequenceTo()`

```cpp
void WriteSubsequenceTo(
    seqpro::SequencePosition sequence_start_position,
    seqpro::SequenceLength subsequence_length,
    std::ostream& output_stream,
    std::size_t transfer_buffer_size_bytes = 1024 * 1024) const;
```

Writes only sequence symbols, without FASTA headers or line endings. A zero transfer-buffer size is
invalid. Stream failures produce `ErrorCode::kIoError`. Use this method when returning a huge
`std::string` would be undesirable.

### `SubsequenceChunks()`

```cpp
seqpro::SequenceChunkRange SubsequenceChunks(
    seqpro::SequencePosition sequence_start_position,
    seqpro::SequenceLength subsequence_length) const;
```

Each `SequenceChunk` contains:

- `sequence_start_position`: logical position of the first byte in this chunk.
- `sequence_bases`: a `string_view` pointing directly into one physical mmap span; no line ending
  is included.

```cpp
const seqpro::SequenceChunkRange chunks =
    chromosome.SubsequenceChunks(1000, 5000);

if (!chunks.empty()) {
  ReserveForApproximately(chunks.estimated_chunk_count());
}

for (const seqpro::SequenceChunk sequence_chunk : chunks) {
  Consume(sequence_chunk.sequence_start_position,
          sequence_chunk.sequence_bases);
}
```

`begin()` and `end()` provide a forward iterator. `estimated_chunk_count()` is suitable for
capacity planning but callers should not treat it as a validation result. The range keeps the mmap
alive; copying `sequence_bases` elsewhere does not extend that lifetime independently.
Range-for uses `SequenceChunkRange::Iterator::operator*`, prefix/postfix `operator++`, and
`operator==`/`operator!=`; callers normally do not invoke those protocol methods directly.

## Errors

`SeqProError` derives from `std::runtime_error`. `what()` contains operation, path, record, line, or
coordinate context when available. `error_code()` returns a stable `ErrorCode`:

| ErrorCode | Typical caller response |
|---|---|
| `kInvalidArgument` | Fix contradictory options, null buffers, or invalid state |
| `kIoError` | Report path/stream failure and preserve the original exception message |
| `kInvalidFasta` | Repair or replace malformed FASTA input |
| `kInvalidFastaIndex` | Rebuild a malformed or physically inconsistent FAI |
| `kStaleFastaIndex` | Rebuild or fully validate against the current FASTA |
| `kDuplicateSequenceName` | Rename duplicate FASTA records |
| `kSequenceNotFound` | Handle missing name/ID or use `FindSequenceId()` |
| `kSequenceRangeOutOfBounds` | Correct sequence, active, or text coordinates |
| `kIntegerOverflow` | Reject an unrepresentable file, interval, or buffer size |
| `kUnsupportedFileFormat` | Decompress gzip/BGZF or reject reserved/unsupported bytes |

Catch `SeqProError` before a broader `std::exception` handler. Do not parse `what()` to make program
decisions.

## Lifetime and concurrency

- `IndexedFasta` is a copyable shared immutable handle.
- `FastaSequenceView` and `SequenceChunkRange` also share mapping ownership.
- Const access is lock-free and safe across threads.
- Query paths create no hidden threads and no whole-sequence cache.
- Writable buffers and output streams remain caller-owned and require external synchronization.
- Destroy every reader/view/range before replacing, truncating, or modifying FASTA, FAI, or
  metadata files.

For authoritative FASTA syntax, metadata, stale-index, and physical-offset rules, see the
[FASTA, FAI, and metadata contract](fai_contract.md).

Buildable C++17 examples:

- <a href="../examples/index_management.cc">Index management</a>
- <a href="../examples/sequence_read_modes.cc">Five reading modes</a>
- <a href="../examples/concurrent_reading.cc">Concurrent shared reading</a>
