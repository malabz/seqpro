#include <cstdint>
#include <type_traits>
#include <variant>

#include "seqpro/sequence_text_layout.h"

namespace {

using TextSizeMethod = seqpro::SequenceTextLength (seqpro::SequenceTextLayout::*)() const;

volatile TextSizeMethod text_size_method = &seqpro::SequenceTextLayout::text_size;

}  // namespace

int main() {
  static_assert(sizeof(seqpro::SequenceTextPosition) == sizeof(std::uint64_t));
  static_assert(!std::is_copy_constructible<seqpro::SequenceTextLayout>::value);
  static_assert(std::is_move_constructible<seqpro::SequenceTextLayout>::value);
  static_assert(std::variant_size<seqpro::SequenceTextLocation>::value == 3U);
  static_assert(seqpro::SequenceTextLayout::kSeparatorByte == 0x01);
  static_assert(seqpro::SequenceTextLayout::kTerminatorByte == 0x00);
  return text_size_method == nullptr ? 1 : 0;
}
