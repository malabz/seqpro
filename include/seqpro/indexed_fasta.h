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

struct IndexedFastaOptions {
  std::filesystem::path fasta_index_path;
  FileAccessPattern file_access_pattern = FileAccessPattern::kOperatingSystemDefault;
  IndexVerificationMode index_verification_mode = IndexVerificationMode::kFast;
  bool require_seqpro_metadata = false;
};

struct SequenceChunk {
  SequencePosition sequence_start;
  std::string_view bases;
};

class FastaSequenceView;

// A lazy range of physical mmap spans that excludes FASTA line endings.
// The range owns the shared mapping lifetime; detached string_views do not.
class SEQPRO_EXPORT SequenceChunkRange {
 private:
  using SharedState = std::shared_ptr<const internal::IndexedFastaState>;

 public:
  class SEQPRO_EXPORT Iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = SequenceChunk;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = SequenceChunk;

    Iterator() = default;

    SequenceChunk operator*() const;
    Iterator& operator++();
    Iterator operator++(int);

    bool operator==(const Iterator& other) const noexcept;
    bool operator!=(const Iterator& other) const noexcept;

   private:
    SEQPRO_NO_EXPORT Iterator(SharedState state, SequenceId sequence_id,
                              SequencePosition current_position,
                              SequencePosition end_position);

    SharedState state_;
    SequenceId sequence_id_ = 0;
    SequencePosition current_position_ = 0;
    SequencePosition end_position_ = 0;

    friend class SequenceChunkRange;
  };

  Iterator begin() const noexcept;
  Iterator end() const noexcept;

  bool empty() const noexcept;
  std::size_t estimated_chunk_count() const noexcept;

 private:
  SEQPRO_NO_EXPORT SequenceChunkRange(SharedState state, SequenceId sequence_id,
                                      SequencePosition sequence_start,
                                      SequencePosition sequence_end);

  SharedState state_;
  SequenceId sequence_id_ = 0;
  SequencePosition sequence_start_ = 0;
  SequencePosition sequence_end_ = 0;

  friend class FastaSequenceView;
};

// An immutable random-access view of one FASTA record.
// Coordinates are zero-based and all intervals are half-open.
class SEQPRO_EXPORT FastaSequenceView {
 public:
  SequenceId sequence_id() const noexcept;
  std::string_view sequence_name() const noexcept;
  SequenceLength sequence_length() const noexcept;
  const FastaIndexEntry& fasta_index_entry() const noexcept;

  // Reads one byte without normalizing case or alphabet.
  char ReadBase(SequencePosition sequence_position) const;

  // Allocates and returns [sequence_start, sequence_start + subsequence_length).
  std::string ReadSubsequence(SequencePosition sequence_start,
                              SequenceLength subsequence_length) const;

  // Copies destination_size bases into caller-owned memory without allocating a result string.
  void CopySubsequenceTo(SequencePosition sequence_start, char* destination,
                         std::size_t destination_size) const;

  // Writes a sequence interval using a bounded transfer buffer.
  void WriteSubsequenceTo(SequencePosition sequence_start, SequenceLength subsequence_length,
                          std::ostream& output_stream,
                          std::size_t transfer_buffer_size = 1024 * 1024) const;

  // Returns zero-copy mmap chunks for a logical sequence interval.
  SequenceChunkRange SubsequenceChunks(SequencePosition sequence_start,
                                       SequenceLength subsequence_length) const;

 private:
  SEQPRO_NO_EXPORT FastaSequenceView(
      std::shared_ptr<const internal::IndexedFastaState> state, SequenceId sequence_id);

  std::shared_ptr<const internal::IndexedFastaState> state_;
  SequenceId sequence_id_ = 0;

  friend class IndexedFasta;
};

// A copyable, immutable handle to a memory-mapped FASTA and its validated FAI.
// Concurrent const access is safe. Input files must not change while a handle is alive.
class SEQPRO_EXPORT IndexedFasta {
 public:
  // Opens an existing index. This function never creates or modifies index files.
  static IndexedFasta Open(const std::filesystem::path& fasta_path,
                           const IndexedFastaOptions& options = {});
  // Explicitly creates or validates an index before opening it.
  static IndexedFasta OpenOrBuildIndex(
      const std::filesystem::path& fasta_path,
      const FastaIndexBuildOptions& build_options = {},
      const IndexedFastaOptions& open_options = {});

  IndexedFasta(const IndexedFasta&) noexcept = default;
  IndexedFasta& operator=(const IndexedFasta&) noexcept = default;
  IndexedFasta(IndexedFasta&&) noexcept = default;
  IndexedFasta& operator=(IndexedFasta&&) noexcept = default;
  ~IndexedFasta() = default;

  const std::filesystem::path& fasta_path() const noexcept;
  const std::filesystem::path& fasta_index_path() const noexcept;
  FastaIndexOrigin fasta_index_origin() const noexcept;
  IndexVerificationStatus index_verification_status() const noexcept;
  std::size_t sequence_count() const noexcept;
  const std::vector<FastaIndexEntry>& fasta_index_entries() const noexcept;

  std::optional<SequenceId> FindSequenceId(std::string_view sequence_name) const noexcept;
  const FastaIndexEntry& IndexEntryById(SequenceId sequence_id) const;
  const FastaIndexEntry& IndexEntryByName(std::string_view sequence_name) const;
  FastaSequenceView SequenceById(SequenceId sequence_id) const;
  FastaSequenceView SequenceByName(std::string_view sequence_name) const;

 private:
  SEQPRO_NO_EXPORT explicit IndexedFasta(
      std::shared_ptr<const internal::IndexedFastaState> state);

  std::shared_ptr<const internal::IndexedFastaState> state_;
};

}  // namespace seqpro

#endif  // SEQPRO_INCLUDE_SEQPRO_INDEXED_FASTA_H_
