#include <cstdlib>
#include <exception>
#include <iostream>
#include <variant>

#include "seqpro/indexed_fasta.h"
#include "seqpro/sequence_text_layout.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 2) {
    std::cerr << "usage: seqpro-sequence-text-example <indexed-fasta>\n";
    return EXIT_FAILURE;
  }

  try {
    const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(argument_values[1]);
    seqpro::SequenceTextLayout layout(indexed_fasta);
    const seqpro::MaterializedSequenceText text = layout.Materialize();

    std::cout << "generation: " << text.layout_generation << '\n'
              << "selected sequences: " << layout.sequence_order().size() << '\n'
              << "active runs: " << layout.active_run_count() << '\n'
              << "text bytes: " << text.bytes.size() << '\n';

    const seqpro::SequenceTextLocation last_location =
        layout.LocateTextPosition(layout.text_size() - 1U);
    if (!std::holds_alternative<seqpro::SequenceTextTerminatorLocation>(last_location)) {
      std::cerr << "the final text byte is not a terminator\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "sequence text example failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
