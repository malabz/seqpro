if(NOT DEFINED SEQPRO_CORE_LIBRARY OR NOT DEFINED SEQPRO_NM_EXECUTABLE OR
   NOT DEFINED SEQPRO_CXXFILT_EXECUTABLE OR NOT DEFINED SEQPRO_READELF_EXECUTABLE)
  message(FATAL_ERROR "SeqPro shared-library audit inputs are required")
endif()

function(read_dynamic_symbols library_path output_variable)
  execute_process(
    COMMAND "${SEQPRO_NM_EXECUTABLE}" -D --defined-only "${library_path}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE nm_output
    ERROR_VARIABLE nm_error)
  if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "nm failed for '${library_path}':\n${nm_error}")
  endif()
  set(${output_variable} "${nm_output}" PARENT_SCOPE)
endfunction()

function(demangle_symbols symbol_text output_variable)
  set(symbol_file "${CMAKE_CURRENT_BINARY_DIR}/seqpro-symbols-${output_variable}.txt")
  file(WRITE "${symbol_file}" "${symbol_text}")
  execute_process(
    COMMAND "${SEQPRO_CXXFILT_EXECUTABLE}"
    INPUT_FILE "${symbol_file}"
    RESULT_VARIABLE demangle_result
    OUTPUT_VARIABLE demangled_output
    ERROR_VARIABLE demangle_error)
  file(REMOVE "${symbol_file}")
  if(NOT demangle_result EQUAL 0)
    message(FATAL_ERROR "c++filt failed:\n${demangle_error}")
  endif()
  set(${output_variable} "${demangled_output}" PARENT_SCOPE)
endfunction()

function(read_elf_dynamic library_path output_variable)
  execute_process(
    COMMAND "${SEQPRO_READELF_EXECUTABLE}" -d "${library_path}"
    RESULT_VARIABLE readelf_result
    OUTPUT_VARIABLE readelf_output
    ERROR_VARIABLE readelf_error)
  if(NOT readelf_result EQUAL 0)
    message(FATAL_ERROR "readelf failed for '${library_path}':\n${readelf_error}")
  endif()
  set(${output_variable} "${readelf_output}" PARENT_SCOPE)
endfunction()

read_dynamic_symbols("${SEQPRO_CORE_LIBRARY}" core_symbols)
demangle_symbols("${core_symbols}" core_demangled_symbols)
read_elf_dynamic("${SEQPRO_CORE_LIBRARY}" core_dynamic_section)

if(NOT core_dynamic_section MATCHES "SONAME.*libseqpro\\.so\\.0\\.2")
  message(FATAL_ERROR "Core SONAME is not libseqpro.so.0.2:\n${core_dynamic_section}")
endif()
if(NOT core_dynamic_section MATCHES "BIND_NOW")
  message(FATAL_ERROR "Core shared library is missing immediate binding hardening")
endif()
if(core_demangled_symbols MATCHES "(^|\n)[0-9a-fA-F]+ [A-Za-z] std::")
  message(FATAL_ERROR "Core shared library exports an STL implementation symbol")
endif()
if(core_demangled_symbols MATCHES "SequenceTextLayout")
  message(FATAL_ERROR "Core shared library exports SequenceText symbols")
endif()
if(NOT core_demangled_symbols MATCHES "seqpro::SeqProError::~SeqProError")
  message(FATAL_ERROR "Core shared library does not own the SeqProError key function")
endif()

if(DEFINED SEQPRO_SEQUENCE_TEXT_LIBRARY AND
   NOT SEQPRO_SEQUENCE_TEXT_LIBRARY STREQUAL "")
  read_dynamic_symbols("${SEQPRO_SEQUENCE_TEXT_LIBRARY}" sequence_text_symbols)
  demangle_symbols("${sequence_text_symbols}" sequence_text_demangled_symbols)
  read_elf_dynamic("${SEQPRO_SEQUENCE_TEXT_LIBRARY}" sequence_text_dynamic_section)

  if(NOT sequence_text_dynamic_section MATCHES
     "SONAME.*libseqpro_sequence_text\\.so\\.0\\.2")
    message(FATAL_ERROR
      "SequenceText SONAME is not libseqpro_sequence_text.so.0.2:\n"
      "${sequence_text_dynamic_section}")
  endif()
  if(NOT sequence_text_dynamic_section MATCHES "NEEDED.*libseqpro\\.so\\.0\\.2")
    message(FATAL_ERROR "SequenceText does not depend on the expected core SONAME")
  endif()
  if(NOT sequence_text_dynamic_section MATCHES "BIND_NOW")
    message(FATAL_ERROR "SequenceText shared library is missing immediate binding hardening")
  endif()
  if(sequence_text_demangled_symbols MATCHES "(^|\n)[0-9a-fA-F]+ [A-Za-z] std::")
    message(FATAL_ERROR "SequenceText shared library exports an STL implementation symbol")
  endif()
  if(sequence_text_demangled_symbols MATCHES "seqpro::SeqProError")
    message(FATAL_ERROR "SequenceText re-exports SeqProError RTTI or implementation symbols")
  endif()
  if(NOT sequence_text_demangled_symbols MATCHES "seqpro::SequenceTextLayout")
    message(FATAL_ERROR "SequenceText shared library exports no public layout symbols")
  endif()
endif()
