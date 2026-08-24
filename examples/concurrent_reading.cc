#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "seqpro/seqpro.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 3) {
    std::cerr << "Usage: seqpro-concurrent-reading-example FASTA SEQUENCE_NAME\n";
    return 2;
  }

  try {
    const seqpro::IndexedFasta indexed_fasta = seqpro::IndexedFasta::Open(argument_values[1]);
    const seqpro::FastaSequenceView sequence_view =
        indexed_fasta.SequenceByName(argument_values[2]);
    if (sequence_view.sequence_length() == 0) {
      std::cerr << "the selected sequence is empty\n";
      return 2;
    }

    constexpr std::size_t kThreadCount = 4;
    constexpr std::uint64_t kQueriesPerThread = 10000;
    std::vector<std::uint64_t> thread_checksums(kThreadCount, 0);
    std::vector<std::thread> worker_threads;
    worker_threads.reserve(kThreadCount);

    for (std::size_t thread_index = 0; thread_index < kThreadCount; ++thread_index) {
      worker_threads.emplace_back([&, thread_index]() {
        std::uint64_t checksum = 0;
        for (std::uint64_t query_index = 0; query_index < kQueriesPerThread; ++query_index) {
          const seqpro::SequencePosition sequence_position =
              (query_index + thread_index) % sequence_view.sequence_length();
          checksum += static_cast<unsigned char>(sequence_view.ReadBase(sequence_position));
        }
        thread_checksums[thread_index] = checksum;
      });
    }
    for (std::thread& worker_thread : worker_threads) {
      worker_thread.join();
    }

    std::uint64_t combined_checksum = 0;
    for (std::uint64_t thread_checksum : thread_checksums) {
      combined_checksum += thread_checksum;
    }
    std::cout << "threads\t" << kThreadCount << '\n'
              << "queries\t" << kThreadCount * kQueriesPerThread << '\n'
              << "checksum\t" << combined_checksum << '\n';
    return 0;
  } catch (const seqpro::SeqProError& seqpro_error) {
    std::cerr << seqpro_error.what() << '\n';
    return 1;
  }
}
