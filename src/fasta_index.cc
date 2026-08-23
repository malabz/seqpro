#include "seqpro/fasta_index.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <unordered_set>
#include <unistd.h>
#include <utility>
#include <vector>

#include "fasta_internal.h"
#include "seqpro/error.h"
#include "xxhash.h"

namespace seqpro::internal {
namespace {

constexpr std::size_t kScanBufferSize = 8U * 1024U * 1024U;
constexpr std::uint64_t kNanosecondsPerSecond = 1000000000ULL;

std::string ErrnoMessage(int error_number) {
  return std::string(std::strerror(error_number));
}

class ScopedFileDescriptor {
 public:
  explicit ScopedFileDescriptor(int file_descriptor) noexcept
      : file_descriptor_(file_descriptor) {}
  ~ScopedFileDescriptor() {
    if (file_descriptor_ >= 0) {
      close(file_descriptor_);
    }
  }
  ScopedFileDescriptor(const ScopedFileDescriptor&) = delete;
  ScopedFileDescriptor& operator=(const ScopedFileDescriptor&) = delete;

  int get() const noexcept { return file_descriptor_; }

  void Close(const std::string& operation, const std::filesystem::path& file_path) {
    const int file_descriptor = std::exchange(file_descriptor_, -1);
    if (file_descriptor >= 0 && close(file_descriptor) != 0) {
      const int error_number = errno;
      throw SeqProError(ErrorCode::kIoError,
                        operation + " '" + file_path.string() + "': " +
                            ErrnoMessage(error_number));
    }
  }

 private:
  int file_descriptor_;
};

[[noreturn]] void ThrowIoError(const std::string& operation,
                               const std::filesystem::path& file_path,
                               int error_number) {
  throw SeqProError(ErrorCode::kIoError,
                    operation + " '" + file_path.string() + "': " +
                        ErrnoMessage(error_number));
}

bool IsAsciiWhitespace(unsigned char character) noexcept {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
         character == '\v' || character == '\f';
}

std::uint64_t CheckedAdd(std::uint64_t left, std::uint64_t right,
                         const std::string& operation) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "integer overflow while " + operation);
  }
  return left + right;
}

std::uint64_t CheckedMultiply(std::uint64_t left, std::uint64_t right,
                              const std::string& operation) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "integer overflow while " + operation);
  }
  return left * right;
}

std::uint64_t ParseUnsignedInteger(std::string_view text,
                                   const std::filesystem::path& file_path,
                                   std::uint64_t line_number, std::string_view field_name,
                                   ErrorCode error_code) {
  if (text.empty() || text.front() == '-' || text.front() == '+') {
    throw SeqProError(error_code,
                      "invalid unsigned " + std::string(field_name) + " in '" +
                          file_path.string() + "' at line " + std::to_string(line_number));
  }
  std::uint64_t parsed_value = 0;
  const char* const text_end = text.data() + text.size();
  const auto conversion = std::from_chars(text.data(), text_end, parsed_value);
  if (conversion.ec != std::errc() || conversion.ptr != text_end) {
    throw SeqProError(error_code,
                      "invalid unsigned " + std::string(field_name) + " in '" +
                          file_path.string() + "' at line " + std::to_string(line_number));
  }
  return parsed_value;
}

class Xxh3State {
 public:
  Xxh3State() : state_(XXH3_createState(), &XXH3_freeState) {
    if (!state_ || XXH3_128bits_reset(state_.get()) == XXH_ERROR) {
      throw SeqProError(ErrorCode::kIoError, "cannot initialize XXH3-128 state");
    }
  }

  void Update(const void* bytes, std::size_t byte_count) {
    if (XXH3_128bits_update(state_.get(), bytes, byte_count) == XXH_ERROR) {
      throw SeqProError(ErrorCode::kIoError, "cannot update XXH3-128 state");
    }
  }

  std::string DigestHex() const {
    XXH128_canonical_t canonical_hash{};
    XXH128_canonicalFromHash(&canonical_hash, XXH3_128bits_digest(state_.get()));
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string hexadecimal_hash;
    hexadecimal_hash.resize(sizeof(canonical_hash.digest) * 2U);
    for (std::size_t byte_index = 0; byte_index < sizeof(canonical_hash.digest); ++byte_index) {
      const unsigned char byte = canonical_hash.digest[byte_index];
      hexadecimal_hash[byte_index * 2U] = kHexDigits[byte >> 4U];
      hexadecimal_hash[byte_index * 2U + 1U] = kHexDigits[byte & 0x0FU];
    }
    return hexadecimal_hash;
  }

 private:
  std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state_;
};

bool IsLowercaseHexHash(std::string_view hash_text) noexcept {
  if (hash_text.size() != 32) {
    return false;
  }
  return std::all_of(hash_text.begin(), hash_text.end(), [](char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

bool SourceIdentityMatches(const SourceFileIdentity& left,
                           const SourceFileIdentity& right) noexcept {
  return left.file_size_bytes == right.file_size_bytes &&
         left.modification_time_nanoseconds == right.modification_time_nanoseconds &&
         left.device_number == right.device_number && left.inode_number == right.inode_number;
}

void RequireUnchangedSourceFile(const std::filesystem::path& fasta_path,
                                const SourceFileIdentity& expected_identity) {
  const SourceFileIdentity current_identity = ReadSourceFileIdentity(fasta_path);
  if (!SourceIdentityMatches(expected_identity, current_identity)) {
    throw SeqProError(ErrorCode::kIoError,
                      "FASTA file changed before index publication: '" +
                          fasta_path.string() + "'");
  }
}

void VerifySameIndexEntries(const std::vector<FastaIndexEntry>& expected_entries,
                            const std::vector<FastaIndexEntry>& actual_entries,
                            const std::filesystem::path& fasta_index_path) {
  if (expected_entries.size() != actual_entries.size()) {
    throw SeqProError(ErrorCode::kStaleFastaIndex,
                      "FASTA index record count does not match FASTA content: '" +
                          fasta_index_path.string() + "'");
  }
  for (std::size_t entry_index = 0; entry_index < expected_entries.size(); ++entry_index) {
    const FastaIndexEntry& expected = expected_entries[entry_index];
    const FastaIndexEntry& actual = actual_entries[entry_index];
    if (expected.sequence_name != actual.sequence_name ||
        expected.sequence_length != actual.sequence_length ||
        expected.first_base_offset_bytes != actual.first_base_offset_bytes ||
        expected.bases_per_line != actual.bases_per_line ||
        expected.bytes_per_line != actual.bytes_per_line) {
      throw SeqProError(ErrorCode::kStaleFastaIndex,
                        "FASTA index entry for sequence '" + expected.sequence_name +
                            "' does not match FASTA content: '" +
                            fasta_index_path.string() + "'");
    }
  }
}

class StreamingFastaScanner {
 public:
  explicit StreamingFastaScanner(std::filesystem::path fasta_path)
      : fasta_path_(std::move(fasta_path)) {
    constexpr std::size_t kInitialSequenceCapacity = 1024;
    fasta_index_entries_.reserve(kInitialSequenceCapacity);
    sequence_names_.reserve(kInitialSequenceCapacity);
  }

  FastaScanReport Scan() {
    const int opened_file_descriptor = open(fasta_path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (opened_file_descriptor < 0) {
      ThrowIoError("cannot open FASTA file", fasta_path_, errno);
    }
    ScopedFileDescriptor file_descriptor(opened_file_descriptor);

    struct stat initial_status {};
    if (fstat(file_descriptor.get(), &initial_status) != 0) {
      ThrowIoError("cannot inspect FASTA file", fasta_path_, errno);
    }
    if (!S_ISREG(initial_status.st_mode)) {
      throw SeqProError(ErrorCode::kIoError,
                        "FASTA path is not a regular file: '" + fasta_path_.string() + "'");
    }
    if (initial_status.st_size <= 0) {
      throw SeqProError(ErrorCode::kInvalidFasta,
                        "FASTA file is empty: '" + fasta_path_.string() + "'");
    }

    static_cast<void>(posix_fadvise(file_descriptor.get(), 0, 0, POSIX_FADV_SEQUENTIAL));
    std::vector<char> scan_buffer(kScanBufferSize);
    Xxh3State hash_state;
    std::uint64_t next_file_offset = 0;
    std::array<unsigned char, 2> compression_magic{};
    std::size_t compression_magic_size = 0;

    while (true) {
      ssize_t bytes_read = read(file_descriptor.get(), scan_buffer.data(), scan_buffer.size());
      if (bytes_read < 0) {
        if (errno == EINTR) {
          continue;
        }
        ThrowIoError("cannot read FASTA file", fasta_path_, errno);
      }
      if (bytes_read == 0) {
        break;
      }

      const std::size_t available_bytes = static_cast<std::size_t>(bytes_read);
      if (compression_magic_size < compression_magic.size()) {
        const std::size_t copied_magic_bytes =
            std::min(compression_magic.size() - compression_magic_size, available_bytes);
        std::memcpy(compression_magic.data() + compression_magic_size, scan_buffer.data(),
                    copied_magic_bytes);
        compression_magic_size += copied_magic_bytes;
        if (compression_magic_size == compression_magic.size() &&
            compression_magic[0] == 0x1FU && compression_magic[1] == 0x8BU) {
          throw SeqProError(ErrorCode::kUnsupportedFileFormat,
                            "compressed FASTA is not supported in SeqPro v0.1.0: '" +
                                fasta_path_.string() + "'");
        }
      }
      hash_state.Update(scan_buffer.data(), available_bytes);
      ProcessBuffer(scan_buffer.data(), available_bytes, next_file_offset);
      next_file_offset = CheckedAdd(next_file_offset, available_bytes,
                                    "tracking FASTA file position");
    }

    struct stat final_status {};
    if (fstat(file_descriptor.get(), &final_status) != 0) {
      ThrowIoError("cannot re-inspect FASTA file", fasta_path_, errno);
    }
    file_descriptor.Close("cannot close FASTA file", fasta_path_);

    if (pending_carriage_return_) {
      throw SeqProError(ErrorCode::kInvalidFasta,
                        "FASTA line ends with a bare carriage return: '" +
                            fasta_path_.string() + "'");
    }
    if (line_is_open_) {
      FinishLine(0);
    }
    FinishCurrentRecord();
    if (fasta_index_entries_.empty()) {
      throw SeqProError(ErrorCode::kInvalidFasta,
                        "FASTA file contains no records: '" + fasta_path_.string() + "'");
    }

    const SourceFileIdentity initial_identity = IdentityFromStatus(initial_status);
    const SourceFileIdentity final_identity = IdentityFromStatus(final_status);
    if (!SourceIdentityMatches(initial_identity, final_identity) ||
        next_file_offset != final_identity.file_size_bytes) {
      throw SeqProError(ErrorCode::kIoError,
                        "FASTA file changed while its index was being built: '" +
                            fasta_path_.string() + "'");
    }

    return FastaScanReport{std::move(fasta_index_entries_), total_base_count_,
                           hash_state.DigestHex(), final_identity};
  }

 private:
  enum class LineKind { kUnknown, kHeader, kSequence };

  static SourceFileIdentity IdentityFromStatus(const struct stat& file_status) {
    if (file_status.st_size < 0 || file_status.st_mtim.tv_sec < 0 ||
        file_status.st_mtim.tv_nsec < 0) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "negative file metadata cannot be represented by SeqPro");
    }
    const std::uint64_t seconds = static_cast<std::uint64_t>(file_status.st_mtim.tv_sec);
    const std::uint64_t nanoseconds = static_cast<std::uint64_t>(file_status.st_mtim.tv_nsec);
    return SourceFileIdentity{
        static_cast<std::uint64_t>(file_status.st_size),
        CheckedAdd(CheckedMultiply(seconds, kNanosecondsPerSecond,
                                   "converting FASTA modification time"),
                   nanoseconds, "converting FASTA modification time"),
        static_cast<std::uint64_t>(file_status.st_dev),
        static_cast<std::uint64_t>(file_status.st_ino)};
  }

  void ProcessBuffer(const char* bytes, std::size_t byte_count,
                     std::uint64_t buffer_file_offset) {
    std::size_t consumed_bytes = 0;
    while (consumed_bytes < byte_count) {
      const void* newline_address =
          std::memchr(bytes + consumed_bytes, '\n', byte_count - consumed_bytes);
      if (newline_address == nullptr) {
        ProcessLineFragment(bytes + consumed_bytes, byte_count - consumed_bytes, false,
                            CheckedAdd(buffer_file_offset, consumed_bytes,
                                       "tracking FASTA line position"));
        return;
      }
      const auto* newline = static_cast<const char*>(newline_address);
      const std::size_t fragment_size =
          static_cast<std::size_t>(newline - (bytes + consumed_bytes));
      ProcessLineFragment(bytes + consumed_bytes, fragment_size, true,
                          CheckedAdd(buffer_file_offset, consumed_bytes,
                                     "tracking FASTA line position"));
      consumed_bytes += fragment_size + 1U;
    }
  }

  void ProcessLineFragment(const char* bytes, std::size_t byte_count, bool line_ends,
                           std::uint64_t fragment_file_offset) {
    if (pending_carriage_return_) {
      if (byte_count != 0) {
        throw SeqProError(ErrorCode::kInvalidFasta,
                          "carriage return occurs inside a FASTA line in '" +
                              fasta_path_.string() + "'");
      }
      if (line_ends) {
        pending_carriage_return_ = false;
        FinishLine(2);
      }
      return;
    }

    const void* carriage_return_address = std::memchr(bytes, '\r', byte_count);
    if (carriage_return_address != nullptr) {
      const auto* carriage_return = static_cast<const char*>(carriage_return_address);
      const std::size_t carriage_return_index =
          static_cast<std::size_t>(carriage_return - bytes);
      if (carriage_return_index + 1U != byte_count) {
        throw SeqProError(ErrorCode::kInvalidFasta,
                          "carriage return occurs inside a FASTA line in '" +
                              fasta_path_.string() + "'");
      }
      ProcessLineContent(bytes, carriage_return_index, fragment_file_offset);
      if (line_ends) {
        FinishLine(2);
      } else {
        pending_carriage_return_ = true;
      }
      return;
    }

    ProcessLineContent(bytes, byte_count, fragment_file_offset);
    if (line_ends) {
      FinishLine(1);
    }
  }

  void ProcessLineContent(const char* bytes, std::size_t byte_count,
                          std::uint64_t fragment_file_offset) {
    if (byte_count == 0) {
      return;
    }
    if (!line_is_open_) {
      BeginLine(bytes[0], fragment_file_offset);
    }

    if (line_kind_ == LineKind::kHeader) {
      ProcessHeaderCharacters(bytes, byte_count);
      return;
    }

    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index) {
      const unsigned char character = static_cast<unsigned char>(bytes[byte_index]);
      if (character == 0 || IsAsciiWhitespace(character)) {
        throw SeqProError(ErrorCode::kInvalidFasta,
                          "sequence line contains NUL or ASCII whitespace in '" +
                              fasta_path_.string() + "' at file offset " +
                              std::to_string(fragment_file_offset + byte_index));
      }
    }
    current_line_base_count_ =
        CheckedAdd(current_line_base_count_, byte_count, "counting FASTA line bases");
  }

  void BeginLine(char first_character, std::uint64_t line_start_file_offset) {
    line_is_open_ = true;
    line_start_file_offset_ = line_start_file_offset;
    current_line_base_count_ = 0;
    if (first_character == '>') {
      FinishCurrentRecord();
      line_kind_ = LineKind::kHeader;
      header_name_.clear();
      header_name_started_ = false;
      header_name_complete_ = false;
    } else {
      if (!has_open_record_) {
        throw SeqProError(ErrorCode::kInvalidFasta,
                          "sequence data appears before the first FASTA header in '" +
                              fasta_path_.string() + "'");
      }
      if (previous_sequence_line_was_short_) {
        throw SeqProError(ErrorCode::kInvalidFasta,
                          "sequence '" + current_entry_.sequence_name +
                              "' continues after a short line in '" + fasta_path_.string() +
                              "'");
      }
      line_kind_ = LineKind::kSequence;
    }
  }

  void ProcessHeaderCharacters(const char* bytes, std::size_t byte_count) {
    std::size_t byte_index = 0;
    if (header_character_count_ == 0) {
      if (bytes[0] != '>') {
        throw SeqProError(ErrorCode::kInvalidFasta,
                          "internal FASTA header parsing error in '" + fasta_path_.string() +
                              "'");
      }
      byte_index = 1;
    }
    for (; byte_index < byte_count; ++byte_index) {
      const unsigned char character = static_cast<unsigned char>(bytes[byte_index]);
      if (character == 0) {
        throw SeqProError(ErrorCode::kInvalidFasta,
                          "FASTA header contains NUL in '" + fasta_path_.string() + "'");
      }
      if (!header_name_started_) {
        if (!IsAsciiWhitespace(character)) {
          header_name_started_ = true;
          header_name_.push_back(static_cast<char>(character));
        }
      } else if (!header_name_complete_) {
        if (IsAsciiWhitespace(character)) {
          header_name_complete_ = true;
        } else {
          header_name_.push_back(static_cast<char>(character));
        }
      }
    }
    header_character_count_ =
        CheckedAdd(header_character_count_, byte_count, "parsing FASTA header");
  }

  void FinishLine(std::uint64_t newline_byte_count) {
    if (!line_is_open_) {
      throw SeqProError(ErrorCode::kInvalidFasta,
                        "FASTA contains an empty line in '" + fasta_path_.string() + "'");
    }
    if (line_kind_ == LineKind::kHeader) {
      FinishHeaderLine();
    } else if (line_kind_ == LineKind::kSequence) {
      FinishSequenceLine(newline_byte_count);
    }
    line_is_open_ = false;
    line_kind_ = LineKind::kUnknown;
    header_character_count_ = 0;
  }

  void FinishHeaderLine() {
    if (header_name_.empty()) {
      throw SeqProError(ErrorCode::kInvalidFasta,
                        "FASTA header has an empty sequence name in '" +
                            fasta_path_.string() + "'");
    }
    if (!sequence_names_.insert(header_name_).second) {
      throw SeqProError(ErrorCode::kDuplicateSequenceName,
                        "duplicate FASTA sequence name '" + header_name_ + "' in '" +
                            fasta_path_.string() + "'");
    }
    if (fasta_index_entries_.size() >
        static_cast<std::size_t>(std::numeric_limits<SequenceId>::max())) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "FASTA contains too many sequences for SequenceId in '" +
                            fasta_path_.string() + "'");
    }
    current_entry_ = FastaIndexEntry{static_cast<SequenceId>(fasta_index_entries_.size()),
                                     header_name_, 0, 0, 0, 0};
    has_open_record_ = true;
    sequence_line_count_ = 0;
    expected_newline_byte_count_ = 0;
    previous_sequence_line_was_short_ = false;
  }

  void FinishSequenceLine(std::uint64_t newline_byte_count) {
    if (current_line_base_count_ == 0) {
      throw SeqProError(ErrorCode::kInvalidFasta,
                        "sequence '" + current_entry_.sequence_name +
                            "' contains an empty line in '" + fasta_path_.string() + "'");
    }

    if (sequence_line_count_ == 0) {
      // HTSlib records a virtual one-byte line ending when the only sequence line reaches EOF.
      // The value is irrelevant to row-zero queries but preserves byte-for-byte FAI compatibility.
      const std::uint64_t indexed_newline_byte_count =
          newline_byte_count == 0 ? 1U : newline_byte_count;
      current_entry_.first_base_offset_bytes = line_start_file_offset_;
      current_entry_.bases_per_line = current_line_base_count_;
      current_entry_.bytes_per_line =
          CheckedAdd(current_line_base_count_, indexed_newline_byte_count,
                     "computing FASTA bytes per line");
      expected_newline_byte_count_ = indexed_newline_byte_count;
    } else {
      if (current_line_base_count_ > current_entry_.bases_per_line) {
        throw SeqProError(ErrorCode::kInvalidFasta,
                          "sequence '" + current_entry_.sequence_name +
                              "' has a line longer than its first sequence line in '" +
                              fasta_path_.string() + "'");
      }
      if (newline_byte_count != 0 && newline_byte_count != expected_newline_byte_count_) {
        throw SeqProError(ErrorCode::kInvalidFasta,
                          "sequence '" + current_entry_.sequence_name +
                              "' mixes LF and CRLF line endings in '" + fasta_path_.string() +
                              "'");
      }
    }

    current_entry_.sequence_length =
        CheckedAdd(current_entry_.sequence_length, current_line_base_count_,
                   "counting sequence bases");
    total_base_count_ = CheckedAdd(total_base_count_, current_line_base_count_,
                                   "counting total FASTA bases");
    ++sequence_line_count_;
    previous_sequence_line_was_short_ =
        current_line_base_count_ < current_entry_.bases_per_line;
  }

  void FinishCurrentRecord() {
    if (!has_open_record_) {
      return;
    }
    if (sequence_line_count_ == 0 || current_entry_.sequence_length == 0) {
      throw SeqProError(ErrorCode::kInvalidFasta,
                        "FASTA sequence '" + current_entry_.sequence_name +
                            "' is empty in '" + fasta_path_.string() + "'");
    }
    fasta_index_entries_.push_back(std::move(current_entry_));
    has_open_record_ = false;
  }

  std::filesystem::path fasta_path_;
  std::vector<FastaIndexEntry> fasta_index_entries_;
  std::unordered_set<std::string> sequence_names_;
  FastaIndexEntry current_entry_{};
  bool has_open_record_ = false;
  std::uint64_t total_base_count_ = 0;

  bool line_is_open_ = false;
  bool pending_carriage_return_ = false;
  LineKind line_kind_ = LineKind::kUnknown;
  std::uint64_t line_start_file_offset_ = 0;
  std::uint64_t current_line_base_count_ = 0;

  std::string header_name_;
  bool header_name_started_ = false;
  bool header_name_complete_ = false;
  std::uint64_t header_character_count_ = 0;

  std::uint64_t sequence_line_count_ = 0;
  std::uint64_t expected_newline_byte_count_ = 0;
  bool previous_sequence_line_was_short_ = false;
};

std::string HeaderNameBeforeOffset(const MappedFile& mapped_fasta,
                                   std::uint64_t first_base_offset,
                                   std::uint64_t* header_start_offset,
                                   const std::filesystem::path& fasta_path) {
  const char* const bytes = mapped_fasta.mapped_bytes();
  if (first_base_offset == 0 || first_base_offset > mapped_fasta.file_size_bytes() ||
      bytes[first_base_offset - 1U] != '\n') {
    throw SeqProError(ErrorCode::kInvalidFastaIndex,
                      "FASTA index offset does not immediately follow a header line in '" +
                          fasta_path.string() + "'");
  }

  std::uint64_t header_content_end = first_base_offset - 1U;
  if (header_content_end > 0 && bytes[header_content_end - 1U] == '\r') {
    --header_content_end;
  }
  std::uint64_t header_start = header_content_end;
  while (header_start > 0 && bytes[header_start - 1U] != '\n') {
    --header_start;
  }
  if (header_start >= header_content_end || bytes[header_start] != '>') {
    throw SeqProError(ErrorCode::kInvalidFastaIndex,
                      "FASTA index offset is not preceded by a FASTA header in '" +
                          fasta_path.string() + "'");
  }

  std::uint64_t name_start = header_start + 1U;
  while (name_start < header_content_end &&
         IsAsciiWhitespace(static_cast<unsigned char>(bytes[name_start]))) {
    ++name_start;
  }
  std::uint64_t name_end = name_start;
  while (name_end < header_content_end &&
         !IsAsciiWhitespace(static_cast<unsigned char>(bytes[name_end]))) {
    if (bytes[name_end] == '\0') {
      throw SeqProError(ErrorCode::kInvalidFasta,
                        "FASTA header contains NUL in '" + fasta_path.string() + "'");
    }
    ++name_end;
  }
  if (name_start == name_end) {
    throw SeqProError(ErrorCode::kInvalidFasta,
                      "FASTA header has an empty sequence name in '" + fasta_path.string() +
                          "'");
  }
  *header_start_offset = header_start;
  return std::string(bytes + name_start, bytes + name_end);
}

void ValidateSequenceByte(char character, const std::filesystem::path& fasta_path,
                          std::uint64_t file_offset) {
  const unsigned char unsigned_character = static_cast<unsigned char>(character);
  if (unsigned_character == 0 || IsAsciiWhitespace(unsigned_character)) {
    throw SeqProError(ErrorCode::kInvalidFastaIndex,
                      "FASTA index points to a non-sequence byte in '" + fasta_path.string() +
                          "' at file offset " + std::to_string(file_offset));
  }
}

void ValidateNewline(const MappedFile& mapped_fasta, std::uint64_t newline_offset,
                     std::uint64_t newline_byte_count,
                     const std::filesystem::path& fasta_path) {
  const std::uint64_t newline_end =
      CheckedAdd(newline_offset, newline_byte_count, "validating FASTA line ending");
  if (newline_end > mapped_fasta.file_size_bytes()) {
    throw SeqProError(ErrorCode::kInvalidFastaIndex,
                      "FASTA index line ending exceeds file size in '" +
                          fasta_path.string() + "'");
  }
  const char* const bytes = mapped_fasta.mapped_bytes();
  if ((newline_byte_count == 1 && bytes[newline_offset] != '\n') ||
      (newline_byte_count == 2 &&
       (bytes[newline_offset] != '\r' || bytes[newline_offset + 1U] != '\n'))) {
    throw SeqProError(ErrorCode::kInvalidFastaIndex,
                      "FASTA index line width does not match physical line endings in '" +
                          fasta_path.string() + "'");
  }
}

std::uint64_t LastBaseFileOffset(const FastaIndexEntry& index_entry) {
  const std::uint64_t last_sequence_position = index_entry.sequence_length - 1U;
  const std::uint64_t physical_line_index =
      last_sequence_position / index_entry.bases_per_line;
  const std::uint64_t base_offset_within_line =
      last_sequence_position % index_entry.bases_per_line;
  return CheckedAdd(
      CheckedAdd(index_entry.first_base_offset_bytes,
                 CheckedMultiply(physical_line_index, index_entry.bytes_per_line,
                                 "computing indexed FASTA line offset"),
                 "computing indexed FASTA line offset"),
      base_offset_within_line, "computing indexed FASTA base offset");
}

bool PathExists(const std::filesystem::path& file_path) {
  std::error_code file_error;
  const bool exists = std::filesystem::exists(file_path, file_error);
  if (file_error) {
    throw SeqProError(ErrorCode::kIoError,
                      "cannot inspect path '" + file_path.string() + "': " +
                          file_error.message());
  }
  return exists;
}

bool FileContentsMatch(const std::filesystem::path& file_path,
                       std::string_view expected_contents) {
  const int opened_file_descriptor = open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (opened_file_descriptor < 0) {
    return false;
  }
  ScopedFileDescriptor file_descriptor(opened_file_descriptor);
  std::array<char, 64U * 1024U> comparison_buffer{};
  std::size_t compared_bytes = 0;
  while (compared_bytes < expected_contents.size()) {
    const std::size_t requested_bytes =
        std::min(comparison_buffer.size(), expected_contents.size() - compared_bytes);
    const ssize_t bytes_read =
        read(file_descriptor.get(), comparison_buffer.data(), requested_bytes);
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (bytes_read == 0) {
      return false;
    }
    const std::size_t available_bytes = static_cast<std::size_t>(bytes_read);
    if (std::memcmp(comparison_buffer.data(), expected_contents.data() + compared_bytes,
                    available_bytes) != 0) {
      return false;
    }
    compared_bytes += available_bytes;
  }

  char extra_byte = '\0';
  while (true) {
    const ssize_t bytes_read = read(file_descriptor.get(), &extra_byte, 1);
    if (bytes_read < 0 && errno == EINTR) {
      continue;
    }
    return bytes_read == 0;
  }
}

void RequireDistinctPaths(const std::filesystem::path& first_path,
                          const std::filesystem::path& second_path,
                          std::string_view relationship_description) {
  std::error_code path_error;
  const std::filesystem::path absolute_first_path =
      std::filesystem::absolute(first_path, path_error).lexically_normal();
  if (path_error) {
    throw SeqProError(ErrorCode::kIoError,
                      "cannot resolve path '" + first_path.string() + "': " +
                          path_error.message());
  }
  const std::filesystem::path absolute_second_path =
      std::filesystem::absolute(second_path, path_error).lexically_normal();
  if (path_error) {
    throw SeqProError(ErrorCode::kIoError,
                      "cannot resolve path '" + second_path.string() + "': " +
                          path_error.message());
  }
  if (absolute_first_path == absolute_second_path) {
    throw SeqProError(ErrorCode::kInvalidArgument,
                      std::string(relationship_description) + ": '" +
                          absolute_first_path.string() + "'");
  }

  const bool first_exists = PathExists(first_path);
  const bool second_exists = PathExists(second_path);
  if (first_exists && second_exists) {
    path_error.clear();
    const bool paths_are_equivalent =
        std::filesystem::equivalent(first_path, second_path, path_error);
    if (path_error) {
      throw SeqProError(ErrorCode::kIoError,
                        "cannot compare paths '" + first_path.string() + "' and '" +
                            second_path.string() + "': " + path_error.message());
    }
    if (paths_are_equivalent) {
      throw SeqProError(ErrorCode::kInvalidArgument,
                        std::string(relationship_description) + ": '" +
                            first_path.string() + "' and '" + second_path.string() + "'");
    }
  }
}

}  // namespace

std::filesystem::path ResolveFastaIndexPath(
    const std::filesystem::path& fasta_path,
    const std::filesystem::path& requested_index_path) {
  if (!requested_index_path.empty()) {
    return requested_index_path;
  }
  return std::filesystem::path(fasta_path.string() + ".fai");
}

std::filesystem::path ResolveMetadataPath(const std::filesystem::path& fasta_index_path) {
  return std::filesystem::path(fasta_index_path.string() + ".seqpro.meta");
}

SourceFileIdentity ReadSourceFileIdentity(const std::filesystem::path& file_path) {
  struct stat file_status {};
  if (stat(file_path.c_str(), &file_status) != 0) {
    ThrowIoError("cannot inspect file", file_path, errno);
  }
  if (!S_ISREG(file_status.st_mode)) {
    throw SeqProError(ErrorCode::kIoError,
                      "path is not a regular file: '" + file_path.string() + "'");
  }
  if (file_status.st_size < 0 || file_status.st_mtim.tv_sec < 0 ||
      file_status.st_mtim.tv_nsec < 0) {
    throw SeqProError(ErrorCode::kIntegerOverflow,
                      "negative file metadata cannot be represented for '" +
                          file_path.string() + "'");
  }
  const std::uint64_t seconds = static_cast<std::uint64_t>(file_status.st_mtim.tv_sec);
  const std::uint64_t nanoseconds = static_cast<std::uint64_t>(file_status.st_mtim.tv_nsec);
  return SourceFileIdentity{
      static_cast<std::uint64_t>(file_status.st_size),
      CheckedAdd(CheckedMultiply(seconds, kNanosecondsPerSecond,
                                 "converting file modification time"),
                 nanoseconds, "converting file modification time"),
      static_cast<std::uint64_t>(file_status.st_dev),
      static_cast<std::uint64_t>(file_status.st_ino)};
}

FastaScanReport ScanFasta(const std::filesystem::path& fasta_path) {
  return StreamingFastaScanner(fasta_path).Scan();
}

std::vector<FastaIndexEntry> ParseFastaIndex(
    const std::filesystem::path& fasta_index_path) {
  std::ifstream index_stream(fasta_index_path, std::ios::binary);
  if (!index_stream) {
    throw SeqProError(ErrorCode::kIoError,
                      "cannot open FASTA index '" + fasta_index_path.string() + "'");
  }

  std::vector<FastaIndexEntry> fasta_index_entries;
  std::unordered_set<std::string> sequence_names;
  std::string line;
  std::uint64_t line_number = 0;
  while (std::getline(index_stream, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line_number == 1 && (line == "YES" || line == "NO")) {
      throw SeqProError(
          ErrorCode::kInvalidFastaIndex,
          "legacy RaMAx private index detected in '" + fasta_index_path.string() +
              "'; rebuild from the original FASTA with seqpro-index build --force");
    }
    if (line.empty()) {
      throw SeqProError(ErrorCode::kInvalidFastaIndex,
                        "empty FASTA index line in '" + fasta_index_path.string() +
                            "' at line " + std::to_string(line_number));
    }

    std::array<std::string_view, 5> fields{};
    std::size_t field_start = 0;
    std::size_t field_count = 0;
    for (std::size_t character_index = 0; character_index <= line.size(); ++character_index) {
      if (character_index == line.size() || line[character_index] == '\t') {
        if (field_count >= fields.size()) {
          ++field_count;
          break;
        }
        fields[field_count++] =
            std::string_view(line.data() + field_start, character_index - field_start);
        field_start = character_index + 1U;
      }
    }
    if (field_count != fields.size()) {
      throw SeqProError(ErrorCode::kInvalidFastaIndex,
                        "FASTA index must contain exactly five TAB-separated columns in '" +
                            fasta_index_path.string() + "' at line " +
                            std::to_string(line_number));
    }

    const std::string sequence_name(fields[0]);
    if (sequence_name.empty() ||
        std::any_of(sequence_name.begin(), sequence_name.end(), [](char character) {
          return character == '\0' || IsAsciiWhitespace(static_cast<unsigned char>(character));
        })) {
      throw SeqProError(ErrorCode::kInvalidFastaIndex,
                        "invalid sequence name in FASTA index '" +
                            fasta_index_path.string() + "' at line " +
                            std::to_string(line_number));
    }
    if (!sequence_names.insert(sequence_name).second) {
      throw SeqProError(ErrorCode::kDuplicateSequenceName,
                        "duplicate sequence name '" + sequence_name + "' in FASTA index '" +
                            fasta_index_path.string() + "'");
    }
    if (fasta_index_entries.size() >
        static_cast<std::size_t>(std::numeric_limits<SequenceId>::max())) {
      throw SeqProError(ErrorCode::kIntegerOverflow,
                        "FASTA index contains too many records: '" +
                            fasta_index_path.string() + "'");
    }

    const std::uint64_t sequence_length = ParseUnsignedInteger(
        fields[1], fasta_index_path, line_number, "sequence length",
        ErrorCode::kInvalidFastaIndex);
    const std::uint64_t first_base_offset = ParseUnsignedInteger(
        fields[2], fasta_index_path, line_number, "first base offset",
        ErrorCode::kInvalidFastaIndex);
    const std::uint64_t bases_per_line = ParseUnsignedInteger(
        fields[3], fasta_index_path, line_number, "bases per line",
        ErrorCode::kInvalidFastaIndex);
    const std::uint64_t bytes_per_line = ParseUnsignedInteger(
        fields[4], fasta_index_path, line_number, "bytes per line",
        ErrorCode::kInvalidFastaIndex);
    if (sequence_length == 0 || bases_per_line == 0 || sequence_length < bases_per_line ||
        bytes_per_line < bases_per_line || bytes_per_line - bases_per_line > 2U) {
      throw SeqProError(ErrorCode::kInvalidFastaIndex,
                        "invalid length or line-width fields in FASTA index '" +
                            fasta_index_path.string() + "' at line " +
                            std::to_string(line_number));
    }

    fasta_index_entries.push_back(FastaIndexEntry{
        static_cast<SequenceId>(fasta_index_entries.size()), sequence_name, sequence_length,
        first_base_offset, bases_per_line, bytes_per_line});
  }
  if (index_stream.bad()) {
    throw SeqProError(ErrorCode::kIoError,
                      "cannot read FASTA index '" + fasta_index_path.string() + "'");
  }
  if (fasta_index_entries.empty()) {
    throw SeqProError(ErrorCode::kInvalidFastaIndex,
                      "FASTA index is empty: '" + fasta_index_path.string() + "'");
  }
  return fasta_index_entries;
}

std::string SerializeFastaIndex(
    const std::vector<FastaIndexEntry>& fasta_index_entries) {
  std::ostringstream output;
  for (const FastaIndexEntry& index_entry : fasta_index_entries) {
    output << index_entry.sequence_name << '\t' << index_entry.sequence_length << '\t'
           << index_entry.first_base_offset_bytes << '\t' << index_entry.bases_per_line << '\t'
           << index_entry.bytes_per_line << '\n';
  }
  if (!output) {
    throw SeqProError(ErrorCode::kIoError, "cannot serialize FASTA index");
  }
  return output.str();
}

std::string HashFileXxh3(const std::filesystem::path& file_path) {
  const int opened_file_descriptor = open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (opened_file_descriptor < 0) {
    ThrowIoError("cannot open file for hashing", file_path, errno);
  }
  ScopedFileDescriptor file_descriptor(opened_file_descriptor);
  Xxh3State hash_state;
  std::vector<char> hash_buffer(kScanBufferSize);
  while (true) {
    const ssize_t bytes_read =
        read(file_descriptor.get(), hash_buffer.data(), hash_buffer.size());
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      ThrowIoError("cannot read file for hashing", file_path, errno);
    }
    if (bytes_read == 0) {
      break;
    }
    hash_state.Update(hash_buffer.data(), static_cast<std::size_t>(bytes_read));
  }
  file_descriptor.Close("cannot close hashed file", file_path);
  return hash_state.DigestHex();
}

SeqProMetadata ParseSeqProMetadata(const std::filesystem::path& metadata_path) {
  std::ifstream metadata_stream(metadata_path, std::ios::binary);
  if (!metadata_stream) {
    throw SeqProError(ErrorCode::kIoError,
                      "cannot open SeqPro metadata '" + metadata_path.string() + "'");
  }
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(metadata_stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  if (metadata_stream.bad()) {
    throw SeqProError(ErrorCode::kIoError,
                      "cannot read SeqPro metadata '" + metadata_path.string() + "'");
  }
  constexpr std::size_t kExpectedLineCount = 7;
  if (lines.size() != kExpectedLineCount || lines[0] != "SEQPRO_META\t1") {
    throw SeqProError(ErrorCode::kStaleFastaIndex,
                      "unsupported or malformed SeqPro metadata schema in '" +
                          metadata_path.string() + "'");
  }

  const auto ParseMetadataField = [&](std::size_t line_index,
                                      std::string_view expected_key) -> std::string_view {
    const std::string_view metadata_line(lines[line_index]);
    const std::size_t separator = metadata_line.find('\t');
    if (separator == std::string_view::npos ||
        metadata_line.substr(0, separator) != expected_key ||
        metadata_line.find('\t', separator + 1U) != std::string_view::npos) {
      throw SeqProError(ErrorCode::kStaleFastaIndex,
                        "malformed field '" + std::string(expected_key) +
                            "' in SeqPro metadata '" + metadata_path.string() + "'");
    }
    return metadata_line.substr(separator + 1U);
  };

  const std::uint64_t fasta_size_bytes =
      ParseUnsignedInteger(ParseMetadataField(1, "fasta_size_bytes"), metadata_path, 2,
                           "FASTA size", ErrorCode::kStaleFastaIndex);
  const std::uint64_t modification_time = ParseUnsignedInteger(
      ParseMetadataField(2, "fasta_mtime_ns"), metadata_path, 3, "FASTA modification time",
      ErrorCode::kStaleFastaIndex);
  const std::string fasta_hash(ParseMetadataField(3, "fasta_xxh3_128"));
  const std::string index_hash(ParseMetadataField(4, "fai_xxh3_128"));
  if (!IsLowercaseHexHash(fasta_hash) || !IsLowercaseHexHash(index_hash)) {
    throw SeqProError(ErrorCode::kStaleFastaIndex,
                      "invalid XXH3-128 value in SeqPro metadata '" +
                          metadata_path.string() + "'");
  }
  const std::uint64_t record_count =
      ParseUnsignedInteger(ParseMetadataField(5, "record_count"), metadata_path, 6,
                           "record count", ErrorCode::kStaleFastaIndex);
  const std::uint64_t total_base_count =
      ParseUnsignedInteger(ParseMetadataField(6, "total_bases"), metadata_path, 7,
                           "total base count", ErrorCode::kStaleFastaIndex);
  return SeqProMetadata{fasta_size_bytes, modification_time, fasta_hash, index_hash,
                        record_count, total_base_count};
}

std::string SerializeSeqProMetadata(const SeqProMetadata& metadata) {
  std::ostringstream output;
  output << "SEQPRO_META\t1\n"
         << "fasta_size_bytes\t" << metadata.fasta_size_bytes << '\n'
         << "fasta_mtime_ns\t" << metadata.fasta_modification_time_nanoseconds << '\n'
         << "fasta_xxh3_128\t" << metadata.fasta_xxh3_128 << '\n'
         << "fai_xxh3_128\t" << metadata.fasta_index_xxh3_128 << '\n'
         << "record_count\t" << metadata.record_count << '\n'
         << "total_bases\t" << metadata.total_base_count << '\n';
  return output.str();
}

void ValidateFastaIndexEntries(
    const std::filesystem::path& fasta_path, const MappedFile& mapped_fasta,
    const std::vector<FastaIndexEntry>& fasta_index_entries) {
  if (mapped_fasta.file_size_bytes() >= 2 &&
      static_cast<unsigned char>(mapped_fasta.mapped_bytes()[0]) == 0x1FU &&
      static_cast<unsigned char>(mapped_fasta.mapped_bytes()[1]) == 0x8BU) {
    throw SeqProError(ErrorCode::kUnsupportedFileFormat,
                      "compressed FASTA is not supported in SeqPro v0.1.0: '" +
                          fasta_path.string() + "'");
  }

  std::vector<std::uint64_t> header_start_offsets;
  header_start_offsets.reserve(fasta_index_entries.size());
  for (const FastaIndexEntry& index_entry : fasta_index_entries) {
    if (index_entry.sequence_length == 0 || index_entry.bases_per_line == 0 ||
        index_entry.sequence_length < index_entry.bases_per_line ||
        index_entry.bytes_per_line < index_entry.bases_per_line ||
        index_entry.bytes_per_line - index_entry.bases_per_line > 2U ||
        index_entry.first_base_offset_bytes >= mapped_fasta.file_size_bytes()) {
      throw SeqProError(ErrorCode::kInvalidFastaIndex,
                        "invalid FASTA index fields for sequence '" +
                            index_entry.sequence_name + "'");
    }
    std::uint64_t header_start_offset = 0;
    const std::string physical_sequence_name =
        HeaderNameBeforeOffset(mapped_fasta, index_entry.first_base_offset_bytes,
                               &header_start_offset, fasta_path);
    if (physical_sequence_name != index_entry.sequence_name) {
      throw SeqProError(ErrorCode::kStaleFastaIndex,
                        "FASTA index name '" + index_entry.sequence_name +
                            "' does not match FASTA header '" + physical_sequence_name + "'");
    }
    header_start_offsets.push_back(header_start_offset);

    const std::uint64_t first_line_end =
        CheckedAdd(index_entry.first_base_offset_bytes, index_entry.bases_per_line,
                   "validating first FASTA sequence line");
    const std::uint64_t newline_byte_count =
        index_entry.bytes_per_line - index_entry.bases_per_line;
    if (index_entry.sequence_length > index_entry.bases_per_line) {
      if (newline_byte_count == 0) {
        throw SeqProError(ErrorCode::kInvalidFastaIndex,
                          "multi-line sequence '" + index_entry.sequence_name +
                              "' has no indexed line ending");
      }
      ValidateNewline(mapped_fasta, first_line_end, newline_byte_count, fasta_path);
    }

    const std::uint64_t last_base_offset = LastBaseFileOffset(index_entry);
    if (last_base_offset >= mapped_fasta.file_size_bytes()) {
      throw SeqProError(ErrorCode::kInvalidFastaIndex,
                        "FASTA index range exceeds file size for sequence '" +
                            index_entry.sequence_name + "'");
    }
    ValidateSequenceByte(mapped_fasta.mapped_bytes()[index_entry.first_base_offset_bytes],
                         fasta_path, index_entry.first_base_offset_bytes);
    ValidateSequenceByte(mapped_fasta.mapped_bytes()[last_base_offset], fasta_path,
                         last_base_offset);
  }

  if (header_start_offsets.front() != 0) {
    throw SeqProError(ErrorCode::kInvalidFasta,
                      "bytes appear before the first FASTA header in '" + fasta_path.string() +
                          "'");
  }
  for (std::size_t entry_index = 0; entry_index < fasta_index_entries.size(); ++entry_index) {
    const FastaIndexEntry& index_entry = fasta_index_entries[entry_index];
    const std::uint64_t last_base_offset = LastBaseFileOffset(index_entry);
    const std::uint64_t sequence_content_end =
        CheckedAdd(last_base_offset, 1U, "validating FASTA record boundary");
    const std::uint64_t newline_byte_count =
        index_entry.bytes_per_line - index_entry.bases_per_line;

    if (entry_index + 1U < fasta_index_entries.size()) {
      if (newline_byte_count == 0) {
        throw SeqProError(ErrorCode::kInvalidFastaIndex,
                          "non-final sequence '" + index_entry.sequence_name +
                              "' has no terminating line ending");
      }
      ValidateNewline(mapped_fasta, sequence_content_end, newline_byte_count, fasta_path);
      const std::uint64_t expected_next_header =
          CheckedAdd(sequence_content_end, newline_byte_count,
                     "validating FASTA record boundary");
      if (expected_next_header != header_start_offsets[entry_index + 1U]) {
        throw SeqProError(ErrorCode::kStaleFastaIndex,
                          "FASTA index record boundary is stale after sequence '" +
                              index_entry.sequence_name + "'");
      }
    } else if (sequence_content_end != mapped_fasta.file_size_bytes()) {
      if (newline_byte_count == 0 ||
          CheckedAdd(sequence_content_end, newline_byte_count,
                     "validating final FASTA line ending") !=
              mapped_fasta.file_size_bytes()) {
        throw SeqProError(ErrorCode::kStaleFastaIndex,
                          "unexpected bytes follow final sequence '" +
                              index_entry.sequence_name + "'");
      }
      ValidateNewline(mapped_fasta, sequence_content_end, newline_byte_count, fasta_path);
    }
  }
}

ValidatedFastaIndex ValidateFastaIndexFiles(
    const std::filesystem::path& fasta_path,
    const std::filesystem::path& fasta_index_path,
    IndexVerificationMode verification_mode, bool require_seqpro_metadata,
    const MappedFile* existing_mapping) {
  std::vector<FastaIndexEntry> fasta_index_entries = ParseFastaIndex(fasta_index_path);
  std::optional<MappedFile> owned_mapping;
  if (existing_mapping == nullptr) {
    owned_mapping.emplace(
        MappedFile::OpenReadOnly(fasta_path, FileAccessPattern::kOperatingSystemDefault));
    existing_mapping = &*owned_mapping;
  }
  ValidateFastaIndexEntries(fasta_path, *existing_mapping, fasta_index_entries);

  std::uint64_t total_base_count = 0;
  for (const FastaIndexEntry& index_entry : fasta_index_entries) {
    total_base_count = CheckedAdd(total_base_count, index_entry.sequence_length,
                                  "counting indexed FASTA bases");
  }

  const std::filesystem::path metadata_path = ResolveMetadataPath(fasta_index_path);
  const bool has_metadata = PathExists(metadata_path);
  if (require_seqpro_metadata && !has_metadata) {
    throw SeqProError(ErrorCode::kStaleFastaIndex,
                      "SeqPro metadata is required but missing: '" + metadata_path.string() +
                          "'");
  }

  FastaIndexOrigin index_origin = FastaIndexOrigin::kExternalStandardFai;
  IndexVerificationStatus verification_status =
      IndexVerificationStatus::kStructureValidated;
  bool fingerprint_matches = false;
  std::optional<SeqProMetadata> metadata;
  if (has_metadata) {
    metadata = ParseSeqProMetadata(metadata_path);
    if (metadata->fasta_index_xxh3_128 != HashFileXxh3(fasta_index_path) ||
        metadata->record_count != fasta_index_entries.size() ||
        metadata->total_base_count != total_base_count) {
      throw SeqProError(ErrorCode::kStaleFastaIndex,
                        "SeqPro metadata does not match the FASTA index: '" +
                            metadata_path.string() + "'");
    }
    if (verification_mode == IndexVerificationMode::kFast) {
      const SourceFileIdentity source_identity = ReadSourceFileIdentity(fasta_path);
      if (metadata->fasta_size_bytes != source_identity.file_size_bytes ||
          metadata->fasta_modification_time_nanoseconds !=
              source_identity.modification_time_nanoseconds) {
        throw SeqProError(ErrorCode::kStaleFastaIndex,
                          "SeqPro metadata does not match FASTA size or modification time: '" +
                              metadata_path.string() + "'");
      }
    }
    index_origin = FastaIndexOrigin::kSeqProVerified;
    verification_status = IndexVerificationStatus::kMetadataValidated;
  }

  if (verification_mode == IndexVerificationMode::kFull) {
    const FastaScanReport scan_report = ScanFasta(fasta_path);
    VerifySameIndexEntries(fasta_index_entries, scan_report.fasta_index_entries,
                           fasta_index_path);
    if (metadata &&
        (metadata->fasta_size_bytes != scan_report.source_file_identity.file_size_bytes ||
         metadata->fasta_xxh3_128 != scan_report.fasta_xxh3_128)) {
      throw SeqProError(
          ErrorCode::kStaleFastaIndex,
          "FASTA content fingerprint does not match SeqPro metadata: '" +
              fasta_path.string() + "'");
    }
    fingerprint_matches = metadata.has_value();
    verification_status = IndexVerificationStatus::kFullContentValidated;
  }

  const std::uint64_t sequence_count =
      static_cast<std::uint64_t>(fasta_index_entries.size());
  return ValidatedFastaIndex{
      std::move(fasta_index_entries),
      FastaIndexValidationReport{index_origin, verification_status, sequence_count,
                                 total_base_count, has_metadata, fingerprint_matches}};
}

void PublishTextFileAtomically(const std::filesystem::path& destination_path,
                               const std::string& file_contents) {
  std::filesystem::path parent_path = destination_path.parent_path();
  if (parent_path.empty()) {
    parent_path = ".";
  }
  std::random_device random_device;
  const std::string temporary_suffix = ".tmp." + std::to_string(getpid()) + "." +
                                       std::to_string(random_device()) + "." +
                                       std::to_string(random_device());
  const std::filesystem::path temporary_path =
      parent_path / (destination_path.filename().string() + temporary_suffix);

  const int opened_file_descriptor =
      open(temporary_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (opened_file_descriptor < 0) {
    ThrowIoError("cannot create temporary index file", temporary_path, errno);
  }
  ScopedFileDescriptor file_descriptor(opened_file_descriptor);

  bool temporary_file_exists = true;
  try {
    std::size_t written_bytes = 0;
    while (written_bytes < file_contents.size()) {
      const ssize_t write_count =
          write(file_descriptor.get(), file_contents.data() + written_bytes,
                file_contents.size() - written_bytes);
      if (write_count < 0) {
        if (errno == EINTR) {
          continue;
        }
        ThrowIoError("cannot write temporary index file", temporary_path, errno);
      }
      if (write_count == 0) {
        throw SeqProError(ErrorCode::kIoError,
                          "writing temporary index file made no progress: '" +
                              temporary_path.string() + "'");
      }
      written_bytes += static_cast<std::size_t>(write_count);
    }
    if (fsync(file_descriptor.get()) != 0) {
      ThrowIoError("cannot synchronize temporary index file", temporary_path, errno);
    }
    file_descriptor.Close("cannot close temporary index file", temporary_path);
    if (!FileContentsMatch(temporary_path, file_contents)) {
      throw SeqProError(ErrorCode::kIoError,
                        "temporary index file failed content verification: '" +
                            temporary_path.string() + "'");
    }
    if (rename(temporary_path.c_str(), destination_path.c_str()) != 0) {
      ThrowIoError("cannot publish index file", destination_path, errno);
    }
    temporary_file_exists = false;
  } catch (...) {
    if (temporary_file_exists) {
      unlink(temporary_path.c_str());
    }
    throw;
  }

  const int directory_descriptor = open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory_descriptor >= 0) {
    static_cast<void>(fsync(directory_descriptor));
    close(directory_descriptor);
  }

  auto retry_delay = std::chrono::milliseconds(10);
  const auto visibility_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (true) {
    struct stat published_status {};
    if (stat(destination_path.c_str(), &published_status) == 0 &&
        published_status.st_size >= 0 &&
        static_cast<std::uint64_t>(published_status.st_size) == file_contents.size() &&
        FileContentsMatch(destination_path, file_contents)) {
      break;
    }
    if (std::chrono::steady_clock::now() >= visibility_deadline) {
      throw SeqProError(ErrorCode::kIoError,
                        "published file did not become visible with the expected size: '" +
                            destination_path.string() + "'");
    }
    std::this_thread::sleep_for(retry_delay);
    retry_delay = std::min(retry_delay * 2, std::chrono::milliseconds(250));
  }
}

}  // namespace seqpro::internal

namespace seqpro {

FastaIndexBuildReport BuildFastaIndex(const std::filesystem::path& fasta_path,
                                      const FastaIndexBuildOptions& options) {
  const std::filesystem::path fasta_index_path =
      internal::ResolveFastaIndexPath(fasta_path, options.fasta_index_path);
  const std::filesystem::path metadata_path =
      internal::ResolveMetadataPath(fasta_index_path);
  internal::RequireDistinctPaths(fasta_path, fasta_index_path,
                                 "FASTA and FASTA index paths must be distinct");
  internal::RequireDistinctPaths(fasta_path, metadata_path,
                                 "FASTA and SeqPro metadata paths must be distinct");
  internal::RequireDistinctPaths(fasta_index_path, metadata_path,
                                 "FASTA index and SeqPro metadata paths must be distinct");
  const bool index_existed = internal::PathExists(fasta_index_path);
  const bool metadata_existed = internal::PathExists(metadata_path);

  if (index_existed && !options.force_rebuild) {
    if (metadata_existed) {
      const FastaIndexValidationReport validation_report = internal::ValidateFastaIndexFiles(
          fasta_path, fasta_index_path, IndexVerificationMode::kFast, true)
                                                                    .validation_report;
      return FastaIndexBuildReport{FastaIndexBuildAction::kReused, fasta_path,
                                   fasta_index_path, metadata_path,
                                   validation_report.sequence_count,
                                   validation_report.total_base_count};
    }

    internal::MappedFile mapped_fasta = internal::MappedFile::OpenReadOnly(
        fasta_path, FileAccessPattern::kOperatingSystemDefault);
    const std::vector<FastaIndexEntry> external_entries =
        internal::ParseFastaIndex(fasta_index_path);
    internal::ValidateFastaIndexEntries(fasta_path, mapped_fasta, external_entries);
    if (!options.write_seqpro_metadata) {
      std::uint64_t total_base_count = 0;
      for (const FastaIndexEntry& index_entry : external_entries) {
        total_base_count = internal::CheckedAdd(total_base_count, index_entry.sequence_length,
                                                "counting indexed FASTA bases");
      }
      return FastaIndexBuildReport{FastaIndexBuildAction::kReused, fasta_path,
                                   fasta_index_path, {},
                                   static_cast<std::uint64_t>(external_entries.size()),
                                   total_base_count};
    }

    const internal::FastaScanReport scan_report = internal::ScanFasta(fasta_path);
    internal::VerifySameIndexEntries(external_entries, scan_report.fasta_index_entries,
                                     fasta_index_path);
    internal::RequireUnchangedSourceFile(fasta_path, scan_report.source_file_identity);
    const internal::SeqProMetadata metadata{
        scan_report.source_file_identity.file_size_bytes,
        scan_report.source_file_identity.modification_time_nanoseconds,
        scan_report.fasta_xxh3_128, internal::HashFileXxh3(fasta_index_path),
        static_cast<std::uint64_t>(external_entries.size()), scan_report.total_base_count};
    internal::PublishTextFileAtomically(metadata_path,
                                        internal::SerializeSeqProMetadata(metadata));
    internal::RequireUnchangedSourceFile(fasta_path, scan_report.source_file_identity);
    internal::ValidateFastaIndexFiles(fasta_path, fasta_index_path,
                                      IndexVerificationMode::kFast, true);
    return FastaIndexBuildReport{FastaIndexBuildAction::kAdoptedExternalIndex, fasta_path,
                                 fasta_index_path, metadata_path,
                                 static_cast<std::uint64_t>(external_entries.size()),
                                 scan_report.total_base_count};
  }

  const internal::FastaScanReport scan_report = internal::ScanFasta(fasta_path);
  internal::RequireUnchangedSourceFile(fasta_path, scan_report.source_file_identity);
  const std::string serialized_index =
      internal::SerializeFastaIndex(scan_report.fasta_index_entries);
  internal::PublishTextFileAtomically(fasta_index_path, serialized_index);
  internal::ParseFastaIndex(fasta_index_path);
  internal::RequireUnchangedSourceFile(fasta_path, scan_report.source_file_identity);

  if (options.write_seqpro_metadata) {
    const internal::SeqProMetadata metadata{
        scan_report.source_file_identity.file_size_bytes,
        scan_report.source_file_identity.modification_time_nanoseconds,
        scan_report.fasta_xxh3_128, internal::HashFileXxh3(fasta_index_path),
        static_cast<std::uint64_t>(scan_report.fasta_index_entries.size()),
        scan_report.total_base_count};
    internal::PublishTextFileAtomically(metadata_path,
                                        internal::SerializeSeqProMetadata(metadata));
  } else if (metadata_existed && unlink(metadata_path.c_str()) != 0 && errno != ENOENT) {
    internal::ThrowIoError("cannot remove obsolete SeqPro metadata", metadata_path, errno);
  }

  internal::RequireUnchangedSourceFile(fasta_path, scan_report.source_file_identity);

  internal::ValidateFastaIndexFiles(fasta_path, fasta_index_path,
                                    IndexVerificationMode::kFast,
                                    options.write_seqpro_metadata);
  return FastaIndexBuildReport{
      index_existed ? FastaIndexBuildAction::kRebuilt : FastaIndexBuildAction::kCreated,
      fasta_path, fasta_index_path,
      options.write_seqpro_metadata ? metadata_path : std::filesystem::path{},
      static_cast<std::uint64_t>(scan_report.fasta_index_entries.size()),
      scan_report.total_base_count};
}

FastaIndexValidationReport ValidateFastaIndex(
    const std::filesystem::path& fasta_path,
    const std::filesystem::path& requested_index_path,
    IndexVerificationMode verification_mode) {
  const std::filesystem::path fasta_index_path =
      internal::ResolveFastaIndexPath(fasta_path, requested_index_path);
  return internal::ValidateFastaIndexFiles(fasta_path, fasta_index_path, verification_mode,
                                           false)
      .validation_report;
}

}  // namespace seqpro
