# Changelog

## 0.2.0 - unreleased

- Added the optional, separately built `SeqPro::sequence_text` component.
- Added move-only `SequenceTextLayout` for excluded intervals and active-run layouts.
- Added original, active, and global sequence-text coordinate conversion.
- Added tagged base, separator, and terminator locations.
- Added generation-bound text-interval exclusion for iterative suffix-index workflows.
- Added byte lookup, exact-buffer copy, streaming output, and explicit materialization.
- Kept the v1 core target, five-column FAI, and metadata schema unchanged.

## 0.1.0 - unreleased

- Added C++17 standard five-column FAI construction and validation.
- Added versioned SeqPro XXH3-128 metadata sidecar.
- Added immutable mmap-backed `IndexedFasta` and `FastaSequenceView` APIs.
- Added zero-copy sequence chunks, caller-buffer reads, and streaming output.
- Added the `seqpro-index` build, validate, and info commands.
- Added CMake build-tree and install-tree package targets as `SeqPro::seqpro`.
