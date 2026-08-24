#include <iostream>

#include "seqpro/seqpro.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 3) {
    std::cerr << "Usage: seqpro-basic-example FASTA SEQUENCE_NAME\n";
    return 2;
  }

  try {
    const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(argument_values[1]);
    const seqpro::FastaSequenceView sequence_view =
        indexed_fasta.SequenceByName(argument_values[2]);
    std::cout << sequence_view.sequence_name() << '\t' << sequence_view.sequence_length() << '\n';
    if (sequence_view.sequence_length() != 0) {
      std::cout << "first_base\t" << sequence_view.ReadBase(0) << '\n';
    }
    return 0;
  } catch (const seqpro::SeqProError& seqpro_error) {
    std::cerr << seqpro_error.what() << '\n';
    return 1;
  }
}
