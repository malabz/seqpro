#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "seqpro/sequence_text_layout.h"

namespace {

void FreezeSequenceTextMethodSignatures() {
  using ExcludeByIdMethod = void (seqpro::SequenceTextLayout::*)(
      seqpro::SequenceId, seqpro::SequencePosition, seqpro::SequencePosition);
  using ExcludeByNameMethod = void (seqpro::SequenceTextLayout::*)(
      std::string_view, seqpro::SequencePosition, seqpro::SequencePosition);
  using ClearByIdMethod = void (seqpro::SequenceTextLayout::*)(seqpro::SequenceId);
  using ClearByNameMethod = void (seqpro::SequenceTextLayout::*)(std::string_view);

  [[maybe_unused]] const auto indexed_fasta_method = &seqpro::SequenceTextLayout::indexed_fasta;
  [[maybe_unused]] const auto sequence_order_method = &seqpro::SequenceTextLayout::sequence_order;
  [[maybe_unused]] const auto is_finalized_method = &seqpro::SequenceTextLayout::is_finalized;
  [[maybe_unused]] const auto layout_generation_method =
      &seqpro::SequenceTextLayout::layout_generation;
  [[maybe_unused]] const ExcludeByIdMethod exclude_by_id_method =
      static_cast<ExcludeByIdMethod>(&seqpro::SequenceTextLayout::ExcludeInterval);
  [[maybe_unused]] const ExcludeByNameMethod exclude_by_name_method =
      static_cast<ExcludeByNameMethod>(&seqpro::SequenceTextLayout::ExcludeInterval);
  [[maybe_unused]] const auto exclude_intervals_method =
      &seqpro::SequenceTextLayout::ExcludeIntervals;
  [[maybe_unused]] const auto exclude_text_intervals_method =
      &seqpro::SequenceTextLayout::ExcludeTextIntervals;
  [[maybe_unused]] const ClearByIdMethod clear_by_id_method =
      static_cast<ClearByIdMethod>(&seqpro::SequenceTextLayout::ClearExcludedIntervals);
  [[maybe_unused]] const ClearByNameMethod clear_by_name_method =
      static_cast<ClearByNameMethod>(&seqpro::SequenceTextLayout::ClearExcludedIntervals);
  [[maybe_unused]] const auto clear_all_method =
      &seqpro::SequenceTextLayout::ClearAllExcludedIntervals;
  [[maybe_unused]] const auto finalize_method = &seqpro::SequenceTextLayout::Finalize;
  [[maybe_unused]] const auto text_size_method = &seqpro::SequenceTextLayout::text_size;
  [[maybe_unused]] const auto active_base_count_method =
      &seqpro::SequenceTextLayout::active_base_count;
  [[maybe_unused]] const auto active_run_count_method =
      &seqpro::SequenceTextLayout::active_run_count;
  [[maybe_unused]] const auto active_sequence_length_method =
      &seqpro::SequenceTextLayout::ActiveSequenceLength;
  [[maybe_unused]] const auto excluded_base_count_method =
      &seqpro::SequenceTextLayout::ExcludedBaseCount;
  [[maybe_unused]] const auto active_intervals_method =
      &seqpro::SequenceTextLayout::ActiveIntervalsById;
  [[maybe_unused]] const auto excluded_intervals_method =
      &seqpro::SequenceTextLayout::ExcludedIntervalsById;
  [[maybe_unused]] const auto find_active_position_method =
      &seqpro::SequenceTextLayout::FindActiveSequencePosition;
  [[maybe_unused]] const auto original_position_method =
      &seqpro::SequenceTextLayout::OriginalSequencePosition;
  [[maybe_unused]] const auto find_text_position_method =
      &seqpro::SequenceTextLayout::FindTextPosition;
  [[maybe_unused]] const auto text_position_from_active_method =
      &seqpro::SequenceTextLayout::TextPositionFromActive;
  [[maybe_unused]] const auto locate_text_position_method =
      &seqpro::SequenceTextLayout::LocateTextPosition;
  [[maybe_unused]] const auto locate_text_interval_method =
      &seqpro::SequenceTextLayout::LocateTextInterval;
  [[maybe_unused]] const auto read_text_byte_method = &seqpro::SequenceTextLayout::ReadTextByte;
  [[maybe_unused]] const auto materialize_method = &seqpro::SequenceTextLayout::Materialize;
  [[maybe_unused]] const auto copy_text_method = &seqpro::SequenceTextLayout::CopyTextTo;
  [[maybe_unused]] const auto write_text_method = &seqpro::SequenceTextLayout::WriteTo;
}

}  // namespace

int main() {
  static_assert(sizeof(seqpro::SequenceTextPosition) == sizeof(std::uint64_t));
  static_assert(sizeof(seqpro::SequenceTextLength) == sizeof(std::uint64_t));
  static_assert(sizeof(seqpro::ActiveSequencePosition) == sizeof(std::uint64_t));
  static_assert(sizeof(seqpro::SequenceTextGeneration) == sizeof(std::uint64_t));
  static_assert(sizeof(seqpro::SequenceRunIndex) == sizeof(std::uint32_t));
  static_assert(!std::is_copy_constructible<seqpro::SequenceTextLayout>::value);
  static_assert(std::is_move_constructible<seqpro::SequenceTextLayout>::value);
  static_assert(std::is_constructible<seqpro::SequenceTextLayout, seqpro::IndexedFasta,
                                      std::vector<seqpro::SequenceId>>::value);
  static_assert(std::variant_size<seqpro::SequenceTextLocation>::value == 3U);
  static_assert(seqpro::SequenceTextLayout::kSeparatorByte == 0x01);
  static_assert(seqpro::SequenceTextLayout::kTerminatorByte == 0x00);

  FreezeSequenceTextMethodSignatures();

  const seqpro::OriginalSequenceInterval original_interval{1, 3};
  const seqpro::ExcludedSequenceInterval excluded_interval{0, 1, 3};
  const seqpro::SequenceTextInterval text_interval{4, 2};
  const seqpro::SequenceTextBaseLocation base_location{0, 1, 3, 2};
  const seqpro::SequenceTextSeparatorLocation separator_location{0, 1};
  const seqpro::SequenceTextTerminatorLocation terminator_location{};
  const seqpro::SequenceTextLocation tagged_location = base_location;
  const seqpro::LocatedSequenceInterval located_interval{0, 1, 3, 2, 2};
  const seqpro::MaterializedSequenceText materialized_text{std::string("A\1\0", 3), 1};

  const bool public_fields_are_usable =
      original_interval.sequence_start_position == 1 &&
      original_interval.sequence_end_position == 3 && excluded_interval.sequence_id == 0 &&
      excluded_interval.sequence_start_position == 1 &&
      excluded_interval.sequence_end_position == 3 && text_interval.text_start_position == 4 &&
      text_interval.text_length == 2 && base_location.sequence_id == 0 &&
      base_location.sequence_run_index == 1 && base_location.original_sequence_position == 3 &&
      base_location.active_sequence_position == 2 &&
      separator_location.preceding_sequence_id == 0 &&
      separator_location.preceding_run_index == 1 &&
      std::holds_alternative<seqpro::SequenceTextBaseLocation>(tagged_location) &&
      std::is_empty<decltype(terminator_location)>::value && located_interval.sequence_id == 0 &&
      located_interval.sequence_run_index == 1 &&
      located_interval.original_sequence_start_position == 3 &&
      located_interval.active_sequence_start_position == 2 &&
      located_interval.interval_length == 2 && materialized_text.sequence_text_bytes.size() == 3 &&
      materialized_text.layout_generation == 1;

  return public_fields_are_usable ? 0 : 1;
}
