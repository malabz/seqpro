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
  /// Inclusive zero-based start in the original sequence.
  SequencePosition sequence_start_position;
  /// Exclusive zero-based end in the original sequence.
  SequencePosition sequence_end_position;
};

/// Original-coordinate interval associated with a sequence ID.
struct ExcludedSequenceInterval {
  /// Selected sequence containing the interval.
  SequenceId sequence_id;
  /// Inclusive zero-based start in the original sequence.
  SequencePosition sequence_start_position;
  /// Exclusive zero-based end in the original sequence.
  SequencePosition sequence_end_position;
};

/// Non-empty half-open interval represented as text start and byte length.
struct SequenceTextInterval {
  /// Inclusive start in the current finalized sequence text.
  SequenceTextPosition text_start_position;
  /// Number of real sequence bytes in the interval.
  SequenceTextLength text_length;
};

/// A sequence-text position containing a real FASTA byte.
struct SequenceTextBaseLocation {
  /// Original FASTA sequence containing the base.
  SequenceId sequence_id;
  /// Active-run index within the selected sequence.
  SequenceRunIndex sequence_run_index;
  /// Position in the original FASTA sequence.
  SequencePosition original_sequence_position;
  /// Position after excluded intervals are removed from this sequence.
  ActiveSequencePosition active_sequence_position;
};

/// A separator belonging to the active run immediately before it.
struct SequenceTextSeparatorLocation {
  /// Sequence whose active run immediately precedes the separator.
  SequenceId preceding_sequence_id;
  /// Run within preceding_sequence_id that owns the separator.
  SequenceRunIndex preceding_run_index;
};

/// The unique final terminator; it intentionally has no sequence ID.
struct SequenceTextTerminatorLocation {};

using SequenceTextLocation = std::variant<SequenceTextBaseLocation, SequenceTextSeparatorLocation,
                                          SequenceTextTerminatorLocation>;

/// Non-empty text interval located completely inside one active run.
struct LocatedSequenceInterval {
  /// Original FASTA sequence containing the interval.
  SequenceId sequence_id;
  /// Active-run index containing the complete interval.
  SequenceRunIndex sequence_run_index;
  /// Inclusive start in the original FASTA sequence.
  SequencePosition original_sequence_start_position;
  /// Inclusive start in the compressed active sequence.
  ActiveSequencePosition active_sequence_start_position;
  /// Number of sequence symbols in the interval.
  SequenceLength interval_length;
};

/// Explicitly allocated text together with the generation that produced it.
struct MaterializedSequenceText {
  /// Active sequence bytes, separators, and the unique final terminator.
  std::string sequence_text_bytes;
  /// Layout generation that produced sequence_text_bytes.
  SequenceTextGeneration layout_generation;
};

/// Mutable suffix-index text assembled from active FASTA runs.
///
/// Mutation and Finalize() must be externally serialized. After Finalize(), const queries are
/// lock-free and may be called concurrently. Any effective mutation makes coordinate and text
/// queries invalid until the next successful Finalize().
class SEQPRO_SEQUENCE_TEXT_EXPORT SequenceTextLayout {
 public:
  /// Byte placed after every non-empty active run.
  static constexpr std::uint8_t kSeparatorByte = 0x01;
  /// Unique byte terminating the complete sequence text.
  static constexpr std::uint8_t kTerminatorByte = 0x00;

  /// Uses all FAI entries in FAI order when selected_sequence_order is empty.
  ///
  /// A non-empty order selects a unique subset and fixes its layout order. Construction performs
  /// the initial no-exclusion Finalize(), so the initial generation is one.
  explicit SequenceTextLayout(IndexedFasta indexed_fasta,
                              std::vector<SequenceId> selected_sequence_order = {});
  /// Destroys the interval metadata while the shared IndexedFasta mapping remains
  /// reference-counted.
  ~SequenceTextLayout();

  SequenceTextLayout(const SequenceTextLayout&) = delete;
  SequenceTextLayout& operator=(const SequenceTextLayout&) = delete;
  /// Transfers all mutable and finalized layout state.
  SequenceTextLayout(SequenceTextLayout&&) noexcept;
  /// Transfers all mutable and finalized layout state.
  SequenceTextLayout& operator=(SequenceTextLayout&&) noexcept;

  /// Returns the immutable FASTA reader retained by this layout.
  const IndexedFasta& indexed_fasta() const noexcept;
  /// Returns the selected sequence identifiers in layout order.
  const std::vector<SequenceId>& sequence_order() const noexcept;
  /// Returns false after a successful mutation and true after Finalize().
  bool is_finalized() const noexcept;
  /// Returns the generation associated with the most recent successful Finalize().
  SequenceTextGeneration layout_generation() const noexcept;

  /// Appends one original-coordinate excluded interval and marks the layout dirty.
  void ExcludeInterval(SequenceId sequence_id, SequencePosition sequence_start_position,
                       SequencePosition sequence_end_position);
  /// Appends one original-coordinate exclusion selected by sequence name.
  void ExcludeInterval(std::string_view sequence_name, SequencePosition sequence_start_position,
                       SequencePosition sequence_end_position);
  /// Validates the complete batch before logically modifying the layout.
  void ExcludeIntervals(const std::vector<ExcludedSequenceInterval>& excluded_intervals);

  /// Converts current-generation text intervals to original exclusions atomically.
  void ExcludeTextIntervals(SequenceTextGeneration source_generation,
                            const std::vector<SequenceTextInterval>& sequence_text_intervals);

  /// Clears every exclusion associated with one selected sequence identifier.
  void ClearExcludedIntervals(SequenceId sequence_id);
  /// Clears every exclusion associated with one selected sequence name.
  void ClearExcludedIntervals(std::string_view sequence_name);
  /// Clears exclusions from every selected sequence.
  void ClearAllExcludedIntervals();
  /// Sorts and merges exclusions, rebuilds active runs, and advances generation when dirty.
  void Finalize();

  /// Returns active base count plus one separator per run and one terminator.
  SequenceTextLength text_size() const;
  /// Returns the total number of non-excluded FASTA symbols.
  SequenceLength active_base_count() const;
  /// Returns the number of non-empty active runs.
  std::size_t active_run_count() const;

  /// Returns the compressed non-excluded length of one selected sequence.
  SequenceLength ActiveSequenceLength(SequenceId sequence_id) const;
  /// Returns the excluded base count of one selected sequence.
  SequenceLength ExcludedBaseCount(SequenceId sequence_id) const;
  /// Returns the finalized active intervals in original coordinates.
  std::vector<OriginalSequenceInterval> ActiveIntervalsById(SequenceId sequence_id) const;
  /// Returns sorted and merged finalized exclusions in original coordinates.
  std::vector<OriginalSequenceInterval> ExcludedIntervalsById(SequenceId sequence_id) const;

  /// Returns null when the original base is excluded.
  std::optional<ActiveSequencePosition> FindActiveSequencePosition(
      SequenceId sequence_id, SequencePosition original_sequence_position) const;
  /// Converts a valid compressed active position to its original FASTA position.
  SequencePosition OriginalSequencePosition(SequenceId sequence_id,
                                            ActiveSequencePosition active_sequence_position) const;
  /// Returns null when the original base is excluded.
  std::optional<SequenceTextPosition> FindTextPosition(
      SequenceId sequence_id, SequencePosition original_sequence_position) const;
  /// Converts a valid active position to a sequence-text position.
  SequenceTextPosition TextPositionFromActive(
      SequenceId sequence_id, ActiveSequencePosition active_sequence_position) const;

  /// Distinguishes real bases, separators, and the final terminator.
  SequenceTextLocation LocateTextPosition(SequenceTextPosition text_position) const;

  /// Returns null unless the complete non-empty interval lies in one active run.
  std::optional<LocatedSequenceInterval> LocateTextInterval(
      SequenceTextPosition text_start_position, SequenceTextLength text_length) const;

  /// Reads an active FASTA byte, separator, or terminator at one text position.
  std::uint8_t ReadTextByte(SequenceTextPosition text_position) const;
  /// Allocates exactly text_size() bytes; the resulting string contains a final NUL.
  MaterializedSequenceText Materialize() const;

  /// Copies into a non-null buffer whose size must equal text_size() exactly.
  void CopyTextTo(char* destination_buffer, std::size_t destination_size_bytes) const;

  /// Streams text with bounded working memory and checks output failures.
  void WriteTo(std::ostream& output_stream,
               std::size_t transfer_buffer_size_bytes = std::size_t{1024} * 1024) const;

 private:
  class State;
  std::unique_ptr<State> layout_state_;
};

}  // namespace seqpro

#endif  // SEQPRO_SEQUENCE_TEXT_INCLUDE_SEQPRO_SEQUENCE_TEXT_LAYOUT_H_
