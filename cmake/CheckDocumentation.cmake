if(NOT DEFINED SEQPRO_SOURCE_DIR)
  message(FATAL_ERROR "SEQPRO_SOURCE_DIR is required")
endif()

set(documentation_pairs
  "README.md|README.zh-CN.md"
  "docs/getting_started.md|docs/zh-CN/getting_started.md"
  "docs/core_api_guide.md|docs/zh-CN/core_api_guide.md"
  "docs/sequence_text_api_guide.md|docs/zh-CN/sequence_text_api_guide.md"
  "docs/fai_contract.md|docs/zh-CN/fai_contract.md"
  "docs/sequence_text_layout.md|docs/zh-CN/sequence_text_layout.md"
)

set(user_documentation_files)
foreach(documentation_pair IN LISTS documentation_pairs)
  string(REPLACE "|" ";" paired_files "${documentation_pair}")
  foreach(relative_documentation_path IN LISTS paired_files)
    set(documentation_path
      "${SEQPRO_SOURCE_DIR}/${relative_documentation_path}")
    if(NOT EXISTS "${documentation_path}")
      message(FATAL_ERROR
        "Required user documentation is missing: ${documentation_path}")
    endif()
    list(APPEND user_documentation_files "${documentation_path}")
  endforeach()
endforeach()
list(REMOVE_DUPLICATES user_documentation_files)

foreach(documentation_path IN LISTS user_documentation_files)
  file(STRINGS "${documentation_path}" documentation_lines)
  get_filename_component(documentation_directory "${documentation_path}" DIRECTORY)
  set(in_fenced_code_block FALSE)
  foreach(documentation_line IN LISTS documentation_lines)
    if(documentation_line MATCHES "^[ 	]*```")
      if(in_fenced_code_block)
        set(in_fenced_code_block FALSE)
      else()
        set(in_fenced_code_block TRUE)
      endif()
      continue()
    endif()
    if(in_fenced_code_block)
      continue()
    endif()

    set(remaining_documentation_line "${documentation_line}")
    while(remaining_documentation_line MATCHES
          "\\[[^]]*\\]\\(([^)]+)\\)")
      set(markdown_link "${CMAKE_MATCH_0}")
      set(link_target "${CMAKE_MATCH_1}")
      string(FIND "${remaining_documentation_line}" "${markdown_link}"
        markdown_link_position)
      string(LENGTH "${markdown_link}" markdown_link_length)
      math(EXPR remaining_contents_position
        "${markdown_link_position} + ${markdown_link_length}")
      string(SUBSTRING "${remaining_documentation_line}"
        ${remaining_contents_position} -1 remaining_documentation_line)
      string(REGEX REPLACE "^<|>$" "" link_target "${link_target}")
      string(REGEX REPLACE "#.*$" "" link_path "${link_target}")
      if(link_path STREQUAL "" OR
         link_target MATCHES "[ 	]" OR
         link_path MATCHES "^[A-Za-z][A-Za-z0-9+.-]*:" OR
         link_path MATCHES "^/")
        continue()
      endif()
      get_filename_component(resolved_link_path
        "${documentation_directory}/${link_path}" ABSOLUTE)
      if(NOT EXISTS "${resolved_link_path}")
        message(FATAL_ERROR
          "Broken relative link '${link_target}' in ${documentation_path}")
      endif()
    endwhile()

    set(remaining_html_line "${documentation_line}")
    while(remaining_html_line MATCHES "href=\"([^\"]+)\"")
      set(html_link "${CMAKE_MATCH_0}")
      set(link_target "${CMAKE_MATCH_1}")
      string(FIND "${remaining_html_line}" "${html_link}" html_link_position)
      string(LENGTH "${html_link}" html_link_length)
      math(EXPR remaining_html_position
        "${html_link_position} + ${html_link_length}")
      string(SUBSTRING "${remaining_html_line}"
        ${remaining_html_position} -1 remaining_html_line)
      string(REGEX REPLACE "#.*$" "" link_path "${link_target}")
      if(link_path STREQUAL "" OR
         link_path MATCHES "^[A-Za-z][A-Za-z0-9+.-]*:" OR
         link_path MATCHES "^/")
        continue()
      endif()
      get_filename_component(resolved_link_path
        "${documentation_directory}/${link_path}" ABSOLUTE)
      if(NOT EXISTS "${resolved_link_path}")
        message(FATAL_ERROR
          "Broken relative HTML link '${link_target}' in ${documentation_path}")
      endif()
    endwhile()
  endforeach()
endforeach()

set(forbidden_legacy_documentation_symbols
  SequenceManager
  MaskedSequenceManager
  MemoryMapper
  SequenceInfo
  SeqProException
  getSubSequence
  getSequenceInfo
  getSequenceId
  buildIndex
  batchQuery
)
foreach(documentation_path IN LISTS user_documentation_files)
  file(READ "${documentation_path}" documentation_contents)
  foreach(forbidden_symbol IN LISTS forbidden_legacy_documentation_symbols)
    if(documentation_contents MATCHES
       "(^|[^A-Za-z0-9_])${forbidden_symbol}([^A-Za-z0-9_]|$)")
      message(FATAL_ERROR
        "Forbidden legacy API '${forbidden_symbol}' appears in ${documentation_path}")
    endif()
  endforeach()
endforeach()

set(core_documentation_files
  "${SEQPRO_SOURCE_DIR}/docs/core_api_guide.md"
  "${SEQPRO_SOURCE_DIR}/docs/zh-CN/core_api_guide.md"
)
set(core_public_symbols
  SequencePosition SequenceLength SequenceId FastaIndexEntry
  sequence_id sequence_name sequence_length first_base_offset_bytes
  bases_per_line bytes_per_line
  FileAccessPattern kOperatingSystemDefault kRandom kSequential
  IndexVerificationMode kFast kFull
  FastaIndexOrigin kSeqProVerified kExternalStandardFai
  IndexVerificationStatus kStructureValidated kMetadataValidated
  kFullContentValidated
  FastaIndexBuildOptions fasta_index_path force_rebuild
  write_seqpro_metadata
  FastaIndexBuildAction kCreated kReused kAdoptedExternalIndex kRebuilt
  FastaIndexBuildReport build_action fasta_path metadata_path
  sequence_count total_base_count
  FastaIndexValidationReport index_origin verification_status
  has_seqpro_metadata is_fasta_fingerprint_current
  BuildFastaIndex ValidateFastaIndex
  IndexedFastaOptions file_access_pattern index_verification_mode
  require_seqpro_metadata
  SequenceChunk sequence_start_position sequence_bases
  SequenceChunkRange Iterator begin end empty estimated_chunk_count
  operator* operator++ operator== operator!=
  FastaSequenceView ReadBase ReadSubsequence CopySubsequenceTo
  WriteSubsequenceTo SubsequenceChunks fasta_index_entry
  IndexedFasta Open OpenOrBuildIndex fasta_path fasta_index_path
  fasta_index_origin index_verification_status fasta_index_entries
  FindSequenceId IndexEntryById IndexEntryByName SequenceById SequenceByName
  SeqProError ErrorCode error_code
  kInvalidArgument kIoError kInvalidFasta kInvalidFastaIndex
  kStaleFastaIndex kDuplicateSequenceName kSequenceNotFound
  kSequenceRangeOutOfBounds kIntegerOverflow kUnsupportedFileFormat
)

set(sequence_text_documentation_files
  "${SEQPRO_SOURCE_DIR}/docs/sequence_text_api_guide.md"
  "${SEQPRO_SOURCE_DIR}/docs/zh-CN/sequence_text_api_guide.md"
)
set(sequence_text_public_symbols
  SequenceTextPosition SequenceTextLength ActiveSequencePosition
  SequenceTextGeneration SequenceRunIndex
  OriginalSequenceInterval ExcludedSequenceInterval SequenceTextInterval
  sequence_start_position sequence_end_position text_start_position text_length
  SequenceTextBaseLocation sequence_id sequence_run_index
  original_sequence_position active_sequence_position
  SequenceTextSeparatorLocation preceding_sequence_id preceding_run_index
  SequenceTextTerminatorLocation SequenceTextLocation
  LocatedSequenceInterval original_sequence_start_position
  active_sequence_start_position interval_length
  MaterializedSequenceText sequence_text_bytes layout_generation
  SequenceTextLayout kSeparatorByte kTerminatorByte indexed_fasta
  sequence_order is_finalized ExcludeInterval ExcludeIntervals
  ExcludeTextIntervals ClearExcludedIntervals ClearAllExcludedIntervals
  Finalize text_size active_base_count active_run_count
  ActiveSequenceLength ExcludedBaseCount ActiveIntervalsById
  ExcludedIntervalsById FindActiveSequencePosition OriginalSequencePosition
  FindTextPosition TextPositionFromActive LocateTextPosition
  LocateTextInterval ReadTextByte Materialize CopyTextTo WriteTo
)

foreach(documentation_path IN LISTS core_documentation_files)
  file(READ "${documentation_path}" documentation_contents)
  foreach(public_symbol IN LISTS core_public_symbols)
    string(FIND "${documentation_contents}" "${public_symbol}" symbol_position)
    if(symbol_position EQUAL -1)
      message(FATAL_ERROR
        "Core public symbol '${public_symbol}' is not documented in ${documentation_path}")
    endif()
  endforeach()
endforeach()

foreach(documentation_path IN LISTS sequence_text_documentation_files)
  file(READ "${documentation_path}" documentation_contents)
  foreach(public_symbol IN LISTS sequence_text_public_symbols)
    string(FIND "${documentation_contents}" "${public_symbol}" symbol_position)
    if(symbol_position EQUAL -1)
      message(FATAL_ERROR
        "SequenceText public symbol '${public_symbol}' is not documented in ${documentation_path}")
    endif()
  endforeach()
endforeach()
