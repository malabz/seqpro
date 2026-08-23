#include <iostream>

#include "seqpro/seqpro.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 3) {
    std::cerr << "Usage: seqpro-basic-example FASTA SEQUENCE_NAME\n";
    return 2;
  }

  try {
    const seqpro::IndexedFasta reference = seqpro::IndexedFasta::Open(argument_values[1]);
    const seqpro::FastaSequenceView sequence =
        reference.SequenceByName(argument_values[2]);
    std::cout << sequence.sequence_name() << '\t' << sequence.sequence_length() << '\n';
    if (sequence.sequence_length() != 0) {
      std::cout << "first_base\t" << sequence.ReadBase(0) << '\n';
    }
    return 0;
  } catch (const seqpro::SeqProError& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

