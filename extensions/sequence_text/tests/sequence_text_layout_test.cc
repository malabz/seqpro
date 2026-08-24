#include "seqpro/sequence_text_layout.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "seqpro/error.h"
#include "seqpro/fasta_index.h"

namespace {

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    std::string temporary_directory_path_template = "/tmp/seqpro-sequence-text-test-XXXXXX";
    char* const created_directory_path = mkdtemp(temporary_directory_path_template.data());
    if (created_directory_path == nullptr) {
      throw std::runtime_error("cannot create unique test directory");
    }
    directory_path_ = created_directory_path;
  }

  ~TemporaryDirectory() {
    std::error_code cleanup_error;
    std::filesystem::remove_all(directory_path_, cleanup_error);
  }

  const std::filesystem::path& DirectoryPath() const noexcept { return directory_path_; }

 private:
  std::filesystem::path directory_path_;
};

void Check(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void WriteBinaryFile(const std::filesystem::path& file_path, std::string_view file_contents) {
  std::ofstream output_stream(file_path, std::ios::binary | std::ios::trunc);
  output_stream.write(file_contents.data(), static_cast<std::streamsize>(file_contents.size()));
  if (!output_stream) {
    throw std::runtime_error("cannot write test fixture: " + file_path.string());
  }
}

template <typename Function>
void ExpectSeqProError(seqpro::ErrorCode expected_error_code, Function&& operation) {
  try {
    operation();
  } catch (const seqpro::SeqProError& seqpro_error) {
    Check(seqpro_error.error_code() == expected_error_code, "unexpected SeqPro error code");
    return;
  }
  throw std::runtime_error("expected SeqProError was not thrown");
}

std::string AppendControlByte(std::string sequence_text_bytes, std::uint8_t control_byte) {
  sequence_text_bytes.push_back(static_cast<char>(control_byte));
  return sequence_text_bytes;
}

std::string BasicExpectedText() {
  std::string expected_sequence_text = "ABCDEFG";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text += "xyz";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  return AppendControlByte(std::move(expected_sequence_text),
                           seqpro::SequenceTextLayout::kTerminatorByte);
}

seqpro::IndexedFasta BuildBasicFasta(const std::filesystem::path& fasta_path) {
  WriteBinaryFile(fasta_path, ">first description\nABCDEFG\n>second\nxyz\n");
  seqpro::BuildFastaIndex(fasta_path);
  return seqpro::IndexedFasta::Open(fasta_path);
}

void CheckAllTextAccessPaths(const seqpro::SequenceTextLayout& layout,
                             const std::string& expected_sequence_text) {
  Check(layout.Materialize().sequence_text_bytes == expected_sequence_text,
        "Materialize differs from expected text");
  Check(layout.text_size() == expected_sequence_text.size(),
        "text_size differs from expected text");

  std::string copied_sequence_text(expected_sequence_text.size(), '?');
  layout.CopyTextTo(copied_sequence_text.data(), copied_sequence_text.size());
  Check(copied_sequence_text == expected_sequence_text, "CopyTextTo differs from expected text");

  std::ostringstream output_stream;
  layout.WriteTo(output_stream, 2);
  Check(output_stream.str() == expected_sequence_text, "WriteTo differs from expected text");

  for (std::size_t text_position = 0; text_position < expected_sequence_text.size();
       ++text_position) {
    Check(layout.ReadTextByte(text_position) ==
              static_cast<std::uint8_t>(
                  static_cast<unsigned char>(expected_sequence_text[text_position])),
          "ReadTextByte differs from expected text");
  }
}

void CheckInterval(const seqpro::OriginalSequenceInterval& original_sequence_interval,
                   seqpro::SequencePosition expected_start_position,
                   seqpro::SequencePosition expected_end_position, std::string_view message) {
  Check(original_sequence_interval.sequence_start_position == expected_start_position &&
            original_sequence_interval.sequence_end_position == expected_end_position,
        message);
}

void TestBasicLayoutAndCoordinates() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.DirectoryPath() / "basic.fa";
  const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);
  seqpro::SequenceTextLayout layout(indexed_fasta);

  const seqpro::SequenceTextGeneration initial_generation = layout.layout_generation();
  layout.ClearExcludedIntervals(0);
  layout.ClearAllExcludedIntervals();
  layout.ExcludeIntervals({});
  layout.ExcludeTextIntervals(initial_generation, {});
  Check(layout.is_finalized() && layout.layout_generation() == initial_generation,
        "no-op mutations changed the initial layout");

  Check(layout.is_finalized(), "new layout is not finalized");
  Check(layout.layout_generation() == 1, "initial generation is not one");
  Check(layout.sequence_order() == std::vector<seqpro::SequenceId>({0, 1}),
        "default sequence order differs from FAI order");
  Check(layout.indexed_fasta().fasta_path() == fasta_path,
        "layout did not retain the indexed FASTA");
  Check(layout.active_base_count() == 10, "active base count is incorrect");
  Check(layout.active_run_count() == 2, "active run count is incorrect");

  const std::string expected_sequence_text = BasicExpectedText();
  const seqpro::MaterializedSequenceText materialized_text = layout.Materialize();
  Check(materialized_text.sequence_text_bytes == expected_sequence_text,
        "materialized text is incorrect");
  Check(materialized_text.layout_generation == 1, "materialized generation is incorrect");
  CheckAllTextAccessPaths(layout, expected_sequence_text);

  const seqpro::SequenceTextLocation first_location = layout.LocateTextPosition(0);
  const auto* first_base_location = std::get_if<seqpro::SequenceTextBaseLocation>(&first_location);
  Check(first_base_location != nullptr && first_base_location->sequence_id == 0 &&
            first_base_location->sequence_run_index == 0 &&
            first_base_location->original_sequence_position == 0 &&
            first_base_location->active_sequence_position == 0,
        "first base location is incorrect");

  const seqpro::SequenceTextLocation first_separator_location = layout.LocateTextPosition(7);
  const auto* first_separator =
      std::get_if<seqpro::SequenceTextSeparatorLocation>(&first_separator_location);
  Check(first_separator != nullptr && first_separator->preceding_sequence_id == 0 &&
            first_separator->preceding_run_index == 0,
        "first separator location is incorrect");
  Check(std::holds_alternative<seqpro::SequenceTextTerminatorLocation>(
            layout.LocateTextPosition(expected_sequence_text.size() - 1U)),
        "terminator location is incorrect");

  Check(layout.FindActiveSequencePosition(0, 6) == seqpro::ActiveSequencePosition{6},
        "original-to-active mapping is incorrect");
  Check(layout.OriginalSequencePosition(1, 2) == 2, "active-to-original mapping is incorrect");
  Check(layout.FindTextPosition(0, 6) == seqpro::SequenceTextPosition{6},
        "original-to-text mapping is incorrect");
  Check(layout.FindTextPosition(1, 0) == seqpro::SequenceTextPosition{8},
        "second-sequence text start is incorrect");
  Check(layout.TextPositionFromActive(1, 2) == 10, "active-to-text mapping is incorrect");

  const std::optional<seqpro::LocatedSequenceInterval> located_sequence_interval =
      layout.LocateTextInterval(1, 3);
  Check(located_sequence_interval && located_sequence_interval->sequence_id == 0 &&
            located_sequence_interval->original_sequence_start_position == 1 &&
            located_sequence_interval->active_sequence_start_position == 1 &&
            located_sequence_interval->interval_length == 3,
        "text interval location is incorrect");
  Check(!layout.LocateTextInterval(6, 2), "interval crossing a separator unexpectedly resolved");
  Check(!layout.LocateTextInterval(7, 1), "separator interval unexpectedly resolved");
  Check(!layout.LocateTextInterval(expected_sequence_text.size() - 1U, 1),
        "terminator interval unexpectedly resolved");
}

void TestExclusionFinalizeAndClear() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.DirectoryPath() / "excluded.fa";
  const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);
  seqpro::SequenceTextLayout layout(indexed_fasta);

  layout.ExcludeInterval(0, 2, 4);
  layout.ExcludeInterval(0, 3, 5);
  layout.ExcludeInterval("second", 0, 1);
  Check(!layout.is_finalized(), "modified layout remained finalized");
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { static_cast<void>(layout.text_size()); });
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { static_cast<void>(layout.Materialize()); });

  layout.Finalize();
  Check(layout.is_finalized(), "Finalize did not finalize the layout");
  Check(layout.layout_generation() == 2, "Finalize did not increment generation");
  Check(layout.ActiveSequenceLength(0) == 4, "first active sequence length is incorrect");
  Check(layout.ExcludedBaseCount(0) == 3, "first excluded base count is incorrect");
  Check(layout.ActiveSequenceLength(1) == 2, "second active sequence length is incorrect");

  const std::vector<seqpro::OriginalSequenceInterval> first_sequence_active_intervals =
      layout.ActiveIntervalsById(0);
  Check(first_sequence_active_intervals.size() == 2, "first active run count is incorrect");
  CheckInterval(first_sequence_active_intervals[0], 0, 2, "first active interval is incorrect");
  CheckInterval(first_sequence_active_intervals[1], 5, 7, "second active interval is incorrect");
  const std::vector<seqpro::OriginalSequenceInterval> first_sequence_excluded_intervals =
      layout.ExcludedIntervalsById(0);
  Check(first_sequence_excluded_intervals.size() == 1, "overlapping intervals were not merged");
  CheckInterval(first_sequence_excluded_intervals[0], 2, 5,
                "merged excluded interval is incorrect");

  std::string expected_sequence_text = "AB";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text += "FG";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text += "yz";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kTerminatorByte);
  CheckAllTextAccessPaths(layout, expected_sequence_text);

  Check(!layout.FindActiveSequencePosition(0, 2),
        "excluded original position has an active coordinate");
  Check(!layout.FindTextPosition(0, 4), "excluded original position has a text coordinate");
  Check(layout.FindActiveSequencePosition(0, 5) == seqpro::ActiveSequencePosition{2},
        "second run active coordinate is incorrect");
  Check(layout.FindTextPosition(0, 5) == seqpro::SequenceTextPosition{3},
        "second run text coordinate is incorrect");
  Check(layout.OriginalSequencePosition(0, 2) == 5,
        "active coordinate did not skip the excluded interval");

  const seqpro::SequenceTextGeneration generation = layout.layout_generation();
  layout.Finalize();
  Check(layout.layout_generation() == generation, "idempotent Finalize changed generation");

  layout.ExcludeInterval(0, 2, 5);
  layout.Finalize();
  Check(layout.layout_generation() == generation + 1U,
        "dirty Finalize did not conservatively change generation");
  Check(layout.Materialize().sequence_text_bytes == expected_sequence_text,
        "duplicate exclusion changed normalized layout");

  layout.ClearExcludedIntervals(0);
  layout.Finalize();
  Check(layout.ActiveSequenceLength(0) == 7, "per-sequence clear did not restore active bases");
  Check(layout.ActiveSequenceLength(1) == 2, "per-sequence clear affected another sequence");

  layout.ClearExcludedIntervals("second");
  layout.Finalize();
  Check(layout.Materialize().sequence_text_bytes == BasicExpectedText(),
        "name-based clear did not restore the selected sequence");

  layout.ExcludeIntervals({{0, 1, 2}, {1, 1, 2}});
  layout.Finalize();
  layout.ClearAllExcludedIntervals();
  layout.Finalize();
  Check(layout.Materialize().sequence_text_bytes == BasicExpectedText(),
        "clear-all did not restore the original layout");
}

void TestSequenceSelectionAndFullExclusion() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.DirectoryPath() / "selection.fa";
  const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);

  seqpro::SequenceTextLayout reordered_layout(indexed_fasta, {1, 0});
  std::string reordered_expected_sequence_text = "xyz";
  reordered_expected_sequence_text = AppendControlByte(std::move(reordered_expected_sequence_text),
                                                       seqpro::SequenceTextLayout::kSeparatorByte);
  reordered_expected_sequence_text += "ABCDEFG";
  reordered_expected_sequence_text = AppendControlByte(std::move(reordered_expected_sequence_text),
                                                       seqpro::SequenceTextLayout::kSeparatorByte);
  reordered_expected_sequence_text = AppendControlByte(std::move(reordered_expected_sequence_text),
                                                       seqpro::SequenceTextLayout::kTerminatorByte);
  CheckAllTextAccessPaths(reordered_layout, reordered_expected_sequence_text);
  Check(reordered_layout.FindTextPosition(1, 0) == seqpro::SequenceTextPosition{0} &&
            reordered_layout.FindTextPosition(0, 0) == seqpro::SequenceTextPosition{4},
        "explicit sequence order coordinate mapping is incorrect");

  seqpro::SequenceTextLayout selected_subset_layout(indexed_fasta, {1});
  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound,
                    [&] { selected_subset_layout.ExcludeInterval(0, 0, 1); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound,
                    [&] { selected_subset_layout.ExcludeInterval("first", 0, 1); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound,
                    [&] { static_cast<void>(selected_subset_layout.ActiveSequenceLength(0)); });
  selected_subset_layout.ExcludeInterval(1, 0, 3);
  selected_subset_layout.Finalize();
  Check(selected_subset_layout.active_base_count() == 0,
        "fully excluded layout retained active bases");
  Check(selected_subset_layout.active_run_count() == 0,
        "fully excluded layout retained an active run");
  Check(selected_subset_layout.text_size() == 1, "fully excluded layout is not terminator-only");
  const seqpro::MaterializedSequenceText terminator_only_text =
      selected_subset_layout.Materialize();
  Check(terminator_only_text.sequence_text_bytes.size() == 1 &&
            terminator_only_text.sequence_text_bytes[0] == '\0',
        "fully excluded layout does not contain only a terminator");
  CheckAllTextAccessPaths(selected_subset_layout, std::string(1, '\0'));
  Check(std::holds_alternative<seqpro::SequenceTextTerminatorLocation>(
            selected_subset_layout.LocateTextPosition(0)),
        "terminator-only location is incorrect");

  seqpro::SequenceTextLayout first_fully_excluded(indexed_fasta);
  first_fully_excluded.ExcludeInterval(0, 0, 7);
  first_fully_excluded.Finalize();
  std::string second_only_expected = "xyz";
  second_only_expected = AppendControlByte(std::move(second_only_expected),
                                           seqpro::SequenceTextLayout::kSeparatorByte);
  second_only_expected = AppendControlByte(std::move(second_only_expected),
                                           seqpro::SequenceTextLayout::kTerminatorByte);
  CheckAllTextAccessPaths(first_fully_excluded, second_only_expected);
  Check(first_fully_excluded.FindTextPosition(1, 0) == seqpro::SequenceTextPosition{0},
        "fully excluded sequence retained a text placeholder");

  seqpro::SequenceTextLayout all_fully_excluded(indexed_fasta);
  all_fully_excluded.ExcludeIntervals({{0, 0, 7}, {1, 0, 3}});
  all_fully_excluded.Finalize();
  CheckAllTextAccessPaths(all_fully_excluded, std::string(1, '\0'));

  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { seqpro::SequenceTextLayout duplicate_order(indexed_fasta, {0, 0}); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound,
                    [&] { seqpro::SequenceTextLayout invalid_order(indexed_fasta, {99}); });
}

void TestWrappedFastaRuns() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.DirectoryPath() / "wrapped.fa";
  WriteBinaryFile(fasta_path, ">wrapped\r\nABCD\r\nEFGH\r\nI\r\n");
  seqpro::BuildFastaIndex(fasta_path);
  const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(fasta_path);
  seqpro::SequenceTextLayout layout(indexed_fasta);
  layout.ExcludeInterval(0, 3, 6);
  layout.Finalize();

  std::string expected_sequence_text = "ABC";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text += "GHI";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kTerminatorByte);
  CheckAllTextAccessPaths(layout, expected_sequence_text);
}

void TestLayoutRetainsFastaLifetime() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.DirectoryPath() / "lifetime.fa";
  std::optional<seqpro::SequenceTextLayout> retained_layout;
  {
    const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);
    retained_layout.emplace(indexed_fasta);
  }
  CheckAllTextAccessPaths(*retained_layout, BasicExpectedText());
}

void TestTextIntervalExclusionAndGeneration() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.DirectoryPath() / "generation.fa";
  const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);
  seqpro::SequenceTextLayout layout(indexed_fasta);

  const seqpro::SequenceTextGeneration initial_generation = layout.layout_generation();
  layout.ExcludeTextIntervals(initial_generation, {{1, 3}, {8, 2}});
  Check(!layout.is_finalized(), "text-coordinate exclusions did not dirty the layout");
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.ExcludeTextIntervals(initial_generation, {{0, 1}}); });
  layout.Finalize();

  std::string expected_sequence_text = "A";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text += "EFG";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text += "z";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kTerminatorByte);
  CheckAllTextAccessPaths(layout, expected_sequence_text);

  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.ExcludeTextIntervals(initial_generation, {{0, 1}}); });
  Check(layout.is_finalized(), "stale-generation rejection changed layout state");

  const seqpro::SequenceTextGeneration current_generation = layout.layout_generation();
  const std::string sequence_text_before_invalid_batch = layout.Materialize().sequence_text_bytes;
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.ExcludeTextIntervals(current_generation, {{0, 1}, {1, 1}}); });
  Check(layout.is_finalized(), "invalid text batch partially modified layout state");
  Check(layout.Materialize().sequence_text_bytes == sequence_text_before_invalid_batch,
        "invalid text batch partially modified layout contents");

  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument, [&] { layout.LocateTextInterval(0, 0); });
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.ExcludeTextIntervals(current_generation, {{0, 0}}); });
}

void TestErrorsAndMoveSemantics() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.DirectoryPath() / "errors.fa";
  const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);
  seqpro::SequenceTextLayout layout(indexed_fasta);

  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument, [&] { layout.ExcludeInterval(0, 2, 2); });
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument, [&] { layout.ExcludeInterval(0, 4, 2); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceRangeOutOfBounds,
                    [&] { layout.ExcludeInterval(0, 0, 8); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound,
                    [&] { layout.ExcludeInterval(42, 0, 1); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound,
                    [&] { layout.ExcludeInterval("missing", 0, 1); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceRangeOutOfBounds,
                    [&] { layout.FindTextPosition(0, 7); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceRangeOutOfBounds,
                    [&] { layout.OriginalSequencePosition(0, 7); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceRangeOutOfBounds,
                    [&] { layout.LocateTextPosition(layout.text_size()); });
  ExpectSeqProError(seqpro::ErrorCode::kIntegerOverflow, [&] {
    static_cast<void>(
        layout.LocateTextInterval(std::numeric_limits<seqpro::SequenceTextPosition>::max(), 2));
  });

  const std::string sequence_text_before_invalid_batch = layout.Materialize().sequence_text_bytes;
  ExpectSeqProError(seqpro::ErrorCode::kSequenceRangeOutOfBounds,
                    [&] { layout.ExcludeIntervals({{0, 0, 1}, {1, 0, 4}}); });
  Check(layout.is_finalized() &&
            layout.Materialize().sequence_text_bytes == sequence_text_before_invalid_batch,
        "invalid original-coordinate batch partially changed the layout");

  std::string undersized_destination_buffer(layout.text_size() - 1U, '?');
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument, [&] {
    layout.CopyTextTo(undersized_destination_buffer.data(), undersized_destination_buffer.size());
  });
  std::string oversized_destination_buffer(layout.text_size() + 1U, '?');
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument, [&] {
    layout.CopyTextTo(oversized_destination_buffer.data(), oversized_destination_buffer.size());
  });
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.CopyTextTo(nullptr, layout.text_size()); });
  std::ostringstream output_stream;
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument, [&] { layout.WriteTo(output_stream, 0); });
  std::ostringstream failed_output_stream;
  failed_output_stream.setstate(std::ios::badbit);
  ExpectSeqProError(seqpro::ErrorCode::kIoError, [&] { layout.WriteTo(failed_output_stream, 2); });

  const std::string expected_sequence_text = layout.Materialize().sequence_text_bytes;
  seqpro::SequenceTextLayout move_constructed_layout(std::move(layout));
  Check(move_constructed_layout.Materialize().sequence_text_bytes == expected_sequence_text,
        "move construction lost layout state");
  seqpro::SequenceTextLayout move_assigned_layout(indexed_fasta, {1});
  move_assigned_layout = std::move(move_constructed_layout);
  Check(move_assigned_layout.Materialize().sequence_text_bytes == expected_sequence_text,
        "move assignment lost layout state");
}

void TestReservedBytes() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.DirectoryPath() / "reserved.fa";
  std::string fasta_contents = ">reserved\nA";
  fasta_contents.push_back('\1');
  fasta_contents += "B\n";
  WriteBinaryFile(fasta_path, fasta_contents);
  seqpro::BuildFastaIndex(fasta_path);
  const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(fasta_path);
  seqpro::SequenceTextLayout layout(indexed_fasta);

  ExpectSeqProError(seqpro::ErrorCode::kUnsupportedFileFormat,
                    [&] { static_cast<void>(layout.ReadTextByte(1)); });
  ExpectSeqProError(seqpro::ErrorCode::kUnsupportedFileFormat,
                    [&] { static_cast<void>(layout.Materialize()); });
  std::string destination_buffer(layout.text_size(), '?');
  ExpectSeqProError(seqpro::ErrorCode::kUnsupportedFileFormat, [&] {
    layout.CopyTextTo(destination_buffer.data(), destination_buffer.size());
  });
  std::ostringstream output_stream;
  ExpectSeqProError(seqpro::ErrorCode::kUnsupportedFileFormat,
                    [&] { layout.WriteTo(output_stream); });

  layout.ExcludeInterval(0, 1, 2);
  layout.Finalize();
  std::string expected_sequence_text = "A";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text += "B";
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kSeparatorByte);
  expected_sequence_text = AppendControlByte(std::move(expected_sequence_text),
                                             seqpro::SequenceTextLayout::kTerminatorByte);
  Check(layout.Materialize().sequence_text_bytes == expected_sequence_text,
        "excluding a reserved source byte did not restore valid output");
}

std::uint64_t NextRandom(std::uint64_t* random_state) {
  *random_state ^= *random_state << 13U;
  *random_state ^= *random_state >> 7U;
  *random_state ^= *random_state << 17U;
  return *random_state;
}

struct OracleRun {
  seqpro::SequenceId sequence_id;
  seqpro::SequenceRunIndex sequence_run_index;
  seqpro::SequencePosition original_start_position;
  seqpro::SequencePosition original_end_position;
  seqpro::ActiveSequencePosition active_start_position;
  seqpro::SequenceTextPosition text_start_position;
};

struct SequenceTextOracle {
  std::string sequence_text_bytes;
  std::vector<std::vector<seqpro::OriginalSequenceInterval>> merged_excluded_intervals_by_sequence;
  std::vector<std::vector<OracleRun>> active_runs_by_sequence;
  std::vector<OracleRun> active_runs_in_text_order;
};

SequenceTextOracle BuildSequenceTextOracle(
    const std::vector<std::string>& fasta_sequences,
    std::vector<std::vector<seqpro::OriginalSequenceInterval>> excluded_intervals_by_sequence) {
  SequenceTextOracle sequence_text_oracle;
  sequence_text_oracle.merged_excluded_intervals_by_sequence.resize(fasta_sequences.size());
  sequence_text_oracle.active_runs_by_sequence.resize(fasta_sequences.size());
  seqpro::SequenceTextPosition text_start_position = 0;

  for (std::size_t sequence_index = 0; sequence_index < fasta_sequences.size(); ++sequence_index) {
    std::vector<seqpro::OriginalSequenceInterval>& excluded_intervals =
        excluded_intervals_by_sequence[sequence_index];
    std::sort(excluded_intervals.begin(), excluded_intervals.end(),
              [](const seqpro::OriginalSequenceInterval& left,
                 const seqpro::OriginalSequenceInterval& right) {
                if (left.sequence_start_position != right.sequence_start_position) {
                  return left.sequence_start_position < right.sequence_start_position;
                }
                return left.sequence_end_position < right.sequence_end_position;
              });
    for (const seqpro::OriginalSequenceInterval& excluded_interval : excluded_intervals) {
      std::vector<seqpro::OriginalSequenceInterval>& merged_excluded_intervals =
          sequence_text_oracle.merged_excluded_intervals_by_sequence[sequence_index];
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

    seqpro::SequencePosition original_start_position = 0;
    seqpro::ActiveSequencePosition active_start_position = 0;
    const auto append_active_run = [&](seqpro::SequencePosition active_run_start_position,
                                       seqpro::SequencePosition active_run_end_position) {
      if (active_run_start_position >= active_run_end_position) {
        return;
      }
      const auto sequence_run_index = static_cast<seqpro::SequenceRunIndex>(
          sequence_text_oracle.active_runs_by_sequence[sequence_index].size());
      const OracleRun oracle_active_run{static_cast<seqpro::SequenceId>(sequence_index),
                                        sequence_run_index,
                                        active_run_start_position,
                                        active_run_end_position,
                                        active_start_position,
                                        text_start_position};
      sequence_text_oracle.active_runs_by_sequence[sequence_index].push_back(oracle_active_run);
      sequence_text_oracle.active_runs_in_text_order.push_back(oracle_active_run);
      sequence_text_oracle.sequence_text_bytes.append(
          fasta_sequences[sequence_index].data() + active_run_start_position,
          static_cast<std::size_t>(active_run_end_position - active_run_start_position));
      sequence_text_oracle.sequence_text_bytes.push_back('\1');
      active_start_position += active_run_end_position - active_run_start_position;
      text_start_position += active_run_end_position - active_run_start_position + 1U;
    };

    for (const seqpro::OriginalSequenceInterval& excluded_interval :
         sequence_text_oracle.merged_excluded_intervals_by_sequence[sequence_index]) {
      append_active_run(original_start_position, excluded_interval.sequence_start_position);
      original_start_position = excluded_interval.sequence_end_position;
    }
    append_active_run(original_start_position, fasta_sequences[sequence_index].size());
  }
  sequence_text_oracle.sequence_text_bytes.push_back('\0');
  return sequence_text_oracle;
}

void TestRandomizedOracle() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.DirectoryPath() / "random.fa";
  const std::vector<std::string> fasta_sequences{"ACGTNRYKMSWBDHVac", "ttgca*.-?ACGTNRYKMSWBDH",
                                                 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcde"};
  std::string fasta_contents;
  for (std::size_t sequence_index = 0; sequence_index < fasta_sequences.size(); ++sequence_index) {
    fasta_contents += ">random_" + std::to_string(sequence_index) + "\n";
    fasta_contents += fasta_sequences[sequence_index] + "\n";
  }
  WriteBinaryFile(fasta_path, fasta_contents);
  seqpro::BuildFastaIndex(fasta_path);
  const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(fasta_path);

  std::uint64_t random_state = 0x123456789abcdef0ULL;
  for (int trial_index = 0; trial_index < 3000; ++trial_index) {
    seqpro::SequenceTextLayout sequence_text_layout(indexed_fasta);
    std::vector<seqpro::ExcludedSequenceInterval> excluded_interval_batch;
    std::vector<std::vector<seqpro::OriginalSequenceInterval>> oracle_excluded_intervals(
        fasta_sequences.size());
    for (std::size_t sequence_index = 0; sequence_index < fasta_sequences.size();
         ++sequence_index) {
      const std::size_t generated_excluded_interval_count = NextRandom(&random_state) % 9U;
      for (std::size_t interval_index = 0; interval_index < generated_excluded_interval_count;
           ++interval_index) {
        const std::size_t sequence_start_position =
            NextRandom(&random_state) % fasta_sequences[sequence_index].size();
        const std::size_t maximum_length =
            fasta_sequences[sequence_index].size() - sequence_start_position;
        const std::size_t interval_length = 1U + NextRandom(&random_state) % maximum_length;
        const std::size_t sequence_end_position = sequence_start_position + interval_length;
        excluded_interval_batch.push_back(
            seqpro::ExcludedSequenceInterval{static_cast<seqpro::SequenceId>(sequence_index),
                                             sequence_start_position, sequence_end_position});
        oracle_excluded_intervals[sequence_index].push_back(
            seqpro::OriginalSequenceInterval{sequence_start_position, sequence_end_position});
      }
    }
    sequence_text_layout.ExcludeIntervals(excluded_interval_batch);
    if (!excluded_interval_batch.empty()) {
      sequence_text_layout.Finalize();
    }

    const SequenceTextOracle sequence_text_oracle =
        BuildSequenceTextOracle(fasta_sequences, oracle_excluded_intervals);
    CheckAllTextAccessPaths(sequence_text_layout, sequence_text_oracle.sequence_text_bytes);
    Check(sequence_text_layout.active_run_count() ==
              sequence_text_oracle.active_runs_in_text_order.size(),
          "randomized run count differs from oracle");

    for (std::size_t sequence_index = 0; sequence_index < fasta_sequences.size();
         ++sequence_index) {
      const auto& oracle_active_runs = sequence_text_oracle.active_runs_by_sequence[sequence_index];
      for (std::size_t original_sequence_position = 0;
           original_sequence_position < fasta_sequences[sequence_index].size();
           ++original_sequence_position) {
        const OracleRun* expected_active_run = nullptr;
        for (const OracleRun& oracle_active_run : oracle_active_runs) {
          if (original_sequence_position >= oracle_active_run.original_start_position &&
              original_sequence_position < oracle_active_run.original_end_position) {
            expected_active_run = &oracle_active_run;
            break;
          }
        }
        const auto actual_active_sequence_position =
            sequence_text_layout.FindActiveSequencePosition(
                static_cast<seqpro::SequenceId>(sequence_index), original_sequence_position);
        const auto actual_text_position = sequence_text_layout.FindTextPosition(
            static_cast<seqpro::SequenceId>(sequence_index), original_sequence_position);
        if (expected_active_run == nullptr) {
          Check(!actual_active_sequence_position && !actual_text_position,
                "randomized excluded position unexpectedly mapped");
        } else {
          const std::uint64_t active_run_offset =
              original_sequence_position - expected_active_run->original_start_position;
          Check(actual_active_sequence_position ==
                    expected_active_run->active_start_position + active_run_offset,
                "randomized active mapping differs from oracle");
          Check(
              actual_text_position == expected_active_run->text_start_position + active_run_offset,
              "randomized text mapping differs from oracle");
          Check(sequence_text_layout.OriginalSequencePosition(
                    static_cast<seqpro::SequenceId>(sequence_index),
                    *actual_active_sequence_position) == original_sequence_position,
                "randomized active round-trip failed");
          Check(sequence_text_layout.TextPositionFromActive(
                    static_cast<seqpro::SequenceId>(sequence_index),
                    *actual_active_sequence_position) == *actual_text_position,
                "randomized active-to-text mapping differs from oracle");
        }
      }
    }

    for (std::size_t text_position = 0;
         text_position < sequence_text_oracle.sequence_text_bytes.size(); ++text_position) {
      Check(sequence_text_layout.ReadTextByte(text_position) ==
                static_cast<std::uint8_t>(static_cast<unsigned char>(
                    sequence_text_oracle.sequence_text_bytes[text_position])),
            "randomized byte query differs from oracle");
      const seqpro::SequenceTextLocation text_location =
          sequence_text_layout.LocateTextPosition(text_position);
      if (text_position + 1U == sequence_text_oracle.sequence_text_bytes.size()) {
        Check(std::holds_alternative<seqpro::SequenceTextTerminatorLocation>(text_location),
              "randomized terminator location is incorrect");
        continue;
      }
      const OracleRun* expected_active_run = nullptr;
      bool is_expected_separator = false;
      for (const OracleRun& oracle_active_run : sequence_text_oracle.active_runs_in_text_order) {
        const std::uint64_t active_run_length =
            oracle_active_run.original_end_position - oracle_active_run.original_start_position;
        if (text_position >= oracle_active_run.text_start_position &&
            text_position < oracle_active_run.text_start_position + active_run_length) {
          expected_active_run = &oracle_active_run;
          break;
        }
        if (text_position == oracle_active_run.text_start_position + active_run_length) {
          expected_active_run = &oracle_active_run;
          is_expected_separator = true;
          break;
        }
      }
      Check(expected_active_run != nullptr, "oracle did not locate randomized text position");
      if (is_expected_separator) {
        const auto* separator_location =
            std::get_if<seqpro::SequenceTextSeparatorLocation>(&text_location);
        Check(
            separator_location != nullptr &&
                separator_location->preceding_sequence_id == expected_active_run->sequence_id &&
                separator_location->preceding_run_index == expected_active_run->sequence_run_index,
            "randomized separator location differs from oracle");
      } else {
        const auto* base_location = std::get_if<seqpro::SequenceTextBaseLocation>(&text_location);
        const std::uint64_t active_run_offset =
            text_position - expected_active_run->text_start_position;
        Check(base_location != nullptr &&
                  base_location->sequence_id == expected_active_run->sequence_id &&
                  base_location->original_sequence_position ==
                      expected_active_run->original_start_position + active_run_offset &&
                  base_location->active_sequence_position ==
                      expected_active_run->active_start_position + active_run_offset,
              "randomized base location differs from oracle");
        const std::optional<seqpro::LocatedSequenceInterval> one_base_interval =
            sequence_text_layout.LocateTextInterval(text_position, 1);
        Check(
            one_base_interval &&
                one_base_interval->sequence_id == expected_active_run->sequence_id &&
                one_base_interval->sequence_run_index == expected_active_run->sequence_run_index &&
                one_base_interval->original_sequence_start_position ==
                    expected_active_run->original_start_position + active_run_offset &&
                one_base_interval->active_sequence_start_position ==
                    expected_active_run->active_start_position + active_run_offset &&
                one_base_interval->interval_length == 1,
            "randomized text interval location differs from oracle");
      }
    }

    for (const OracleRun& oracle_active_run : sequence_text_oracle.active_runs_in_text_order) {
      const std::uint64_t active_run_length =
          oracle_active_run.original_end_position - oracle_active_run.original_start_position;
      for (std::uint64_t active_run_offset = 0; active_run_offset < active_run_length;
           ++active_run_offset) {
        const std::uint64_t remaining_length = active_run_length - active_run_offset;
        const std::optional<seqpro::LocatedSequenceInterval> located_interval =
            sequence_text_layout.LocateTextInterval(
                oracle_active_run.text_start_position + active_run_offset, remaining_length);
        Check(located_interval && located_interval->sequence_id == oracle_active_run.sequence_id &&
                  located_interval->sequence_run_index == oracle_active_run.sequence_run_index &&
                  located_interval->original_sequence_start_position ==
                      oracle_active_run.original_start_position + active_run_offset &&
                  located_interval->active_sequence_start_position ==
                      oracle_active_run.active_start_position + active_run_offset &&
                  located_interval->interval_length == remaining_length,
              "randomized maximal in-run interval differs from oracle");
        Check(!sequence_text_layout.LocateTextInterval(
                  oracle_active_run.text_start_position + active_run_offset, remaining_length + 1U),
              "randomized interval crossed a run boundary");
      }
    }

    if (!excluded_interval_batch.empty() && trial_index % 20 == 0) {
      sequence_text_layout.ClearAllExcludedIntervals();
      sequence_text_layout.Finalize();
      const SequenceTextOracle cleared_sequence_text_oracle = BuildSequenceTextOracle(
          fasta_sequences,
          std::vector<std::vector<seqpro::OriginalSequenceInterval>>(fasta_sequences.size()));
      Check(sequence_text_layout.Materialize().sequence_text_bytes ==
                cleared_sequence_text_oracle.sequence_text_bytes,
            "randomized clear-all layout differs from oracle");

      sequence_text_layout.ExcludeIntervals(excluded_interval_batch);
      sequence_text_layout.Finalize();
      Check(sequence_text_layout.Materialize().sequence_text_bytes ==
                sequence_text_oracle.sequence_text_bytes,
            "randomized re-added layout differs from oracle");
    }
  }
}

void TestConcurrentFinalizedQueries() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.DirectoryPath() / "concurrent.fa";
  const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);
  seqpro::SequenceTextLayout layout(indexed_fasta);
  layout.ExcludeIntervals({{0, 2, 4}, {1, 1, 2}});
  layout.Finalize();
  const std::string expected_sequence_text = layout.Materialize().sequence_text_bytes;

  for (const int thread_count : {1, 2, 8, 32}) {
    std::atomic<bool> observed_failure{false};
    std::vector<std::thread> worker_threads;
    worker_threads.reserve(static_cast<std::size_t>(thread_count));
    for (int worker_index = 0; worker_index < thread_count; ++worker_index) {
      worker_threads.emplace_back([&, worker_index] {
        try {
          for (int iteration = 0; iteration < 300; ++iteration) {
            const std::size_t text_position =
                static_cast<std::size_t>(worker_index + iteration) % expected_sequence_text.size();
            if (layout.ReadTextByte(text_position) !=
                static_cast<std::uint8_t>(
                    static_cast<unsigned char>(expected_sequence_text[text_position]))) {
              observed_failure.store(true);
            }
            static_cast<void>(layout.LocateTextPosition(text_position));
            const seqpro::SequenceTextLocation location = layout.LocateTextPosition(text_position);
            if (const auto* base_location =
                    std::get_if<seqpro::SequenceTextBaseLocation>(&location)) {
              if (layout.FindTextPosition(base_location->sequence_id,
                                          base_location->original_sequence_position) !=
                      text_position ||
                  !layout.LocateTextInterval(text_position, 1)) {
                observed_failure.store(true);
              }
            }
            if (iteration % 50 == 0 &&
                layout.Materialize().sequence_text_bytes != expected_sequence_text) {
              observed_failure.store(true);
            }
          }
        } catch (...) {
          observed_failure.store(true);
        }
      });
    }
    for (std::thread& worker_thread : worker_threads) {
      worker_thread.join();
    }
    Check(!observed_failure.load(), "concurrent finalized query returned inconsistent data");
  }
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string_view, std::function<void()>>> test_cases{
      {"basic layout and coordinates", TestBasicLayoutAndCoordinates},
      {"exclusion finalize and clear", TestExclusionFinalizeAndClear},
      {"sequence selection and full exclusion", TestSequenceSelectionAndFullExclusion},
      {"wrapped FASTA runs", TestWrappedFastaRuns},
      {"layout retains FASTA lifetime", TestLayoutRetainsFastaLifetime},
      {"text interval exclusion and generation", TestTextIntervalExclusionAndGeneration},
      {"errors and move semantics", TestErrorsAndMoveSemantics},
      {"reserved bytes", TestReservedBytes},
      {"randomized oracle", TestRandomizedOracle},
      {"concurrent finalized queries", TestConcurrentFinalizedQueries},
  };

  std::size_t failed_test_count = 0;
  for (const auto& test_case : test_cases) {
    try {
      test_case.second();
      std::cout << "[PASS] " << test_case.first << '\n';
    } catch (const std::exception& test_error) {
      ++failed_test_count;
      std::cerr << "[FAIL] " << test_case.first << ": " << test_error.what() << '\n';
    }
  }
  if (failed_test_count != 0) {
    std::cerr << failed_test_count << " test group(s) failed\n";
    return 1;
  }
  return 0;
}
