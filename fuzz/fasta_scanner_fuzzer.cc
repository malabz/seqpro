#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "fasta_internal.h"
#include "seqpro/error.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* input_bytes,
                                      std::size_t input_size_bytes) {
  if (input_size_bytes == 0) {
    return 0;
  }
  const std::size_t input_chunk_size_bytes =
      std::max<std::size_t>(1U, static_cast<std::size_t>(input_bytes[0]));
  const std::string_view fasta_text(reinterpret_cast<const char*>(input_bytes + 1U),
                                    input_size_bytes - 1U);
  try {
    static_cast<void>(seqpro::internal::ScanFastaText(fasta_text, input_chunk_size_bytes));
  } catch (const seqpro::SeqProError&) {
  }
  return 0;
}
