#include "seqpro/indexed_fasta.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fasta_internal.h"
#include "mapped_file.h"
#include "seqpro/error.h"

namespace seqpro::internal {
namespace {

std::uint64_t CheckedAdd(std::uint64_t left, std::uint64_t right, const std::string& operation) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    throw SeqProError(ErrorCode::kIntegerOverflow, "integer overflow while " + operation);
  }
  return left + right;
}

std::uint64_t CheckedMultiply(std::uint64_t left, std::uint64_t right,
                              const std::string& operation) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    throw SeqProError(ErrorCode::kIntegerOverflow, "integer overflow while " + operation);
  }
  return left * right;
}

}  // namespace

class IndexedFastaState {
 public:
  IndexedFastaState(std::filesystem::path fasta_path, std::filesystem::path fasta_index_path,
                    MappedFile mapped_fasta, ValidatedFastaIndex validated_index)
      : fasta_path_(std::move(fasta_path)),
        fasta_index_path_(std::move(fasta_index_path)),
        mapped_fasta_(std::move(mapped_fasta)),
        fasta_index_entries_(std::move(validated_index.fasta_index_entries)),
        validation_report_(validated_index.validation_report) {
    sequence_name_to_id_.reserve(fasta_index_entries_.size());
    for (const FastaIndexEntry& index_entry : fasta_index_entries_) {
      sequence_name_to_id_.emplace(index_entry.sequence_name, index_entry.sequence_id);
    }
  }

  const std::filesystem::path& fasta_path() const noexcept { return fasta_path_; }
  const std::filesystem::path& fasta_index_path() const noexcept { return fasta_index_path_; }
  const FastaIndexValidationReport& validation_report() const noexcept {
    return validation_report_;
  }
  const std::vector<FastaIndexEntry>& fasta_index_entries() const noexcept {
    return fasta_index_entries_;
  }

  std::optional<SequenceId> FindSequenceId(std::string_view sequence_name) const noexcept {
    const auto sequence_iterator = sequence_name_to_id_.find(sequence_name);
    if (sequence_iterator == sequence_name_to_id_.end()) {
      return std::nullopt;
    }
    return sequence_iterator->second;
  }

  const FastaIndexEntry& IndexEntryById(SequenceId sequence_id) const {
    const std::size_t entry_index = static_cast<std::size_t>(sequence_id);
    if (entry_index >= fasta_index_entries_.size()) {
      throw SeqProError(ErrorCode::kSequenceNotFound, "sequence ID " + std::to_string(sequence_id) +
                                                          " does not exist in FASTA '" +
                                                          fasta_path_.string() + "'");
    }
    return fasta_index_entries_[entry_index];
  }

  const FastaIndexEntry& IndexEntryByIdUnchecked(SequenceId sequence_id) const noexcept {
    return fasta_index_entries_[static_cast<std::size_t>(sequence_id)];
  }

  const FastaIndexEntry& IndexEntryByName(std::string_view sequence_name) const {
    const std::optional<SequenceId> sequence_id = FindSequenceId(sequence_name);
    if (!sequence_id) {
      throw SeqProError(ErrorCode::kSequenceNotFound, "sequence '" + std::string(sequence_name) +
                                                          "' does not exist in FASTA '" +
                                                          fasta_path_.string() + "'");
    }
    return IndexEntryById(*sequence_id);
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void ValidateRange(SequenceId sequence_id, SequencePosition sequence_start_position,
                     SequenceLength subsequence_length) const {
    const FastaIndexEntry& index_entry = IndexEntryById(sequence_id);
    if (sequence_start_position > index_entry.sequence_length ||
        subsequence_length > index_entry.sequence_length - sequence_start_position) {
      throw SeqProError(ErrorCode::kSequenceRangeOutOfBounds,
                        "sequence range [" + std::to_string(sequence_start_position) + ", " +
                            std::to_string(sequence_start_position) + " + " +
                            std::to_string(subsequence_length) + ") exceeds sequence '" +
                            index_entry.sequence_name + "' of length " +
                            std::to_string(index_entry.sequence_length));
    }
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  SequenceChunk ChunkAt(SequenceId sequence_id, SequencePosition sequence_position,
                        SequencePosition sequence_end_position) const {
    const FastaIndexEntry& index_entry = IndexEntryById(sequence_id);
    if (sequence_position >= sequence_end_position ||
        sequence_end_position > index_entry.sequence_length) {
      throw SeqProError(
          ErrorCode::kSequenceRangeOutOfBounds,
          "cannot create sequence chunk outside sequence '" + index_entry.sequence_name + "'");
    }

    const std::uint64_t physical_line_index = sequence_position / index_entry.bases_per_line;
    const std::uint64_t base_offset_within_line = sequence_position % index_entry.bases_per_line;
    const std::uint64_t available_bases_in_line =
        index_entry.bases_per_line - base_offset_within_line;
    const std::uint64_t requested_base_count = sequence_end_position - sequence_position;
    const std::uint64_t chunk_base_count = std::min(available_bases_in_line, requested_base_count);
    const std::uint64_t chunk_file_offset =
        CheckedAdd(CheckedAdd(index_entry.first_base_offset_bytes,
                              CheckedMultiply(physical_line_index, index_entry.bytes_per_line,
                                              "computing indexed FASTA line offset"),
                              "computing indexed FASTA line offset"),
                   base_offset_within_line, "computing indexed FASTA base offset");
    const std::uint64_t chunk_file_end =
        CheckedAdd(chunk_file_offset, chunk_base_count, "computing indexed FASTA chunk end");
    if (chunk_file_end > mapped_fasta_.file_size_bytes() ||
        chunk_base_count > std::numeric_limits<std::size_t>::max()) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "indexed FASTA chunk cannot be represented for sequence '" +
                            index_entry.sequence_name + "'");
    }
    return SequenceChunk{sequence_position,
                         std::string_view(mapped_fasta_.mapped_bytes() + chunk_file_offset,
                                          static_cast<std::size_t>(chunk_base_count))};
  }

 private:
  std::filesystem::path fasta_path_;
  std::filesystem::path fasta_index_path_;
  MappedFile mapped_fasta_;
  std::vector<FastaIndexEntry> fasta_index_entries_;
  std::unordered_map<std::string_view, SequenceId> sequence_name_to_id_;
  FastaIndexValidationReport validation_report_;
};

}  // namespace seqpro::internal

namespace seqpro {

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
SequenceChunkRange::Iterator::Iterator(SharedIndexedFastaState shared_indexed_fasta_state,
                                       SequenceId sequence_id,
                                       SequencePosition current_sequence_position,
                                       SequencePosition sequence_end_position)
    : shared_indexed_fasta_state_(std::move(shared_indexed_fasta_state)),
      sequence_id_(sequence_id),
      current_sequence_position_(current_sequence_position),
      sequence_end_position_(sequence_end_position) {}
// NOLINTEND(bugprone-easily-swappable-parameters)

SequenceChunk SequenceChunkRange::Iterator::operator*() const {
  if (!shared_indexed_fasta_state_ || current_sequence_position_ >= sequence_end_position_) {
    throw SeqProError(ErrorCode::kInvalidArgument,
                      "cannot dereference an exhausted sequence chunk iterator");
  }
  return shared_indexed_fasta_state_->ChunkAt(sequence_id_, current_sequence_position_,
                                              sequence_end_position_);
}

SequenceChunkRange::Iterator& SequenceChunkRange::Iterator::operator++() {
  if (shared_indexed_fasta_state_ && current_sequence_position_ < sequence_end_position_) {
    const SequenceChunk current_chunk = shared_indexed_fasta_state_->ChunkAt(
        sequence_id_, current_sequence_position_, sequence_end_position_);
    current_sequence_position_ +=
        static_cast<SequencePosition>(current_chunk.sequence_bases.size());
  }
  return *this;
}

SequenceChunkRange::Iterator SequenceChunkRange::Iterator::operator++(int) {
  Iterator previous_iterator = *this;
  ++(*this);
  return previous_iterator;
}

bool SequenceChunkRange::Iterator::operator==(const Iterator& other) const noexcept {
  return shared_indexed_fasta_state_.get() == other.shared_indexed_fasta_state_.get() &&
         sequence_id_ == other.sequence_id_ &&
         current_sequence_position_ == other.current_sequence_position_ &&
         sequence_end_position_ == other.sequence_end_position_;
}

bool SequenceChunkRange::Iterator::operator!=(const Iterator& other) const noexcept {
  return !(*this == other);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
SequenceChunkRange::SequenceChunkRange(SharedIndexedFastaState shared_indexed_fasta_state,
                                       SequenceId sequence_id,
                                       SequencePosition sequence_start_position,
                                       SequencePosition sequence_end_position)
    : shared_indexed_fasta_state_(std::move(shared_indexed_fasta_state)),
      sequence_id_(sequence_id),
      sequence_start_position_(sequence_start_position),
      sequence_end_position_(sequence_end_position) {}
// NOLINTEND(bugprone-easily-swappable-parameters)

SequenceChunkRange::Iterator SequenceChunkRange::begin() const noexcept {
  return Iterator(shared_indexed_fasta_state_, sequence_id_, sequence_start_position_,
                  sequence_end_position_);
}

SequenceChunkRange::Iterator SequenceChunkRange::end() const noexcept {
  return Iterator(shared_indexed_fasta_state_, sequence_id_, sequence_end_position_,
                  sequence_end_position_);
}

bool SequenceChunkRange::empty() const noexcept {
  return sequence_start_position_ == sequence_end_position_;
}

std::size_t SequenceChunkRange::estimated_chunk_count() const noexcept {
  if (empty() || !shared_indexed_fasta_state_) {
    return 0;
  }
  const FastaIndexEntry& index_entry =
      shared_indexed_fasta_state_->IndexEntryByIdUnchecked(sequence_id_);
  const std::uint64_t first_physical_line_index =
      sequence_start_position_ / index_entry.bases_per_line;
  const std::uint64_t last_physical_line_index =
      (sequence_end_position_ - 1U) / index_entry.bases_per_line;
  const std::uint64_t chunk_count = last_physical_line_index - first_physical_line_index + 1U;
  if (chunk_count > std::numeric_limits<std::size_t>::max()) {
    return std::numeric_limits<std::size_t>::max();
  }
  return static_cast<std::size_t>(chunk_count);
}

FastaSequenceView::FastaSequenceView(
    std::shared_ptr<const internal::IndexedFastaState> shared_indexed_fasta_state,
    SequenceId sequence_id)
    : shared_indexed_fasta_state_(std::move(shared_indexed_fasta_state)),
      sequence_id_(sequence_id) {}

SequenceId FastaSequenceView::sequence_id() const noexcept { return sequence_id_; }

std::string_view FastaSequenceView::sequence_name() const noexcept {
  return shared_indexed_fasta_state_->IndexEntryByIdUnchecked(sequence_id_).sequence_name;
}

SequenceLength FastaSequenceView::sequence_length() const noexcept {
  return shared_indexed_fasta_state_->IndexEntryByIdUnchecked(sequence_id_).sequence_length;
}

const FastaIndexEntry& FastaSequenceView::fasta_index_entry() const noexcept {
  return shared_indexed_fasta_state_->IndexEntryByIdUnchecked(sequence_id_);
}

char FastaSequenceView::ReadBase(SequencePosition sequence_position) const {
  shared_indexed_fasta_state_->ValidateRange(sequence_id_, sequence_position, 1);
  return shared_indexed_fasta_state_
      ->ChunkAt(sequence_id_, sequence_position, sequence_position + 1U)
      .sequence_bases.front();
}

std::string FastaSequenceView::ReadSubsequence(SequencePosition sequence_start_position,
                                               SequenceLength subsequence_length) const {
  shared_indexed_fasta_state_->ValidateRange(sequence_id_, sequence_start_position,
                                             subsequence_length);
  if (subsequence_length > std::numeric_limits<std::size_t>::max()) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "requested subsequence cannot be represented as std::string");
  }
  std::string subsequence_bases;
  if (subsequence_length > subsequence_bases.max_size()) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "requested subsequence exceeds std::string::max_size()");
  }
  subsequence_bases.resize(static_cast<std::size_t>(subsequence_length));
  CopySubsequenceTo(sequence_start_position, subsequence_bases.data(), subsequence_bases.size());
  return subsequence_bases;
}

void FastaSequenceView::CopySubsequenceTo(SequencePosition sequence_start_position,
                                          char* destination_buffer,
                                          std::size_t destination_size_bytes) const {
  if (destination_buffer == nullptr && destination_size_bytes != 0) {
    throw SeqProError(ErrorCode::kInvalidArgument,
                      "CopySubsequenceTo destination is null for a non-empty request");
  }
  shared_indexed_fasta_state_->ValidateRange(sequence_id_, sequence_start_position,
                                             static_cast<SequenceLength>(destination_size_bytes));
  std::size_t copied_size_bytes = 0;
  SequencePosition current_sequence_position = sequence_start_position;
  const SequencePosition sequence_end_position =
      sequence_start_position + static_cast<SequenceLength>(destination_size_bytes);
  while (current_sequence_position < sequence_end_position) {
    const SequenceChunk sequence_chunk = shared_indexed_fasta_state_->ChunkAt(
        sequence_id_, current_sequence_position, sequence_end_position);
    std::memcpy(destination_buffer + copied_size_bytes, sequence_chunk.sequence_bases.data(),
                sequence_chunk.sequence_bases.size());
    copied_size_bytes += sequence_chunk.sequence_bases.size();
    current_sequence_position +=
        static_cast<SequencePosition>(sequence_chunk.sequence_bases.size());
  }
}

void FastaSequenceView::WriteSubsequenceTo(SequencePosition sequence_start_position,
                                           SequenceLength subsequence_length,
                                           std::ostream& output_stream,
                                           std::size_t transfer_buffer_size_bytes) const {
  shared_indexed_fasta_state_->ValidateRange(sequence_id_, sequence_start_position,
                                             subsequence_length);
  if (subsequence_length == 0) {
    return;
  }
  if (transfer_buffer_size_bytes == 0) {
    throw SeqProError(ErrorCode::kInvalidArgument,
                      "WriteSubsequenceTo transfer buffer size must be greater than zero");
  }
  const std::uint64_t maximum_stream_write_size =
      static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max());
  const std::uint64_t bounded_buffer_size_bytes =
      std::min<std::uint64_t>(transfer_buffer_size_bytes, maximum_stream_write_size);
  const std::size_t effective_buffer_size_bytes =
      static_cast<std::size_t>(std::min(bounded_buffer_size_bytes, subsequence_length));
  std::vector<char> transfer_buffer;
  if (effective_buffer_size_bytes > transfer_buffer.max_size()) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "transfer buffer exceeds std::vector::max_size()");
  }
  transfer_buffer.resize(effective_buffer_size_bytes);
  SequencePosition current_sequence_position = sequence_start_position;
  SequenceLength remaining_subsequence_length = subsequence_length;
  while (remaining_subsequence_length != 0) {
    const std::size_t current_transfer_size_bytes = static_cast<std::size_t>(
        std::min<std::uint64_t>(transfer_buffer.size(), remaining_subsequence_length));
    CopySubsequenceTo(current_sequence_position, transfer_buffer.data(),
                      current_transfer_size_bytes);
    output_stream.write(transfer_buffer.data(),
                        static_cast<std::streamsize>(current_transfer_size_bytes));
    if (!output_stream) {
      throw SeqProError(
          ErrorCode::kIoError,
          "cannot write subsequence from '" + std::string(sequence_name()) + "' to output stream");
    }
    current_sequence_position += static_cast<SequencePosition>(current_transfer_size_bytes);
    remaining_subsequence_length -= static_cast<SequenceLength>(current_transfer_size_bytes);
  }
}

SequenceChunkRange FastaSequenceView::SubsequenceChunks(SequencePosition sequence_start_position,
                                                        SequenceLength subsequence_length) const {
  shared_indexed_fasta_state_->ValidateRange(sequence_id_, sequence_start_position,
                                             subsequence_length);
  return SequenceChunkRange(shared_indexed_fasta_state_, sequence_id_, sequence_start_position,
                            sequence_start_position + subsequence_length);
}

IndexedFasta::IndexedFasta(
    std::shared_ptr<const internal::IndexedFastaState> shared_indexed_fasta_state)
    : shared_indexed_fasta_state_(std::move(shared_indexed_fasta_state)) {}

IndexedFasta IndexedFasta::Open(const std::filesystem::path& fasta_path,
                                const IndexedFastaOptions& open_options) {
  const std::filesystem::path fasta_index_path =
      internal::ResolveFastaIndexPath(fasta_path, open_options.fasta_index_path);
  internal::MappedFile mapped_fasta =
      internal::MappedFile::OpenReadOnly(fasta_path, open_options.file_access_pattern);
  internal::ValidatedFastaIndex validated_index = internal::ValidateFastaIndexFiles(
      fasta_path, fasta_index_path, open_options.index_verification_mode,
      open_options.require_seqpro_metadata, &mapped_fasta);
  auto shared_indexed_fasta_state = std::make_shared<const internal::IndexedFastaState>(
      fasta_path, fasta_index_path, std::move(mapped_fasta), std::move(validated_index));
  return IndexedFasta(std::move(shared_indexed_fasta_state));
}

IndexedFasta IndexedFasta::OpenOrBuildIndex(const std::filesystem::path& fasta_path,
                                            const FastaIndexBuildOptions& requested_build_options,
                                            const IndexedFastaOptions& requested_open_options) {
  FastaIndexBuildOptions build_options = requested_build_options;
  IndexedFastaOptions open_options = requested_open_options;
  if (!build_options.fasta_index_path.empty() && !open_options.fasta_index_path.empty() &&
      build_options.fasta_index_path != open_options.fasta_index_path) {
    throw SeqProError(ErrorCode::kInvalidArgument,
                      "OpenOrBuildIndex received different build and open index paths");
  }
  if (build_options.fasta_index_path.empty()) {
    build_options.fasta_index_path = open_options.fasta_index_path;
  }
  const FastaIndexBuildReport index_build_report = BuildFastaIndex(fasta_path, build_options);
  open_options.fasta_index_path = index_build_report.fasta_index_path;
  return Open(fasta_path, open_options);
}

const std::filesystem::path& IndexedFasta::fasta_path() const noexcept {
  return shared_indexed_fasta_state_->fasta_path();
}

const std::filesystem::path& IndexedFasta::fasta_index_path() const noexcept {
  return shared_indexed_fasta_state_->fasta_index_path();
}

FastaIndexOrigin IndexedFasta::fasta_index_origin() const noexcept {
  return shared_indexed_fasta_state_->validation_report().index_origin;
}

IndexVerificationStatus IndexedFasta::index_verification_status() const noexcept {
  return shared_indexed_fasta_state_->validation_report().verification_status;
}

std::size_t IndexedFasta::sequence_count() const noexcept {
  return shared_indexed_fasta_state_->fasta_index_entries().size();
}

const std::vector<FastaIndexEntry>& IndexedFasta::fasta_index_entries() const noexcept {
  return shared_indexed_fasta_state_->fasta_index_entries();
}

std::optional<SequenceId> IndexedFasta::FindSequenceId(
    std::string_view sequence_name) const noexcept {
  return shared_indexed_fasta_state_->FindSequenceId(sequence_name);
}

const FastaIndexEntry& IndexedFasta::IndexEntryById(SequenceId sequence_id) const {
  return shared_indexed_fasta_state_->IndexEntryById(sequence_id);
}

const FastaIndexEntry& IndexedFasta::IndexEntryByName(std::string_view sequence_name) const {
  return shared_indexed_fasta_state_->IndexEntryByName(sequence_name);
}

FastaSequenceView IndexedFasta::SequenceById(SequenceId sequence_id) const {
  shared_indexed_fasta_state_->IndexEntryById(sequence_id);
  return FastaSequenceView(shared_indexed_fasta_state_, sequence_id);
}

FastaSequenceView IndexedFasta::SequenceByName(std::string_view sequence_name) const {
  const std::optional<SequenceId> sequence_id =
      shared_indexed_fasta_state_->FindSequenceId(sequence_name);
  if (!sequence_id) {
    throw SeqProError(ErrorCode::kSequenceNotFound, "sequence '" + std::string(sequence_name) +
                                                        "' does not exist in FASTA '" +
                                                        fasta_path().string() + "'");
  }
  return FastaSequenceView(shared_indexed_fasta_state_, *sequence_id);
}

}  // namespace seqpro
