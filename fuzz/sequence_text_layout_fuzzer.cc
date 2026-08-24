#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "seqpro/error.h"
#include "seqpro/fasta_index.h"
#include "seqpro/indexed_fasta.h"
#include "seqpro/sequence_text_layout.h"

namespace {

const seqpro::IndexedFasta& FuzzIndexedFasta() {
  static const seqpro::IndexedFasta indexed_fasta = [] {
    const std::filesystem::path fuzz_directory =
        std::filesystem::path("/tmp") / ("seqpro-sequence-text-fuzzer-" + std::to_string(getpid()));
    std::filesystem::create_directories(fuzz_directory);
    const std::filesystem::path fasta_path = fuzz_directory / "fuzz.fa";
    std::ofstream fasta_stream(fasta_path, std::ios::binary | std::ios::trunc);
    fasta_stream << ">alpha\nACGTNRYKMSWBDHVN\n"
                 << ">beta\nttgcaACGTNRY\n"
                 << ">gamma\nABCDEFGHIJKLMNOPQRST\n";
    fasta_stream.close();
    if (!fasta_stream) {
      throw std::runtime_error("cannot create SequenceText fuzz fixture");
    }
    seqpro::FastaIndexBuildOptions build_options;
    build_options.force_rebuild = true;
    seqpro::BuildFastaIndex(fasta_path, build_options);
    return seqpro::IndexedFasta::Open(fasta_path);
  }();
  return indexed_fasta;
}

seqpro::OriginalSequenceInterval ValidInterval(seqpro::SequenceLength sequence_length,
                                               std::uint8_t first_coordinate_byte,
                                               std::uint8_t second_coordinate_byte) {
  seqpro::SequencePosition first_position = first_coordinate_byte % sequence_length;
  seqpro::SequencePosition second_position = second_coordinate_byte % sequence_length;
  if (first_position > second_position) {
    std::swap(first_position, second_position);
  }
  const seqpro::SequencePosition sequence_end_position =
      std::min<seqpro::SequencePosition>(sequence_length, second_position + 1U);
  return seqpro::OriginalSequenceInterval{first_position, sequence_end_position};
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* input_bytes,
                                      std::size_t input_size_bytes) {
  try {
    seqpro::SequenceTextLayout layout(FuzzIndexedFasta());
    constexpr std::array<seqpro::SequenceLength, 3> kSequenceLengths{16, 12, 20};
    std::size_t input_offset_bytes = 0;
    while (input_size_bytes - input_offset_bytes >= 4U) {
      const std::uint8_t operation = input_bytes[input_offset_bytes] % 10U;
      const seqpro::SequenceId sequence_id = static_cast<seqpro::SequenceId>(
          input_bytes[input_offset_bytes + 1U] % kSequenceLengths.size());
      const seqpro::OriginalSequenceInterval valid_interval =
          ValidInterval(kSequenceLengths[sequence_id], input_bytes[input_offset_bytes + 2U],
                        input_bytes[input_offset_bytes + 3U]);
      input_offset_bytes += 4U;

      try {
        switch (operation) {
          case 0:
            layout.ExcludeInterval(sequence_id, valid_interval.sequence_start_position,
                                   valid_interval.sequence_end_position);
            break;
          case 1:
            layout.ClearExcludedIntervals(sequence_id);
            break;
          case 2:
            layout.Finalize();
            break;
          case 3:
            if (layout.is_finalized()) {
              static_cast<void>(
                  layout.FindTextPosition(sequence_id, valid_interval.sequence_start_position));
            }
            break;
          case 4:
            if (layout.is_finalized()) {
              const seqpro::SequenceTextPosition text_position =
                  input_bytes[input_offset_bytes - 1U] % layout.text_size();
              static_cast<void>(layout.ReadTextByte(text_position));
            }
            break;
          case 5:
            if (layout.is_finalized()) {
              const seqpro::SequenceTextPosition text_position =
                  input_bytes[input_offset_bytes - 1U] % layout.text_size();
              static_cast<void>(layout.LocateTextInterval(text_position, 1));
            }
            break;
          case 6:
            if (layout.is_finalized()) {
              volatile std::size_t materialized_size =
                  layout.Materialize().sequence_text_bytes.size();
              static_cast<void>(materialized_size);
            }
            break;
          case 7:
            if (layout.is_finalized()) {
              const seqpro::SequenceTextPosition text_position =
                  input_bytes[input_offset_bytes - 1U] % layout.text_size();
              layout.ExcludeTextIntervals(layout.layout_generation(), {{text_position, 1}});
            }
            break;
          case 8:
            layout.ClearAllExcludedIntervals();
            break;
          case 9:
            layout.ExcludeIntervals({
                {sequence_id, valid_interval.sequence_start_position,
                 valid_interval.sequence_end_position},
            });
            break;
        }
      } catch (const seqpro::SeqProError&) {
      }
    }

    if (!layout.is_finalized()) {
      layout.Finalize();
    }
    static_cast<void>(layout.Materialize());
  } catch (const seqpro::SeqProError&) {
  }
  return 0;
}
