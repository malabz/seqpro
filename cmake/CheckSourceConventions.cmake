if(NOT DEFINED SEQPRO_SOURCE_DIR)
  message(FATAL_ERROR "SEQPRO_SOURCE_DIR is required")
endif()

set(seqpro_owned_source_patterns
  "${SEQPRO_SOURCE_DIR}/include/*.h"
  "${SEQPRO_SOURCE_DIR}/src/*.h"
  "${SEQPRO_SOURCE_DIR}/src/*.cc"
  "${SEQPRO_SOURCE_DIR}/tools/*.cc"
  "${SEQPRO_SOURCE_DIR}/examples/*.cc"
  "${SEQPRO_SOURCE_DIR}/benchmarks/*.cc"
  "${SEQPRO_SOURCE_DIR}/tests/*.cc"
  "${SEQPRO_SOURCE_DIR}/tests/consumer/*.cc"
  "${SEQPRO_SOURCE_DIR}/extensions/sequence_text/include/*.h"
  "${SEQPRO_SOURCE_DIR}/extensions/sequence_text/src/*.cc"
  "${SEQPRO_SOURCE_DIR}/extensions/sequence_text/examples/*.cc"
  "${SEQPRO_SOURCE_DIR}/extensions/sequence_text/benchmarks/*.cc"
  "${SEQPRO_SOURCE_DIR}/extensions/sequence_text/tests/*.cc"
  "${SEQPRO_SOURCE_DIR}/extensions/sequence_text/tests/consumer/*.cc"
  "${SEQPRO_SOURCE_DIR}/fuzz/*.cc"
)
file(GLOB_RECURSE seqpro_owned_source_files LIST_DIRECTORIES FALSE
  ${seqpro_owned_source_patterns})

set(forbidden_legacy_symbols
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

foreach(source_file IN LISTS seqpro_owned_source_files)
  file(READ "${source_file}" source_contents)
  foreach(forbidden_symbol IN LISTS forbidden_legacy_symbols)
    if(source_contents MATCHES "(^|[^A-Za-z0-9_])${forbidden_symbol}([^A-Za-z0-9_]|$)")
      message(FATAL_ERROR
        "Forbidden legacy symbol '${forbidden_symbol}' appears in ${source_file}")
    endif()
  endforeach()

  file(STRINGS "${source_file}" source_lines)
  foreach(source_line IN LISTS source_lines)
    if(source_line MATCHES
       "(^|[;{}(, *&])(seq|pos|len|idx|mgr|ptr|buf|tmp|val|res|obj|ctx|ret) *(=|;|,|\\)|\\{|\\[)")
      message(FATAL_ERROR
        "Ambiguous abbreviated identifier appears in ${source_file}: ${source_line}")
    endif()
  endforeach()
endforeach()
