#include <filesystem>
#include <iostream>
#include <string>

#include "seqpro/seqpro.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 3) {
    std::cerr << "Usage: seqpro-basic-example FASTA SEQUENCE_NAME\n";
    return 2;
  }

  try {
    const seqpro::IndexedFasta indexed_fasta =
        seqpro::IndexedFasta::Open(std::filesystem::path(argument_values[1]));
    const seqpro::FastaSequenceView sequence_view =
        indexed_fasta.SequenceByName(argument_values[2]);
    std::cout << "sequence\t" << sequence_view.sequence_name() << '\n'
              << "length\t" << sequence_view.sequence_length() << '\n';
    if (sequence_view.sequence_length() != 0) {
      std::cout << "first_base\t" << sequence_view.ReadBase(0) << '\n';
    }
    if (sequence_view.sequence_length() >= 6) {
      const std::string sequence_region = sequence_view.ReadSubsequence(2, 4);
      std::cout << "region_2_6\t" << sequence_region << '\n';
    }
    return 0;
  } catch (const seqpro::SeqProError& seqpro_error) {
    std::cerr << "SeqPro error " << static_cast<unsigned>(seqpro_error.error_code()) << ": "
              << seqpro_error.what() << '\n';
    return 1;
  }
}
