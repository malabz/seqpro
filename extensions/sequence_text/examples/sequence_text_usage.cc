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
    seqpro::SequenceTextLayout sequence_text_layout(indexed_fasta);
    const seqpro::MaterializedSequenceText first_generation_text =
        sequence_text_layout.Materialize();

    sequence_text_layout.ExcludeTextIntervals(first_generation_text.layout_generation,
                                               {{1, 2}});
    sequence_text_layout.Finalize();
    const seqpro::MaterializedSequenceText second_generation_text =
        sequence_text_layout.Materialize();

    std::cout << "first_generation\t" << first_generation_text.layout_generation << '\n'
              << "second_generation\t" << second_generation_text.layout_generation << '\n'
              << "selected sequences: " << sequence_text_layout.sequence_order().size() << '\n'
              << "active runs: " << sequence_text_layout.active_run_count() << '\n'
              << "text bytes: " << second_generation_text.sequence_text_bytes.size() << '\n';

    const seqpro::SequenceTextLocation last_location =
        sequence_text_layout.LocateTextPosition(sequence_text_layout.text_size() - 1U);
    if (!std::holds_alternative<seqpro::SequenceTextTerminatorLocation>(last_location)) {
      std::cerr << "the final text byte is not a terminator\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  } catch (const std::exception& example_error) {
    std::cerr << "sequence text example failed: " << example_error.what() << '\n';
    return EXIT_FAILURE;
  }
}
