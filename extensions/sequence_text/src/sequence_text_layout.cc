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
  SequencePosition original_sequence_start_position;
  SequencePosition original_sequence_end_position;
  ActiveSequencePosition active_sequence_start_position;
  SequenceTextPosition text_start_position;
};

SequenceLength ActiveRunLength(const ActiveRun& active_run) noexcept {
  return active_run.original_sequence_end_position - active_run.original_sequence_start_position;
}

}  // namespace

class SequenceTextLayout::State {
 public:
  State(IndexedFasta indexed_fasta, std::vector<SequenceId> selected_sequence_order)
      : indexed_fasta_(std::move(indexed_fasta)) {
    const std::size_t sequence_count = indexed_fasta_.sequence_count();
    uses_identity_sequence_order_ = selected_sequence_order.empty();

    if (uses_identity_sequence_order_) {
      selected_sequence_order.reserve(sequence_count);
      for (const FastaIndexEntry& index_entry : indexed_fasta_.fasta_index_entries()) {
        if (static_cast<std::size_t>(index_entry.sequence_id) != selected_sequence_order.size()) {
          throw SeqProError(ErrorCode::kInvalidFastaIndex,
                            "FASTA index contains a non-contiguous sequence ID");
        }
        selected_sequence_order.push_back(index_entry.sequence_id);
      }
    }

    if (!uses_identity_sequence_order_) {
      selected_sequence_index_by_id_.reserve(selected_sequence_order.size());
    }
    original_sequence_lengths_.reserve(selected_sequence_order.size());
    sequence_views_.reserve(selected_sequence_order.size());
    for (const SequenceId sequence_id : selected_sequence_order) {
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

    sequence_order_ = std::move(selected_sequence_order);
    excluded_intervals_by_sequence_.resize(sequence_order_.size());
    active_runs_by_sequence_.resize(sequence_order_.size());
    active_sequence_lengths_.resize(sequence_order_.size(), 0);
    excluded_base_counts_.resize(sequence_order_.size(), 0);
    Finalize();
  }

  const IndexedFasta& indexed_fasta() const noexcept { return indexed_fasta_; }

  const std::vector<SequenceId>& sequence_order() const noexcept { return sequence_order_; }

  bool is_finalized() const noexcept { return is_finalized_; }

  SequenceTextGeneration layout_generation() const noexcept { return layout_generation_; }

  void ExcludeInterval(SequenceId sequence_id, SequencePosition sequence_start_position,
                       SequencePosition sequence_end_position) {
    const std::size_t selected_sequence_index =
        ValidateOriginalInterval(sequence_id, sequence_start_position, sequence_end_position);
    std::vector<OriginalSequenceInterval>& excluded_intervals_for_sequence =
        excluded_intervals_by_sequence_[selected_sequence_index];
    excluded_intervals_for_sequence.push_back(
        OriginalSequenceInterval{sequence_start_position, sequence_end_position});
    is_finalized_ = false;
  }

  void ExcludeInterval(std::string_view sequence_name, SequencePosition sequence_start_position,
                       SequencePosition sequence_end_position) {
    ExcludeInterval(SelectedSequenceIdByName(sequence_name), sequence_start_position,
                    sequence_end_position);
  }

  void ExcludeIntervals(const std::vector<ExcludedSequenceInterval>& excluded_intervals) {
    if (excluded_intervals.empty()) {
      return;
    }

    std::vector<std::size_t> additional_interval_counts(excluded_intervals_by_sequence_.size(), 0);
    for (const ExcludedSequenceInterval& excluded_interval : excluded_intervals) {
      const std::size_t selected_sequence_index = ValidateOriginalInterval(
          excluded_interval.sequence_id, excluded_interval.sequence_start_position,
          excluded_interval.sequence_end_position);
      std::size_t& additional_interval_count = additional_interval_counts[selected_sequence_index];
      if (additional_interval_count == std::numeric_limits<std::size_t>::max()) {
        throw SeqProError(ErrorCode::kIntegerOverflow, "too many excluded intervals in one batch");
      }
      ++additional_interval_count;
    }

    for (std::size_t selected_sequence_index = 0;
         selected_sequence_index < additional_interval_counts.size(); ++selected_sequence_index) {
      if (additional_interval_counts[selected_sequence_index] == 0) {
        continue;
      }
      std::vector<OriginalSequenceInterval>& excluded_intervals_for_sequence =
          excluded_intervals_by_sequence_[selected_sequence_index];
      if (additional_interval_counts[selected_sequence_index] >
          excluded_intervals_for_sequence.max_size() - excluded_intervals_for_sequence.size()) {
        throw SeqProError(ErrorCode::kIntegerOverflow, "excluded interval vector size overflow");
      }
      excluded_intervals_for_sequence.reserve(excluded_intervals_for_sequence.size() +
                                              additional_interval_counts[selected_sequence_index]);
    }

    for (const ExcludedSequenceInterval& excluded_interval : excluded_intervals) {
      const std::size_t selected_sequence_index =
          RequireSelectedSequence(excluded_interval.sequence_id);
      excluded_intervals_by_sequence_[selected_sequence_index].push_back(OriginalSequenceInterval{
          excluded_interval.sequence_start_position, excluded_interval.sequence_end_position});
    }
    is_finalized_ = false;
  }

  void ExcludeTextIntervals(SequenceTextGeneration source_generation,
                            const std::vector<SequenceTextInterval>& sequence_text_intervals) {
    EnsureFinalized();
    if (source_generation != layout_generation_) {
      throw SeqProError(ErrorCode::kInvalidArgument, "sequence text generation " +
                                                         std::to_string(source_generation) +
                                                         " does not match current generation " +
                                                         std::to_string(layout_generation_));
    }
    if (sequence_text_intervals.empty()) {
      return;
    }

    std::vector<ExcludedSequenceInterval> converted_excluded_intervals;
    converted_excluded_intervals.reserve(sequence_text_intervals.size());
    for (const SequenceTextInterval& sequence_text_interval : sequence_text_intervals) {
      const std::optional<LocatedSequenceInterval> located_sequence_interval = LocateTextInterval(
          sequence_text_interval.text_start_position, sequence_text_interval.text_length);
      if (!located_sequence_interval) {
        const SequenceTextPosition text_end_position = CheckedAdd(
            sequence_text_interval.text_start_position, sequence_text_interval.text_length,
            "formatting an invalid sequence text interval");
        throw SeqProError(ErrorCode::kInvalidArgument,
                          "sequence text interval [" +
                              std::to_string(sequence_text_interval.text_start_position) + ", " +
                              std::to_string(text_end_position) +
                              ") is not contained in one active run");
      }
      const SequencePosition sequence_end_position =
          CheckedAdd(located_sequence_interval->original_sequence_start_position,
                     located_sequence_interval->interval_length,
                     "converting a sequence text interval to original coordinates");
      converted_excluded_intervals.push_back(ExcludedSequenceInterval{
          located_sequence_interval->sequence_id,
          located_sequence_interval->original_sequence_start_position, sequence_end_position});
    }
    ExcludeIntervals(converted_excluded_intervals);
  }

  void ClearExcludedIntervals(SequenceId sequence_id) {
    const std::size_t selected_sequence_index = RequireSelectedSequence(sequence_id);
    if (excluded_intervals_by_sequence_[selected_sequence_index].empty()) {
      return;
    }
    excluded_intervals_by_sequence_[selected_sequence_index].clear();
    is_finalized_ = false;
  }

  void ClearExcludedIntervals(std::string_view sequence_name) {
    ClearExcludedIntervals(SelectedSequenceIdByName(sequence_name));
  }

  void ClearAllExcludedIntervals() {
    bool removed_any_interval = false;
    for (std::size_t selected_sequence_index = 0; selected_sequence_index < sequence_order_.size();
         ++selected_sequence_index) {
      std::vector<OriginalSequenceInterval>& excluded_intervals_for_sequence =
          excluded_intervals_by_sequence_[selected_sequence_index];
      if (!excluded_intervals_for_sequence.empty()) {
        excluded_intervals_for_sequence.clear();
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

    std::vector<std::vector<OriginalSequenceInterval>> normalized_excluded_intervals_by_sequence =
        excluded_intervals_by_sequence_;
    std::vector<std::vector<ActiveRun>> prospective_active_runs_by_sequence(
        excluded_intervals_by_sequence_.size());
    std::vector<SequenceLength> prospective_active_sequence_lengths(
        excluded_intervals_by_sequence_.size(), 0);
    std::vector<SequenceLength> prospective_excluded_base_counts(
        excluded_intervals_by_sequence_.size(), 0);
    std::vector<ActiveRun> prospective_active_runs_in_text_order;

    std::size_t estimated_active_run_count = sequence_order_.size();
    for (const std::vector<OriginalSequenceInterval>& excluded_intervals_for_sequence :
         normalized_excluded_intervals_by_sequence) {
      const std::size_t excluded_interval_count = excluded_intervals_for_sequence.size();
      if (excluded_interval_count >
          std::numeric_limits<std::size_t>::max() - estimated_active_run_count) {
        throw SeqProError(ErrorCode::kIntegerOverflow, "active run count estimate overflow");
      }
      estimated_active_run_count += excluded_interval_count;
    }
    if (estimated_active_run_count > prospective_active_runs_in_text_order.max_size()) {
      throw SeqProError(ErrorCode::kIntegerOverflow, "active run count exceeds vector capacity");
    }
    prospective_active_runs_in_text_order.reserve(estimated_active_run_count);

    SequenceTextPosition next_text_position = 0;
    SequenceLength prospective_active_base_count = 0;

    for (std::size_t selected_sequence_index = 0; selected_sequence_index < sequence_order_.size();
         ++selected_sequence_index) {
      const SequenceId sequence_id = sequence_order_[selected_sequence_index];
      const SequenceLength original_sequence_length =
          original_sequence_lengths_[selected_sequence_index];
      std::vector<OriginalSequenceInterval>& excluded_intervals_for_sequence =
          normalized_excluded_intervals_by_sequence[selected_sequence_index];

      std::sort(excluded_intervals_for_sequence.begin(), excluded_intervals_for_sequence.end(),
                [](const OriginalSequenceInterval& left, const OriginalSequenceInterval& right) {
                  if (left.sequence_start_position != right.sequence_start_position) {
                    return left.sequence_start_position < right.sequence_start_position;
                  }
                  return left.sequence_end_position < right.sequence_end_position;
                });

      std::vector<OriginalSequenceInterval> merged_excluded_intervals;
      merged_excluded_intervals.reserve(excluded_intervals_for_sequence.size());
      for (const OriginalSequenceInterval& excluded_interval : excluded_intervals_for_sequence) {
        ValidateOriginalInterval(sequence_id, excluded_interval.sequence_start_position,
                                 excluded_interval.sequence_end_position);
        if (merged_excluded_intervals.empty() ||
            merged_excluded_intervals.back().sequence_end_position <
                excluded_interval.sequence_start_position) {
          merged_excluded_intervals.push_back(excluded_interval);
        } else {
          merged_excluded_intervals.back().sequence_end_position =
              std::max(merged_excluded_intervals.back().sequence_end_position,
                       excluded_interval.sequence_end_position);
        }
      }
      excluded_intervals_for_sequence = std::move(merged_excluded_intervals);

      std::vector<ActiveRun>& active_runs_for_sequence =
          prospective_active_runs_by_sequence[selected_sequence_index];
      if (excluded_intervals_for_sequence.size() >= active_runs_for_sequence.max_size()) {
        throw SeqProError(ErrorCode::kIntegerOverflow, "active run count exceeds vector capacity");
      }
      active_runs_for_sequence.reserve(excluded_intervals_for_sequence.size() + 1U);
      SequencePosition next_original_position = 0;
      ActiveSequencePosition next_active_position = 0;

      const auto append_active_run = [&](SequencePosition active_run_start_position,
                                         SequencePosition active_run_end_position) {
        if (active_run_start_position >= active_run_end_position) {
          return;
        }
        if (active_runs_for_sequence.size() >
            static_cast<std::size_t>(std::numeric_limits<SequenceRunIndex>::max())) {
          throw SeqProError(ErrorCode::kIntegerOverflow,
                            "active run count exceeds SequenceRunIndex capacity");
        }
        const SequenceRunIndex sequence_run_index =
            static_cast<SequenceRunIndex>(active_runs_for_sequence.size());
        const ActiveRun active_run{sequence_id,
                                   sequence_run_index,
                                   active_run_start_position,
                                   active_run_end_position,
                                   next_active_position,
                                   next_text_position};
        active_runs_for_sequence.push_back(active_run);
        prospective_active_runs_in_text_order.push_back(active_run);

        const SequenceLength active_run_length =
            active_run_end_position - active_run_start_position;
        next_active_position = CheckedAdd(next_active_position, active_run_length,
                                          "computing an active sequence position prefix");
        prospective_active_base_count =
            CheckedAdd(prospective_active_base_count, active_run_length,
                       "counting active bases across selected sequences");
        next_text_position = CheckedAdd(next_text_position, active_run_length,
                                        "computing a sequence text run boundary");
        next_text_position =
            CheckedAdd(next_text_position, 1U, "reserving a sequence text separator");
      };

      for (const OriginalSequenceInterval& excluded_interval : excluded_intervals_for_sequence) {
        append_active_run(next_original_position, excluded_interval.sequence_start_position);
        next_original_position = excluded_interval.sequence_end_position;
      }
      append_active_run(next_original_position, original_sequence_length);

      prospective_active_sequence_lengths[selected_sequence_index] = next_active_position;
      prospective_excluded_base_counts[selected_sequence_index] =
          original_sequence_length - next_active_position;
    }

    const SequenceTextLength prospective_text_size =
        CheckedAdd(next_text_position, 1U, "reserving the sequence text terminator");
    const SequenceTextGeneration prospective_generation = layout_generation_ + 1U;

    excluded_intervals_by_sequence_.swap(normalized_excluded_intervals_by_sequence);
    active_runs_by_sequence_.swap(prospective_active_runs_by_sequence);
    active_sequence_lengths_.swap(prospective_active_sequence_lengths);
    excluded_base_counts_.swap(prospective_excluded_base_counts);
    active_runs_in_text_order_.swap(prospective_active_runs_in_text_order);
    active_base_count_ = prospective_active_base_count;
    text_size_ = prospective_text_size;
    layout_generation_ = prospective_generation;
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
    return active_runs_in_text_order_.size();
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
    const std::vector<ActiveRun>& active_runs_for_sequence =
        active_runs_by_sequence_[RequireSelectedSequence(sequence_id)];
    std::vector<OriginalSequenceInterval> active_intervals;
    active_intervals.reserve(active_runs_for_sequence.size());
    for (const ActiveRun& active_run : active_runs_for_sequence) {
      active_intervals.push_back(OriginalSequenceInterval{
          active_run.original_sequence_start_position, active_run.original_sequence_end_position});
    }
    return active_intervals;
  }

  std::vector<OriginalSequenceInterval> ExcludedIntervalsById(SequenceId sequence_id) const {
    EnsureFinalized();
    return excluded_intervals_by_sequence_[RequireSelectedSequence(sequence_id)];
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
    return CheckedAdd(active_run->active_sequence_start_position,
                      original_sequence_position - active_run->original_sequence_start_position,
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
    return CheckedAdd(active_run.original_sequence_start_position,
                      active_sequence_position - active_run.active_sequence_start_position,
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
    return CheckedAdd(active_run->text_start_position,
                      original_sequence_position - active_run->original_sequence_start_position,
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
    return CheckedAdd(active_run.text_start_position,
                      active_sequence_position - active_run.active_sequence_start_position,
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
    const SequenceLength active_run_length = ActiveRunLength(active_run);
    const SequenceTextPosition active_run_end_position = CheckedAdd(
        active_run.text_start_position, active_run_length, "locating a sequence text run boundary");
    if (text_position == active_run_end_position) {
      return SequenceTextSeparatorLocation{active_run.sequence_id, active_run.sequence_run_index};
    }
    if (text_position > active_run_end_position) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "sequence text run table is internally inconsistent");
    }

    const SequenceTextPosition active_run_offset = text_position - active_run.text_start_position;
    return SequenceTextBaseLocation{
        active_run.sequence_id, active_run.sequence_run_index,
        CheckedAdd(active_run.original_sequence_start_position, active_run_offset,
                   "locating an original sequence position"),
        CheckedAdd(active_run.active_sequence_start_position, active_run_offset,
                   "locating an active sequence position")};
  }

  std::optional<LocatedSequenceInterval> LocateTextInterval(
      SequenceTextPosition text_start_position, SequenceTextLength text_length) const {
    EnsureFinalized();
    if (text_length == 0) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "sequence text interval length must be greater than zero");
    }
    if (text_length > std::numeric_limits<SequenceTextPosition>::max() - text_start_position) {
      throw SeqProError(ErrorCode::kIntegerOverflow, "sequence text interval end overflow");
    }
    if (text_start_position >= text_size_ || text_length > text_size_ - text_start_position ||
        text_start_position == text_size_ - 1U) {
      return std::nullopt;
    }

    const ActiveRun& active_run = GlobalRunForPosition(text_start_position);
    const SequenceTextPosition active_run_end_position =
        CheckedAdd(active_run.text_start_position, ActiveRunLength(active_run),
                   "locating a sequence text interval boundary");
    if (text_start_position >= active_run_end_position ||
        text_length > active_run_end_position - text_start_position) {
      return std::nullopt;
    }

    const SequenceTextPosition active_run_offset =
        text_start_position - active_run.text_start_position;
    return LocatedSequenceInterval{
        active_run.sequence_id, active_run.sequence_run_index,
        CheckedAdd(active_run.original_sequence_start_position, active_run_offset,
                   "locating an original sequence interval"),
        CheckedAdd(active_run.active_sequence_start_position, active_run_offset,
                   "locating an active sequence interval"),
        text_length};
  }

  std::uint8_t ReadTextByte(SequenceTextPosition text_position) const {
    const SequenceTextLocation text_location = LocateTextPosition(text_position);
    if (const auto* base_location = std::get_if<SequenceTextBaseLocation>(&text_location)) {
      const char sequence_base = SequenceView(base_location->sequence_id)
                                     .ReadBase(base_location->original_sequence_position);
      ValidateReservedByte(sequence_base, base_location->sequence_id,
                           base_location->original_sequence_position);
      return static_cast<std::uint8_t>(static_cast<unsigned char>(sequence_base));
    }
    if (std::holds_alternative<SequenceTextSeparatorLocation>(text_location)) {
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
    std::string sequence_text_bytes;
    if (text_size_ > static_cast<SequenceTextLength>(sequence_text_bytes.max_size())) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "sequence text exceeds std::string::max_size()");
    }
    sequence_text_bytes.resize(static_cast<std::size_t>(text_size_));
    CopyTextTo(sequence_text_bytes.data(), sequence_text_bytes.size());
    return MaterializedSequenceText{std::move(sequence_text_bytes), layout_generation_};
  }

  void CopyTextTo(char* destination_buffer, std::size_t destination_size_bytes) const {
    EnsureFinalized();
    if (text_size_ > static_cast<SequenceTextLength>(std::numeric_limits<std::size_t>::max())) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "sequence text cannot be represented by a caller buffer");
    }
    const std::size_t required_size_bytes = static_cast<std::size_t>(text_size_);
    if (destination_size_bytes != required_size_bytes) {
      throw SeqProError(ErrorCode::kInvalidArgument, "CopyTextTo destination size " +
                                                         std::to_string(destination_size_bytes) +
                                                         " does not equal sequence text size " +
                                                         std::to_string(required_size_bytes));
    }
    if (destination_buffer == nullptr) {
      throw SeqProError(ErrorCode::kInvalidArgument, "CopyTextTo destination is null");
    }

    for (const ActiveRun& active_run : active_runs_in_text_order_) {
      const SequenceLength active_run_length = ActiveRunLength(active_run);
      if (active_run_length >
          static_cast<SequenceLength>(std::numeric_limits<std::size_t>::max())) {
        throw SeqProError(ErrorCode::kIntegerOverflow,
                          "active run cannot be represented by a caller buffer");
      }
      const std::size_t text_offset_bytes =
          static_cast<std::size_t>(active_run.text_start_position);
      const std::size_t copy_size_bytes = static_cast<std::size_t>(active_run_length);
      SequenceView(active_run.sequence_id)
          .CopySubsequenceTo(active_run.original_sequence_start_position,
                             destination_buffer + text_offset_bytes, copy_size_bytes);
      ValidateReservedBytes(destination_buffer + text_offset_bytes, copy_size_bytes,
                            active_run.sequence_id, active_run.original_sequence_start_position);
      destination_buffer[text_offset_bytes + copy_size_bytes] =
          static_cast<char>(SequenceTextLayout::kSeparatorByte);
    }
    destination_buffer[required_size_bytes - 1U] =
        static_cast<char>(SequenceTextLayout::kTerminatorByte);
  }

  void WriteTo(std::ostream& output_stream, std::size_t transfer_buffer_size_bytes) const {
    EnsureFinalized();
    if (transfer_buffer_size_bytes == 0) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "WriteTo transfer buffer size must be greater than zero");
    }

    const std::uint64_t maximum_stream_write_size =
        static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max());
    const std::uint64_t bounded_buffer_size_bytes =
        std::min<std::uint64_t>(transfer_buffer_size_bytes, maximum_stream_write_size);
    const std::uint64_t effective_buffer_size_bytes =
        std::min<std::uint64_t>(bounded_buffer_size_bytes, active_base_count_);
    std::vector<char> transfer_buffer;
    if (effective_buffer_size_bytes > transfer_buffer.max_size()) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "WriteTo transfer buffer exceeds vector capacity");
    }
    transfer_buffer.resize(static_cast<std::size_t>(effective_buffer_size_bytes));

    for (const ActiveRun& active_run : active_runs_in_text_order_) {
      SequencePosition current_original_position = active_run.original_sequence_start_position;
      SequenceLength remaining_active_run_length = ActiveRunLength(active_run);
      while (remaining_active_run_length != 0) {
        const std::size_t current_transfer_size_bytes = static_cast<std::size_t>(
            std::min<std::uint64_t>(transfer_buffer.size(), remaining_active_run_length));
        SequenceView(active_run.sequence_id)
            .CopySubsequenceTo(current_original_position, transfer_buffer.data(),
                               current_transfer_size_bytes);
        ValidateReservedBytes(transfer_buffer.data(), current_transfer_size_bytes,
                              active_run.sequence_id, current_original_position);
        output_stream.write(transfer_buffer.data(),
                            static_cast<std::streamsize>(current_transfer_size_bytes));
        if (!output_stream) {
          throw SeqProError(ErrorCode::kIoError,
                            "cannot write active sequence bases to output stream");
        }
        current_original_position =
            CheckedAdd(current_original_position, current_transfer_size_bytes,
                       "advancing a streamed original sequence position");
        remaining_active_run_length -= static_cast<SequenceLength>(current_transfer_size_bytes);
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

  std::size_t ValidateOriginalInterval(SequenceId sequence_id,
                                       SequencePosition sequence_start_position,
                                       SequencePosition sequence_end_position) const {
    const std::size_t selected_sequence_index = RequireSelectedSequence(sequence_id);
    if (sequence_start_position >= sequence_end_position) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "excluded interval must have start smaller than end");
    }
    if (sequence_end_position > original_sequence_lengths_[selected_sequence_index]) {
      throw SeqProError(ErrorCode::kSequenceRangeOutOfBounds,
                        "excluded interval [" + std::to_string(sequence_start_position) + ", " +
                            std::to_string(sequence_end_position) + ") exceeds sequence ID " +
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
    const std::vector<ActiveRun>& active_runs_for_sequence =
        active_runs_by_sequence_[selected_sequence_index];
    auto active_run_iterator = std::upper_bound(
        active_runs_for_sequence.begin(), active_runs_for_sequence.end(),
        original_sequence_position, [](SequencePosition position, const ActiveRun& active_run) {
          return position < active_run.original_sequence_start_position;
        });
    if (active_run_iterator == active_runs_for_sequence.begin()) {
      return nullptr;
    }
    --active_run_iterator;
    if (original_sequence_position >= active_run_iterator->original_sequence_end_position) {
      return nullptr;
    }
    return &*active_run_iterator;
  }

  const ActiveRun& RunByActivePosition(
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      std::size_t selected_sequence_index, ActiveSequencePosition active_sequence_position) const {
    const std::vector<ActiveRun>& active_runs_for_sequence =
        active_runs_by_sequence_[selected_sequence_index];
    auto active_run_iterator = std::upper_bound(
        active_runs_for_sequence.begin(), active_runs_for_sequence.end(), active_sequence_position,
        [](ActiveSequencePosition position, const ActiveRun& active_run) {
          return position < active_run.active_sequence_start_position;
        });
    if (active_run_iterator == active_runs_for_sequence.begin()) {
      throw SeqProError(ErrorCode::kInvalidArgument, "active run table is internally inconsistent");
    }
    --active_run_iterator;
    return *active_run_iterator;
  }

  const ActiveRun& GlobalRunForPosition(SequenceTextPosition text_position) const {
    auto active_run_iterator = std::upper_bound(
        active_runs_in_text_order_.begin(), active_runs_in_text_order_.end(), text_position,
        [](SequenceTextPosition position, const ActiveRun& active_run) {
          return position < active_run.text_start_position;
        });
    if (active_run_iterator == active_runs_in_text_order_.begin()) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        "sequence text run table is internally inconsistent");
    }
    --active_run_iterator;
    return *active_run_iterator;
  }

  void ValidateReservedByte(
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      char sequence_base, SequenceId sequence_id, SequencePosition sequence_position) const {
    const std::uint8_t sequence_byte =
        static_cast<std::uint8_t>(static_cast<unsigned char>(sequence_base));
    if (sequence_byte == SequenceTextLayout::kSeparatorByte ||
        sequence_byte == SequenceTextLayout::kTerminatorByte) {
      throw SeqProError(ErrorCode::kUnsupportedFileFormat,
                        "FASTA sequence ID " + std::to_string(sequence_id) +
                            " contains reserved sequence text byte " +
                            std::to_string(sequence_byte) + " at original position " +
                            std::to_string(sequence_position));
    }
  }

  void ValidateReservedBytes(
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      const char* sequence_bases, std::size_t sequence_base_count, SequenceId sequence_id,
      SequencePosition sequence_start_position) const {
    if (sequence_base_count == 0) {
      return;
    }
    const auto* const separator_byte_location = static_cast<const char*>(
        std::memchr(sequence_bases, SequenceTextLayout::kSeparatorByte, sequence_base_count));
    const auto* const terminator_byte_location = static_cast<const char*>(
        std::memchr(sequence_bases, SequenceTextLayout::kTerminatorByte, sequence_base_count));
    const char* reserved_sequence_byte = separator_byte_location;
    if (reserved_sequence_byte == nullptr || (terminator_byte_location != nullptr &&
                                              terminator_byte_location < reserved_sequence_byte)) {
      reserved_sequence_byte = terminator_byte_location;
    }
    if (reserved_sequence_byte != nullptr) {
      const std::size_t sequence_base_index =
          static_cast<std::size_t>(reserved_sequence_byte - sequence_bases);
      ValidateReservedByte(*reserved_sequence_byte, sequence_id,
                           CheckedAdd(sequence_start_position, sequence_base_index,
                                      "locating a reserved FASTA byte"));
    }
  }

  IndexedFasta indexed_fasta_;
  std::vector<SequenceId> sequence_order_;
  std::unordered_map<SequenceId, std::size_t> selected_sequence_index_by_id_;
  std::vector<SequenceLength> original_sequence_lengths_;
  std::vector<FastaSequenceView> sequence_views_;
  std::vector<std::vector<OriginalSequenceInterval>> excluded_intervals_by_sequence_;
  std::vector<std::vector<ActiveRun>> active_runs_by_sequence_;
  std::vector<ActiveRun> active_runs_in_text_order_;
  std::vector<SequenceLength> active_sequence_lengths_;
  std::vector<SequenceLength> excluded_base_counts_;
  SequenceLength active_base_count_ = 0;
  SequenceTextLength text_size_ = 0;
  SequenceTextGeneration layout_generation_ = 0;
  bool is_finalized_ = false;
  bool uses_identity_sequence_order_ = false;
};

SequenceTextLayout::SequenceTextLayout(IndexedFasta indexed_fasta,
                                       std::vector<SequenceId> selected_sequence_order)
    : layout_state_(
          std::make_unique<State>(std::move(indexed_fasta), std::move(selected_sequence_order))) {}

SequenceTextLayout::~SequenceTextLayout() = default;

SequenceTextLayout::SequenceTextLayout(SequenceTextLayout&&) noexcept = default;

SequenceTextLayout& SequenceTextLayout::operator=(SequenceTextLayout&&) noexcept = default;

const IndexedFasta& SequenceTextLayout::indexed_fasta() const noexcept {
  return layout_state_->indexed_fasta();
}

const std::vector<SequenceId>& SequenceTextLayout::sequence_order() const noexcept {
  return layout_state_->sequence_order();
}

bool SequenceTextLayout::is_finalized() const noexcept { return layout_state_->is_finalized(); }

SequenceTextGeneration SequenceTextLayout::layout_generation() const noexcept {
  return layout_state_->layout_generation();
}

void SequenceTextLayout::ExcludeInterval(SequenceId sequence_id,
                                         SequencePosition sequence_start_position,
                                         SequencePosition sequence_end_position) {
  layout_state_->ExcludeInterval(sequence_id, sequence_start_position, sequence_end_position);
}

void SequenceTextLayout::ExcludeInterval(std::string_view sequence_name,
                                         SequencePosition sequence_start_position,
                                         SequencePosition sequence_end_position) {
  layout_state_->ExcludeInterval(sequence_name, sequence_start_position, sequence_end_position);
}

void SequenceTextLayout::ExcludeIntervals(
    const std::vector<ExcludedSequenceInterval>& excluded_intervals) {
  layout_state_->ExcludeIntervals(excluded_intervals);
}

void SequenceTextLayout::ExcludeTextIntervals(
    SequenceTextGeneration source_generation,
    const std::vector<SequenceTextInterval>& sequence_text_intervals) {
  layout_state_->ExcludeTextIntervals(source_generation, sequence_text_intervals);
}

void SequenceTextLayout::ClearExcludedIntervals(SequenceId sequence_id) {
  layout_state_->ClearExcludedIntervals(sequence_id);
}

void SequenceTextLayout::ClearExcludedIntervals(std::string_view sequence_name) {
  layout_state_->ClearExcludedIntervals(sequence_name);
}

void SequenceTextLayout::ClearAllExcludedIntervals() { layout_state_->ClearAllExcludedIntervals(); }

void SequenceTextLayout::Finalize() { layout_state_->Finalize(); }

SequenceTextLength SequenceTextLayout::text_size() const { return layout_state_->text_size(); }

SequenceLength SequenceTextLayout::active_base_count() const {
  return layout_state_->active_base_count();
}

std::size_t SequenceTextLayout::active_run_count() const {
  return layout_state_->active_run_count();
}

SequenceLength SequenceTextLayout::ActiveSequenceLength(SequenceId sequence_id) const {
  return layout_state_->ActiveSequenceLength(sequence_id);
}

SequenceLength SequenceTextLayout::ExcludedBaseCount(SequenceId sequence_id) const {
  return layout_state_->ExcludedBaseCount(sequence_id);
}

std::vector<OriginalSequenceInterval> SequenceTextLayout::ActiveIntervalsById(
    SequenceId sequence_id) const {
  return layout_state_->ActiveIntervalsById(sequence_id);
}

std::vector<OriginalSequenceInterval> SequenceTextLayout::ExcludedIntervalsById(
    SequenceId sequence_id) const {
  return layout_state_->ExcludedIntervalsById(sequence_id);
}

std::optional<ActiveSequencePosition> SequenceTextLayout::FindActiveSequencePosition(
    SequenceId sequence_id, SequencePosition original_sequence_position) const {
  return layout_state_->FindActiveSequencePosition(sequence_id, original_sequence_position);
}

SequencePosition SequenceTextLayout::OriginalSequencePosition(
    SequenceId sequence_id, ActiveSequencePosition active_sequence_position) const {
  return layout_state_->OriginalSequencePosition(sequence_id, active_sequence_position);
}

std::optional<SequenceTextPosition> SequenceTextLayout::FindTextPosition(
    SequenceId sequence_id, SequencePosition original_sequence_position) const {
  return layout_state_->FindTextPosition(sequence_id, original_sequence_position);
}

SequenceTextPosition SequenceTextLayout::TextPositionFromActive(
    SequenceId sequence_id, ActiveSequencePosition active_sequence_position) const {
  return layout_state_->TextPositionFromActive(sequence_id, active_sequence_position);
}

SequenceTextLocation SequenceTextLayout::LocateTextPosition(
    SequenceTextPosition text_position) const {
  return layout_state_->LocateTextPosition(text_position);
}

std::optional<LocatedSequenceInterval> SequenceTextLayout::LocateTextInterval(
    SequenceTextPosition text_start_position, SequenceTextLength text_length) const {
  return layout_state_->LocateTextInterval(text_start_position, text_length);
}

std::uint8_t SequenceTextLayout::ReadTextByte(SequenceTextPosition text_position) const {
  return layout_state_->ReadTextByte(text_position);
}

MaterializedSequenceText SequenceTextLayout::Materialize() const {
  return layout_state_->Materialize();
}

void SequenceTextLayout::CopyTextTo(char* destination_buffer,
                                    std::size_t destination_size_bytes) const {
  layout_state_->CopyTextTo(destination_buffer, destination_size_bytes);
}

void SequenceTextLayout::WriteTo(std::ostream& output_stream,
                                 std::size_t transfer_buffer_size_bytes) const {
  layout_state_->WriteTo(output_stream, transfer_buffer_size_bytes);
}

}  // namespace seqpro
