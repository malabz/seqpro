#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "seqpro/seqpro.h"

namespace {

std::uint64_t NextRandom(std::uint64_t* random_state) {
  *random_state ^= *random_state << 13U;
  *random_state ^= *random_state >> 7U;
  *random_state ^= *random_state << 17U;
  return *random_state;
}

std::uint64_t ParseQueryCount(std::string_view query_count_text) {
  std::uint64_t query_count = 0;
  const auto parse_result = std::from_chars(
      query_count_text.data(), query_count_text.data() + query_count_text.size(), query_count);
  if (parse_result.ec != std::errc() ||
      parse_result.ptr != query_count_text.data() + query_count_text.size() || query_count == 0) {
    throw std::runtime_error("QUERY_COUNT must be a positive integer");
  }
  return query_count;
}

}  // namespace

int main(int argument_count, char** argument_values) {
  if (argument_count < 3 || argument_count > 4) {
    std::cerr << "Usage: seqpro-benchmark FASTA SEQUENCE_NAME [QUERY_COUNT]\n";
    return 2;
  }

  try {
    const std::uint64_t query_count =
        argument_count == 4 ? ParseQueryCount(argument_values[3]) : 1'000'000;
    const auto open_start_time = std::chrono::steady_clock::now();
    const seqpro::IndexedFasta indexed_fasta =
        seqpro::IndexedFasta::Open(std::filesystem::path(argument_values[1]));
    const auto open_end_time = std::chrono::steady_clock::now();
    const seqpro::FastaSequenceView sequence_view =
        indexed_fasta.SequenceByName(argument_values[2]);
    if (sequence_view.sequence_length() == 0) {
      throw std::runtime_error("benchmark sequence is empty");
    }

    std::uint64_t random_state = 0x9e3779b97f4a7c15ULL;
    std::uint64_t query_checksum = 0;
    const auto base_query_start_time = std::chrono::steady_clock::now();
    for (std::uint64_t query_index = 0; query_index < query_count; ++query_index) {
      const seqpro::SequencePosition sequence_position =
          NextRandom(&random_state) % sequence_view.sequence_length();
      query_checksum += static_cast<unsigned char>(sequence_view.ReadBase(sequence_position));
    }
    const auto base_query_end_time = std::chrono::steady_clock::now();

    const double open_seconds =
        std::chrono::duration<double>(open_end_time - open_start_time).count();
    const double query_seconds =
        std::chrono::duration<double>(base_query_end_time - base_query_start_time).count();
    std::cout << "open_seconds\t" << open_seconds << '\n'
              << "sequence_length\t" << sequence_view.sequence_length() << '\n'
              << "base_query_count\t" << query_count << '\n'
              << "base_queries_per_second\t" << static_cast<double>(query_count) / query_seconds
              << '\n'
              << "checksum\t" << query_checksum << '\n';
    return 0;
  } catch (const std::exception& benchmark_error) {
    std::cerr << "seqpro-benchmark: " << benchmark_error.what() << '\n';
    return 1;
  }
}
