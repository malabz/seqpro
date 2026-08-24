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
  ///
  /// @param indexed_fasta Copyable owning FASTA handle retained by the layout.
  /// @param selected_sequence_order Optional unique sequence-ID subset in desired text order.
  /// @throws SeqProError with kSequenceNotFound for an invalid ID, kInvalidArgument for duplicate
  /// IDs, or kIntegerOverflow when the initial layout size cannot be represented.
  /// @note Construction does not scan FASTA bases.
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
  ///
  /// @param sequence_id Selected sequence containing the interval.
  /// @param sequence_start_position Inclusive zero-based original coordinate.
  /// @param sequence_end_position Exclusive zero-based original coordinate.
  /// @throws SeqProError with kSequenceNotFound for an unselected ID, kInvalidArgument for an empty
  /// or reversed interval, or kSequenceRangeOutOfBounds when the end exceeds sequence length.
  /// @warning Mutation must be externally serialized and must not overlap const queries.
  void ExcludeInterval(SequenceId sequence_id, SequencePosition sequence_start_position,
                       SequencePosition sequence_end_position);
  /// Appends one original-coordinate exclusion selected by sequence name.
  ///
  /// @param sequence_name Exact name of a selected sequence.
  /// @param sequence_start_position Inclusive zero-based original coordinate.
  /// @param sequence_end_position Exclusive zero-based original coordinate.
  /// @throws SeqProError with kSequenceNotFound for an absent or unselected name and the same range
  /// errors as the ID overload.
  void ExcludeInterval(std::string_view sequence_name, SequencePosition sequence_start_position,
                       SequencePosition sequence_end_position);
  /// Validates the complete batch before logically modifying the layout.
  ///
  /// @param excluded_intervals Original-coordinate non-empty intervals to append atomically.
  /// @throws SeqProError for any invalid sequence or interval, or integer overflow. Validation
  /// failure leaves the logical exclusions unchanged.
  /// @warning A successful non-empty update makes the layout dirty until Finalize().
  void ExcludeIntervals(const std::vector<ExcludedSequenceInterval>& excluded_intervals);

  /// Converts current-generation text intervals to original exclusions atomically.
  ///
  /// @param source_generation Generation that produced every supplied text coordinate.
  /// @param sequence_text_intervals Non-empty intervals, each completely within one active run.
  /// @throws SeqProError with kInvalidArgument when the layout is dirty or generation differs,
  /// kSequenceRangeOutOfBounds for an invalid text interval, or kIntegerOverflow for checked
  /// arithmetic failure. Any invalid batch member leaves the object unchanged.
  /// @pre is_finalized() is true and source_generation equals layout_generation().
  void ExcludeTextIntervals(SequenceTextGeneration source_generation,
                            const std::vector<SequenceTextInterval>& sequence_text_intervals);

  /// Clears every exclusion associated with one selected sequence identifier.
  ///
  /// @param sequence_id Selected sequence to restore completely.
  /// @throws SeqProError with kSequenceNotFound for an invalid or unselected ID.
  void ClearExcludedIntervals(SequenceId sequence_id);
  /// Clears every exclusion associated with one selected sequence name.
  ///
  /// @param sequence_name Exact name of a selected sequence to restore completely.
  /// @throws SeqProError with kSequenceNotFound for an absent or unselected name.
  void ClearExcludedIntervals(std::string_view sequence_name);
  /// Clears exclusions from every selected sequence.
  void ClearAllExcludedIntervals();
  /// Sorts and merges exclusions, rebuilds active runs, and advances generation when dirty.
  ///
  /// Overlapping and adjacent intervals are merged. A clean call is an idempotent no-op. Failed
  /// checked arithmetic leaves the layout dirty and does not publish partial run tables.
  ///
  /// @throws SeqProError with kIntegerOverflow when run counts, prefixes, text size, or generation
  /// cannot be represented.
  /// @warning Mutation and Finalize() are single-threaded phases and must not overlap queries.
  /// @par Complexity
  /// O(M log M + S + R), where M is exclusion count, S selected sequences, and R active runs.
  void Finalize();

  /// Returns active base count plus one separator per run and one terminator.
  SequenceTextLength text_size() const;
  /// Returns the total number of non-excluded FASTA symbols.
  SequenceLength active_base_count() const;
  /// Returns the number of non-empty active runs.
  std::size_t active_run_count() const;

  /// Returns the compressed non-excluded length of one selected sequence.
  ///
  /// @param sequence_id Selected sequence to inspect.
  /// @return Number of non-excluded original bases.
  /// @throws SeqProError with kInvalidArgument when dirty or kSequenceNotFound for an unselected ID.
  SequenceLength ActiveSequenceLength(SequenceId sequence_id) const;
  /// Returns the excluded base count of one selected sequence.
  ///
  /// @param sequence_id Selected sequence to inspect.
  /// @return Number of excluded original bases after interval merging.
  /// @throws SeqProError with kInvalidArgument when dirty or kSequenceNotFound for an unselected ID.
  SequenceLength ExcludedBaseCount(SequenceId sequence_id) const;
  /// Returns the finalized active intervals in original coordinates.
  ///
  /// @param sequence_id Selected sequence to inspect.
  /// @return Owning vector of non-empty, sorted original-coordinate intervals.
  /// @throws SeqProError with kInvalidArgument when dirty or kSequenceNotFound for an unselected ID.
  std::vector<OriginalSequenceInterval> ActiveIntervalsById(SequenceId sequence_id) const;
  /// Returns sorted and merged finalized exclusions in original coordinates.
  ///
  /// @param sequence_id Selected sequence to inspect.
  /// @return Owning vector of non-empty, sorted, disjoint original-coordinate intervals.
  /// @throws SeqProError with kInvalidArgument when dirty or kSequenceNotFound for an unselected ID.
  std::vector<OriginalSequenceInterval> ExcludedIntervalsById(SequenceId sequence_id) const;

  /// Returns null when the original base is excluded.
  ///
  /// @param sequence_id Selected sequence containing the original coordinate.
  /// @param original_sequence_position Zero-based original FASTA position.
  /// @return Compressed active position, or std::nullopt when the base is excluded.
  /// @throws SeqProError with kInvalidArgument when dirty, kSequenceNotFound for an unselected ID,
  /// or kSequenceRangeOutOfBounds for an invalid original position.
  /// @par Complexity
  /// Logarithmic in active runs for the sequence.
  std::optional<ActiveSequencePosition> FindActiveSequencePosition(
      SequenceId sequence_id, SequencePosition original_sequence_position) const;
  /// Converts a valid compressed active position to its original FASTA position.
  ///
  /// @param sequence_id Selected sequence containing the active coordinate.
  /// @param active_sequence_position Zero-based coordinate after exclusions are removed.
  /// @return Corresponding original FASTA coordinate.
  /// @throws SeqProError with kInvalidArgument when dirty, kSequenceNotFound for an unselected ID,
  /// or kSequenceRangeOutOfBounds for an invalid active position.
  SequencePosition OriginalSequencePosition(SequenceId sequence_id,
                                            ActiveSequencePosition active_sequence_position) const;
  /// Returns null when the original base is excluded.
  ///
  /// @param sequence_id Selected sequence containing the original coordinate.
  /// @param original_sequence_position Zero-based original FASTA position.
  /// @return Sequence-text coordinate, or std::nullopt when the base is excluded.
  /// @throws SeqProError with kInvalidArgument when dirty, kSequenceNotFound for an unselected ID,
  /// or kSequenceRangeOutOfBounds for an invalid original position.
  std::optional<SequenceTextPosition> FindTextPosition(
      SequenceId sequence_id, SequencePosition original_sequence_position) const;
  /// Converts a valid active position to a sequence-text position.
  ///
  /// @param sequence_id Selected sequence containing the active coordinate.
  /// @param active_sequence_position Zero-based compressed active coordinate.
  /// @return Coordinate of the real base, never a separator or terminator.
  /// @throws SeqProError with kInvalidArgument when dirty, kSequenceNotFound for an unselected ID,
  /// or kSequenceRangeOutOfBounds for an invalid active position.
  SequenceTextPosition TextPositionFromActive(
      SequenceId sequence_id, ActiveSequencePosition active_sequence_position) const;

  /// Distinguishes real bases, separators, and the final terminator.
  ///
  /// @param text_position Zero-based coordinate including control bytes.
  /// @return Tagged variant preserving the distinction between base, separator, and terminator.
  /// @throws SeqProError with kInvalidArgument when dirty or kSequenceRangeOutOfBounds for an
  /// invalid text coordinate.
  /// @par Complexity
  /// Logarithmic in total active-run count.
  SequenceTextLocation LocateTextPosition(SequenceTextPosition text_position) const;

  /// Returns null unless the complete non-empty interval lies in one active run.
  ///
  /// @param text_start_position Inclusive sequence-text start.
  /// @param text_length Nonzero number of real sequence bytes.
  /// @return Located original interval, or std::nullopt for control-byte starts, cross-run ranges,
  /// or ranges extending beyond the text.
  /// @throws SeqProError with kInvalidArgument when dirty or text_length is zero, and
  /// kIntegerOverflow when interval arithmetic is unrepresentable.
  std::optional<LocatedSequenceInterval> LocateTextInterval(
      SequenceTextPosition text_start_position, SequenceTextLength text_length) const;

  /// Reads an active FASTA byte, separator, or terminator at one text position.
  ///
  /// @param text_position Zero-based coordinate including control bytes.
  /// @return Original active byte, kSeparatorByte, or kTerminatorByte.
  /// @throws SeqProError with kInvalidArgument when dirty, kSequenceRangeOutOfBounds when out of
  /// range, or kUnsupportedFileFormat if an active FASTA byte is reserved.
  std::uint8_t ReadTextByte(SequenceTextPosition text_position) const;
  /// Allocates exactly text_size() bytes; the resulting string contains a final NUL.
  ///
  /// @return Owning bytes and the generation that produced their coordinates.
  /// @throws SeqProError with kInvalidArgument when dirty, kIntegerOverflow when the string size is
  /// unrepresentable, or kUnsupportedFileFormat for a reserved active byte.
  /// @warning Use sequence_text_bytes.size(), never strlen(), because the terminator is embedded.
  MaterializedSequenceText Materialize() const;

  /// Copies into a non-null buffer whose size must equal text_size() exactly.
  ///
  /// @param destination_buffer Caller-owned writable storage.
  /// @param destination_size_bytes Exact buffer size; smaller and larger buffers are rejected.
  /// @throws SeqProError with kInvalidArgument when dirty or the buffer contract is violated, and
  /// kUnsupportedFileFormat for a reserved active FASTA byte.
  /// @note Concurrent calls require independent, non-overlapping destination buffers.
  void CopyTextTo(char* destination_buffer, std::size_t destination_size_bytes) const;

  /// Streams text with bounded working memory and checks output failures.
  ///
  /// @param output_stream Destination stream, retained and owned by the caller.
  /// @param transfer_buffer_size_bytes Nonzero bounded transfer allocation.
  /// @throws SeqProError with kInvalidArgument when dirty or the buffer size is zero, kIoError on
  /// output failure, or kUnsupportedFileFormat for a reserved active FASTA byte.
  /// @note Concurrent calls require separately synchronized output streams.
  void WriteTo(std::ostream& output_stream,
               std::size_t transfer_buffer_size_bytes = std::size_t{1024} * 1024) const;

 private:
  class State;
  std::unique_ptr<State> layout_state_;
};

}  // namespace seqpro

#endif  // SEQPRO_SEQUENCE_TEXT_INCLUDE_SEQPRO_SEQUENCE_TEXT_LAYOUT_H_
