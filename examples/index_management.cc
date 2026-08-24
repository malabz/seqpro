#include <filesystem>
#include <iostream>

#include "seqpro/seqpro.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 3) {
    std::cerr << "Usage: seqpro-index-management-example FASTA FAI\n";
    return 2;
  }

  try {
    seqpro::FastaIndexBuildOptions build_options;
    build_options.fasta_index_path = argument_values[2];
    build_options.write_seqpro_metadata = true;

    const seqpro::FastaIndexBuildReport build_report =
        seqpro::BuildFastaIndex(argument_values[1], build_options);
    const seqpro::FastaIndexValidationReport validation_report =
        seqpro::ValidateFastaIndex(argument_values[1], argument_values[2],
                                   seqpro::IndexVerificationMode::kFull);

    std::cout << "sequences\t" << build_report.sequence_count << '\n'
              << "bases\t" << build_report.total_base_count << '\n'
              << "metadata\t" << (validation_report.has_seqpro_metadata ? "yes" : "no") << '\n'
              << "full_content\t"
              << (validation_report.verification_status ==
                          seqpro::IndexVerificationStatus::kFullContentValidated
                      ? "yes"
                      : "no")
              << '\n';
    return 0;
  } catch (const seqpro::SeqProError& seqpro_error) {
    std::cerr << seqpro_error.what() << '\n';
    return 1;
  }
}
