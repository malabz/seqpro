#include "seqpro/error.h"

#include <utility>

namespace seqpro {

SeqProError::SeqProError(ErrorCode error_code, std::string message)
    : std::runtime_error(std::move(message)), error_code_(error_code) {}

ErrorCode SeqProError::error_code() const noexcept { return error_code_; }

}  // namespace seqpro

