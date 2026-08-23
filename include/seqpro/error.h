#ifndef SEQPRO_INCLUDE_SEQPRO_ERROR_H_
#define SEQPRO_INCLUDE_SEQPRO_ERROR_H_

#include <stdexcept>
#include <string>

#include "seqpro/export.h"

namespace seqpro {

enum class ErrorCode {
  kInvalidArgument,
  kIoError,
  kInvalidFasta,
  kInvalidFastaIndex,
  kStaleFastaIndex,
  kDuplicateSequenceName,
  kSequenceNotFound,
  kSequenceRangeOutOfBounds,
  kIntegerOverflow,
  kUnsupportedFileFormat,
};

// The exception type thrown by SeqPro public APIs.
class SEQPRO_EXPORT SeqProError : public std::runtime_error {
 public:
  SeqProError(ErrorCode error_code, std::string message);

  ErrorCode error_code() const noexcept;

 private:
  ErrorCode error_code_;
};

}  // namespace seqpro

#endif  // SEQPRO_INCLUDE_SEQPRO_ERROR_H_
