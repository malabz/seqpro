#include <filesystem>
#include <iostream>
#include <vector>

#include "seqpro/error.h"
#include "seqpro/indexed_fasta.h"
#include "seqpro/sequence_text_layout.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 3) {
    std::cerr << "expected FASTA and FAI fixture paths\n";
    return 2;
  }

  try {
    seqpro::IndexedFastaOptions open_options;
    open_options.fasta_index_path = std::filesystem::path(argument_values[2]);
    const seqpro::IndexedFasta indexed_fasta =
        seqpro::IndexedFasta::Open(std::filesystem::path(argument_values[1]), open_options);

    try {
      seqpro::SequenceTextLayout invalid_layout(indexed_fasta,
                                                std::vector<seqpro::SequenceId>{0, 0});
      static_cast<void>(invalid_layout);
    } catch (const seqpro::SeqProError& sequence_text_error) {
      return sequence_text_error.error_code() == seqpro::ErrorCode::kInvalidArgument ? 0 : 1;
    }
  } catch (const std::exception& unexpected_error) {
    std::cerr << unexpected_error.what() << '\n';
    return 1;
  }

  std::cerr << "SequenceText did not throw SeqProError across the shared-library boundary\n";
  return 1;
}
