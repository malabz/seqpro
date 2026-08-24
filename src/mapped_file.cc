#include "mapped_file.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "seqpro/error.h"

namespace seqpro::internal {
namespace {

static_assert(sizeof(void*) >= 8, "SeqPro requires a 64-bit address space");
static_assert(sizeof(off_t) >= 8, "SeqPro requires 64-bit file offsets");

std::string ErrnoMessage(int error_number) { return std::string(std::strerror(error_number)); }

class ScopedFileDescriptor {
 public:
  explicit ScopedFileDescriptor(int file_descriptor) noexcept : file_descriptor_(file_descriptor) {}
  ~ScopedFileDescriptor() {
    if (file_descriptor_ >= 0) {
      close(file_descriptor_);
    }
  }
  ScopedFileDescriptor(const ScopedFileDescriptor&) = delete;
  ScopedFileDescriptor& operator=(const ScopedFileDescriptor&) = delete;

  int get() const noexcept { return file_descriptor_; }

  void Close() noexcept {
    if (file_descriptor_ >= 0) {
      close(file_descriptor_);
      file_descriptor_ = -1;
    }
  }

 private:
  int file_descriptor_;
};

}  // namespace

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 13
#pragma GCC diagnostic push
// GCC's analyzer does not follow this local RAII destructor on every exception path.
#pragma GCC diagnostic ignored "-Wanalyzer-fd-leak"
#endif
MappedFile MappedFile::OpenReadOnly(const std::filesystem::path& file_path,
                                    FileAccessPattern access_pattern) {
  const int opened_file_descriptor = open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (opened_file_descriptor < 0) {
    const int error_number = errno;
    throw SeqProError(ErrorCode::kIoError, "cannot open FASTA file '" + file_path.string() +
                                               "': " + ErrnoMessage(error_number));
  }
  ScopedFileDescriptor file_descriptor(opened_file_descriptor);
  struct stat file_status {};
  if (fstat(file_descriptor.get(), &file_status) != 0) {
    const int error_number = errno;
    throw SeqProError(ErrorCode::kIoError, "cannot inspect FASTA file '" + file_path.string() +
                                               "': " + ErrnoMessage(error_number));
  }
  if (!S_ISREG(file_status.st_mode)) {
    throw SeqProError(ErrorCode::kIoError,
                      "FASTA path is not a regular file: '" + file_path.string() + "'");
  }
  if (file_status.st_size <= 0) {
    throw SeqProError(ErrorCode::kInvalidFasta,
                      "FASTA file is empty: '" + file_path.string() + "'");
  }

  const auto unsigned_file_size = static_cast<std::uint64_t>(file_status.st_size);
  if (unsigned_file_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw SeqProError(
        ErrorCode::kIntegerOverflow,
        "FASTA file is too large for this address space: '" + file_path.string() + "'");
  }

  void* mapped_address = mmap(nullptr, static_cast<std::size_t>(unsigned_file_size), PROT_READ,
                              MAP_PRIVATE, file_descriptor.get(), 0);
  const int mapping_error = errno;
  file_descriptor.Close();
  if (mapped_address == MAP_FAILED) {
    throw SeqProError(ErrorCode::kIoError, "cannot memory-map FASTA file '" + file_path.string() +
                                               "': " + ErrnoMessage(mapping_error));
  }

  if (access_pattern != FileAccessPattern::kOperatingSystemDefault) {
    const int advice =
        access_pattern == FileAccessPattern::kRandom ? POSIX_MADV_RANDOM : POSIX_MADV_SEQUENTIAL;
    const int advice_error =
        posix_madvise(mapped_address, static_cast<std::size_t>(unsigned_file_size), advice);
    if (advice_error != 0 && advice_error != EINVAL && advice_error != ENOSYS) {
      munmap(mapped_address, static_cast<std::size_t>(unsigned_file_size));
      throw SeqProError(ErrorCode::kIoError, "cannot apply access advice to FASTA file '" +
                                                 file_path.string() +
                                                 "': " + ErrnoMessage(advice_error));
    }
  }

  return MappedFile(mapped_address, unsigned_file_size);
}
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 13
#pragma GCC diagnostic pop
#endif

MappedFile::MappedFile(void* mapped_address, std::uint64_t file_size_bytes) noexcept
    : mapped_address_(mapped_address), file_size_bytes_(file_size_bytes) {}

MappedFile::~MappedFile() { Release(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : mapped_address_(std::exchange(other.mapped_address_, nullptr)),
      file_size_bytes_(std::exchange(other.file_size_bytes_, 0)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
  if (this != &other) {
    Release();
    mapped_address_ = std::exchange(other.mapped_address_, nullptr);
    file_size_bytes_ = std::exchange(other.file_size_bytes_, 0);
  }
  return *this;
}

const char* MappedFile::mapped_bytes() const noexcept {
  return static_cast<const char*>(mapped_address_);
}

std::uint64_t MappedFile::file_size_bytes() const noexcept { return file_size_bytes_; }

void MappedFile::Release() noexcept {
  if (mapped_address_ != nullptr) {
    munmap(mapped_address_, static_cast<std::size_t>(file_size_bytes_));
    mapped_address_ = nullptr;
    file_size_bytes_ = 0;
  }
}

}  // namespace seqpro::internal
