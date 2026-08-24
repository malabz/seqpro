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

/// One standard uncompressed FASTA FAI row.
///
/// Sequence coordinates are zero-based. File offsets and line widths are measured in bytes.
struct FastaIndexEntry {
  /// Stable zero-based identifier assigned in FAI record order.
  SequenceId sequence_id;
  /// First whitespace-delimited token from the FASTA header.
  std::string sequence_name;
  /// Number of sequence symbols in the record.
  SequenceLength sequence_length;
  /// File byte offset of the first sequence symbol.
  std::uint64_t first_base_offset_bytes;
  /// Number of sequence symbols in each complete physical line.
  std::uint64_t bases_per_line;
  /// Number of bytes in each complete physical line, including its line ending.
  std::uint64_t bytes_per_line;
};

/// Operating-system page-access advice for the read-only FASTA mapping.
enum class FileAccessPattern : std::uint8_t {
  /// Do not override the operating system's default policy.
  kOperatingSystemDefault,
  /// Advise that pages will usually be accessed non-sequentially.
  kRandom,
  /// Advise that pages will usually be accessed sequentially.
  kSequential,
};

/// Strength used when validating a FASTA and its index.
enum class IndexVerificationMode : std::uint8_t {
  /// Validate metadata, structure, and selected physical offsets without hashing the FASTA.
  kFast,
  /// Recompute the complete FASTA XXH3-128 fingerprint.
  kFull,
};

/// Provenance assigned to a validated FAI.
enum class FastaIndexOrigin : std::uint8_t {
  /// FAI has a matching SeqPro metadata sidecar.
  kSeqProVerified,
  /// Standard external FAI has no SeqPro metadata sidecar.
  kExternalStandardFai,
};

/// Strongest validation completed for an opened index.
enum class IndexVerificationStatus : std::uint8_t {
  /// Standard FAI structure and physical offsets were validated.
  kStructureValidated,
  /// SeqPro metadata and its fast fingerprint checks were validated.
  kMetadataValidated,
  /// The complete FASTA content fingerprint was recomputed and validated.
  kFullContentValidated,
};

/// Options controlling the explicit index-building operation.
struct FastaIndexBuildOptions {
  /// Destination FAI path; an empty path selects `<fasta_path>.fai`.
  std::filesystem::path fasta_index_path;
  /// Rebuild an existing malformed or stale index instead of rejecting it.
  bool force_rebuild = false;
  /// Write the versioned SeqPro metadata sidecar after publishing the FAI.
  bool write_seqpro_metadata = true;
};

/// Action performed by BuildFastaIndex().
enum class FastaIndexBuildAction : std::uint8_t {
  /// No index existed and a new one was published.
  kCreated,
  /// Existing standard FAI and metadata were already current.
  kReused,
  /// Existing external FAI was preserved and given SeqPro metadata.
  kAdoptedExternalIndex,
  /// Existing index was replaced after force_rebuild was requested.
  kRebuilt,
};

/// Summary of a completed BuildFastaIndex() call.
struct FastaIndexBuildReport {
  /// Operation performed by BuildFastaIndex().
  FastaIndexBuildAction build_action;
  /// FASTA source path supplied by the caller.
  std::filesystem::path fasta_path;
  /// Standard five-column FAI path.
  std::filesystem::path fasta_index_path;
  /// SeqPro metadata path, or an empty path when metadata was disabled.
  std::filesystem::path metadata_path;
  /// Number of indexed FASTA records.
  std::uint64_t sequence_count;
  /// Sum of all indexed sequence lengths.
  std::uint64_t total_base_count;
};

/// Summary of a completed read-only index validation.
struct FastaIndexValidationReport {
  /// Whether the FAI has validated SeqPro metadata or is an external standard index.
  FastaIndexOrigin index_origin;
  /// Strongest verification completed by this operation.
  IndexVerificationStatus verification_status;
  /// Number of validated FASTA records.
  std::uint64_t sequence_count;
  /// Sum of all validated sequence lengths.
  std::uint64_t total_base_count;
  /// True when the SeqPro metadata sidecar exists.
  bool has_seqpro_metadata;
  /// True when the available fingerprint checks match the current FASTA.
  bool is_fasta_fingerprint_current;
};

/// Creates, reuses, adopts, or explicitly rebuilds a standard five-column FAI.
///
/// The FASTA is scanned without materializing complete sequences. Existing current SeqPro indexes
/// are reused, and a valid external standard FAI can be adopted by adding metadata. Publication is
/// atomic at file granularity.
///
/// @param fasta_path Path to an uncompressed FASTA file.
/// @param build_options Destination, replacement, and metadata policy.
/// @return Paths, counts, and the action performed. Returned paths are owned values.
/// @throws SeqProError with kInvalidArgument for contradictory options, kIoError for file-system
/// failures, kInvalidFasta for malformed FASTA, kInvalidFastaIndex or kStaleFastaIndex when an
/// existing index cannot be reused without force_rebuild, kDuplicateSequenceName for ambiguous
/// record names, kIntegerOverflow for unrepresentable sizes, or kUnsupportedFileFormat for
/// compressed input.
/// @note This function writes index files and must be externally serialized with other writers for
/// the same target paths.
/// @par Complexity
/// Linear in FASTA file bytes when scanning or hashing; reuse is linear in the number of FAI
/// records plus validation work.
SEQPRO_EXPORT FastaIndexBuildReport BuildFastaIndex(
    const std::filesystem::path& fasta_path, const FastaIndexBuildOptions& build_options = {});

/// Validates an existing FASTA and FAI without modifying either file.
///
/// @param fasta_path Path to the uncompressed FASTA file described by the FAI.
/// @param fasta_index_path FAI path; an empty path selects `<fasta_path>.fai`.
/// @param verification_mode Fast metadata/physical validation or a full FASTA fingerprint.
/// @return Validation provenance, strength, counts, and fingerprint state. The report owns its
/// values.
/// @throws SeqProError with kIoError for file-system failures, kInvalidFastaIndex for malformed or
/// physically inconsistent FAI data, kStaleFastaIndex for mismatched metadata, kIntegerOverflow
/// for unrepresentable fields, or kUnsupportedFileFormat for unsupported input.
/// @note This function is read-only. Independent validations may run concurrently if the files are
/// not modified.
/// @par Complexity
/// Linear in FAI record count in kFast mode; kFull additionally reads all FASTA bytes.
SEQPRO_EXPORT FastaIndexValidationReport ValidateFastaIndex(
    const std::filesystem::path& fasta_path, const std::filesystem::path& fasta_index_path = {},
    IndexVerificationMode verification_mode = IndexVerificationMode::kFast);

}  // namespace seqpro

#endif  // SEQPRO_INCLUDE_SEQPRO_FASTA_INDEX_H_
