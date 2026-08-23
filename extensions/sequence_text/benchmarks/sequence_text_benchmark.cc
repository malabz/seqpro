#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "seqpro/indexed_fasta.h"
#include "seqpro/sequence_text_layout.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 4) {
    std::cerr << "usage: seqpro-sequence-text-benchmark "
                 "<indexed-fasta> <sequence-name> <lookup-count>\n";
    return EXIT_FAILURE;
  }

  try {
    const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(argument_values[1]);
    const std::string_view sequence_name = argument_values[2];
    const std::optional<seqpro::SequenceId> sequence_id =
        indexed_fasta.FindSequenceId(sequence_name);
    if (!sequence_id) {
      std::cerr << "sequence not found: " << sequence_name << '\n';
      return EXIT_FAILURE;
    }
    const std::uint64_t lookup_count = std::stoull(argument_values[3]);
    seqpro::SequenceTextLayout layout(indexed_fasta, {*sequence_id});

    const auto materialize_start = std::chrono::steady_clock::now();
    const seqpro::MaterializedSequenceText text = layout.Materialize();
    const auto materialize_end = std::chrono::steady_clock::now();

    std::uint64_t checksum = 0;
    const auto lookup_start = std::chrono::steady_clock::now();
    for (std::uint64_t lookup_index = 0; lookup_index < lookup_count; ++lookup_index) {
      const seqpro::SequenceTextPosition text_position = lookup_index % layout.text_size();
      checksum += layout.ReadTextByte(text_position);
    }
    const auto lookup_end = std::chrono::steady_clock::now();

    const auto materialize_milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(materialize_end - materialize_start)
            .count();
    const auto lookup_milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(lookup_end - lookup_start).count();
    std::cout << "text_bytes=" << text.bytes.size()
              << " materialize_ms=" << materialize_milliseconds << " lookups=" << lookup_count
              << " lookup_ms=" << lookup_milliseconds << " checksum=" << checksum << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "sequence text benchmark failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
