# Changelog

All notable public changes are documented here. SeqPro 0.2.0 is the first public release; the
earlier 0.1.0 name referred only to an internal development milestone and was never tagged.

## 0.2.0 - Unreleased

### Added

- Standard five-column FAI construction and validation for uncompressed FASTA.
- Versioned SeqPro metadata schema 1 with FASTA and FAI XXH3-128 fingerprints.
- Immutable mmap-backed `IndexedFasta` and `FastaSequenceView` random access.
- Strict single-base and half-open interval queries, caller-buffer copies, streaming output, and
  zero-copy `SequenceChunkRange` access.
- The `seqpro-index build|validate|info` command-line tool.
- Build-tree, FetchContent, and relocatable install-tree CMake packages as `SeqPro::seqpro`.
- Optional, separately built and installed `SeqPro::sequence_text` component.
- Move-only `SequenceTextLayout` with excluded intervals, active runs, original/active/text
  coordinate conversion, tagged base/separator/terminator locations, generation-bound text
  exclusion, byte lookup, exact-buffer copy, streaming output, and explicit materialization.
- Strict English API documentation plus synchronized Chinese user and contract documentation.
- Task-oriented English and Chinese getting-started, Core API, and SequenceText API guides, with
  buildable C++17 examples, smoke tests, relative-link validation, and public-symbol coverage gates.
- Local build matrices, sanitizers, static analysis, Samtools interoperability, manual fuzz
  targets, ABI checks, shared-library symbol audits, and reproducible source release-candidate
  tooling.

### Changed

- Froze public coordinate fields with explicit `_position`, `_length`, `_count`,
  `_size_bytes`, and `_offset_bytes` naming before the first public tag.
- Renamed `SequenceChunk::sequence_start` to `sequence_start_position` and
  `SequenceChunk::bases` to `sequence_bases`.
- Renamed original/text interval boundaries to `sequence_start_position`,
  `sequence_end_position`, and `text_start_position`.
- Renamed located interval starts to `original_sequence_start_position` and
  `active_sequence_start_position`.
- Renamed `MaterializedSequenceText::bytes` to `sequence_text_bytes`.
- Established 0.2.x patch compatibility with SONAME `0.2` and CMake
  `SameMinorVersion` package selection.
- Generated the public version header from the root CMake project version.

### Fixed

- Corrected active-run capacity estimation in `SequenceTextLayout::Finalize()` and added checked
  container-size arithmetic.
- Ensured failed finalization remains dirty and cannot publish partially rebuilt layout state.
- Anchored `SeqProError` RTTI and vtable in the core shared library with an out-of-line virtual
  destructor.
- Restricted core and SequenceText dynamic exports to their intended public boundaries.
- Made the installed SequenceText shared library resolve its co-located core dependency through
  `$ORIGIN`, including after an installation prefix is moved.

### Compatibility notes

- Standard FAI and metadata schema 1 are unchanged by the optional SequenceText component.
- The first supported platform is x86_64, 64-bit Linux/WSL with GCC 9+, Clang 10+, and CMake 3.20+.
- Binary compatibility is not promised across different minor versions, compiler runtime ABIs,
  libc++/libstdc++ boundaries, or `_GLIBCXX_USE_CXX11_ABI` configurations.
- No compatibility aliases are provided for names used only on the untagged development branch.
