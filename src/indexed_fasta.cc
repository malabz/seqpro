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

std::uint64_t CheckedAdd(std::uint64_t left, std::uint64_t right,
                         const std::string& operation) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "integer overflow while " + operation);
  }
  return left + right;
}

std::uint64_t CheckedMultiply(std::uint64_t left, std::uint64_t right,
                              const std::string& operation) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "integer overflow while " + operation);
  }
  return left * right;
}

}  // namespace

class IndexedFastaState {
 public:
  IndexedFastaState(std::filesystem::path fasta_path,
                    std::filesystem::path fasta_index_path, MappedFile mapped_fasta,
                    ValidatedFastaIndex validated_index)
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
      throw SeqProError(ErrorCode::kSequenceNotFound,
                        "sequence ID " + std::to_string(sequence_id) +
                            " does not exist in FASTA '" + fasta_path_.string() + "'");
    }
    return fasta_index_entries_[entry_index];
  }

  const FastaIndexEntry& IndexEntryByName(std::string_view sequence_name) const {
    const std::optional<SequenceId> sequence_id = FindSequenceId(sequence_name);
    if (!sequence_id) {
      throw SeqProError(ErrorCode::kSequenceNotFound,
                        "sequence '" + std::string(sequence_name) +
                            "' does not exist in FASTA '" + fasta_path_.string() + "'");
    }
    return IndexEntryById(*sequence_id);
  }

  void ValidateRange(SequenceId sequence_id, SequencePosition sequence_start,
                     SequenceLength subsequence_length) const {
    const FastaIndexEntry& index_entry = IndexEntryById(sequence_id);
    if (sequence_start > index_entry.sequence_length ||
        subsequence_length > index_entry.sequence_length - sequence_start) {
      throw SeqProError(
          ErrorCode::kSequenceRangeOutOfBounds,
          "sequence range [" + std::to_string(sequence_start) + ", " +
              std::to_string(sequence_start) + " + " + std::to_string(subsequence_length) +
              ") exceeds sequence '" + index_entry.sequence_name + "' of length " +
              std::to_string(index_entry.sequence_length));
    }
  }

  SequenceChunk ChunkAt(SequenceId sequence_id, SequencePosition sequence_position,
                        SequencePosition sequence_end) const {
    const FastaIndexEntry& index_entry = IndexEntryById(sequence_id);
    if (sequence_position >= sequence_end || sequence_end > index_entry.sequence_length) {
      throw SeqProError(ErrorCode::kSequenceRangeOutOfBounds,
                        "cannot create sequence chunk outside sequence '" +
                            index_entry.sequence_name + "'");
    }

    const std::uint64_t physical_line_index =
        sequence_position / index_entry.bases_per_line;
    const std::uint64_t base_offset_within_line =
        sequence_position % index_entry.bases_per_line;
    const std::uint64_t available_bases_in_line =
        index_entry.bases_per_line - base_offset_within_line;
    const std::uint64_t requested_bases = sequence_end - sequence_position;
    const std::uint64_t chunk_base_count =
        std::min(available_bases_in_line, requested_bases);
    const std::uint64_t chunk_file_offset = CheckedAdd(
        CheckedAdd(index_entry.first_base_offset_bytes,
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
    return SequenceChunk{
        sequence_position,
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

SequenceChunkRange::Iterator::Iterator(SharedState state, SequenceId sequence_id,
                                       SequencePosition current_position,
                                       SequencePosition end_position)
    : state_(std::move(state)),
      sequence_id_(sequence_id),
      current_position_(current_position),
      end_position_(end_position) {}

SequenceChunk SequenceChunkRange::Iterator::operator*() const {
  if (!state_ || current_position_ >= end_position_) {
    throw SeqProError(ErrorCode::kInvalidArgument,
                      "cannot dereference an exhausted sequence chunk iterator");
  }
  return state_->ChunkAt(sequence_id_, current_position_, end_position_);
}

SequenceChunkRange::Iterator& SequenceChunkRange::Iterator::operator++() {
  if (state_ && current_position_ < end_position_) {
    const SequenceChunk current_chunk =
        state_->ChunkAt(sequence_id_, current_position_, end_position_);
    current_position_ += static_cast<SequencePosition>(current_chunk.bases.size());
  }
  return *this;
}

SequenceChunkRange::Iterator SequenceChunkRange::Iterator::operator++(int) {
  Iterator previous = *this;
  ++(*this);
  return previous;
}

bool SequenceChunkRange::Iterator::operator==(const Iterator& other) const noexcept {
  return state_.get() == other.state_.get() && sequence_id_ == other.sequence_id_ &&
         current_position_ == other.current_position_ && end_position_ == other.end_position_;
}

bool SequenceChunkRange::Iterator::operator!=(const Iterator& other) const noexcept {
  return !(*this == other);
}

SequenceChunkRange::SequenceChunkRange(SharedState state, SequenceId sequence_id,
                                       SequencePosition sequence_start,
                                       SequencePosition sequence_end)
    : state_(std::move(state)),
      sequence_id_(sequence_id),
      sequence_start_(sequence_start),
      sequence_end_(sequence_end) {}

SequenceChunkRange::Iterator SequenceChunkRange::begin() const noexcept {
  return Iterator(state_, sequence_id_, sequence_start_, sequence_end_);
}

SequenceChunkRange::Iterator SequenceChunkRange::end() const noexcept {
  return Iterator(state_, sequence_id_, sequence_end_, sequence_end_);
}

bool SequenceChunkRange::empty() const noexcept { return sequence_start_ == sequence_end_; }

std::size_t SequenceChunkRange::estimated_chunk_count() const noexcept {
  if (empty() || !state_) {
    return 0;
  }
  const FastaIndexEntry& index_entry = state_->IndexEntryById(sequence_id_);
  const std::uint64_t first_line = sequence_start_ / index_entry.bases_per_line;
  const std::uint64_t last_line = (sequence_end_ - 1U) / index_entry.bases_per_line;
  const std::uint64_t chunk_count = last_line - first_line + 1U;
  if (chunk_count > std::numeric_limits<std::size_t>::max()) {
    return std::numeric_limits<std::size_t>::max();
  }
  return static_cast<std::size_t>(chunk_count);
}

FastaSequenceView::FastaSequenceView(
    std::shared_ptr<const internal::IndexedFastaState> state, SequenceId sequence_id)
    : state_(std::move(state)), sequence_id_(sequence_id) {}

SequenceId FastaSequenceView::sequence_id() const noexcept { return sequence_id_; }

std::string_view FastaSequenceView::sequence_name() const noexcept {
  return state_->IndexEntryById(sequence_id_).sequence_name;
}

SequenceLength FastaSequenceView::sequence_length() const noexcept {
  return state_->IndexEntryById(sequence_id_).sequence_length;
}

const FastaIndexEntry& FastaSequenceView::fasta_index_entry() const noexcept {
  return state_->IndexEntryById(sequence_id_);
}

char FastaSequenceView::ReadBase(SequencePosition sequence_position) const {
  state_->ValidateRange(sequence_id_, sequence_position, 1);
  return state_->ChunkAt(sequence_id_, sequence_position, sequence_position + 1U).bases.front();
}

std::string FastaSequenceView::ReadSubsequence(SequencePosition sequence_start,
                                               SequenceLength subsequence_length) const {
  state_->ValidateRange(sequence_id_, sequence_start, subsequence_length);
  if (subsequence_length > std::numeric_limits<std::size_t>::max()) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "requested subsequence cannot be represented as std::string");
  }
  std::string subsequence;
  if (subsequence_length > subsequence.max_size()) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "requested subsequence exceeds std::string::max_size()");
  }
  subsequence.resize(static_cast<std::size_t>(subsequence_length));
  CopySubsequenceTo(sequence_start, subsequence.data(), subsequence.size());
  return subsequence;
}

void FastaSequenceView::CopySubsequenceTo(SequencePosition sequence_start, char* destination,
                                          std::size_t destination_size) const {
  if (destination == nullptr && destination_size != 0) {
    throw SeqProError(ErrorCode::kInvalidArgument,
                      "CopySubsequenceTo destination is null for a non-empty request");
  }
  state_->ValidateRange(sequence_id_, sequence_start,
                        static_cast<SequenceLength>(destination_size));
  std::size_t copied_bytes = 0;
  SequencePosition current_position = sequence_start;
  const SequencePosition sequence_end =
      sequence_start + static_cast<SequenceLength>(destination_size);
  while (current_position < sequence_end) {
    const SequenceChunk sequence_chunk =
        state_->ChunkAt(sequence_id_, current_position, sequence_end);
    std::memcpy(destination + copied_bytes, sequence_chunk.bases.data(),
                sequence_chunk.bases.size());
    copied_bytes += sequence_chunk.bases.size();
    current_position += static_cast<SequencePosition>(sequence_chunk.bases.size());
  }
}

void FastaSequenceView::WriteSubsequenceTo(SequencePosition sequence_start,
                                           SequenceLength subsequence_length,
                                           std::ostream& output_stream,
                                           std::size_t transfer_buffer_size) const {
  state_->ValidateRange(sequence_id_, sequence_start, subsequence_length);
  if (subsequence_length == 0) {
    return;
  }
  if (transfer_buffer_size == 0) {
    throw SeqProError(ErrorCode::kInvalidArgument,
                      "WriteSubsequenceTo transfer buffer size must be greater than zero");
  }
  const std::uint64_t maximum_stream_write_size =
      static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max());
  const std::uint64_t bounded_buffer_size =
      std::min<std::uint64_t>(transfer_buffer_size, maximum_stream_write_size);
  const std::size_t effective_buffer_size =
      static_cast<std::size_t>(std::min(bounded_buffer_size, subsequence_length));
  std::vector<char> transfer_buffer;
  if (effective_buffer_size > transfer_buffer.max_size()) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "transfer buffer exceeds std::vector::max_size()");
  }
  transfer_buffer.resize(effective_buffer_size);
  SequencePosition current_position = sequence_start;
  SequenceLength remaining_length = subsequence_length;
  while (remaining_length != 0) {
    const std::size_t current_transfer_size = static_cast<std::size_t>(
        std::min<std::uint64_t>(transfer_buffer.size(), remaining_length));
    CopySubsequenceTo(current_position, transfer_buffer.data(), current_transfer_size);
    output_stream.write(transfer_buffer.data(),
                        static_cast<std::streamsize>(current_transfer_size));
    if (!output_stream) {
      throw SeqProError(ErrorCode::kIoError,
                        "cannot write subsequence from '" + std::string(sequence_name()) +
                            "' to output stream");
    }
    current_position += static_cast<SequencePosition>(current_transfer_size);
    remaining_length -= static_cast<SequenceLength>(current_transfer_size);
  }
}

SequenceChunkRange FastaSequenceView::SubsequenceChunks(
    SequencePosition sequence_start, SequenceLength subsequence_length) const {
  state_->ValidateRange(sequence_id_, sequence_start, subsequence_length);
  return SequenceChunkRange(state_, sequence_id_, sequence_start,
                            sequence_start + subsequence_length);
}

IndexedFasta::IndexedFasta(std::shared_ptr<const internal::IndexedFastaState> state)
    : state_(std::move(state)) {}

IndexedFasta IndexedFasta::Open(const std::filesystem::path& fasta_path,
                                const IndexedFastaOptions& options) {
  const std::filesystem::path fasta_index_path =
      internal::ResolveFastaIndexPath(fasta_path, options.fasta_index_path);
  internal::MappedFile mapped_fasta =
      internal::MappedFile::OpenReadOnly(fasta_path, options.file_access_pattern);
  internal::ValidatedFastaIndex validated_index = internal::ValidateFastaIndexFiles(
      fasta_path, fasta_index_path, options.index_verification_mode,
      options.require_seqpro_metadata, &mapped_fasta);
  auto state = std::make_shared<const internal::IndexedFastaState>(
      fasta_path, fasta_index_path, std::move(mapped_fasta), std::move(validated_index));
  return IndexedFasta(std::move(state));
}

IndexedFasta IndexedFasta::OpenOrBuildIndex(
    const std::filesystem::path& fasta_path,
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
  const FastaIndexBuildReport build_report = BuildFastaIndex(fasta_path, build_options);
  open_options.fasta_index_path = build_report.fasta_index_path;
  return Open(fasta_path, open_options);
}

const std::filesystem::path& IndexedFasta::fasta_path() const noexcept {
  return state_->fasta_path();
}

const std::filesystem::path& IndexedFasta::fasta_index_path() const noexcept {
  return state_->fasta_index_path();
}

FastaIndexOrigin IndexedFasta::fasta_index_origin() const noexcept {
  return state_->validation_report().index_origin;
}

IndexVerificationStatus IndexedFasta::index_verification_status() const noexcept {
  return state_->validation_report().verification_status;
}

std::size_t IndexedFasta::sequence_count() const noexcept {
  return state_->fasta_index_entries().size();
}

const std::vector<FastaIndexEntry>& IndexedFasta::fasta_index_entries() const noexcept {
  return state_->fasta_index_entries();
}

std::optional<SequenceId> IndexedFasta::FindSequenceId(
    std::string_view sequence_name) const noexcept {
  return state_->FindSequenceId(sequence_name);
}

const FastaIndexEntry& IndexedFasta::IndexEntryById(SequenceId sequence_id) const {
  return state_->IndexEntryById(sequence_id);
}

const FastaIndexEntry& IndexedFasta::IndexEntryByName(
    std::string_view sequence_name) const {
  return state_->IndexEntryByName(sequence_name);
}

FastaSequenceView IndexedFasta::SequenceById(SequenceId sequence_id) const {
  state_->IndexEntryById(sequence_id);
  return FastaSequenceView(state_, sequence_id);
}

FastaSequenceView IndexedFasta::SequenceByName(std::string_view sequence_name) const {
  const std::optional<SequenceId> sequence_id = state_->FindSequenceId(sequence_name);
  if (!sequence_id) {
    throw SeqProError(ErrorCode::kSequenceNotFound,
                      "sequence '" + std::string(sequence_name) +
                          "' does not exist in FASTA '" + fasta_path().string() + "'");
  }
  return FastaSequenceView(state_, *sequence_id);
}

}  // namespace seqpro
