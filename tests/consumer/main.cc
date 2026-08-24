#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "seqpro/error.h"
#include "seqpro/fasta_index.h"
#include "seqpro/indexed_fasta.h"
#include "seqpro/seqpro.h"
#include "seqpro/version.h"

namespace {

void FreezeCoreMethodSignatures() {
  using BuildIndexFunction = seqpro::FastaIndexBuildReport (*)(
      const std::filesystem::path&, const seqpro::FastaIndexBuildOptions&);
  using ValidateIndexFunction = seqpro::FastaIndexValidationReport (*)(
      const std::filesystem::path&, const std::filesystem::path&, seqpro::IndexVerificationMode);
  using OpenFunction =
      seqpro::IndexedFasta (*)(const std::filesystem::path&, const seqpro::IndexedFastaOptions&);
  using OpenOrBuildFunction =
      seqpro::IndexedFasta (*)(const std::filesystem::path&, const seqpro::FastaIndexBuildOptions&,
                               const seqpro::IndexedFastaOptions&);

  [[maybe_unused]] const BuildIndexFunction build_index_function = &seqpro::BuildFastaIndex;
  [[maybe_unused]] const ValidateIndexFunction validate_index_function =
      &seqpro::ValidateFastaIndex;
  [[maybe_unused]] const OpenFunction open_function = &seqpro::IndexedFasta::Open;
  [[maybe_unused]] const OpenOrBuildFunction open_or_build_function =
      &seqpro::IndexedFasta::OpenOrBuildIndex;

  [[maybe_unused]] const auto error_code_method = &seqpro::SeqProError::error_code;
  [[maybe_unused]] const auto chunk_iterator_dereference_method =
      &seqpro::SequenceChunkRange::Iterator::operator*;
  [[maybe_unused]] const auto chunk_iterator_preincrement_method =
      static_cast<seqpro::SequenceChunkRange::Iterator& (
          seqpro::SequenceChunkRange::Iterator::*)()>(
          &seqpro::SequenceChunkRange::Iterator::operator++);
  [[maybe_unused]] const auto chunk_iterator_postincrement_method =
      static_cast<seqpro::SequenceChunkRange::Iterator (seqpro::SequenceChunkRange::Iterator::*)(
          int)>(&seqpro::SequenceChunkRange::Iterator::operator++);
  [[maybe_unused]] const auto chunk_iterator_equality_method =
      &seqpro::SequenceChunkRange::Iterator::operator==;
  [[maybe_unused]] const auto chunk_iterator_inequality_method =
      &seqpro::SequenceChunkRange::Iterator::operator!=;
  [[maybe_unused]] const auto chunk_range_begin_method = &seqpro::SequenceChunkRange::begin;
  [[maybe_unused]] const auto chunk_range_end_method = &seqpro::SequenceChunkRange::end;
  [[maybe_unused]] const auto chunk_range_empty_method = &seqpro::SequenceChunkRange::empty;
  [[maybe_unused]] const auto estimated_chunk_count_method =
      &seqpro::SequenceChunkRange::estimated_chunk_count;

  [[maybe_unused]] const auto sequence_id_method = &seqpro::FastaSequenceView::sequence_id;
  [[maybe_unused]] const auto sequence_name_method = &seqpro::FastaSequenceView::sequence_name;
  [[maybe_unused]] const auto sequence_length_method = &seqpro::FastaSequenceView::sequence_length;
  [[maybe_unused]] const auto fasta_index_entry_method =
      &seqpro::FastaSequenceView::fasta_index_entry;
  [[maybe_unused]] const auto read_base_method = &seqpro::FastaSequenceView::ReadBase;
  [[maybe_unused]] const auto read_subsequence_method = &seqpro::FastaSequenceView::ReadSubsequence;
  [[maybe_unused]] const auto copy_subsequence_method =
      &seqpro::FastaSequenceView::CopySubsequenceTo;
  [[maybe_unused]] const auto write_subsequence_method =
      &seqpro::FastaSequenceView::WriteSubsequenceTo;
  [[maybe_unused]] const auto subsequence_chunks_method =
      &seqpro::FastaSequenceView::SubsequenceChunks;

  [[maybe_unused]] const auto fasta_path_method = &seqpro::IndexedFasta::fasta_path;
  [[maybe_unused]] const auto fasta_index_path_method = &seqpro::IndexedFasta::fasta_index_path;
  [[maybe_unused]] const auto fasta_index_origin_method = &seqpro::IndexedFasta::fasta_index_origin;
  [[maybe_unused]] const auto verification_status_method =
      &seqpro::IndexedFasta::index_verification_status;
  [[maybe_unused]] const auto sequence_count_method = &seqpro::IndexedFasta::sequence_count;
  [[maybe_unused]] const auto fasta_index_entries_method =
      &seqpro::IndexedFasta::fasta_index_entries;
  [[maybe_unused]] const auto find_sequence_id_method = &seqpro::IndexedFasta::FindSequenceId;
  [[maybe_unused]] const auto index_entry_by_id_method = &seqpro::IndexedFasta::IndexEntryById;
  [[maybe_unused]] const auto index_entry_by_name_method = &seqpro::IndexedFasta::IndexEntryByName;
  [[maybe_unused]] const auto sequence_by_id_method = &seqpro::IndexedFasta::SequenceById;
  [[maybe_unused]] const auto sequence_by_name_method = &seqpro::IndexedFasta::SequenceByName;
}

}  // namespace

int main() {
  static_assert(sizeof(seqpro::SequencePosition) == sizeof(std::uint64_t));
  static_assert(sizeof(seqpro::SequenceLength) == sizeof(std::uint64_t));
  static_assert(sizeof(seqpro::SequenceId) == sizeof(std::uint32_t));
  static_assert(sizeof(seqpro::ErrorCode) == sizeof(std::uint8_t));
  static_assert(sizeof(seqpro::FileAccessPattern) == sizeof(std::uint8_t));
  static_assert(sizeof(seqpro::IndexVerificationMode) == sizeof(std::uint8_t));
  static_assert(sizeof(seqpro::FastaIndexOrigin) == sizeof(std::uint8_t));
  static_assert(sizeof(seqpro::IndexVerificationStatus) == sizeof(std::uint8_t));
  static_assert(sizeof(seqpro::FastaIndexBuildAction) == sizeof(std::uint8_t));
  static_assert(std::is_copy_constructible<seqpro::IndexedFasta>::value);
  static_assert(std::is_move_constructible<seqpro::IndexedFasta>::value);
  static_assert(std::is_default_constructible<seqpro::SequenceChunkRange::Iterator>::value);
  static_assert(seqpro::kVersionMajor == 0);
  static_assert(seqpro::kVersionMinor == 2);
  static_assert(seqpro::kVersionPatch == 0);

  FreezeCoreMethodSignatures();

  const seqpro::FastaIndexEntry fasta_index_entry{0, "chr1", 4, 6, 4, 5};
  const seqpro::SequenceChunk sequence_chunk{1, std::string_view("CG")};
  seqpro::FastaIndexBuildOptions build_options;
  seqpro::IndexedFastaOptions open_options;
  build_options.fasta_index_path = "reference.fa.fai";
  build_options.force_rebuild = true;
  build_options.write_seqpro_metadata = false;
  open_options.fasta_index_path = build_options.fasta_index_path;
  open_options.file_access_pattern = seqpro::FileAccessPattern::kRandom;
  open_options.index_verification_mode = seqpro::IndexVerificationMode::kFull;
  open_options.require_seqpro_metadata = true;

  const seqpro::FastaIndexBuildReport build_report{seqpro::FastaIndexBuildAction::kCreated,
                                                   "reference.fa",
                                                   "reference.fa.fai",
                                                   "reference.fa.fai.seqpro.meta",
                                                   1,
                                                   4};
  const seqpro::FastaIndexValidationReport validation_report{
      seqpro::FastaIndexOrigin::kSeqProVerified,
      seqpro::IndexVerificationStatus::kFullContentValidated,
      1,
      4,
      true,
      true};
  const seqpro::SeqProError seqpro_error(seqpro::ErrorCode::kInvalidArgument, "consumer error");

  const bool public_api_is_usable =
      fasta_index_entry.sequence_id == 0 && fasta_index_entry.sequence_name == "chr1" &&
      fasta_index_entry.sequence_length == 4 && fasta_index_entry.first_base_offset_bytes == 6 &&
      fasta_index_entry.bases_per_line == 4 && fasta_index_entry.bytes_per_line == 5 &&
      sequence_chunk.sequence_start_position == 1 && sequence_chunk.sequence_bases == "CG" &&
      build_report.build_action == seqpro::FastaIndexBuildAction::kCreated &&
      build_report.fasta_path == "reference.fa" &&
      build_report.fasta_index_path == "reference.fa.fai" &&
      build_report.metadata_path == "reference.fa.fai.seqpro.meta" &&
      build_report.sequence_count == 1 && build_report.total_base_count == 4 &&
      validation_report.index_origin == seqpro::FastaIndexOrigin::kSeqProVerified &&
      validation_report.verification_status ==
          seqpro::IndexVerificationStatus::kFullContentValidated &&
      validation_report.sequence_count == 1 && validation_report.total_base_count == 4 &&
      validation_report.has_seqpro_metadata && validation_report.is_fasta_fingerprint_current &&
      open_options.fasta_index_path == build_options.fasta_index_path &&
      seqpro_error.error_code() == seqpro::ErrorCode::kInvalidArgument &&
      std::string_view(seqpro::kVersionString) == "0.2.0";

  return public_api_is_usable ? 0 : 1;
}
