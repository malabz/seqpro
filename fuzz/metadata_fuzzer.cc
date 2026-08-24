#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "fasta_internal.h"
#include "seqpro/error.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* input_bytes,
                                      std::size_t input_size_bytes) {
  const std::string_view metadata_text(reinterpret_cast<const char*>(input_bytes),
                                       input_size_bytes);
  try {
    static_cast<void>(seqpro::internal::ParseSeqProMetadataText(
        metadata_text, std::filesystem::path("<fuzz-metadata>")));
  } catch (const seqpro::SeqProError&) {
  }
  return 0;
}
