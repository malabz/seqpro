#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "seqpro/seqpro.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 3) {
    std::cerr << "Usage: seqpro-read-modes-example FASTA SEQUENCE_NAME\n";
    return 2;
  }

  try {
    const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(argument_values[1]);
    const seqpro::FastaSequenceView sequence_view =
        indexed_fasta.SequenceByName(argument_values[2]);
    if (sequence_view.sequence_length() < 8) {
      std::cerr << "the example requires at least eight sequence symbols\n";
      return 2;
    }

    constexpr seqpro::SequencePosition kSequenceStartPosition = 2;
    constexpr seqpro::SequenceLength kSubsequenceLength = 6;
    const std::string allocated_subsequence =
        sequence_view.ReadSubsequence(kSequenceStartPosition, kSubsequenceLength);

    std::vector<char> destination_buffer(kSubsequenceLength);
    sequence_view.CopySubsequenceTo(kSequenceStartPosition, destination_buffer.data(),
                                   destination_buffer.size());
    const std::string copied_subsequence(destination_buffer.begin(), destination_buffer.end());

    std::ostringstream output_stream;
    sequence_view.WriteSubsequenceTo(kSequenceStartPosition, kSubsequenceLength, output_stream, 4);

    std::string chunked_subsequence;
    const seqpro::SequenceChunkRange sequence_chunks =
        sequence_view.SubsequenceChunks(kSequenceStartPosition, kSubsequenceLength);
    chunked_subsequence.reserve(kSubsequenceLength);
    for (const seqpro::SequenceChunk sequence_chunk : sequence_chunks) {
      chunked_subsequence.append(sequence_chunk.sequence_bases);
    }

    if (allocated_subsequence != copied_subsequence ||
        allocated_subsequence != output_stream.str() ||
        allocated_subsequence != chunked_subsequence) {
      std::cerr << "the interval reading APIs returned different bytes\n";
      return 1;
    }

    std::cout << "first_base\t" << sequence_view.ReadBase(0) << '\n'
              << "region\t" << allocated_subsequence << '\n'
              << "estimated_chunks\t" << sequence_chunks.estimated_chunk_count() << '\n';
    return 0;
  } catch (const seqpro::SeqProError& seqpro_error) {
    std::cerr << seqpro_error.what() << '\n';
    return 1;
  }
}
