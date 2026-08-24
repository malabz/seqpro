#include <cstdlib>
#include <iostream>
#include <optional>
#include <variant>

#include "seqpro/error.h"
#include "seqpro/indexed_fasta.h"
#include "seqpro/sequence_text_layout.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 3) {
    std::cerr << "usage: seqpro-sequence-text-coordinates-example FASTA SEQUENCE_NAME\n";
    return EXIT_FAILURE;
  }

  try {
    const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(argument_values[1]);
    const std::optional<seqpro::SequenceId> sequence_id =
        indexed_fasta.FindSequenceId(argument_values[2]);
    if (!sequence_id || indexed_fasta.IndexEntryById(*sequence_id).sequence_length < 8) {
      std::cerr << "the example requires a selected sequence with at least eight symbols\n";
      return EXIT_FAILURE;
    }

    seqpro::SequenceTextLayout sequence_text_layout(indexed_fasta, {*sequence_id});
    sequence_text_layout.ExcludeInterval(*sequence_id, 2, 4);
    sequence_text_layout.Finalize();

    const auto active_position =
        sequence_text_layout.FindActiveSequencePosition(*sequence_id, 4);
    const auto text_position = sequence_text_layout.FindTextPosition(*sequence_id, 4);
    if (!active_position || !text_position) {
      std::cerr << "an active position unexpectedly disappeared\n";
      return EXIT_FAILURE;
    }

    const seqpro::SequenceTextLocation text_location =
        sequence_text_layout.LocateTextPosition(*text_position);
    const auto* base_location =
        std::get_if<seqpro::SequenceTextBaseLocation>(&text_location);
    const auto located_interval =
        sequence_text_layout.LocateTextInterval(*text_position, 2);
    if (base_location == nullptr || !located_interval) {
      std::cerr << "text coordinates did not locate an active FASTA interval\n";
      return EXIT_FAILURE;
    }

    std::cout << "active_position\t" << *active_position << '\n'
              << "text_position\t" << *text_position << '\n'
              << "original_position\t" << base_location->original_sequence_position << '\n'
              << "interval_length\t" << located_interval->interval_length << '\n';
    return EXIT_SUCCESS;
  } catch (const seqpro::SeqProError& seqpro_error) {
    std::cerr << seqpro_error.what() << '\n';
    return EXIT_FAILURE;
  }
}
