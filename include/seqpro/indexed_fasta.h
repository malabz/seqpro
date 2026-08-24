#ifndef SEQPRO_INCLUDE_SEQPRO_INDEXED_FASTA_H_
#define SEQPRO_INCLUDE_SEQPRO_INDEXED_FASTA_H_

#include <cstddef>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "seqpro/export.h"
#include "seqpro/fasta_index.h"

namespace seqpro {
namespace internal {
class IndexedFastaState;
}  // namespace internal

/// Options controlling read-only IndexedFasta::Open().
struct IndexedFastaOptions {
  /// FAI path; an empty path selects `<fasta_path>.fai`.
  std::filesystem::path fasta_index_path;
  /// Page-access advice supplied to the operating system for the FASTA mapping.
  FileAccessPattern file_access_pattern = FileAccessPattern::kOperatingSystemDefault;
  /// Validation strength applied while opening the index.
  IndexVerificationMode index_verification_mode = IndexVerificationMode::kFast;
  /// Reject an otherwise valid external FAI that has no SeqPro metadata sidecar.
  bool require_seqpro_metadata = false;
};

/// One physically contiguous span of sequence symbols in the mapped FASTA.
struct SequenceChunk {
  /// Zero-based position of the first symbol in this chunk.
  SequencePosition sequence_start_position;
  /// Symbols mapped directly from the FASTA; line endings are excluded.
  std::string_view sequence_bases;
};

class FastaSequenceView;

/// Lazy range of physical mmap spans that excludes FASTA line endings.
///
/// The range owns the shared mapping lifetime; detached string_views do not.
class SEQPRO_EXPORT SequenceChunkRange {
 private:
  using SharedIndexedFastaState = std::shared_ptr<const internal::IndexedFastaState>;

 public:
  /// Forward iterator producing SequenceChunk values without allocating sequence data.
  class SEQPRO_EXPORT Iterator {
   public:
    /// STL iterator category.
    using iterator_category = std::forward_iterator_tag;
    /// Value returned by dereferencing the iterator.
    using value_type = SequenceChunk;
    /// Signed distance type required by the iterator protocol.
    using difference_type = std::ptrdiff_t;
    /// Iterator does not expose an arrow operator.
    using pointer = void;
    /// Dereferencing returns a lightweight value rather than a reference.
    using reference = SequenceChunk;

    /// Constructs a sentinel iterator.
    Iterator() = default;

    /// Returns the current physically contiguous sequence chunk.
    ///
    /// @return A lightweight value whose string_view points into the retained FASTA mapping.
    /// @pre The iterator is not equal to the range sentinel.
    SequenceChunk operator*() const;
    /// Advances to the next physical FASTA line chunk.
    ///
    /// @return This iterator after advancement.
    /// @pre The iterator is not equal to the range sentinel.
    Iterator& operator++();
    /// Advances to the next chunk and returns the previous iterator value.
    ///
    /// @return Iterator value before advancement.
    /// @pre The iterator is not equal to the range sentinel.
    Iterator operator++(int);

    /// Compares iterator identity and logical position.
    ///
    /// @param other Iterator to compare.
    /// @return True when both iterators refer to the same range position.
    bool operator==(const Iterator& other) const noexcept;
    /// Returns the inverse of operator==().
    ///
    /// @param other Iterator to compare.
    /// @return True when the iterators differ.
    bool operator!=(const Iterator& other) const noexcept;

   private:
    SEQPRO_NO_EXPORT Iterator(SharedIndexedFastaState shared_indexed_fasta_state,
                              SequenceId sequence_id, SequencePosition current_sequence_position,
                              SequencePosition sequence_end_position);

    SharedIndexedFastaState shared_indexed_fasta_state_;
    SequenceId sequence_id_ = 0;
    SequencePosition current_sequence_position_ = 0;
    SequencePosition sequence_end_position_ = 0;

    friend class SequenceChunkRange;
  };

  /// Returns an iterator at the first sequence chunk.
  ///
  /// @return Iterator retaining the FASTA mapping lifetime.
  Iterator begin() const noexcept;
  /// Returns the range sentinel.
  ///
  /// @return Sentinel associated with this range.
  Iterator end() const noexcept;

  /// Returns true when the represented logical interval has length zero.
  ///
  /// @return True exactly when begin() equals end().
  bool empty() const noexcept;
  /// Returns the physical line count, saturated at std::size_t maximum.
  ///
  /// @return Allocation hint only; callers must not treat it as a correctness boundary.
  std::size_t estimated_chunk_count() const noexcept;

 private:
  SEQPRO_NO_EXPORT SequenceChunkRange(SharedIndexedFastaState shared_indexed_fasta_state,
                                      SequenceId sequence_id,
                                      SequencePosition sequence_start_position,
                                      SequencePosition sequence_end_position);

  SharedIndexedFastaState shared_indexed_fasta_state_;
  SequenceId sequence_id_ = 0;
  SequencePosition sequence_start_position_ = 0;
  SequencePosition sequence_end_position_ = 0;

  friend class FastaSequenceView;
};

/// Immutable random-access view of one FASTA record.
///
/// Coordinates are zero-based and all intervals are half-open. The view shares ownership of the
/// FASTA mapping, so it remains valid after its originating IndexedFasta handle is destroyed.
class SEQPRO_EXPORT FastaSequenceView {
 public:
  /// Returns the contiguous zero-based FAI identifier.
  SequenceId sequence_id() const noexcept;
  /// Returns the indexed sequence name without allocating.
  std::string_view sequence_name() const noexcept;
  /// Returns the number of sequence symbols in this record.
  SequenceLength sequence_length() const noexcept;
  /// Returns the immutable standard FAI entry backing this view.
  const FastaIndexEntry& fasta_index_entry() const noexcept;

  /// Reads one byte without normalizing case or alphabet.
  ///
  /// @param sequence_position Zero-based position in this sequence.
  /// @return The original FASTA byte at sequence_position.
  /// @throws SeqProError with kSequenceRangeOutOfBounds when sequence_position is not smaller than
  /// sequence_length().
  /// @note Concurrent calls on the same view are safe.
  /// @par Complexity
  /// Constant time.
  char ReadBase(SequencePosition sequence_position) const;

  /// Allocates and returns the requested zero-based half-open interval.
  ///
  /// @param sequence_start_position Inclusive zero-based sequence start.
  /// @param subsequence_length Number of sequence bytes to return.
  /// @return An owning string containing exactly subsequence_length original FASTA bytes.
  /// @throws SeqProError with kSequenceRangeOutOfBounds for an invalid interval or kIntegerOverflow
  /// when the result cannot be represented by std::string.
  /// @note Empty intervals are valid when sequence_start_position is at most sequence_length().
  /// Concurrent calls are safe because each call owns its result.
  /// @par Complexity
  /// Linear in returned bytes plus crossed physical FASTA lines.
  std::string ReadSubsequence(SequencePosition sequence_start_position,
                              SequenceLength subsequence_length) const;

  /// Copies destination_size_bytes symbols into caller-owned storage without allocating.
  ///
  /// @param sequence_start_position Inclusive zero-based sequence start.
  /// @param destination_buffer Caller-owned writable storage. It may be null only when
  /// destination_size_bytes is zero.
  /// @param destination_size_bytes Number of sequence bytes to copy and writable buffer bytes.
  /// @throws SeqProError with kInvalidArgument for a null non-empty destination or
  /// kSequenceRangeOutOfBounds for an invalid interval.
  /// @note The caller owns the buffer. Concurrent calls require independent, non-overlapping
  /// destination storage.
  /// @par Complexity
  /// Linear in copied bytes plus crossed physical FASTA lines.
  void CopySubsequenceTo(SequencePosition sequence_start_position, char* destination_buffer,
                         std::size_t destination_size_bytes) const;

  /// Writes a sequence interval using a bounded transfer buffer.
  ///
  /// @param sequence_start_position Inclusive zero-based sequence start.
  /// @param subsequence_length Number of sequence bytes to write.
  /// @param output_stream Destination stream, which remains owned by the caller.
  /// @param transfer_buffer_size_bytes Maximum internal transfer allocation; must be nonzero for a
  /// non-empty interval.
  /// @throws SeqProError with kInvalidArgument for an unusable buffer size,
  /// kSequenceRangeOutOfBounds for an invalid interval, kIntegerOverflow for unrepresentable
  /// allocation sizes, or kIoError when the stream write fails.
  /// @note Concurrent calls require separately synchronized output streams.
  /// @par Complexity
  /// Linear in written sequence bytes.
  void WriteSubsequenceTo(SequencePosition sequence_start_position,
                          SequenceLength subsequence_length, std::ostream& output_stream,
                          std::size_t transfer_buffer_size_bytes = std::size_t{1024} *
                                                                   std::size_t{1024}) const;

  /// Returns zero-copy mmap chunks for a logical sequence interval.
  ///
  /// @param sequence_start_position Inclusive zero-based sequence start.
  /// @param subsequence_length Number of logical sequence bytes in the range.
  /// @return A range retaining the mmap lifetime. Each yielded SequenceChunk points directly into
  /// mapped FASTA storage and excludes line endings.
  /// @throws SeqProError with kSequenceRangeOutOfBounds for an invalid interval.
  /// @warning Copying a yielded sequence_bases string_view out of the range does not retain the
  /// mapping lifetime.
  /// @par Complexity
  /// Range construction is constant time; complete traversal is linear in crossed physical lines.
  SequenceChunkRange SubsequenceChunks(SequencePosition sequence_start_position,
                                       SequenceLength subsequence_length) const;

 private:
  SEQPRO_NO_EXPORT FastaSequenceView(
      std::shared_ptr<const internal::IndexedFastaState> shared_indexed_fasta_state,
      SequenceId sequence_id);

  std::shared_ptr<const internal::IndexedFastaState> shared_indexed_fasta_state_;
  SequenceId sequence_id_ = 0;

  friend class IndexedFasta;
};

/// Copyable immutable handle to a memory-mapped FASTA and its validated FAI.
///
/// Concurrent const access is safe. Input files must not change while a handle or derived view is
/// alive. Copying shares the immutable mapping and index metadata.
class SEQPRO_EXPORT IndexedFasta {
 public:
  /// Opens an existing index without creating or modifying files.
  ///
  /// @param fasta_path Path to an uncompressed FASTA file.
  /// @param open_options FAI path, mmap access advice, and validation policy.
  /// @return A copyable owning handle to immutable mapped state.
  /// @throws SeqProError for I/O, malformed or stale index, unsupported format, integer overflow,
  /// or a missing required metadata sidecar.
  /// @note Concurrent const operations on the returned handle and derived views are safe.
  /// @par Complexity
  /// Linear in FAI record count in fast mode; full verification additionally reads all FASTA bytes.
  static IndexedFasta Open(const std::filesystem::path& fasta_path,
                           const IndexedFastaOptions& open_options = {});
  /// Explicitly creates or validates an index before opening the FASTA.
  ///
  /// @param fasta_path Path to an uncompressed FASTA file.
  /// @param build_options Index publication policy used before opening.
  /// @param open_options Read-only mapping and verification policy.
  /// @return A copyable owning handle to immutable mapped state.
  /// @throws SeqProError for any BuildFastaIndex() or Open() failure.
  /// @note This method can write FAI and metadata files; externally serialize competing writers.
  static IndexedFasta OpenOrBuildIndex(const std::filesystem::path& fasta_path,
                                       const FastaIndexBuildOptions& build_options = {},
                                       const IndexedFastaOptions& open_options = {});

  /// Shares ownership of an existing immutable reader state.
  IndexedFasta(const IndexedFasta&) noexcept = default;
  /// Shares ownership of an existing immutable reader state.
  IndexedFasta& operator=(const IndexedFasta&) noexcept = default;
  /// Transfers one shared reader handle.
  IndexedFasta(IndexedFasta&&) noexcept = default;
  /// Transfers one shared reader handle.
  IndexedFasta& operator=(IndexedFasta&&) noexcept = default;
  /// Releases this handle and unmaps the FASTA after the last shared view is destroyed.
  ~IndexedFasta() = default;

  /// Returns the FASTA path used to open this reader.
  const std::filesystem::path& fasta_path() const noexcept;
  /// Returns the validated standard FAI path.
  const std::filesystem::path& fasta_index_path() const noexcept;
  /// Returns whether the FAI is SeqPro-verified or an external standard index.
  FastaIndexOrigin fasta_index_origin() const noexcept;
  /// Returns the strongest verification completed while opening.
  IndexVerificationStatus index_verification_status() const noexcept;
  /// Returns the number of indexed sequences.
  std::size_t sequence_count() const noexcept;
  /// Returns all immutable FAI entries in file order.
  const std::vector<FastaIndexEntry>& fasta_index_entries() const noexcept;

  /// Finds a sequence identifier without throwing when the name is absent.
  ///
  /// @param sequence_name Exact indexed first header token.
  /// @return Sequence ID when present, otherwise std::nullopt. No string allocation is required.
  /// @note Thread-safe for concurrent const access.
  /// @par Complexity
  /// Average constant time.
  std::optional<SequenceId> FindSequenceId(std::string_view sequence_name) const noexcept;
  /// Returns one FAI entry or throws kSequenceNotFound for an invalid identifier.
  ///
  /// @param sequence_id Contiguous zero-based FAI identifier.
  /// @return Immutable reference owned by this reader's shared state.
  /// @throws SeqProError with kSequenceNotFound for an invalid identifier.
  /// @par Complexity
  /// Constant time.
  const FastaIndexEntry& IndexEntryById(SequenceId sequence_id) const;
  /// Returns one FAI entry or throws kSequenceNotFound for an absent name.
  ///
  /// @param sequence_name Exact indexed first header token.
  /// @return Immutable reference owned by this reader's shared state.
  /// @throws SeqProError with kSequenceNotFound when the name is absent.
  /// @par Complexity
  /// Average constant time.
  const FastaIndexEntry& IndexEntryByName(std::string_view sequence_name) const;
  /// Returns an owning immutable view selected by identifier.
  ///
  /// @param sequence_id Contiguous zero-based FAI identifier.
  /// @return View sharing the mmap lifetime independently of this IndexedFasta handle.
  /// @throws SeqProError with kSequenceNotFound for an invalid identifier.
  /// @par Complexity
  /// Constant time.
  FastaSequenceView SequenceById(SequenceId sequence_id) const;
  /// Returns an owning immutable view selected by name.
  ///
  /// @param sequence_name Exact indexed first header token.
  /// @return View sharing the mmap lifetime independently of this IndexedFasta handle.
  /// @throws SeqProError with kSequenceNotFound when the name is absent.
  /// @par Complexity
  /// Average constant time for lookup.
  FastaSequenceView SequenceByName(std::string_view sequence_name) const;

 private:
  SEQPRO_NO_EXPORT explicit IndexedFasta(
      std::shared_ptr<const internal::IndexedFastaState> shared_indexed_fasta_state);

  std::shared_ptr<const internal::IndexedFastaState> shared_indexed_fasta_state_;
};

}  // namespace seqpro

#endif  // SEQPRO_INCLUDE_SEQPRO_INDEXED_FASTA_H_
