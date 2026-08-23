#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "seqpro/seqpro.h"

namespace {

void PrintUsage(std::ostream& output_stream) {
  output_stream
      << "SeqPro " << seqpro::kVersionString << "\n"
      << "Usage:\n"
      << "  seqpro-index build FASTA [-o FAI] [--force] [--no-metadata]\n"
      << "  seqpro-index validate FASTA [-i FAI] [--full] [--require-metadata]\n"
      << "  seqpro-index info FASTA [-i FAI]\n";
}

std::string_view IndexOriginName(seqpro::FastaIndexOrigin index_origin) {
  if (index_origin == seqpro::FastaIndexOrigin::kSeqProVerified) {
    return "seqpro_verified";
  }
  return "external_standard_fai";
}

std::string_view VerificationStatusName(seqpro::IndexVerificationStatus status) {
  switch (status) {
    case seqpro::IndexVerificationStatus::kStructureValidated:
      return "structure_validated";
    case seqpro::IndexVerificationStatus::kMetadataValidated:
      return "metadata_validated";
    case seqpro::IndexVerificationStatus::kFullContentValidated:
      return "full_content_validated";
  }
  return "unknown";
}

std::string_view BuildActionName(seqpro::FastaIndexBuildAction build_action) {
  switch (build_action) {
    case seqpro::FastaIndexBuildAction::kCreated:
      return "created";
    case seqpro::FastaIndexBuildAction::kReused:
      return "reused";
    case seqpro::FastaIndexBuildAction::kAdoptedExternalIndex:
      return "adopted_external_index";
    case seqpro::FastaIndexBuildAction::kRebuilt:
      return "rebuilt";
  }
  return "unknown";
}

int ExitCodeForError(seqpro::ErrorCode error_code) {
  if (error_code == seqpro::ErrorCode::kInvalidArgument) {
    return 2;
  }
  if (error_code == seqpro::ErrorCode::kIoError) {
    return 4;
  }
  return 3;
}

std::filesystem::path RequireFastaPath(int argument_count, char** argument_values) {
  if (argument_count < 3 || argument_values[2][0] == '-') {
    throw seqpro::SeqProError(seqpro::ErrorCode::kInvalidArgument,
                              "a FASTA path is required");
  }
  return argument_values[2];
}

int BuildCommand(int argument_count, char** argument_values) {
  const std::filesystem::path fasta_path =
      RequireFastaPath(argument_count, argument_values);
  seqpro::FastaIndexBuildOptions options;
  for (int argument_index = 3; argument_index < argument_count; ++argument_index) {
    const std::string_view argument(argument_values[argument_index]);
    if (argument == "-o") {
      if (++argument_index >= argument_count) {
        throw seqpro::SeqProError(seqpro::ErrorCode::kInvalidArgument,
                                  "-o requires a FASTA index path");
      }
      options.fasta_index_path = argument_values[argument_index];
    } else if (argument == "--force") {
      options.force_rebuild = true;
    } else if (argument == "--no-metadata") {
      options.write_seqpro_metadata = false;
    } else {
      throw seqpro::SeqProError(seqpro::ErrorCode::kInvalidArgument,
                                "unknown build option: " + std::string(argument));
    }
  }

  const seqpro::FastaIndexBuildReport report =
      seqpro::BuildFastaIndex(fasta_path, options);
  std::cout << "action\t" << BuildActionName(report.build_action) << '\n'
            << "fasta\t" << report.fasta_path.string() << '\n'
            << "fasta_index\t" << report.fasta_index_path.string() << '\n'
            << "metadata\t" << report.metadata_path.string() << '\n'
            << "sequence_count\t" << report.sequence_count << '\n'
            << "total_bases\t" << report.total_base_count << '\n';
  return 0;
}

int ValidateCommand(int argument_count, char** argument_values) {
  const std::filesystem::path fasta_path =
      RequireFastaPath(argument_count, argument_values);
  std::filesystem::path fasta_index_path;
  seqpro::IndexVerificationMode verification_mode = seqpro::IndexVerificationMode::kFast;
  bool require_seqpro_metadata = false;
  for (int argument_index = 3; argument_index < argument_count; ++argument_index) {
    const std::string_view argument(argument_values[argument_index]);
    if (argument == "-i") {
      if (++argument_index >= argument_count) {
        throw seqpro::SeqProError(seqpro::ErrorCode::kInvalidArgument,
                                  "-i requires a FASTA index path");
      }
      fasta_index_path = argument_values[argument_index];
    } else if (argument == "--full") {
      verification_mode = seqpro::IndexVerificationMode::kFull;
    } else if (argument == "--require-metadata") {
      require_seqpro_metadata = true;
    } else {
      throw seqpro::SeqProError(seqpro::ErrorCode::kInvalidArgument,
                                "unknown validate option: " + std::string(argument));
    }
  }

  const seqpro::FastaIndexValidationReport report =
      seqpro::ValidateFastaIndex(fasta_path, fasta_index_path, verification_mode);
  if (require_seqpro_metadata && !report.has_seqpro_metadata) {
    throw seqpro::SeqProError(seqpro::ErrorCode::kStaleFastaIndex,
                              "SeqPro metadata is required but missing");
  }
  std::cout << "index_origin\t" << IndexOriginName(report.index_origin) << '\n'
            << "verification_status\t" << VerificationStatusName(report.verification_status)
            << '\n'
            << "sequence_count\t" << report.sequence_count << '\n'
            << "total_bases\t" << report.total_base_count << '\n'
            << "has_seqpro_metadata\t" << (report.has_seqpro_metadata ? "true" : "false")
            << '\n'
            << "is_fasta_fingerprint_current\t"
            << (report.is_fasta_fingerprint_current ? "true" : "false") << '\n';
  return 0;
}

int InfoCommand(int argument_count, char** argument_values) {
  const std::filesystem::path fasta_path =
      RequireFastaPath(argument_count, argument_values);
  seqpro::IndexedFastaOptions options;
  for (int argument_index = 3; argument_index < argument_count; ++argument_index) {
    const std::string_view argument(argument_values[argument_index]);
    if (argument == "-i") {
      if (++argument_index >= argument_count) {
        throw seqpro::SeqProError(seqpro::ErrorCode::kInvalidArgument,
                                  "-i requires a FASTA index path");
      }
      options.fasta_index_path = argument_values[argument_index];
    } else {
      throw seqpro::SeqProError(seqpro::ErrorCode::kInvalidArgument,
                                "unknown info option: " + std::string(argument));
    }
  }

  const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(fasta_path, options);
  std::uint64_t total_base_count = 0;
  for (const seqpro::FastaIndexEntry& index_entry : indexed_fasta.fasta_index_entries()) {
    total_base_count += index_entry.sequence_length;
  }
  std::cout << "fasta\t" << indexed_fasta.fasta_path().string() << '\n'
            << "fasta_index\t" << indexed_fasta.fasta_index_path().string() << '\n'
            << "index_origin\t" << IndexOriginName(indexed_fasta.fasta_index_origin()) << '\n'
            << "verification_status\t"
            << VerificationStatusName(indexed_fasta.index_verification_status()) << '\n'
            << "sequence_count\t" << indexed_fasta.sequence_count() << '\n'
            << "total_bases\t" << total_base_count << '\n';
  return 0;
}

}  // namespace

int main(int argument_count, char** argument_values) {
  try {
    if (argument_count == 2 && std::string_view(argument_values[1]) == "--version") {
      std::cout << "seqpro-index " << seqpro::kVersionString << '\n';
      return 0;
    }
    if (argument_count < 2) {
      PrintUsage(std::cerr);
      return 2;
    }
    const std::string_view command(argument_values[1]);
    if (command == "build") {
      return BuildCommand(argument_count, argument_values);
    }
    if (command == "validate") {
      return ValidateCommand(argument_count, argument_values);
    }
    if (command == "info") {
      return InfoCommand(argument_count, argument_values);
    }
    PrintUsage(std::cerr);
    std::cerr << "error: unknown command: " << command << '\n';
    return 2;
  } catch (const seqpro::SeqProError& error) {
    std::cerr << "seqpro-index: " << error.what() << '\n';
    return ExitCodeForError(error.error_code());
  } catch (const std::exception& error) {
    std::cerr << "seqpro-index: unexpected error: " << error.what() << '\n';
    return 4;
  }
}
