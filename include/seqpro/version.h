#ifndef SEQPRO_INCLUDE_SEQPRO_VERSION_H_
#define SEQPRO_INCLUDE_SEQPRO_VERSION_H_

#include <string_view>

#define SEQPRO_VERSION_MAJOR 0
#define SEQPRO_VERSION_MINOR 1
#define SEQPRO_VERSION_PATCH 0
#define SEQPRO_VERSION_STRING "0.1.0"

namespace seqpro {

inline constexpr int kVersionMajor = SEQPRO_VERSION_MAJOR;
inline constexpr int kVersionMinor = SEQPRO_VERSION_MINOR;
inline constexpr int kVersionPatch = SEQPRO_VERSION_PATCH;
inline constexpr std::string_view kVersionString = SEQPRO_VERSION_STRING;

}  // namespace seqpro

#endif  // SEQPRO_INCLUDE_SEQPRO_VERSION_H_
