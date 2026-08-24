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
    SequenceChunk operator*() const;
    /// Advances to the next physical FASTA line chunk.
    Iterator& operator++();
    /// Advances to the next chunk and returns the previous iterator value.
    Iterator operator++(int);

    /// Compares iterator identity and logical position.
    bool operator==(const Iterator& other) const noexcept;
    /// Returns the inverse of operator==().
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
  Iterator begin() const noexcept;
  /// Returns the range sentinel.
  Iterator end() const noexcept;

  /// Returns true when the represented logical interval has length zero.
  bool empty() const noexcept;
  /// Returns the physical line count, saturated at std::size_t maximum.
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
  char ReadBase(SequencePosition sequence_position) const;

  /// Allocates and returns the requested zero-based half-open interval.
  std::string ReadSubsequence(SequencePosition sequence_start_position,
                              SequenceLength subsequence_length) const;

  /// Copies destination_size_bytes symbols into caller-owned storage without allocating.
  void CopySubsequenceTo(SequencePosition sequence_start_position, char* destination_buffer,
                         std::size_t destination_size_bytes) const;

  /// Writes a sequence interval using a bounded transfer buffer.
  void WriteSubsequenceTo(SequencePosition sequence_start_position,
                          SequenceLength subsequence_length, std::ostream& output_stream,
                          std::size_t transfer_buffer_size_bytes = std::size_t{1024} *
                                                                   std::size_t{1024}) const;

  /// Returns zero-copy mmap chunks for a logical sequence interval.
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
  static IndexedFasta Open(const std::filesystem::path& fasta_path,
                           const IndexedFastaOptions& open_options = {});
  /// Explicitly creates or validates an index before opening the FASTA.
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
  std::optional<SequenceId> FindSequenceId(std::string_view sequence_name) const noexcept;
  /// Returns one FAI entry or throws kSequenceNotFound for an invalid identifier.
  const FastaIndexEntry& IndexEntryById(SequenceId sequence_id) const;
  /// Returns one FAI entry or throws kSequenceNotFound for an absent name.
  const FastaIndexEntry& IndexEntryByName(std::string_view sequence_name) const;
  /// Returns an owning immutable view selected by identifier.
  FastaSequenceView SequenceById(SequenceId sequence_id) const;
  /// Returns an owning immutable view selected by name.
  FastaSequenceView SequenceByName(std::string_view sequence_name) const;

 private:
  SEQPRO_NO_EXPORT explicit IndexedFasta(
      std::shared_ptr<const internal::IndexedFastaState> shared_indexed_fasta_state);

  std::shared_ptr<const internal::IndexedFastaState> shared_indexed_fasta_state_;
};

}  // namespace seqpro

#endif  // SEQPRO_INCLUDE_SEQPRO_INDEXED_FASTA_H_
