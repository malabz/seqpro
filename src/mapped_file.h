#ifndef SEQPRO_SRC_MAPPED_FILE_H_
#define SEQPRO_SRC_MAPPED_FILE_H_

#include <cstdint>
#include <filesystem>

#include "seqpro/fasta_index.h"

namespace seqpro::internal {

class MappedFile {
 public:
  static MappedFile OpenReadOnly(const std::filesystem::path& file_path,
                                 FileAccessPattern access_pattern);

  MappedFile() = default;
  ~MappedFile();
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  MappedFile(MappedFile&& other) noexcept;
  MappedFile& operator=(MappedFile&& other) noexcept;

  const char* mapped_bytes() const noexcept;
  std::uint64_t file_size_bytes() const noexcept;

 private:
  MappedFile(void* mapped_address, std::uint64_t file_size_bytes) noexcept;
  void Release() noexcept;

  void* mapped_address_ = nullptr;
  std::uint64_t file_size_bytes_ = 0;
};

}  // namespace seqpro::internal

#endif  // SEQPRO_SRC_MAPPED_FILE_H_
