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
    std::string path_template = "/tmp/seqpro-sequence-text-test-XXXXXX";
    char* const created_path = mkdtemp(path_template.data());
    if (created_path == nullptr) {
      throw std::runtime_error("cannot create unique test directory");
    }
    path_ = created_path;
  }

  ~TemporaryDirectory() {
    std::error_code cleanup_error;
    std::filesystem::remove_all(path_, cleanup_error);
  }

  const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

void Check(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void WriteBinaryFile(const std::filesystem::path& file_path, std::string_view contents) {
  std::ofstream output_stream(file_path, std::ios::binary | std::ios::trunc);
  output_stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output_stream) {
    throw std::runtime_error("cannot write test fixture: " + file_path.string());
  }
}

template <typename Function>
void ExpectSeqProError(seqpro::ErrorCode expected_error_code, Function&& operation) {
  try {
    operation();
  } catch (const seqpro::SeqProError& error) {
    Check(error.error_code() == expected_error_code, "unexpected SeqPro error code");
    return;
  }
  throw std::runtime_error("expected SeqProError was not thrown");
}

std::string AppendControlByte(std::string bytes, std::uint8_t control_byte) {
  bytes.push_back(static_cast<char>(control_byte));
  return bytes;
}

std::string BasicExpectedText() {
  std::string expected = "ABCDEFG";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected += "xyz";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  return AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kTerminatorByte);
}

seqpro::IndexedFasta BuildBasicFasta(const std::filesystem::path& fasta_path) {
  WriteBinaryFile(fasta_path, ">first description\nABCDEFG\n>second\nxyz\n");
  seqpro::BuildFastaIndex(fasta_path);
  return seqpro::IndexedFasta::Open(fasta_path);
}

void CheckAllTextAccessPaths(const seqpro::SequenceTextLayout& layout,
                             const std::string& expected) {
  Check(layout.Materialize().bytes == expected, "Materialize differs from expected text");
  Check(layout.text_size() == expected.size(), "text_size differs from expected text");

  std::string copied(expected.size(), '?');
  layout.CopyTextTo(copied.data(), copied.size());
  Check(copied == expected, "CopyTextTo differs from expected text");

  std::ostringstream output_stream;
  layout.WriteTo(output_stream, 2);
  Check(output_stream.str() == expected, "WriteTo differs from expected text");

  for (std::size_t text_position = 0; text_position < expected.size(); ++text_position) {
    Check(layout.ReadTextByte(text_position) ==
              static_cast<std::uint8_t>(static_cast<unsigned char>(expected[text_position])),
          "ReadTextByte differs from expected text");
  }
}

void CheckInterval(const seqpro::OriginalSequenceInterval& interval,
                   seqpro::SequencePosition expected_start, seqpro::SequencePosition expected_end,
                   std::string_view message) {
  Check(interval.sequence_start == expected_start && interval.sequence_end == expected_end,
        message);
}

void TestBasicLayoutAndCoordinates() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "basic.fa";
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

  const std::string expected = BasicExpectedText();
  const seqpro::MaterializedSequenceText materialized = layout.Materialize();
  Check(materialized.bytes == expected, "materialized text is incorrect");
  Check(materialized.layout_generation == 1, "materialized generation is incorrect");
  CheckAllTextAccessPaths(layout, expected);

  const seqpro::SequenceTextLocation first_location = layout.LocateTextPosition(0);
  const auto* first_base = std::get_if<seqpro::SequenceTextBaseLocation>(&first_location);
  Check(first_base != nullptr && first_base->sequence_id == 0 &&
            first_base->sequence_run_index == 0 && first_base->original_sequence_position == 0 &&
            first_base->active_sequence_position == 0,
        "first base location is incorrect");

  const seqpro::SequenceTextLocation first_separator_location = layout.LocateTextPosition(7);
  const auto* first_separator =
      std::get_if<seqpro::SequenceTextSeparatorLocation>(&first_separator_location);
  Check(first_separator != nullptr && first_separator->preceding_sequence_id == 0 &&
            first_separator->preceding_run_index == 0,
        "first separator location is incorrect");
  Check(std::holds_alternative<seqpro::SequenceTextTerminatorLocation>(
            layout.LocateTextPosition(expected.size() - 1U)),
        "terminator location is incorrect");

  Check(layout.FindActiveSequencePosition(0, 6) == seqpro::ActiveSequencePosition{6},
        "original-to-active mapping is incorrect");
  Check(layout.OriginalSequencePosition(1, 2) == 2, "active-to-original mapping is incorrect");
  Check(layout.FindTextPosition(0, 6) == seqpro::SequenceTextPosition{6},
        "original-to-text mapping is incorrect");
  Check(layout.FindTextPosition(1, 0) == seqpro::SequenceTextPosition{8},
        "second-sequence text start is incorrect");
  Check(layout.TextPositionFromActive(1, 2) == 10, "active-to-text mapping is incorrect");

  const std::optional<seqpro::LocatedSequenceInterval> located = layout.LocateTextInterval(1, 3);
  Check(located && located->sequence_id == 0 && located->original_sequence_start == 1 &&
            located->active_sequence_start == 1 && located->interval_length == 3,
        "text interval location is incorrect");
  Check(!layout.LocateTextInterval(6, 2), "interval crossing a separator unexpectedly resolved");
  Check(!layout.LocateTextInterval(7, 1), "separator interval unexpectedly resolved");
  Check(!layout.LocateTextInterval(expected.size() - 1U, 1),
        "terminator interval unexpectedly resolved");
}

void TestExclusionFinalizeAndClear() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "excluded.fa";
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

  const std::vector<seqpro::OriginalSequenceInterval> first_active = layout.ActiveIntervalsById(0);
  Check(first_active.size() == 2, "first active run count is incorrect");
  CheckInterval(first_active[0], 0, 2, "first active interval is incorrect");
  CheckInterval(first_active[1], 5, 7, "second active interval is incorrect");
  const std::vector<seqpro::OriginalSequenceInterval> first_excluded =
      layout.ExcludedIntervalsById(0);
  Check(first_excluded.size() == 1, "overlapping intervals were not merged");
  CheckInterval(first_excluded[0], 2, 5, "merged excluded interval is incorrect");

  std::string expected = "AB";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected += "FG";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected += "yz";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kTerminatorByte);
  CheckAllTextAccessPaths(layout, expected);

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
  Check(layout.Materialize().bytes == expected, "duplicate exclusion changed normalized layout");

  layout.ClearExcludedIntervals(0);
  layout.Finalize();
  Check(layout.ActiveSequenceLength(0) == 7, "per-sequence clear did not restore active bases");
  Check(layout.ActiveSequenceLength(1) == 2, "per-sequence clear affected another sequence");

  layout.ClearExcludedIntervals("second");
  layout.Finalize();
  Check(layout.Materialize().bytes == BasicExpectedText(),
        "name-based clear did not restore the selected sequence");

  layout.ExcludeIntervals({{0, 1, 2}, {1, 1, 2}});
  layout.Finalize();
  layout.ClearAllExcludedIntervals();
  layout.Finalize();
  Check(layout.Materialize().bytes == BasicExpectedText(),
        "clear-all did not restore the original layout");
}

void TestSequenceSelectionAndFullExclusion() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "selection.fa";
  const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);

  seqpro::SequenceTextLayout reordered(indexed_fasta, {1, 0});
  std::string reordered_expected = "xyz";
  reordered_expected =
      AppendControlByte(std::move(reordered_expected), seqpro::SequenceTextLayout::kSeparatorByte);
  reordered_expected += "ABCDEFG";
  reordered_expected =
      AppendControlByte(std::move(reordered_expected), seqpro::SequenceTextLayout::kSeparatorByte);
  reordered_expected =
      AppendControlByte(std::move(reordered_expected), seqpro::SequenceTextLayout::kTerminatorByte);
  CheckAllTextAccessPaths(reordered, reordered_expected);
  Check(reordered.FindTextPosition(1, 0) == seqpro::SequenceTextPosition{0} &&
            reordered.FindTextPosition(0, 0) == seqpro::SequenceTextPosition{4},
        "explicit sequence order coordinate mapping is incorrect");

  seqpro::SequenceTextLayout subset(indexed_fasta, {1});
  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound, [&] { subset.ExcludeInterval(0, 0, 1); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound,
                    [&] { subset.ExcludeInterval("first", 0, 1); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound,
                    [&] { static_cast<void>(subset.ActiveSequenceLength(0)); });
  subset.ExcludeInterval(1, 0, 3);
  subset.Finalize();
  Check(subset.active_base_count() == 0, "fully excluded layout retained active bases");
  Check(subset.active_run_count() == 0, "fully excluded layout retained an active run");
  Check(subset.text_size() == 1, "fully excluded layout is not terminator-only");
  const seqpro::MaterializedSequenceText empty_text = subset.Materialize();
  Check(empty_text.bytes.size() == 1 && empty_text.bytes[0] == '\0',
        "fully excluded layout does not contain only a terminator");
  CheckAllTextAccessPaths(subset, std::string(1, '\0'));
  Check(
      std::holds_alternative<seqpro::SequenceTextTerminatorLocation>(subset.LocateTextPosition(0)),
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
  const std::filesystem::path fasta_path = temporary_directory.path() / "wrapped.fa";
  WriteBinaryFile(fasta_path, ">wrapped\r\nABCD\r\nEFGH\r\nI\r\n");
  seqpro::BuildFastaIndex(fasta_path);
  const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(fasta_path);
  seqpro::SequenceTextLayout layout(indexed_fasta);
  layout.ExcludeInterval(0, 3, 6);
  layout.Finalize();

  std::string expected = "ABC";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected += "GHI";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kTerminatorByte);
  CheckAllTextAccessPaths(layout, expected);
}

void TestLayoutRetainsFastaLifetime() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "lifetime.fa";
  std::optional<seqpro::SequenceTextLayout> retained_layout;
  {
    const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);
    retained_layout.emplace(indexed_fasta);
  }
  CheckAllTextAccessPaths(*retained_layout, BasicExpectedText());
}

void TestTextIntervalExclusionAndGeneration() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "generation.fa";
  const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);
  seqpro::SequenceTextLayout layout(indexed_fasta);

  const seqpro::SequenceTextGeneration initial_generation = layout.layout_generation();
  layout.ExcludeTextIntervals(initial_generation, {{1, 3}, {8, 2}});
  Check(!layout.is_finalized(), "text-coordinate exclusions did not dirty the layout");
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.ExcludeTextIntervals(initial_generation, {{0, 1}}); });
  layout.Finalize();

  std::string expected = "A";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected += "EFG";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected += "z";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kTerminatorByte);
  CheckAllTextAccessPaths(layout, expected);

  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.ExcludeTextIntervals(initial_generation, {{0, 1}}); });
  Check(layout.is_finalized(), "stale-generation rejection changed layout state");

  const seqpro::SequenceTextGeneration current_generation = layout.layout_generation();
  const std::string before_invalid_batch = layout.Materialize().bytes;
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.ExcludeTextIntervals(current_generation, {{0, 1}, {1, 1}}); });
  Check(layout.is_finalized(), "invalid text batch partially modified layout state");
  Check(layout.Materialize().bytes == before_invalid_batch,
        "invalid text batch partially modified layout contents");

  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument, [&] { layout.LocateTextInterval(0, 0); });
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.ExcludeTextIntervals(current_generation, {{0, 0}}); });
}

void TestErrorsAndMoveSemantics() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "errors.fa";
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

  const std::string before_invalid_batch = layout.Materialize().bytes;
  ExpectSeqProError(seqpro::ErrorCode::kSequenceRangeOutOfBounds,
                    [&] { layout.ExcludeIntervals({{0, 0, 1}, {1, 0, 4}}); });
  Check(layout.is_finalized() && layout.Materialize().bytes == before_invalid_batch,
        "invalid original-coordinate batch partially changed the layout");

  std::string wrong_size(layout.text_size() - 1U, '?');
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.CopyTextTo(wrong_size.data(), wrong_size.size()); });
  std::string oversized(layout.text_size() + 1U, '?');
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.CopyTextTo(oversized.data(), oversized.size()); });
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { layout.CopyTextTo(nullptr, layout.text_size()); });
  std::ostringstream output_stream;
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument, [&] { layout.WriteTo(output_stream, 0); });
  std::ostringstream failed_output_stream;
  failed_output_stream.setstate(std::ios::badbit);
  ExpectSeqProError(seqpro::ErrorCode::kIoError, [&] { layout.WriteTo(failed_output_stream, 2); });

  const std::string expected = layout.Materialize().bytes;
  seqpro::SequenceTextLayout move_constructed(std::move(layout));
  Check(move_constructed.Materialize().bytes == expected, "move construction lost layout state");
  seqpro::SequenceTextLayout move_assigned(indexed_fasta, {1});
  move_assigned = std::move(move_constructed);
  Check(move_assigned.Materialize().bytes == expected, "move assignment lost layout state");
}

void TestReservedBytes() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "reserved.fa";
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
  std::string destination(layout.text_size(), '?');
  ExpectSeqProError(seqpro::ErrorCode::kUnsupportedFileFormat,
                    [&] { layout.CopyTextTo(destination.data(), destination.size()); });
  std::ostringstream output_stream;
  ExpectSeqProError(seqpro::ErrorCode::kUnsupportedFileFormat,
                    [&] { layout.WriteTo(output_stream); });

  layout.ExcludeInterval(0, 1, 2);
  layout.Finalize();
  std::string expected = "A";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected += "B";
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kSeparatorByte);
  expected = AppendControlByte(std::move(expected), seqpro::SequenceTextLayout::kTerminatorByte);
  Check(layout.Materialize().bytes == expected,
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
  seqpro::SequencePosition original_start;
  seqpro::SequencePosition original_end;
  seqpro::ActiveSequencePosition active_start;
  seqpro::SequenceTextPosition text_start;
};

struct OracleLayout {
  std::string bytes;
  std::vector<std::vector<seqpro::OriginalSequenceInterval>> excluded;
  std::vector<std::vector<OracleRun>> runs_by_sequence;
  std::vector<OracleRun> global_runs;
};

OracleLayout BuildOracle(const std::vector<std::string>& sequences,
                         std::vector<std::vector<seqpro::OriginalSequenceInterval>> excluded) {
  OracleLayout oracle;
  oracle.excluded.resize(sequences.size());
  oracle.runs_by_sequence.resize(sequences.size());
  seqpro::SequenceTextPosition text_start = 0;

  for (std::size_t sequence_index = 0; sequence_index < sequences.size(); ++sequence_index) {
    std::vector<seqpro::OriginalSequenceInterval>& intervals = excluded[sequence_index];
    std::sort(intervals.begin(), intervals.end(),
              [](const seqpro::OriginalSequenceInterval& left,
                 const seqpro::OriginalSequenceInterval& right) {
                if (left.sequence_start != right.sequence_start) {
                  return left.sequence_start < right.sequence_start;
                }
                return left.sequence_end < right.sequence_end;
              });
    for (const seqpro::OriginalSequenceInterval& interval : intervals) {
      if (oracle.excluded[sequence_index].empty() ||
          oracle.excluded[sequence_index].back().sequence_end < interval.sequence_start) {
        oracle.excluded[sequence_index].push_back(interval);
      } else {
        oracle.excluded[sequence_index].back().sequence_end =
            std::max(oracle.excluded[sequence_index].back().sequence_end, interval.sequence_end);
      }
    }

    seqpro::SequencePosition original_start = 0;
    seqpro::ActiveSequencePosition active_start = 0;
    const auto append_run = [&](seqpro::SequencePosition run_start,
                                seqpro::SequencePosition run_end) {
      if (run_start >= run_end) {
        return;
      }
      const auto run_index =
          static_cast<seqpro::SequenceRunIndex>(oracle.runs_by_sequence[sequence_index].size());
      const OracleRun run{static_cast<seqpro::SequenceId>(sequence_index),
                          run_index,
                          run_start,
                          run_end,
                          active_start,
                          text_start};
      oracle.runs_by_sequence[sequence_index].push_back(run);
      oracle.global_runs.push_back(run);
      oracle.bytes.append(sequences[sequence_index].data() + run_start,
                          static_cast<std::size_t>(run_end - run_start));
      oracle.bytes.push_back('\1');
      active_start += run_end - run_start;
      text_start += run_end - run_start + 1U;
    };

    for (const seqpro::OriginalSequenceInterval& interval : oracle.excluded[sequence_index]) {
      append_run(original_start, interval.sequence_start);
      original_start = interval.sequence_end;
    }
    append_run(original_start, sequences[sequence_index].size());
  }
  oracle.bytes.push_back('\0');
  return oracle;
}

void TestRandomizedOracle() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "random.fa";
  const std::vector<std::string> sequences{"ACGTNRYKMSWBDHVac", "ttgca*.-?ACGTNRYKMSWBDH",
                                           "ABCDEFGHIJKLMNOPQRSTUVWXYZabcde"};
  std::string fasta_contents;
  for (std::size_t sequence_index = 0; sequence_index < sequences.size(); ++sequence_index) {
    fasta_contents += ">random_" + std::to_string(sequence_index) + "\n";
    fasta_contents += sequences[sequence_index] + "\n";
  }
  WriteBinaryFile(fasta_path, fasta_contents);
  seqpro::BuildFastaIndex(fasta_path);
  const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(fasta_path);

  std::uint64_t random_state = 0x123456789abcdef0ULL;
  for (int trial = 0; trial < 3000; ++trial) {
    seqpro::SequenceTextLayout layout(indexed_fasta);
    std::vector<seqpro::ExcludedSequenceInterval> batch;
    std::vector<std::vector<seqpro::OriginalSequenceInterval>> oracle_intervals(sequences.size());
    for (std::size_t sequence_index = 0; sequence_index < sequences.size(); ++sequence_index) {
      const std::size_t interval_count = NextRandom(&random_state) % 9U;
      for (std::size_t interval_index = 0; interval_index < interval_count; ++interval_index) {
        const std::size_t sequence_start =
            NextRandom(&random_state) % sequences[sequence_index].size();
        const std::size_t maximum_length = sequences[sequence_index].size() - sequence_start;
        const std::size_t interval_length = 1U + NextRandom(&random_state) % maximum_length;
        const std::size_t sequence_end = sequence_start + interval_length;
        batch.push_back(seqpro::ExcludedSequenceInterval{
            static_cast<seqpro::SequenceId>(sequence_index), sequence_start, sequence_end});
        oracle_intervals[sequence_index].push_back(
            seqpro::OriginalSequenceInterval{sequence_start, sequence_end});
      }
    }
    layout.ExcludeIntervals(batch);
    if (!batch.empty()) {
      layout.Finalize();
    }

    const OracleLayout oracle = BuildOracle(sequences, oracle_intervals);
    CheckAllTextAccessPaths(layout, oracle.bytes);
    Check(layout.active_run_count() == oracle.global_runs.size(),
          "randomized run count differs from oracle");

    for (std::size_t sequence_index = 0; sequence_index < sequences.size(); ++sequence_index) {
      const auto& oracle_runs = oracle.runs_by_sequence[sequence_index];
      for (std::size_t original_position = 0; original_position < sequences[sequence_index].size();
           ++original_position) {
        const OracleRun* expected_run = nullptr;
        for (const OracleRun& run : oracle_runs) {
          if (original_position >= run.original_start && original_position < run.original_end) {
            expected_run = &run;
            break;
          }
        }
        const auto active_position = layout.FindActiveSequencePosition(
            static_cast<seqpro::SequenceId>(sequence_index), original_position);
        const auto text_position = layout.FindTextPosition(
            static_cast<seqpro::SequenceId>(sequence_index), original_position);
        if (expected_run == nullptr) {
          Check(!active_position && !text_position,
                "randomized excluded position unexpectedly mapped");
        } else {
          const std::uint64_t run_offset = original_position - expected_run->original_start;
          Check(active_position == expected_run->active_start + run_offset,
                "randomized active mapping differs from oracle");
          Check(text_position == expected_run->text_start + run_offset,
                "randomized text mapping differs from oracle");
          Check(layout.OriginalSequencePosition(static_cast<seqpro::SequenceId>(sequence_index),
                                                *active_position) == original_position,
                "randomized active round-trip failed");
          Check(layout.TextPositionFromActive(static_cast<seqpro::SequenceId>(sequence_index),
                                              *active_position) == *text_position,
                "randomized active-to-text mapping differs from oracle");
        }
      }
    }

    for (std::size_t text_position = 0; text_position < oracle.bytes.size(); ++text_position) {
      Check(layout.ReadTextByte(text_position) ==
                static_cast<std::uint8_t>(static_cast<unsigned char>(oracle.bytes[text_position])),
            "randomized byte query differs from oracle");
      const seqpro::SequenceTextLocation location = layout.LocateTextPosition(text_position);
      if (text_position + 1U == oracle.bytes.size()) {
        Check(std::holds_alternative<seqpro::SequenceTextTerminatorLocation>(location),
              "randomized terminator location is incorrect");
        continue;
      }
      const OracleRun* expected_run = nullptr;
      bool expected_separator = false;
      for (const OracleRun& run : oracle.global_runs) {
        const std::uint64_t run_length = run.original_end - run.original_start;
        if (text_position >= run.text_start && text_position < run.text_start + run_length) {
          expected_run = &run;
          break;
        }
        if (text_position == run.text_start + run_length) {
          expected_run = &run;
          expected_separator = true;
          break;
        }
      }
      Check(expected_run != nullptr, "oracle did not locate randomized text position");
      if (expected_separator) {
        const auto* separator = std::get_if<seqpro::SequenceTextSeparatorLocation>(&location);
        Check(separator != nullptr &&
                  separator->preceding_sequence_id == expected_run->sequence_id &&
                  separator->preceding_run_index == expected_run->sequence_run_index,
              "randomized separator location differs from oracle");
      } else {
        const auto* base = std::get_if<seqpro::SequenceTextBaseLocation>(&location);
        const std::uint64_t run_offset = text_position - expected_run->text_start;
        Check(base != nullptr && base->sequence_id == expected_run->sequence_id &&
                  base->original_sequence_position == expected_run->original_start + run_offset &&
                  base->active_sequence_position == expected_run->active_start + run_offset,
              "randomized base location differs from oracle");
        const std::optional<seqpro::LocatedSequenceInterval> one_base_interval =
            layout.LocateTextInterval(text_position, 1);
        Check(one_base_interval && one_base_interval->sequence_id == expected_run->sequence_id &&
                  one_base_interval->sequence_run_index == expected_run->sequence_run_index &&
                  one_base_interval->original_sequence_start ==
                      expected_run->original_start + run_offset &&
                  one_base_interval->active_sequence_start ==
                      expected_run->active_start + run_offset &&
                  one_base_interval->interval_length == 1,
              "randomized text interval location differs from oracle");
      }
    }

    for (const OracleRun& run : oracle.global_runs) {
      const std::uint64_t run_length = run.original_end - run.original_start;
      for (std::uint64_t run_offset = 0; run_offset < run_length; ++run_offset) {
        const std::uint64_t remaining_length = run_length - run_offset;
        const std::optional<seqpro::LocatedSequenceInterval> located_interval =
            layout.LocateTextInterval(run.text_start + run_offset, remaining_length);
        Check(located_interval && located_interval->sequence_id == run.sequence_id &&
                  located_interval->sequence_run_index == run.sequence_run_index &&
                  located_interval->original_sequence_start == run.original_start + run_offset &&
                  located_interval->active_sequence_start == run.active_start + run_offset &&
                  located_interval->interval_length == remaining_length,
              "randomized maximal in-run interval differs from oracle");
        Check(!layout.LocateTextInterval(run.text_start + run_offset, remaining_length + 1U),
              "randomized interval crossed a run boundary");
      }
    }

    if (!batch.empty() && trial % 20 == 0) {
      layout.ClearAllExcludedIntervals();
      layout.Finalize();
      const OracleLayout cleared_oracle = BuildOracle(
          sequences, std::vector<std::vector<seqpro::OriginalSequenceInterval>>(sequences.size()));
      Check(layout.Materialize().bytes == cleared_oracle.bytes,
            "randomized clear-all layout differs from oracle");

      layout.ExcludeIntervals(batch);
      layout.Finalize();
      Check(layout.Materialize().bytes == oracle.bytes,
            "randomized re-added layout differs from oracle");
    }
  }
}

void TestConcurrentFinalizedQueries() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "concurrent.fa";
  const seqpro::IndexedFasta indexed_fasta = BuildBasicFasta(fasta_path);
  seqpro::SequenceTextLayout layout(indexed_fasta);
  layout.ExcludeIntervals({{0, 2, 4}, {1, 1, 2}});
  layout.Finalize();
  const std::string expected = layout.Materialize().bytes;

  for (const int thread_count : {1, 2, 8, 32}) {
    std::atomic<bool> observed_failure{false};
    std::vector<std::thread> workers;
    for (int worker_index = 0; worker_index < thread_count; ++worker_index) {
      workers.emplace_back([&, worker_index] {
        try {
          for (int iteration = 0; iteration < 300; ++iteration) {
            const std::size_t text_position =
                static_cast<std::size_t>(worker_index + iteration) % expected.size();
            if (layout.ReadTextByte(text_position) !=
                static_cast<std::uint8_t>(static_cast<unsigned char>(expected[text_position]))) {
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
            if (iteration % 50 == 0 && layout.Materialize().bytes != expected) {
              observed_failure.store(true);
            }
          }
        } catch (...) {
          observed_failure.store(true);
        }
      });
    }
    for (std::thread& worker : workers) {
      worker.join();
    }
    Check(!observed_failure.load(), "concurrent finalized query returned inconsistent data");
  }
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string_view, std::function<void()>>> tests{
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
  for (const auto& test : tests) {
    try {
      test.second();
      std::cout << "[PASS] " << test.first << '\n';
    } catch (const std::exception& error) {
      ++failed_test_count;
      std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
    }
  }
  if (failed_test_count != 0) {
    std::cerr << failed_test_count << " test group(s) failed\n";
    return 1;
  }
  return 0;
}
