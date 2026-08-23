#include <cstdint>
#include <string_view>

#include "seqpro/error.h"
#include "seqpro/fasta_index.h"
#include "seqpro/indexed_fasta.h"
#include "seqpro/seqpro.h"
#include "seqpro/version.h"

int main() {
  static_assert(sizeof(seqpro::SequencePosition) == sizeof(std::uint64_t));
  static_assert(seqpro::kVersionMajor == 0);
  static_assert(seqpro::kVersionMinor == 2);
  static_assert(seqpro::kVersionPatch == 0);
  const std::string_view library_name = "SeqPro";
  return library_name.empty() || seqpro::kVersionString != "0.2.0" ? 1 : 0;
}
