#ifndef SEQPRO_INCLUDE_SEQPRO_FASTA_INDEX_H_
#define SEQPRO_INCLUDE_SEQPRO_FASTA_INDEX_H_

#include <cstdint>
#include <filesystem>
#include <string>

#include "seqpro/export.h"

namespace seqpro {

using SequencePosition = std::uint64_t;
using SequenceLength = std::uint64_t;
using SequenceId = std::uint32_t;

// One standard uncompressed FASTA FAI row. All offsets are byte offsets.
struct FastaIndexEntry {
  SequenceId sequence_id;
  std::string sequence_name;
  SequenceLength sequence_length;
  std::uint64_t first_base_offset_bytes;
  std::uint64_t bases_per_line;
  std::uint64_t bytes_per_line;
};

enum class FileAccessPattern {
  kOperatingSystemDefault,
  kRandom,
  kSequential,
};

enum class IndexVerificationMode {
  kFast,
  kFull,
};

enum class FastaIndexOrigin {
  kSeqProVerified,
  kExternalStandardFai,
};

enum class IndexVerificationStatus {
  kStructureValidated,
  kMetadataValidated,
  kFullContentValidated,
};

struct FastaIndexBuildOptions {
  std::filesystem::path fasta_index_path;
  bool force_rebuild = false;
  bool write_seqpro_metadata = true;
};

enum class FastaIndexBuildAction {
  kCreated,
  kReused,
  kAdoptedExternalIndex,
  kRebuilt,
};

struct FastaIndexBuildReport {
  FastaIndexBuildAction build_action;
  std::filesystem::path fasta_path;
  std::filesystem::path fasta_index_path;
  std::filesystem::path metadata_path;
  std::uint64_t sequence_count;
  std::uint64_t total_base_count;
};

struct FastaIndexValidationReport {
  FastaIndexOrigin index_origin;
  IndexVerificationStatus verification_status;
  std::uint64_t sequence_count;
  std::uint64_t total_base_count;
  bool has_seqpro_metadata;
  bool is_fasta_fingerprint_current;
};

// Creates, reuses, adopts, or explicitly rebuilds a standard five-column FAI.
SEQPRO_EXPORT FastaIndexBuildReport BuildFastaIndex(
    const std::filesystem::path& fasta_path,
    const FastaIndexBuildOptions& options = {});

// Validates an existing FASTA and FAI without modifying either file.
SEQPRO_EXPORT FastaIndexValidationReport ValidateFastaIndex(
    const std::filesystem::path& fasta_path,
    const std::filesystem::path& fasta_index_path = {},
    IndexVerificationMode verification_mode = IndexVerificationMode::kFast);

}  // namespace seqpro

#endif  // SEQPRO_INCLUDE_SEQPRO_FASTA_INDEX_H_
