#include "seqpro/error.h"

namespace seqpro {

// The by-value parameter is part of the frozen public construction API.
// NOLINTNEXTLINE(performance-unnecessary-value-param)
SeqProError::SeqProError(ErrorCode error_code, std::string message)
    : std::runtime_error(message), error_code_(error_code) {}

SeqProError::~SeqProError() = default;

ErrorCode SeqProError::error_code() const noexcept { return error_code_; }

}  // namespace seqpro
