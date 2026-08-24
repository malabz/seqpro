#ifndef SEQPRO_INCLUDE_SEQPRO_ERROR_H_
#define SEQPRO_INCLUDE_SEQPRO_ERROR_H_

#include <cstdint>
#include <stdexcept>
#include <string>

#include "seqpro/export.h"

namespace seqpro {

/// Stable categories attached to every SeqProError.
enum class ErrorCode : std::uint8_t {
  /// A caller supplied a contradictory or unsupported argument combination.
  kInvalidArgument,
  /// A file or stream operation failed.
  kIoError,
  /// FASTA contents violate the supported uncompressed FASTA contract.
  kInvalidFasta,
  /// A FAI is malformed or physically inconsistent with its FASTA.
  kInvalidFastaIndex,
  /// A FAI or SeqPro metadata sidecar no longer describes the FASTA.
  kStaleFastaIndex,
  /// Two FASTA records use the same sequence name.
  kDuplicateSequenceName,
  /// A sequence name or identifier does not exist in the selected data set.
  kSequenceNotFound,
  /// A sequence, active, or text coordinate is outside its valid range.
  kSequenceRangeOutOfBounds,
  /// A checked integer operation cannot be represented by its result type.
  kIntegerOverflow,
  /// The input uses a format or reserved byte that this release does not support.
  kUnsupportedFileFormat,
};

/// Exception thrown by SeqPro public APIs for deterministic library failures.
class SEQPRO_EXPORT SeqProError : public std::runtime_error {
 public:
  /// Constructs an error with a stable category and human-readable context.
  SeqProError(ErrorCode error_code, std::string message);
  /// Owns the exported RTTI and vtable key function in the core shared library.
  ~SeqProError() override;

  /// Returns the stable machine-readable error category.
  ErrorCode error_code() const noexcept;

 private:
  ErrorCode error_code_;
};

}  // namespace seqpro

#endif  // SEQPRO_INCLUDE_SEQPRO_ERROR_H_
