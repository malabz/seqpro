#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "seqpro/seqpro.h"

namespace {

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    std::string path_template = "/tmp/seqpro-test-XXXXXX";
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

std::string ReadBinaryFile(const std::filesystem::path& file_path) {
  std::ifstream input_stream(file_path, std::ios::binary);
  std::ostringstream contents;
  contents << input_stream.rdbuf();
  if (!input_stream && !input_stream.eof()) {
    throw std::runtime_error("cannot read test fixture: " + file_path.string());
  }
  return contents.str();
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

std::string JoinChunks(const seqpro::SequenceChunkRange& chunks) {
  std::string joined_bases;
  for (const seqpro::SequenceChunk chunk : chunks) {
    joined_bases.append(chunk.bases);
  }
  return joined_bases;
}

void TestLfBuildAndQueries() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "reference.fa";
  WriteBinaryFile(fasta_path,
                  ">chr1 human chromosome\n"
                  "ACGTac\n"
                  "NRY*-.\n"
                  ">chr2\n"
                  "TTTT\n"
                  "GG\n");

  const seqpro::FastaIndexBuildReport build_report = seqpro::BuildFastaIndex(fasta_path);
  Check(build_report.build_action == seqpro::FastaIndexBuildAction::kCreated,
        "new index was not reported as created");
  Check(build_report.sequence_count == 2, "incorrect sequence count");
  Check(build_report.total_base_count == 18, "incorrect total base count");
  Check(std::filesystem::exists(build_report.fasta_index_path), "FAI was not created");
  Check(std::filesystem::exists(build_report.metadata_path), "metadata was not created");

  const seqpro::FastaIndexBuildReport reused_report = seqpro::BuildFastaIndex(fasta_path);
  Check(reused_report.build_action == seqpro::FastaIndexBuildAction::kReused,
        "matching index was not reused");

  const seqpro::FastaIndexValidationReport validation_report =
      seqpro::ValidateFastaIndex(fasta_path, {}, seqpro::IndexVerificationMode::kFull);
  Check(validation_report.index_origin == seqpro::FastaIndexOrigin::kSeqProVerified,
        "SeqPro metadata origin was not detected");
  Check(validation_report.verification_status ==
            seqpro::IndexVerificationStatus::kFullContentValidated,
        "full validation status was not reported");
  Check(validation_report.is_fasta_fingerprint_current,
        "full validation did not report a matched FASTA fingerprint");

  const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(fasta_path);
  Check(indexed_fasta.sequence_count() == 2, "opened sequence count is incorrect");
  Check(indexed_fasta.FindSequenceId("chr1") == seqpro::SequenceId{0},
        "name lookup returned wrong ID");
  Check(!indexed_fasta.FindSequenceId("missing"), "missing name unexpectedly resolved");

  const seqpro::FastaSequenceView chromosome_one = indexed_fasta.SequenceByName("chr1");
  Check(chromosome_one.sequence_length() == 12, "chr1 length is incorrect");
  Check(chromosome_one.ReadBase(0) == 'A', "first base is incorrect");
  Check(chromosome_one.ReadBase(11) == '.', "last base is incorrect");
  Check(chromosome_one.ReadSubsequence(4, 6) == "acNRY*",
        "cross-line subsequence is incorrect");
  Check(chromosome_one.ReadSubsequence(12, 0).empty(),
        "empty terminal interval is incorrect");

  std::string copied_bases(9, '\0');
  chromosome_one.CopySubsequenceTo(2, copied_bases.data(), copied_bases.size());
  Check(copied_bases == "GTacNRY*-", "CopySubsequenceTo returned incorrect bases");
  Check(JoinChunks(chromosome_one.SubsequenceChunks(2, 9)) == copied_bases,
        "chunk range differs from copied sequence");
  Check(chromosome_one.SubsequenceChunks(2, 9).estimated_chunk_count() == 2,
        "chunk count estimate is incorrect");

  std::ostringstream output_stream;
  chromosome_one.WriteSubsequenceTo(1, 10, output_stream, 3);
  Check(output_stream.str() == "CGTacNRY*-", "streamed subsequence is incorrect");

  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound,
                    [&] { indexed_fasta.SequenceByName("missing"); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceNotFound,
                    [&] { indexed_fasta.SequenceById(10); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceRangeOutOfBounds,
                    [&] { chromosome_one.ReadBase(12); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceRangeOutOfBounds,
                    [&] { chromosome_one.ReadSubsequence(11, 2); });
  ExpectSeqProError(seqpro::ErrorCode::kSequenceRangeOutOfBounds, [&] {
    chromosome_one.ReadSubsequence(1, std::numeric_limits<seqpro::SequenceLength>::max());
  });
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { chromosome_one.CopySubsequenceTo(0, nullptr, 1); });
  chromosome_one.CopySubsequenceTo(12, nullptr, 0);
}

void TestCrlfAndNoFinalNewline() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "crlf.fa";
  WriteBinaryFile(fasta_path, ">alpha description\r\nACGT\r\nTG\r\n>beta\nxyz");
  seqpro::BuildFastaIndex(fasta_path);
  const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(fasta_path);
  Check(indexed_fasta.SequenceByName("alpha").ReadSubsequence(0, 6) == "ACGTTG",
        "CRLF sequence was decoded incorrectly");
  Check(indexed_fasta.SequenceByName("beta").ReadSubsequence(0, 3) == "xyz",
        "unterminated final line was decoded incorrectly");
  const seqpro::FastaIndexEntry& alpha = indexed_fasta.IndexEntryByName("alpha");
  Check(alpha.bases_per_line == 4 && alpha.bytes_per_line == 6,
        "CRLF FAI widths are incorrect");
  const seqpro::FastaIndexEntry& beta = indexed_fasta.IndexEntryByName("beta");
  Check(beta.bases_per_line == 3 && beta.bytes_per_line == 4,
        "unterminated single-line FAI width is not HTSlib-compatible");
}

void TestViewLifetimeAndConcurrency() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "lifetime.fa";
  WriteBinaryFile(fasta_path, ">sequence\nACGTACGT\nACGTACGT\n");
  seqpro::BuildFastaIndex(fasta_path);

  seqpro::FastaSequenceView surviving_view = [&] {
    const seqpro::IndexedFasta local_reader = seqpro::IndexedFasta::Open(fasta_path);
    return local_reader.SequenceByName("sequence");
  }();
  Check(surviving_view.ReadSubsequence(3, 8) == "TACGTACG",
        "view did not keep mapping alive");

  const seqpro::SequenceChunkRange surviving_chunks = [&] {
    const seqpro::IndexedFasta local_reader = seqpro::IndexedFasta::Open(fasta_path);
    return local_reader.SequenceByName("sequence").SubsequenceChunks(1, 12);
  }();
  Check(JoinChunks(surviving_chunks) == "CGTACGTACGTA",
        "chunk range did not keep mapping alive");

  const seqpro::IndexedFasta shared_reader = seqpro::IndexedFasta::Open(fasta_path);
  for (const int thread_count : {1, 2, 8, 32}) {
    std::atomic<bool> observed_failure{false};
    std::vector<std::thread> workers;
    for (int worker_index = 0; worker_index < thread_count; ++worker_index) {
      workers.emplace_back([&] {
        try {
          const seqpro::FastaSequenceView sequence = shared_reader.SequenceByName("sequence");
          for (int iteration = 0; iteration < 500; ++iteration) {
            if (sequence.ReadSubsequence(2, 10) != "GTACGTACGT") {
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
    if (observed_failure.load()) {
      throw std::runtime_error("concurrent read returned inconsistent data with " +
                               std::to_string(thread_count) + " threads");
    }
  }
}

void TestReaderMoveSemantics() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "move.fa";
  WriteBinaryFile(fasta_path, ">move\nACGT\nTGCA\n");
  seqpro::BuildFastaIndex(fasta_path);

  seqpro::IndexedFasta original_reader = seqpro::IndexedFasta::Open(fasta_path);
  seqpro::FastaSequenceView retained_view = original_reader.SequenceByName("move");
  seqpro::IndexedFasta move_constructed_reader(std::move(original_reader));
  Check(move_constructed_reader.SequenceById(0).ReadSubsequence(0, 8) == "ACGTTGCA",
        "move construction did not transfer the indexed FASTA state");

  seqpro::IndexedFasta move_assigned_reader = seqpro::IndexedFasta::Open(fasta_path);
  move_assigned_reader = std::move(move_constructed_reader);
  Check(move_assigned_reader.SequenceByName("move").ReadBase(7) == 'A',
        "move assignment did not transfer the indexed FASTA state");
  Check(retained_view.ReadSubsequence(2, 4) == "GTTG",
        "moving the reader invalidated an existing sequence view");

  seqpro::FastaSequenceView moved_view(std::move(retained_view));
  Check(moved_view.ReadSubsequence(1, 6) == "CGTTGC",
        "moving a sequence view invalidated its mapping state");
}

std::uint64_t NextRandom(std::uint64_t* random_state) {
  *random_state ^= *random_state << 13U;
  *random_state ^= *random_state >> 7U;
  *random_state ^= *random_state << 17U;
  return *random_state;
}

void TestRandomizedQueries() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "random.fa";
  constexpr std::string_view kAlphabet = "ACGTNRYKMSWBDHVacgt*.-?";
  const std::vector<std::size_t> sequence_lengths{1, 257, 1025};
  const std::vector<std::size_t> line_widths{1, 17, 64};
  std::vector<std::string> oracle_sequences;
  std::string fasta_contents;
  std::uint64_t random_state = 0x123456789abcdef0ULL;

  for (std::size_t sequence_index = 0; sequence_index < sequence_lengths.size();
       ++sequence_index) {
    fasta_contents += ">random_" + std::to_string(sequence_index) + " description";
    fasta_contents += sequence_index == 1 ? "\r\n" : "\n";
    std::string sequence;
    sequence.reserve(sequence_lengths[sequence_index]);
    for (std::size_t base_index = 0; base_index < sequence_lengths[sequence_index]; ++base_index) {
      sequence.push_back(kAlphabet[NextRandom(&random_state) % kAlphabet.size()]);
    }
    oracle_sequences.push_back(sequence);
    const std::string_view newline = sequence_index == 1 ? "\r\n" : "\n";
    for (std::size_t sequence_start = 0; sequence_start < sequence.size();
         sequence_start += line_widths[sequence_index]) {
      const std::size_t line_length =
          std::min(line_widths[sequence_index], sequence.size() - sequence_start);
      fasta_contents.append(sequence.data() + sequence_start, line_length);
      const bool is_last_line = sequence_start + line_length == sequence.size();
      const bool omit_final_newline =
          sequence_index + 1 == sequence_lengths.size() && is_last_line;
      if (!omit_final_newline) {
        fasta_contents.append(newline);
      }
    }
  }
  WriteBinaryFile(fasta_path, fasta_contents);
  seqpro::BuildFastaIndex(fasta_path);
  const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(fasta_path);

  for (std::size_t sequence_index = 0; sequence_index < oracle_sequences.size();
       ++sequence_index) {
    const seqpro::FastaSequenceView sequence =
        indexed_fasta.SequenceById(static_cast<seqpro::SequenceId>(sequence_index));
    for (std::size_t base_index = 0; base_index < oracle_sequences[sequence_index].size();
         ++base_index) {
      Check(sequence.ReadBase(base_index) == oracle_sequences[sequence_index][base_index],
            "randomized base query differs from oracle");
    }
  }

  for (int query_index = 0; query_index < 5000; ++query_index) {
    const std::size_t sequence_index = NextRandom(&random_state) % oracle_sequences.size();
    const std::string& oracle = oracle_sequences[sequence_index];
    const std::size_t sequence_start = NextRandom(&random_state) % (oracle.size() + 1U);
    const std::size_t maximum_length = oracle.size() - sequence_start;
    const std::size_t subsequence_length =
        maximum_length == 0 ? 0 : NextRandom(&random_state) % (maximum_length + 1U);
    const seqpro::FastaSequenceView sequence =
        indexed_fasta.SequenceById(static_cast<seqpro::SequenceId>(sequence_index));
    const std::string expected = oracle.substr(sequence_start, subsequence_length);
    Check(sequence.ReadSubsequence(sequence_start, subsequence_length) == expected,
          "randomized subsequence differs from oracle");
    Check(JoinChunks(sequence.SubsequenceChunks(sequence_start, subsequence_length)) == expected,
          "randomized chunks differ from oracle");
  }
}

void TestScannerBufferBoundary() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "buffer-boundary.fa";
  constexpr std::size_t kScannerBufferSize = 8U * 1024U * 1024U;
  constexpr std::size_t kHeaderSize = 6;
  const std::size_t first_line_length = kScannerBufferSize - kHeaderSize - 1U;
  std::string fasta_contents = ">long\n";
  fasta_contents.append(first_line_length, 'A');
  fasta_contents.back() = 'C';
  fasta_contents += "\r\nXYZ";
  WriteBinaryFile(fasta_path, fasta_contents);

  seqpro::BuildFastaIndex(fasta_path);
  const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(fasta_path);
  const seqpro::FastaSequenceView sequence = indexed_fasta.SequenceByName("long");
  Check(sequence.sequence_length() == first_line_length + 3U,
        "buffer-boundary sequence length is incorrect");
  Check(sequence.ReadSubsequence(first_line_length - 2U, 5) == "ACXYZ",
        "CRLF split across scanner buffers was parsed incorrectly");
  Check(sequence.fasta_index_entry().bytes_per_line == first_line_length + 2U,
        "buffer-boundary CRLF width is incorrect");
}

void TestExternalIndexAdoption() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "external.fa";
  WriteBinaryFile(fasta_path, ">one\nAAAA\nCCCC\n");
  seqpro::FastaIndexBuildOptions no_metadata_options;
  no_metadata_options.write_seqpro_metadata = false;
  const seqpro::FastaIndexBuildReport initial_report =
      seqpro::BuildFastaIndex(fasta_path, no_metadata_options);
  Check(initial_report.metadata_path.empty(), "metadata path should be empty");
  const seqpro::FastaIndexValidationReport external_report =
      seqpro::ValidateFastaIndex(fasta_path);
  Check(external_report.index_origin == seqpro::FastaIndexOrigin::kExternalStandardFai,
        "index without sidecar was not treated as external");

  const std::string external_index_contents =
      ReadBinaryFile(initial_report.fasta_index_path);
  const seqpro::FastaIndexBuildReport adopted_report = seqpro::BuildFastaIndex(fasta_path);
  Check(adopted_report.build_action == seqpro::FastaIndexBuildAction::kAdoptedExternalIndex,
        "external index was not adopted");
  Check(std::filesystem::exists(adopted_report.metadata_path),
        "adoption did not create metadata");
  Check(ReadBinaryFile(initial_report.fasta_index_path) == external_index_contents,
        "adoption rewrote the external standard FAI");

  seqpro::FastaIndexBuildOptions rebuild_without_metadata;
  rebuild_without_metadata.force_rebuild = true;
  rebuild_without_metadata.write_seqpro_metadata = false;
  seqpro::BuildFastaIndex(fasta_path, rebuild_without_metadata);
  Check(!std::filesystem::exists(adopted_report.metadata_path),
        "forced no-metadata rebuild left a stale sidecar");
  Check(seqpro::IndexedFasta::Open(fasta_path).fasta_index_origin() ==
            seqpro::FastaIndexOrigin::kExternalStandardFai,
        "no-metadata rebuild did not reopen as an external FAI");
}

void TestMalformedInputs() {
  struct InvalidFixture {
    std::string file_name;
    std::string contents;
    seqpro::ErrorCode expected_error;
  };
  const std::vector<InvalidFixture> fixtures{
      {"empty.fa", "", seqpro::ErrorCode::kInvalidFasta},
      {"before-header.fa", "ACGT\n", seqpro::ErrorCode::kInvalidFasta},
      {"empty-name.fa", ">   \nACGT\n", seqpro::ErrorCode::kInvalidFasta},
      {"duplicate.fa", ">a\nAC\n>a duplicate\nGT\n",
       seqpro::ErrorCode::kDuplicateSequenceName},
      {"empty-sequence.fa", ">a\n>b\nAC\n", seqpro::ErrorCode::kInvalidFasta},
      {"blank-line.fa", ">a\nAC\n\nGT\n", seqpro::ErrorCode::kInvalidFasta},
      {"space.fa", ">a\nAC GT\n", seqpro::ErrorCode::kInvalidFasta},
      {"short-middle.fa", ">a\nAAAA\nAA\nTT\n", seqpro::ErrorCode::kInvalidFasta},
      {"mixed-newline.fa", ">a\nAAAA\nTT\r\n", seqpro::ErrorCode::kInvalidFasta},
      {"gzip.fa", std::string("\x1f\x8b", 2),
       seqpro::ErrorCode::kUnsupportedFileFormat},
      {"nul.fa", std::string(">a\nAC\0GT\n", 9), seqpro::ErrorCode::kInvalidFasta},
      {"tab.fa", ">a\nAC\tGT\n", seqpro::ErrorCode::kInvalidFasta},
      {"bare-cr.fa", ">a\nACGT\r", seqpro::ErrorCode::kInvalidFasta},
  };

  TemporaryDirectory temporary_directory;
  for (const InvalidFixture& fixture : fixtures) {
    const std::filesystem::path fasta_path = temporary_directory.path() / fixture.file_name;
    WriteBinaryFile(fasta_path, fixture.contents);
    ExpectSeqProError(fixture.expected_error, [&] { seqpro::BuildFastaIndex(fasta_path); });
  }
}

void TestMalformedIndexesAndMetadata() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "index-errors.fa";
  const std::filesystem::path fasta_index_path =
      std::filesystem::path(fasta_path.string() + ".fai");
  const std::filesystem::path metadata_path =
      std::filesystem::path(fasta_index_path.string() + ".seqpro.meta");
  WriteBinaryFile(fasta_path, ">a\nACGT\n");

  WriteBinaryFile(fasta_index_path, "a\t4\t3\t4\t5\textra\n");
  ExpectSeqProError(seqpro::ErrorCode::kInvalidFastaIndex,
                    [&] { seqpro::ValidateFastaIndex(fasta_path); });
  WriteBinaryFile(fasta_index_path, "a\t-4\t3\t4\t5\n");
  ExpectSeqProError(seqpro::ErrorCode::kInvalidFastaIndex,
                    [&] { seqpro::ValidateFastaIndex(fasta_path); });
  WriteBinaryFile(fasta_index_path, "a\t18446744073709551616\t3\t4\t5\n");
  ExpectSeqProError(seqpro::ErrorCode::kInvalidFastaIndex,
                    [&] { seqpro::ValidateFastaIndex(fasta_path); });
  WriteBinaryFile(fasta_index_path, "a\t18446744073709551615\t3\t4\t5\n");
  ExpectSeqProError(seqpro::ErrorCode::kIntegerOverflow,
                    [&] { seqpro::ValidateFastaIndex(fasta_path); });
  WriteBinaryFile(fasta_index_path, "a\t4\t4\t4\t5\n");
  ExpectSeqProError(seqpro::ErrorCode::kInvalidFastaIndex,
                    [&] { seqpro::ValidateFastaIndex(fasta_path); });
  WriteBinaryFile(fasta_index_path, "a\t4\t3\t4\t3\n");
  ExpectSeqProError(seqpro::ErrorCode::kInvalidFastaIndex,
                    [&] { seqpro::ValidateFastaIndex(fasta_path); });

  seqpro::FastaIndexBuildOptions rebuild_options;
  rebuild_options.force_rebuild = true;
  seqpro::BuildFastaIndex(fasta_path, rebuild_options);
  WriteBinaryFile(metadata_path, "SEQPRO_META\t2\n");
  ExpectSeqProError(seqpro::ErrorCode::kStaleFastaIndex,
                    [&] { seqpro::ValidateFastaIndex(fasta_path); });

  std::filesystem::remove(metadata_path);
  seqpro::IndexedFastaOptions require_metadata_options;
  require_metadata_options.require_seqpro_metadata = true;
  ExpectSeqProError(seqpro::ErrorCode::kStaleFastaIndex, [&] {
    seqpro::IndexedFasta::Open(fasta_path, require_metadata_options);
  });
}

void TestCustomIndexPathAndOpenModes() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "custom.fa";
  const std::filesystem::path custom_index_path = temporary_directory.path() / "index/custom.fai";
  std::filesystem::create_directories(custom_index_path.parent_path());
  WriteBinaryFile(fasta_path, ">custom\nACGTACGT\n");

  seqpro::FastaIndexBuildOptions build_options;
  build_options.fasta_index_path = custom_index_path;
  seqpro::IndexedFastaOptions open_options;
  open_options.file_access_pattern = seqpro::FileAccessPattern::kSequential;
  const seqpro::IndexedFasta indexed_fasta =
      seqpro::IndexedFasta::OpenOrBuildIndex(fasta_path, build_options, open_options);
  Check(indexed_fasta.fasta_index_path() == custom_index_path,
        "custom FASTA index path was not retained");
  Check(indexed_fasta.SequenceByName("custom").ReadSubsequence(1, 6) == "CGTACG",
        "custom-index reader returned incorrect sequence");

  seqpro::IndexedFastaOptions random_access_options;
  random_access_options.fasta_index_path = custom_index_path;
  random_access_options.file_access_pattern = seqpro::FileAccessPattern::kRandom;
  Check(seqpro::IndexedFasta::Open(fasta_path, random_access_options)
                .SequenceByName("custom")
                .ReadBase(7) == 'T',
        "random access advice changed query semantics");

  seqpro::FastaIndexBuildOptions mismatched_build_options;
  mismatched_build_options.fasta_index_path = temporary_directory.path() / "one.fai";
  seqpro::IndexedFastaOptions mismatched_open_options;
  mismatched_open_options.fasta_index_path = temporary_directory.path() / "two.fai";
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument, [&] {
    seqpro::IndexedFasta::OpenOrBuildIndex(fasta_path, mismatched_build_options,
                                          mismatched_open_options);
  });
}

void TestOutputPathSafety() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "source.fa";
  constexpr std::string_view kFastaContents = ">source\nACGT\n";
  const std::string fasta_contents(kFastaContents);
  WriteBinaryFile(fasta_path, kFastaContents);

  seqpro::FastaIndexBuildOptions same_path_options;
  same_path_options.fasta_index_path = fasta_path;
  same_path_options.force_rebuild = true;
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { seqpro::BuildFastaIndex(fasta_path, same_path_options); });
  Check(ReadBinaryFile(fasta_path) == fasta_contents,
        "same-path rejection modified the source FASTA");

  const std::filesystem::path hard_link_path = temporary_directory.path() / "hard-link.fai";
  std::filesystem::create_hard_link(fasta_path, hard_link_path);
  seqpro::FastaIndexBuildOptions hard_link_options;
  hard_link_options.fasta_index_path = hard_link_path;
  hard_link_options.force_rebuild = true;
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument,
                    [&] { seqpro::BuildFastaIndex(fasta_path, hard_link_options); });
  Check(ReadBinaryFile(fasta_path) == fasta_contents,
        "hard-link rejection modified the source FASTA");

  const std::filesystem::path metadata_alias_fasta =
      temporary_directory.path() / "metadata-target.fai.seqpro.meta";
  WriteBinaryFile(metadata_alias_fasta, kFastaContents);
  seqpro::FastaIndexBuildOptions metadata_alias_options;
  metadata_alias_options.fasta_index_path = temporary_directory.path() / "metadata-target.fai";
  ExpectSeqProError(seqpro::ErrorCode::kInvalidArgument, [&] {
    seqpro::BuildFastaIndex(metadata_alias_fasta, metadata_alias_options);
  });
  Check(ReadBinaryFile(metadata_alias_fasta) == fasta_contents,
        "metadata-path rejection modified the source FASTA");
}

void TestMovableIndexBundle() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path original_fasta_path = temporary_directory.path() / "original.fa";
  WriteBinaryFile(original_fasta_path, ">movable\nAACCGGTT\n");
  const seqpro::FastaIndexBuildReport build_report =
      seqpro::BuildFastaIndex(original_fasta_path);

  const std::filesystem::path moved_fasta_path = temporary_directory.path() / "moved.fa";
  const std::filesystem::path moved_index_path =
      std::filesystem::path(moved_fasta_path.string() + ".fai");
  const std::filesystem::path moved_metadata_path =
      std::filesystem::path(moved_index_path.string() + ".seqpro.meta");
  std::filesystem::rename(original_fasta_path, moved_fasta_path);
  std::filesystem::rename(build_report.fasta_index_path, moved_index_path);
  std::filesystem::rename(build_report.metadata_path, moved_metadata_path);

  const seqpro::IndexedFasta moved_reader = seqpro::IndexedFasta::Open(moved_fasta_path);
  Check(moved_reader.SequenceByName("movable").ReadSubsequence(0, 8) == "AACCGGTT",
        "moved FASTA/FAI/metadata bundle did not remain valid");
}

void TestLegacyAndStaleIndexes() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "stale.fa";
  WriteBinaryFile(fasta_path, ">a\nACGT\n");
  seqpro::BuildFastaIndex(fasta_path);
  WriteBinaryFile(fasta_path, ">a\nACGTA\n");
  ExpectSeqProError(seqpro::ErrorCode::kStaleFastaIndex,
                    [&] { seqpro::IndexedFasta::Open(fasta_path); });

  const std::filesystem::path legacy_fasta_path = temporary_directory.path() / "legacy.fa";
  WriteBinaryFile(legacy_fasta_path, ">a\nACGT\n");
  WriteBinaryFile(std::filesystem::path(legacy_fasta_path.string() + ".fai"),
                  "YES\na\t0\t4\t3\t4\t5\n");
  ExpectSeqProError(seqpro::ErrorCode::kInvalidFastaIndex,
                    [&] { seqpro::ValidateFastaIndex(legacy_fasta_path); });
}

void TestFullFingerprintDetection() {
  TemporaryDirectory temporary_directory;
  const std::filesystem::path fasta_path = temporary_directory.path() / "fingerprint.fa";
  WriteBinaryFile(fasta_path, ">a\nAAAA\n");
  seqpro::BuildFastaIndex(fasta_path);
  const std::filesystem::file_time_type original_modification_time =
      std::filesystem::last_write_time(fasta_path);

  std::filesystem::last_write_time(fasta_path,
                                   original_modification_time + std::chrono::seconds(2));
  ExpectSeqProError(seqpro::ErrorCode::kStaleFastaIndex, [&] {
    seqpro::ValidateFastaIndex(fasta_path, {}, seqpro::IndexVerificationMode::kFast);
  });
  const seqpro::FastaIndexValidationReport copied_file_report =
      seqpro::ValidateFastaIndex(fasta_path, {}, seqpro::IndexVerificationMode::kFull);
  Check(copied_file_report.is_fasta_fingerprint_current,
        "full validation did not accept unchanged content with a different mtime");
  seqpro::IndexedFastaOptions full_open_options;
  full_open_options.index_verification_mode = seqpro::IndexVerificationMode::kFull;
  Check(seqpro::IndexedFasta::Open(fasta_path, full_open_options)
                .SequenceByName("a")
                .ReadSubsequence(0, 4) == "AAAA",
        "full open rejected unchanged content with a different mtime");

  WriteBinaryFile(fasta_path, ">a\nTTTT\n");
  std::filesystem::last_write_time(fasta_path, original_modification_time);
  const seqpro::FastaIndexValidationReport fast_report =
      seqpro::ValidateFastaIndex(fasta_path, {}, seqpro::IndexVerificationMode::kFast);
  Check(fast_report.verification_status ==
            seqpro::IndexVerificationStatus::kMetadataValidated,
        "fast validation unexpectedly scanned same-size FASTA content");
  Check(!fast_report.is_fasta_fingerprint_current,
        "fast validation incorrectly claimed to recompute the FASTA fingerprint");
  ExpectSeqProError(seqpro::ErrorCode::kStaleFastaIndex, [&] {
    seqpro::ValidateFastaIndex(fasta_path, {}, seqpro::IndexVerificationMode::kFull);
  });
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string_view, std::function<void()>>> tests{
      {"LF build and queries", TestLfBuildAndQueries},
      {"CRLF and no final newline", TestCrlfAndNoFinalNewline},
      {"view lifetime and concurrency", TestViewLifetimeAndConcurrency},
      {"reader move semantics", TestReaderMoveSemantics},
      {"randomized queries", TestRandomizedQueries},
      {"scanner buffer boundary", TestScannerBufferBoundary},
      {"external index adoption", TestExternalIndexAdoption},
      {"malformed inputs", TestMalformedInputs},
      {"malformed indexes and metadata", TestMalformedIndexesAndMetadata},
      {"custom index path and open modes", TestCustomIndexPathAndOpenModes},
      {"output path safety", TestOutputPathSafety},
      {"movable index bundle", TestMovableIndexBundle},
      {"legacy and stale indexes", TestLegacyAndStaleIndexes},
      {"full fingerprint detection", TestFullFingerprintDetection},
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
