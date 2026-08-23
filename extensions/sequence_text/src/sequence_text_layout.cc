#include "seqpro/sequence_text_layout.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "seqpro/error.h"

namespace seqpro {
namespace {

std::uint64_t CheckedAdd(std::uint64_t left, std::uint64_t right, std::string_view operation) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "integer overflow while " + std::string(operation));
  }
  return left + right;
}

struct ActiveRun {
  SequenceId sequence_id;
  SequenceRunIndex sequence_run_index;
  SequencePosition original_sequence_start;
  SequencePosition original_sequence_end;
  ActiveSequencePosition active_sequence_start;
  SequenceTextPosition text_start;
};

SequenceLength RunLength(const ActiveRun& active_run) noexcept {
  return active_run.original_sequence_end - active_run.original_sequence_start;
}

}  // namespace

class SequenceTextLayout::State {
 public:
  State(IndexedFasta indexed_fasta, std::vector<SequenceId> sequence_order)
      : indexed_fasta_(std::move(indexed_fasta)) {
    const std::size_t sequence_count = indexed_fasta_.sequence_count();
    uses_identity_sequence_order_ = sequence_order.empty();

    if (uses_identity_sequence_order_) {
      sequence_order.reserve(sequence_count);
      for (const FastaIndexEntry& index_entry : indexed_fasta_.fasta_index_entries()) {
        if (static_cast<std::size_t>(index_entry.sequence_id) != sequence_order.size()) {
          throw SeqProError(ErrorCode::kInvalidFastaIndex,
                            "FASTA index contains a non-contiguous sequence ID");
        }
        sequence_order.push_back(index_entry.sequence_id);
      }
    }

    if (!uses_identity_sequence_order_) {
      selected_sequence_index_by_id_.reserve(sequence_order.size());
    }
    original_sequence_lengths_.reserve(sequence_order.size());
    sequence_views_.reserve(sequence_order.size());
    for (const SequenceId sequence_id : sequence_order) {
      if (static_cast<std::size_t>(sequence_id) >= sequence_count) {
        throw SeqProError(
            ErrorCode::kSequenceNotFound,
            "sequence ID " + std::to_string(sequence_id) + " does not exist in the indexed FASTA");
      }
      const std::size_t selected_sequence_index = original_sequence_lengths_.size();
      if (!uses_identity_sequence_order_) {
        const auto selected_sequence_insertion =
            selected_sequence_index_by_id_.emplace(sequence_id, selected_sequence_index);
        if (!selected_sequence_insertion.second) {
          throw SeqProError(ErrorCode::kInvalidArgument,
                            "sequence ID " + std::to_string(sequence_id) +
                                " appears more than once in sequence order");
        }
      }
      original_sequence_lengths_.push_back(
          indexed_fasta_.IndexEntryById(sequence_id).sequence_length);
      sequence_views_.push_back(indexed_fasta_.SequenceById(sequence_id));
    }

    sequence_order_ = std::move(sequence_order);
    excluded_intervals_.resize(sequence_order_.size());
    active_runs_by_sequence_.resize(sequence_order_.size());
    active_sequence_lengths_.resize(sequence_order_.size(), 0);
    excluded_base_counts_.resize(sequence_order_.size(), 0);
    Finalize();
  }

  const IndexedFasta& indexed_fasta() const noexcept { return indexed_fasta_; }

  const std::vector<SequenceId>& sequence_order() const noexcept { return sequence_order_; }

  bool is_finalized() const noexcept { return is_finalized_; }

  SequenceTextGeneration layout_generation() const noexcept { return layout_generation_; }

  void ExcludeInterval(SequenceId sequence_id, SequencePosition sequence_start,
                       SequencePosition sequence_end) {
    const std::size_t selected_sequence_index =
        ValidateOriginalInterval(sequence_id, sequence_start, sequence_end);
    std::vector<OriginalSequenceInterval>& sequence_intervals =
        excluded_intervals_[selected_sequence_index];
    sequence_intervals.push_back(OriginalSequenceInterval{sequence_start, sequence_end});
    is_finalized_ = false;
  }

  void ExcludeInterval(std::string_view sequence_name, SequencePosition sequence_start,
                       SequencePosition sequence_end) {
    ExcludeInterval(SelectedSequenceIdByName(sequence_name), sequence_start, sequence_end);
  }

  void ExcludeIntervals(const std::vector<ExcludedSequenceInterval>& intervals) {
    if (intervals.empty()) {
      return;
    }

    std::vector<std::size_t> additions(excluded_intervals_.size(), 0);
    for (const ExcludedSequenceInterval& interval : intervals) {
      const std::size_t selected_sequence_index = ValidateOriginalInterval(
          interval.sequence_id, interval.sequence_start, interval.sequence_end);
      std::size_t& addition_count = additions[selected_sequence_index];
      if (addition_count == std::numeric_limits<std::size_t>::max()) {
        throw SeqProError(ErrorCode::kIntegerOverflow, "too many excluded intervals in one batch");
      }
      ++addition_count;
    }

    for (std::size_t selected_sequence_index = 0; selected_sequence_index < additions.size();
         ++selected_sequence_index) {
      if (additions[selected_sequence_index] == 0) {
        continue;
      }
      std::vector<OriginalSequenceInterval>& sequence_intervals =
          excluded_intervals_[selected_sequence_index];
      if (additions[selected_sequence_index] >
          sequence_intervals.max_size() - sequence_intervals.size()) {
        throw SeqProError(ErrorCode::kIntegerOverflow, "excluded interval vector size overflow");
      }
      sequence_intervals.reserve(sequence_intervals.size() + additions[selected_sequence_index]);
    }

    for (const ExcludedSequenceInterval& interval : intervals) {
      const std::size_t selected_sequence_index = RequireSelectedSequence(interval.sequence_id);
      excluded_intervals_[selected_sequence_index].push_back(
          OriginalSequenceInterval{interval.sequence_start, interval.sequence_end});
    }
    is_finalized_ = false;
  }

  void ExcludeTextIntervals(SequenceTextGeneration source_generation,
                            const std::vector<SequenceTextInterval>& intervals) {
    EnsureFinalized();
    if (source_generation != layout_generation_) {
      throw SeqProError(ErrorCode::kInvalidArgument, "sequence text generation " +
                                                         std::to_string(source_generation) +
                                                         " does not match current generation " +
                                                         std::to_string(layout_generation_));
    }
    if (intervals.empty()) {
      return;
    }

    std::vector<ExcludedSequenceInterval> converted_intervals;
    converted_intervals.reserve(intervals.size());
    for (const SequenceTextInterval& interval : intervals) {
      const std::optional<LocatedSequenceInterval> located_interval =
          LocateTextInterval(interval.text_start, interval.text_length);
      if (!located_interval) {
        const SequenceTextPosition text_end =
            CheckedAdd(interval.text_start, interval.text_length,
                       "formatting an invalid sequence text interval");
        throw SeqProError(ErrorCode::kInvalidArgument,
                          "sequence text interval [" + std::to_string(interval.text_start) + ", " +
                              std::to_string(text_end) + ") is not contained in one active run");
      }
      const SequencePosition sequence_end =
          CheckedAdd(located_interval->original_sequence_start, located_interval->interval_length,
                     "converting a sequence text interval to original coordinates");
      converted_intervals.push_back(ExcludedSequenceInterval{
          located_interval->sequence_id, located_interval->original_sequence_start, sequence_end});
    }
    ExcludeIntervals(converted_intervals);
  }

  void ClearExcludedIntervals(SequenceId sequence_id) {
    const std::size_t selected_sequence_index = RequireSelectedSequence(sequence_id);
    if (excluded_intervals_[selected_sequence_index].empty()) {
      return;
    }
    excluded_intervals_[selected_sequence_index].clear();
    is_finalized_ = false;
  }

  void ClearExcludedIntervals(std::string_view sequence_name) {
    ClearExcludedIntervals(SelectedSequenceIdByName(sequence_name));
  }

  void ClearAllExcludedIntervals() {
    bool removed_any_interval = false;
    for (std::size_t selected_sequence_index = 0; selected_sequence_index < sequence_order_.size();
         ++selected_sequence_index) {
      std::vector<OriginalSequenceInterval>& sequence_intervals =
          excluded_intervals_[selected_sequence_index];
      if (!sequence_intervals.empty()) {
        sequence_intervals.clear();
        removed_any_interval = true;
      }
    }
    if (removed_any_interval) {
      is_finalized_ = false;
    }
  }

  void Finalize() {
    if (is_finalized_) {
      return;
    }
    if (layout_generation_ == std::numeric_limits<SequenceTextGeneration>::max()) {
      throw SeqProError(ErrorCode::kIntegerOverflow, "sequence text generation overflow");
    }

    std::vector<std::vector<OriginalSequenceInterval>> normalized_intervals = excluded_intervals_;
    std::vector<std::vector<ActiveRun>> new_active_runs_by_sequence(excluded_intervals_.size());
    std::vector<SequenceLength> new_active_sequence_lengths(excluded_intervals_.size(), 0);
    std::vector<SequenceLength> new_excluded_base_counts(excluded_intervals_.size(), 0);
    std::vector<ActiveRun> new_global_runs;

    std::size_t estimated_run_count = sequence_order_.size();
    for (const std::vector<OriginalSequenceInterval>& sequence_intervals : normalized_intervals) {
      const std::size_t interval_count = sequence_intervals.size();
      if (interval_count > std::numeric_limits<std::size_t>::max() - estimated_run_count) {
        throw SeqProError(ErrorCode::kIntegerOverflow, "active run count estimate overflow");
      }
      estimated_run_count += interval_count;
    }
    if (estimated_run_count > new_global_runs.max_size()) {
      throw SeqProError(ErrorCode::kIntegerOverflow, "active run count exceeds vector capacity");
    }
    new_global_runs.reserve(estimated_run_count);

    SequenceTextPosition next_text_position = 0;
    SequenceLength new_active_base_count = 0;

    for (std::size_t selected_sequence_index = 0; selected_sequence_index < sequence_order_.size();
         ++selected_sequence_index) {
      const SequenceId sequence_id = sequence_order_[selected_sequence_index];
      const SequenceLength original_sequence_length =
          original_sequence_lengths_[selected_sequence_index];
      std::vector<OriginalSequenceInterval>& sequence_intervals =
          normalized_intervals[selected_sequence_index];

      std::sort(sequence_intervals.begin(), sequence_intervals.end(),
                [](const OriginalSequenceInterval& left, const OriginalSequenceInterval& right) {
                  if (left.sequence_start != right.sequence_start) {
                    return left.sequence_start < right.sequence_start;
                  }
                  return left.sequence_end < right.sequence_end;
                });

      std::vector<OriginalSequenceInterval> merged_intervals;
      merged_intervals.reserve(sequence_intervals.size());
      for (const OriginalSequenceInterval& interval : sequence_intervals) {
        ValidateOriginalInterval(sequence_id, interval.sequence_start, interval.sequence_end);
        if (merged_intervals.empty() ||
            merged_intervals.back().sequence_end < interval.sequence_start) {
          merged_intervals.push_back(interval);
        } else {
          merged_intervals.back().sequence_end =
              std::max(merged_intervals.back().sequence_end, interval.sequence_end);
        }
      }
      sequence_intervals = std::move(merged_intervals);

      std::vector<ActiveRun>& sequence_runs = new_active_runs_by_sequence[selected_sequence_index];
      if (sequence_intervals.size() >= sequence_runs.max_size()) {
        throw SeqProError(ErrorCode::kIntegerOverflow, "active run count exceeds vector capacity");
      }
      sequence_runs.reserve(sequence_intervals.size() + 1U);
      SequencePosition next_original_position = 0;
      ActiveSequencePosition next_active_position = 0;

      const auto append_active_run = [&](SequencePosition run_start, SequencePosition run_end) {
        if (run_start >= run_end) {
          return;
        }
        if (sequence_runs.size() >
            static_cast<std::size_t>(std::numeric_limits<SequenceRunIndex>::max())) {
          throw SeqProError(ErrorCode::kIntegerOverflow,
                            "active run count exceeds SequenceRunIndex capacity");
        }
        const SequenceRunIndex sequence_run_index =
            static_cast<SequenceRunIndex>(sequence_runs.size());
        const ActiveRun active_run{sequence_id, sequence_run_index,   run_start,
                                   run_end,     next_active_position, next_text_position};
        sequence_runs.push_back(active_run);
        new_global_runs.push_back(active_run);

        const SequenceLength run_length = run_end - run_start;
        next_active_position = CheckedAdd(next_active_position, run_length,
                                          "computing an active sequence position prefix");
        new_active_base_count = CheckedAdd(new_active_base_count, run_length,
                                           "counting active bases across selected sequences");
        next_text_position =
            CheckedAdd(next_text_position, run_length, "computing a sequence text run boundary");
        next_text_position =
            CheckedAdd(next_text_position, 1U, "reserving a sequence text separator");
      };

      for (const OriginalSequenceInterval& interval : sequence_intervals) {
        append_active_run(next_original_position, interval.sequence_start);
        next_original_position = interval.sequence_end;
      }
      append_active_run(next_original_position, original_sequence_length);

      new_active_sequence_lengths[selected_sequence_index] = next_active_position;
      new_excluded_base_counts[selected_sequence_index] =
          original_sequence_length - next_active_position;
    }

    const SequenceTextLength new_text_size =
        CheckedAdd(next_text_position, 1U, "reserving the sequence text terminator");
    const SequenceTextGeneration new_generation = layout_generation_ + 1U;

    excluded_intervals_.swap(normalized_intervals);
    active_runs_by_sequence_.swap(new_active_runs_by_sequence);
    active_sequence_lengths_.swap(new_active_sequence_lengths);
    excluded_base_counts_.swap(new_excluded_base_counts);
    global_runs_.swap(new_global_runs);
    active_base_count_ = new_active_base_count;
    text_size_ = new_text_size;
    layout_generation_ = new_generation;
    is_finalized_ = true;
  }

  SequenceTextLength text_size() const {
    EnsureFinalized();
    return text_size_;
  }

  SequenceLength active_base_count() const {
    EnsureFinalized();
    return active_base_count_;
  }

  std::size_t active_run_count() const {
    EnsureFinalized();
    return global_runs_.size();
  }

  SequenceLength ActiveSequenceLength(SequenceId sequence_id) const {
    EnsureFinalized();
    return active_sequence_lengths_[RequireSelectedSequence(sequence_id)];
  }

  SequenceLength ExcludedBaseCount(SequenceId sequence_id) const {
    EnsureFinalized();
    return excluded_base_counts_[RequireSelectedSequence(sequence_id)];
  }

  std::vector<OriginalSequenceInterval> ActiveIntervalsById(SequenceId sequence_id) const {
    EnsureFinalized();
    const std::vector<ActiveRun>& sequence_runs =
        active_runs_by_sequence_[RequireSelectedSequence(sequence_id)];
    std::vector<OriginalSequenceInterval> active_intervals;
    active_intervals.reserve(sequence_runs.size());
    for (const ActiveRun& active_run : sequence_runs) {
      active_intervals.push_back(OriginalSequenceInterval{active_run.original_sequence_start,
                                                          active_run.original_sequence_end});
    }
    return active_intervals;
  }

  std::vector<OriginalSequenceInterval> ExcludedIntervalsById(SequenceId sequence_id) const {
    EnsureFinalized();
    return excluded_intervals_[RequireSelectedSequence(sequence_id)];
  }

  std::optional<ActiveSequencePosition> FindActiveSequencePosition(
      SequenceId sequence_id, SequencePosition original_sequence_position) const {
    EnsureFinalized();
    const std::size_t selected_sequence_index = RequireSelectedSequence(sequence_id);
    ValidateOriginalPosition(sequence_id, original_sequence_position);
    const ActiveRun* const active_run =
        FindRunByOriginalPosition(selected_sequence_index, original_sequence_position);
    if (active_run == nullptr) {
      return std::nullopt;
    }
    return CheckedAdd(active_run->active_sequence_start,
                      original_sequence_position - active_run->original_sequence_start,
                      "mapping an original position to an active position");
  }

  SequencePosition OriginalSequencePosition(SequenceId sequence_id,
                                            ActiveSequencePosition active_sequence_position) const {
    EnsureFinalized();
    const std::size_t selected_sequence_index = RequireSelectedSequence(sequence_id);
    if (active_sequence_position >= active_sequence_lengths_[selected_sequence_index]) {
      throw SeqProError(ErrorCode::kSequenceRangeOutOfBounds,
                        "active position " + std::to_string(active_sequence_position) +
                            " is outside selected sequence ID " + std::to_string(sequence_id));
    }
    const ActiveRun& active_run =
        RunByActivePosition(selected_sequence_index, active_sequence_position);
    return CheckedAdd(active_run.original_sequence_start,
                      active_sequence_position - active_run.active_sequence_start,
                      "mapping an active position to an original position");
  }

  std::optional<SequenceTextPosition> FindTextPosition(
      SequenceId sequence_id, SequencePosition original_sequence_position) const {
    EnsureFinalized();
    const std::size_t selected_sequence_index = RequireSelectedSequence(sequence_id);
    ValidateOriginalPosition(sequence_id, original_sequence_position);
    const ActiveRun* const active_run =
        FindRunByOriginalPosition(selected_sequence_index, original_sequence_position);
    if (active_run == nullptr) {
      return std::nullopt;
    }
    return CheckedAdd(active_run->text_start,
                      original_sequence_position - active_run->original_sequence_start,
                      "mapping an original position to a sequence text position");
  }

  SequenceTextPosition TextPositionFromActive(
      SequenceId sequence_id, ActiveSequencePosition active_sequence_position) const {
    EnsureFinalized();
    const std::size_t selected_sequence_index = RequireSelectedSequence(sequence_id);
    if (active_sequence_position >= active_sequence_lengths_[selected_sequence_index]) {
      throw SeqProError(ErrorCode::kSequenceRangeOutOfBounds,
                        "active position " + std::to_string(active_sequence_position) +
                            " is outside selected sequence ID " + std::to_string(sequence_id));
    }
    const ActiveRun& active_run =
        RunByActivePosition(selected_sequence_index, active_sequence_position);
    return CheckedAdd(active_run.text_start,
                      active_sequence_position - active_run.active_sequence_start,
                      "mapping an active position to a sequence text position");
  }

  SequenceTextLocation LocateTextPosition(SequenceTextPosition text_position) const {
    EnsureFinalized();
    if (text_position >= text_size_) {
      throw SeqProError(ErrorCode::kSequenceRangeOutOfBounds,
                        "sequence text position " + std::to_string(text_position) +
                            " is outside text of length " + std::to_string(text_size_));
    }
    if (text_position == text_size_ - 1U) {
      return SequenceTextTerminatorLocation{};
    }

    const ActiveRun& active_run = GlobalRunForPosition(text_position);
    const SequenceLength run_length = RunLength(active_run);
    const SequenceTextPosition run_end =
        CheckedAdd(active_run.text_start, run_length, "locating a sequence text run boundary");
    if (text_position == run_end) {
      return SequenceTextSeparatorLocation{active_run.sequence_id, active_run.sequence_run_index};
    }
    if (text_position > run_end) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "sequence text run table is internally inconsistent");
    }

    const SequenceTextPosition run_offset = text_position - active_run.text_start;
    return SequenceTextBaseLocation{active_run.sequence_id, active_run.sequence_run_index,
                                    CheckedAdd(active_run.original_sequence_start, run_offset,
                                               "locating an original sequence position"),
                                    CheckedAdd(active_run.active_sequence_start, run_offset,
                                               "locating an active sequence position")};
  }

  std::optional<LocatedSequenceInterval> LocateTextInterval(SequenceTextPosition text_start,
                                                            SequenceTextLength text_length) const {
    EnsureFinalized();
    if (text_length == 0) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "sequence text interval length must be greater than zero");
    }
    if (text_length > std::numeric_limits<SequenceTextPosition>::max() - text_start) {
      throw SeqProError(ErrorCode::kIntegerOverflow, "sequence text interval end overflow");
    }
    if (text_start >= text_size_ || text_length > text_size_ - text_start ||
        text_start == text_size_ - 1U) {
      return std::nullopt;
    }

    const ActiveRun& active_run = GlobalRunForPosition(text_start);
    const SequenceTextPosition run_end = CheckedAdd(active_run.text_start, RunLength(active_run),
                                                    "locating a sequence text interval boundary");
    if (text_start >= run_end || text_length > run_end - text_start) {
      return std::nullopt;
    }

    const SequenceTextPosition run_offset = text_start - active_run.text_start;
    return LocatedSequenceInterval{active_run.sequence_id, active_run.sequence_run_index,
                                   CheckedAdd(active_run.original_sequence_start, run_offset,
                                              "locating an original sequence interval"),
                                   CheckedAdd(active_run.active_sequence_start, run_offset,
                                              "locating an active sequence interval"),
                                   text_length};
  }

  std::uint8_t ReadTextByte(SequenceTextPosition text_position) const {
    const SequenceTextLocation location = LocateTextPosition(text_position);
    if (const auto* base_location = std::get_if<SequenceTextBaseLocation>(&location)) {
      const char base = SequenceView(base_location->sequence_id)
                            .ReadBase(base_location->original_sequence_position);
      ValidateReservedByte(base, base_location->sequence_id,
                           base_location->original_sequence_position);
      return static_cast<std::uint8_t>(static_cast<unsigned char>(base));
    }
    if (std::holds_alternative<SequenceTextSeparatorLocation>(location)) {
      return SequenceTextLayout::kSeparatorByte;
    }
    return SequenceTextLayout::kTerminatorByte;
  }

  MaterializedSequenceText Materialize() const {
    EnsureFinalized();
    if (text_size_ > static_cast<SequenceTextLength>(std::numeric_limits<std::size_t>::max())) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "sequence text cannot be represented as std::string");
    }
    std::string bytes;
    if (text_size_ > static_cast<SequenceTextLength>(bytes.max_size())) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "sequence text exceeds std::string::max_size()");
    }
    bytes.resize(static_cast<std::size_t>(text_size_));
    CopyTextTo(bytes.data(), bytes.size());
    return MaterializedSequenceText{std::move(bytes), layout_generation_};
  }

  void CopyTextTo(char* destination, std::size_t destination_size) const {
    EnsureFinalized();
    if (text_size_ > static_cast<SequenceTextLength>(std::numeric_limits<std::size_t>::max())) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "sequence text cannot be represented by a caller buffer");
    }
    const std::size_t required_size = static_cast<std::size_t>(text_size_);
    if (destination_size != required_size) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "CopyTextTo destination size " + std::to_string(destination_size) +
                            " does not equal sequence text size " + std::to_string(required_size));
    }
    if (destination == nullptr) {
      throw SeqProError(ErrorCode::kInvalidArgument, "CopyTextTo destination is null");
    }

    for (const ActiveRun& active_run : global_runs_) {
      const SequenceLength run_length = RunLength(active_run);
      if (run_length > static_cast<SequenceLength>(std::numeric_limits<std::size_t>::max())) {
        throw SeqProError(ErrorCode::kIntegerOverflow,
                          "active run cannot be represented by a caller buffer");
      }
      const std::size_t text_offset = static_cast<std::size_t>(active_run.text_start);
      const std::size_t copy_size = static_cast<std::size_t>(run_length);
      SequenceView(active_run.sequence_id)
          .CopySubsequenceTo(active_run.original_sequence_start, destination + text_offset,
                             copy_size);
      ValidateReservedBytes(destination + text_offset, copy_size, active_run.sequence_id,
                            active_run.original_sequence_start);
      destination[text_offset + copy_size] = static_cast<char>(SequenceTextLayout::kSeparatorByte);
    }
    destination[required_size - 1U] = static_cast<char>(SequenceTextLayout::kTerminatorByte);
  }

  void WriteTo(std::ostream& output_stream, std::size_t transfer_buffer_size) const {
    EnsureFinalized();
    if (transfer_buffer_size == 0) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "WriteTo transfer buffer size must be greater than zero");
    }

    const std::uint64_t maximum_stream_write_size =
        static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max());
    const std::uint64_t bounded_buffer_size =
        std::min<std::uint64_t>(transfer_buffer_size, maximum_stream_write_size);
    const std::uint64_t effective_buffer_size =
        std::min<std::uint64_t>(bounded_buffer_size, active_base_count_);
    std::vector<char> transfer_buffer;
    if (effective_buffer_size > transfer_buffer.max_size()) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "WriteTo transfer buffer exceeds vector capacity");
    }
    transfer_buffer.resize(static_cast<std::size_t>(effective_buffer_size));

    for (const ActiveRun& active_run : global_runs_) {
      SequencePosition current_position = active_run.original_sequence_start;
      SequenceLength remaining_length = RunLength(active_run);
      while (remaining_length != 0) {
        const std::size_t current_transfer_size = static_cast<std::size_t>(
            std::min<std::uint64_t>(transfer_buffer.size(), remaining_length));
        SequenceView(active_run.sequence_id)
            .CopySubsequenceTo(current_position, transfer_buffer.data(), current_transfer_size);
        ValidateReservedBytes(transfer_buffer.data(), current_transfer_size, active_run.sequence_id,
                              current_position);
        output_stream.write(transfer_buffer.data(),
                            static_cast<std::streamsize>(current_transfer_size));
        if (!output_stream) {
          throw SeqProError(ErrorCode::kIoError,
                            "cannot write active sequence bases to output stream");
        }
        current_position = CheckedAdd(current_position, current_transfer_size,
                                      "advancing a streamed original sequence position");
        remaining_length -= static_cast<SequenceLength>(current_transfer_size);
      }
      output_stream.put(static_cast<char>(SequenceTextLayout::kSeparatorByte));
      if (!output_stream) {
        throw SeqProError(ErrorCode::kIoError,
                          "cannot write sequence text separator to output stream");
      }
    }

    output_stream.put(static_cast<char>(SequenceTextLayout::kTerminatorByte));
    if (!output_stream) {
      throw SeqProError(ErrorCode::kIoError,
                        "cannot write sequence text terminator to output stream");
    }
  }

 private:
  void EnsureFinalized() const {
    if (!is_finalized_) {
      throw SeqProError(
          ErrorCode::kInvalidArgument,
          "SequenceTextLayout contains unfinalized interval changes; call Finalize() first");
    }
  }

  std::size_t RequireSelectedSequence(SequenceId sequence_id) const {
    if (uses_identity_sequence_order_) {
      const std::size_t selected_sequence_index = static_cast<std::size_t>(sequence_id);
      if (selected_sequence_index < sequence_order_.size()) {
        return selected_sequence_index;
      }
      throw SeqProError(ErrorCode::kSequenceNotFound, "sequence ID " + std::to_string(sequence_id) +
                                                          " does not exist in the indexed FASTA");
    }
    const auto selected_sequence_iterator = selected_sequence_index_by_id_.find(sequence_id);
    if (selected_sequence_iterator != selected_sequence_index_by_id_.end()) {
      return selected_sequence_iterator->second;
    }
    if (static_cast<std::size_t>(sequence_id) >= indexed_fasta_.sequence_count()) {
      throw SeqProError(ErrorCode::kSequenceNotFound, "sequence ID " + std::to_string(sequence_id) +
                                                          " does not exist in the indexed FASTA");
    }
    throw SeqProError(ErrorCode::kSequenceNotFound,
                      "sequence ID " + std::to_string(sequence_id) +
                          " is not selected in this SequenceTextLayout");
  }

  SequenceId SelectedSequenceIdByName(std::string_view sequence_name) const {
    const std::optional<SequenceId> sequence_id = indexed_fasta_.FindSequenceId(sequence_name);
    if (!sequence_id) {
      throw SeqProError(ErrorCode::kSequenceNotFound, "sequence '" + std::string(sequence_name) +
                                                          "' does not exist in the indexed FASTA");
    }
    RequireSelectedSequence(*sequence_id);
    return *sequence_id;
  }

  std::size_t ValidateOriginalInterval(SequenceId sequence_id, SequencePosition sequence_start,
                                       SequencePosition sequence_end) const {
    const std::size_t selected_sequence_index = RequireSelectedSequence(sequence_id);
    if (sequence_start >= sequence_end) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "excluded interval must have start smaller than end");
    }
    if (sequence_end > original_sequence_lengths_[selected_sequence_index]) {
      throw SeqProError(ErrorCode::kSequenceRangeOutOfBounds,
                        "excluded interval [" + std::to_string(sequence_start) + ", " +
                            std::to_string(sequence_end) + ") exceeds sequence ID " +
                            std::to_string(sequence_id) + " of length " +
                            std::to_string(original_sequence_lengths_[selected_sequence_index]));
    }
    return selected_sequence_index;
  }

  void ValidateOriginalPosition(SequenceId sequence_id,
                                SequencePosition original_sequence_position) const {
    const std::size_t selected_sequence_index = RequireSelectedSequence(sequence_id);
    if (original_sequence_position >= original_sequence_lengths_[selected_sequence_index]) {
      throw SeqProError(ErrorCode::kSequenceRangeOutOfBounds,
                        "original position " + std::to_string(original_sequence_position) +
                            " is outside sequence ID " + std::to_string(sequence_id));
    }
  }

  const FastaSequenceView& SequenceView(SequenceId sequence_id) const {
    return sequence_views_[RequireSelectedSequence(sequence_id)];
  }

  const ActiveRun* FindRunByOriginalPosition(
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      std::size_t selected_sequence_index, SequencePosition original_sequence_position) const {
    const std::vector<ActiveRun>& sequence_runs = active_runs_by_sequence_[selected_sequence_index];
    auto run_iterator =
        std::upper_bound(sequence_runs.begin(), sequence_runs.end(), original_sequence_position,
                         [](SequencePosition position, const ActiveRun& active_run) {
                           return position < active_run.original_sequence_start;
                         });
    if (run_iterator == sequence_runs.begin()) {
      return nullptr;
    }
    --run_iterator;
    if (original_sequence_position >= run_iterator->original_sequence_end) {
      return nullptr;
    }
    return &*run_iterator;
  }

  const ActiveRun& RunByActivePosition(
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      std::size_t selected_sequence_index, ActiveSequencePosition active_sequence_position) const {
    const std::vector<ActiveRun>& sequence_runs = active_runs_by_sequence_[selected_sequence_index];
    auto run_iterator =
        std::upper_bound(sequence_runs.begin(), sequence_runs.end(), active_sequence_position,
                         [](ActiveSequencePosition position, const ActiveRun& active_run) {
                           return position < active_run.active_sequence_start;
                         });
    if (run_iterator == sequence_runs.begin()) {
      throw SeqProError(ErrorCode::kInvalidArgument, "active run table is internally inconsistent");
    }
    --run_iterator;
    return *run_iterator;
  }

  const ActiveRun& GlobalRunForPosition(SequenceTextPosition text_position) const {
    auto run_iterator =
        std::upper_bound(global_runs_.begin(), global_runs_.end(), text_position,
                         [](SequenceTextPosition position, const ActiveRun& active_run) {
                           return position < active_run.text_start;
                         });
    if (run_iterator == global_runs_.begin()) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "sequence text run table is internally inconsistent");
    }
    --run_iterator;
    return *run_iterator;
  }

  void ValidateReservedByte(
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      char base, SequenceId sequence_id, SequencePosition sequence_position) const {
    const std::uint8_t byte = static_cast<std::uint8_t>(static_cast<unsigned char>(base));
    if (byte == SequenceTextLayout::kSeparatorByte || byte == SequenceTextLayout::kTerminatorByte) {
      throw SeqProError(ErrorCode::kUnsupportedFileFormat,
                        "FASTA sequence ID " + std::to_string(sequence_id) +
                            " contains reserved sequence text byte " + std::to_string(byte) +
                            " at original position " + std::to_string(sequence_position));
    }
  }

  void ValidateReservedBytes(
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      const char* bases, std::size_t base_count, SequenceId sequence_id,
      SequencePosition sequence_start) const {
    if (base_count == 0) {
      return;
    }
    const auto* const separator = static_cast<const char*>(
        std::memchr(bases, SequenceTextLayout::kSeparatorByte, base_count));
    const auto* const terminator = static_cast<const char*>(
        std::memchr(bases, SequenceTextLayout::kTerminatorByte, base_count));
    const char* reserved_byte = separator;
    if (reserved_byte == nullptr || (terminator != nullptr && terminator < reserved_byte)) {
      reserved_byte = terminator;
    }
    if (reserved_byte != nullptr) {
      const std::size_t base_index = static_cast<std::size_t>(reserved_byte - bases);
      ValidateReservedByte(
          *reserved_byte, sequence_id,
          CheckedAdd(sequence_start, base_index, "locating a reserved FASTA byte"));
    }
  }

  IndexedFasta indexed_fasta_;
  std::vector<SequenceId> sequence_order_;
  std::unordered_map<SequenceId, std::size_t> selected_sequence_index_by_id_;
  std::vector<SequenceLength> original_sequence_lengths_;
  std::vector<FastaSequenceView> sequence_views_;
  std::vector<std::vector<OriginalSequenceInterval>> excluded_intervals_;
  std::vector<std::vector<ActiveRun>> active_runs_by_sequence_;
  std::vector<ActiveRun> global_runs_;
  std::vector<SequenceLength> active_sequence_lengths_;
  std::vector<SequenceLength> excluded_base_counts_;
  SequenceLength active_base_count_ = 0;
  SequenceTextLength text_size_ = 0;
  SequenceTextGeneration layout_generation_ = 0;
  bool is_finalized_ = false;
  bool uses_identity_sequence_order_ = false;
};

SequenceTextLayout::SequenceTextLayout(IndexedFasta indexed_fasta,
                                       std::vector<SequenceId> sequence_order)
    : state_(std::make_unique<State>(std::move(indexed_fasta), std::move(sequence_order))) {}

SequenceTextLayout::~SequenceTextLayout() = default;

SequenceTextLayout::SequenceTextLayout(SequenceTextLayout&&) noexcept = default;

SequenceTextLayout& SequenceTextLayout::operator=(SequenceTextLayout&&) noexcept = default;

const IndexedFasta& SequenceTextLayout::indexed_fasta() const noexcept {
  return state_->indexed_fasta();
}

const std::vector<SequenceId>& SequenceTextLayout::sequence_order() const noexcept {
  return state_->sequence_order();
}

bool SequenceTextLayout::is_finalized() const noexcept { return state_->is_finalized(); }

SequenceTextGeneration SequenceTextLayout::layout_generation() const noexcept {
  return state_->layout_generation();
}

void SequenceTextLayout::ExcludeInterval(SequenceId sequence_id, SequencePosition sequence_start,
                                         SequencePosition sequence_end) {
  state_->ExcludeInterval(sequence_id, sequence_start, sequence_end);
}

void SequenceTextLayout::ExcludeInterval(std::string_view sequence_name,
                                         SequencePosition sequence_start,
                                         SequencePosition sequence_end) {
  state_->ExcludeInterval(sequence_name, sequence_start, sequence_end);
}

void SequenceTextLayout::ExcludeIntervals(const std::vector<ExcludedSequenceInterval>& intervals) {
  state_->ExcludeIntervals(intervals);
}

void SequenceTextLayout::ExcludeTextIntervals(SequenceTextGeneration source_generation,
                                              const std::vector<SequenceTextInterval>& intervals) {
  state_->ExcludeTextIntervals(source_generation, intervals);
}

void SequenceTextLayout::ClearExcludedIntervals(SequenceId sequence_id) {
  state_->ClearExcludedIntervals(sequence_id);
}

void SequenceTextLayout::ClearExcludedIntervals(std::string_view sequence_name) {
  state_->ClearExcludedIntervals(sequence_name);
}

void SequenceTextLayout::ClearAllExcludedIntervals() { state_->ClearAllExcludedIntervals(); }

void SequenceTextLayout::Finalize() { state_->Finalize(); }

SequenceTextLength SequenceTextLayout::text_size() const { return state_->text_size(); }

SequenceLength SequenceTextLayout::active_base_count() const { return state_->active_base_count(); }

std::size_t SequenceTextLayout::active_run_count() const { return state_->active_run_count(); }

SequenceLength SequenceTextLayout::ActiveSequenceLength(SequenceId sequence_id) const {
  return state_->ActiveSequenceLength(sequence_id);
}

SequenceLength SequenceTextLayout::ExcludedBaseCount(SequenceId sequence_id) const {
  return state_->ExcludedBaseCount(sequence_id);
}

std::vector<OriginalSequenceInterval> SequenceTextLayout::ActiveIntervalsById(
    SequenceId sequence_id) const {
  return state_->ActiveIntervalsById(sequence_id);
}

std::vector<OriginalSequenceInterval> SequenceTextLayout::ExcludedIntervalsById(
    SequenceId sequence_id) const {
  return state_->ExcludedIntervalsById(sequence_id);
}

std::optional<ActiveSequencePosition> SequenceTextLayout::FindActiveSequencePosition(
    SequenceId sequence_id, SequencePosition original_sequence_position) const {
  return state_->FindActiveSequencePosition(sequence_id, original_sequence_position);
}

SequencePosition SequenceTextLayout::OriginalSequencePosition(
    SequenceId sequence_id, ActiveSequencePosition active_sequence_position) const {
  return state_->OriginalSequencePosition(sequence_id, active_sequence_position);
}

std::optional<SequenceTextPosition> SequenceTextLayout::FindTextPosition(
    SequenceId sequence_id, SequencePosition original_sequence_position) const {
  return state_->FindTextPosition(sequence_id, original_sequence_position);
}

SequenceTextPosition SequenceTextLayout::TextPositionFromActive(
    SequenceId sequence_id, ActiveSequencePosition active_sequence_position) const {
  return state_->TextPositionFromActive(sequence_id, active_sequence_position);
}

SequenceTextLocation SequenceTextLayout::LocateTextPosition(
    SequenceTextPosition text_position) const {
  return state_->LocateTextPosition(text_position);
}

std::optional<LocatedSequenceInterval> SequenceTextLayout::LocateTextInterval(
    SequenceTextPosition text_start, SequenceTextLength text_length) const {
  return state_->LocateTextInterval(text_start, text_length);
}

std::uint8_t SequenceTextLayout::ReadTextByte(SequenceTextPosition text_position) const {
  return state_->ReadTextByte(text_position);
}

MaterializedSequenceText SequenceTextLayout::Materialize() const { return state_->Materialize(); }

void SequenceTextLayout::CopyTextTo(char* destination, std::size_t destination_size) const {
  state_->CopyTextTo(destination, destination_size);
}

void SequenceTextLayout::WriteTo(std::ostream& output_stream,
                                 std::size_t transfer_buffer_size) const {
  state_->WriteTo(output_stream, transfer_buffer_size);
}

}  // namespace seqpro
