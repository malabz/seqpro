#ifndef SEQPRO_SEQUENCE_TEXT_INCLUDE_SEQPRO_SEQUENCE_TEXT_LAYOUT_H_
#define SEQPRO_SEQUENCE_TEXT_INCLUDE_SEQPRO_SEQUENCE_TEXT_LAYOUT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "seqpro/indexed_fasta.h"
#include "seqpro/sequence_text_export.h"

namespace seqpro {

/// Zero-based position in the assembled text, including control bytes.
using SequenceTextPosition = std::uint64_t;
/// Number of bytes in an assembled-text interval.
using SequenceTextLength = std::uint64_t;
/// Zero-based position after excluded bases are removed from one sequence.
using ActiveSequencePosition = std::uint64_t;
/// Version binding coordinates to one finalized layout.
using SequenceTextGeneration = std::uint64_t;
/// Zero-based active-run index within one selected sequence.
using SequenceRunIndex = std::uint32_t;

/// Non-empty, zero-based, half-open interval in an original FASTA sequence.
struct OriginalSequenceInterval {
  SequencePosition sequence_start;
  SequencePosition sequence_end;
};

/// Original-coordinate interval associated with a sequence ID.
struct ExcludedSequenceInterval {
  SequenceId sequence_id;
  SequencePosition sequence_start;
  SequencePosition sequence_end;
};

/// Non-empty half-open interval represented as text start and byte length.
struct SequenceTextInterval {
  SequenceTextPosition text_start;
  SequenceTextLength text_length;
};

/// A sequence-text position containing a real FASTA byte.
struct SequenceTextBaseLocation {
  SequenceId sequence_id;
  SequenceRunIndex sequence_run_index;
  SequencePosition original_sequence_position;
  ActiveSequencePosition active_sequence_position;
};

/// A separator belonging to the active run immediately before it.
struct SequenceTextSeparatorLocation {
  SequenceId preceding_sequence_id;
  SequenceRunIndex preceding_run_index;
};

/// The unique final terminator; it intentionally has no sequence ID.
struct SequenceTextTerminatorLocation {};

using SequenceTextLocation = std::variant<SequenceTextBaseLocation, SequenceTextSeparatorLocation,
                                          SequenceTextTerminatorLocation>;

struct LocatedSequenceInterval {
  SequenceId sequence_id;
  SequenceRunIndex sequence_run_index;
  SequencePosition original_sequence_start;
  ActiveSequencePosition active_sequence_start;
  SequenceLength interval_length;
};

/// Explicitly allocated text together with the generation that produced it.
struct MaterializedSequenceText {
  std::string bytes;
  SequenceTextGeneration layout_generation;
};

/// Mutable suffix-index text assembled from active FASTA runs.
///
/// Mutation and Finalize() must be externally serialized. After Finalize(), const queries are
/// lock-free and may be called concurrently. Any effective mutation makes coordinate and text
/// queries invalid until the next successful Finalize().
class SEQPRO_SEQUENCE_TEXT_EXPORT SequenceTextLayout {
 public:
  static constexpr std::uint8_t kSeparatorByte = 0x01;
  static constexpr std::uint8_t kTerminatorByte = 0x00;

  /// Uses all FAI entries in FAI order when sequence_order is empty.
  ///
  /// A non-empty order selects a unique subset and fixes its layout order. Construction performs
  /// the initial no-exclusion Finalize(), so the initial generation is one.
  explicit SequenceTextLayout(IndexedFasta indexed_fasta,
                              std::vector<SequenceId> sequence_order = {});
  ~SequenceTextLayout();

  SequenceTextLayout(const SequenceTextLayout&) = delete;
  SequenceTextLayout& operator=(const SequenceTextLayout&) = delete;
  SequenceTextLayout(SequenceTextLayout&&) noexcept;
  SequenceTextLayout& operator=(SequenceTextLayout&&) noexcept;

  const IndexedFasta& indexed_fasta() const noexcept;
  const std::vector<SequenceId>& sequence_order() const noexcept;
  bool is_finalized() const noexcept;
  SequenceTextGeneration layout_generation() const noexcept;

  /// Appends one original-coordinate excluded interval and marks the layout dirty.
  void ExcludeInterval(SequenceId sequence_id, SequencePosition sequence_start,
                       SequencePosition sequence_end);
  void ExcludeInterval(std::string_view sequence_name, SequencePosition sequence_start,
                       SequencePosition sequence_end);
  /// Validates the complete batch before logically modifying the layout.
  void ExcludeIntervals(const std::vector<ExcludedSequenceInterval>& intervals);

  /// Converts current-generation text intervals to original exclusions atomically.
  void ExcludeTextIntervals(SequenceTextGeneration source_generation,
                            const std::vector<SequenceTextInterval>& intervals);

  void ClearExcludedIntervals(SequenceId sequence_id);
  void ClearExcludedIntervals(std::string_view sequence_name);
  void ClearAllExcludedIntervals();
  /// Sorts and merges exclusions, rebuilds active runs, and advances generation when dirty.
  void Finalize();

  SequenceTextLength text_size() const;
  SequenceLength active_base_count() const;
  std::size_t active_run_count() const;

  SequenceLength ActiveSequenceLength(SequenceId sequence_id) const;
  SequenceLength ExcludedBaseCount(SequenceId sequence_id) const;
  std::vector<OriginalSequenceInterval> ActiveIntervalsById(SequenceId sequence_id) const;
  std::vector<OriginalSequenceInterval> ExcludedIntervalsById(SequenceId sequence_id) const;

  /// Returns null when the original base is excluded.
  std::optional<ActiveSequencePosition> FindActiveSequencePosition(
      SequenceId sequence_id, SequencePosition original_sequence_position) const;
  SequencePosition OriginalSequencePosition(SequenceId sequence_id,
                                            ActiveSequencePosition active_sequence_position) const;
  /// Returns null when the original base is excluded.
  std::optional<SequenceTextPosition> FindTextPosition(
      SequenceId sequence_id, SequencePosition original_sequence_position) const;
  SequenceTextPosition TextPositionFromActive(
      SequenceId sequence_id, ActiveSequencePosition active_sequence_position) const;

  /// Distinguishes real bases, separators, and the final terminator.
  SequenceTextLocation LocateTextPosition(SequenceTextPosition text_position) const;

  /// Returns null unless the complete non-empty interval lies in one active run.
  std::optional<LocatedSequenceInterval> LocateTextInterval(SequenceTextPosition text_start,
                                                            SequenceTextLength text_length) const;

  std::uint8_t ReadTextByte(SequenceTextPosition text_position) const;
  /// Allocates exactly text_size() bytes; the resulting string contains a final NUL.
  MaterializedSequenceText Materialize() const;

  /// Copies into a non-null buffer whose size must equal text_size() exactly.
  void CopyTextTo(char* destination, std::size_t destination_size) const;

  /// Streams text with bounded working memory and checks output failures.
  void WriteTo(std::ostream& output_stream,
               std::size_t transfer_buffer_size = std::size_t{1024} * 1024) const;

 private:
  class State;
  std::unique_ptr<State> state_;
};

}  // namespace seqpro

#endif  // SEQPRO_SEQUENCE_TEXT_INCLUDE_SEQPRO_SEQUENCE_TEXT_LAYOUT_H_
