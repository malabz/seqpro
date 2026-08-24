#ifndef SEQPRO_SRC_FASTA_INTERNAL_H_
#define SEQPRO_SRC_FASTA_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "mapped_file.h"
#include "seqpro/fasta_index.h"

namespace seqpro::internal {

struct SourceFileIdentity {
  std::uint64_t file_size_bytes;
  std::uint64_t modification_time_nanoseconds;
  std::uint64_t device_number;
  std::uint64_t inode_number;
};

struct FastaScanReport {
  std::vector<FastaIndexEntry> fasta_index_entries;
  std::uint64_t total_base_count;
  std::string fasta_xxh3_128;
  SourceFileIdentity source_file_identity;
};

struct SeqProMetadata {
  std::uint64_t fasta_size_bytes;
  std::uint64_t fasta_modification_time_nanoseconds;
  std::string fasta_xxh3_128;
  std::string fasta_index_xxh3_128;
  std::uint64_t record_count;
  std::uint64_t total_base_count;
};

struct ValidatedFastaIndex {
  std::vector<FastaIndexEntry> fasta_index_entries;
  FastaIndexValidationReport validation_report;
};

std::filesystem::path ResolveFastaIndexPath(const std::filesystem::path& fasta_path,
                                            const std::filesystem::path& requested_index_path);
std::filesystem::path ResolveMetadataPath(const std::filesystem::path& fasta_index_path);

SourceFileIdentity ReadSourceFileIdentity(const std::filesystem::path& file_path);
FastaScanReport ScanFasta(const std::filesystem::path& fasta_path);
FastaScanReport ScanFastaText(std::string_view fasta_text, std::size_t input_chunk_size_bytes);
std::vector<FastaIndexEntry> ParseFastaIndex(const std::filesystem::path& fasta_index_path);
std::vector<FastaIndexEntry> ParseFastaIndexText(
    std::string_view fasta_index_text, const std::filesystem::path& logical_fasta_index_path);
std::string SerializeFastaIndex(const std::vector<FastaIndexEntry>& fasta_index_entries);
std::string HashFileXxh3(const std::filesystem::path& file_path);

SeqProMetadata ParseSeqProMetadata(const std::filesystem::path& metadata_path);
SeqProMetadata ParseSeqProMetadataText(std::string_view metadata_text,
                                       const std::filesystem::path& logical_metadata_path);
std::string SerializeSeqProMetadata(const SeqProMetadata& metadata);

void ValidateFastaIndexEntries(const std::filesystem::path& fasta_path,
                               const MappedFile& mapped_fasta,
                               const std::vector<FastaIndexEntry>& fasta_index_entries);
ValidatedFastaIndex ValidateFastaIndexFiles(const std::filesystem::path& fasta_path,
                                            const std::filesystem::path& fasta_index_path,
                                            IndexVerificationMode verification_mode,
                                            bool require_seqpro_metadata,
                                            const MappedFile* existing_mapping = nullptr);

void PublishTextFileAtomically(const std::filesystem::path& destination_path,
                               const std::string& file_contents);

}  // namespace seqpro::internal

#endif  // SEQPRO_SRC_FASTA_INTERNAL_H_
