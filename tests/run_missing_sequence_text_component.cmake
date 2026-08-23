if(NOT DEFINED SEQPRO_SOURCE_DIR OR NOT DEFINED SEQPRO_BINARY_DIR OR
   NOT DEFINED SEQPRO_TEST_ROOT)
  message(FATAL_ERROR "SeqPro missing-component test paths are required")
endif()

file(REMOVE_RECURSE "${SEQPRO_TEST_ROOT}")
file(MAKE_DIRECTORY "${SEQPRO_TEST_ROOT}/source")
configure_file(
  "${SEQPRO_SOURCE_DIR}/tests/consumer/missing_sequence_text.CMakeLists.txt"
  "${SEQPRO_TEST_ROOT}/source/CMakeLists.txt" COPYONLY)
file(MAKE_DIRECTORY "${SEQPRO_TEST_ROOT}/optional-source")
configure_file(
  "${SEQPRO_SOURCE_DIR}/tests/consumer/optional_sequence_text.CMakeLists.txt"
  "${SEQPRO_TEST_ROOT}/optional-source/CMakeLists.txt" COPYONLY)

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${SEQPRO_BINARY_DIR}"
          --prefix "${SEQPRO_TEST_ROOT}/prefix"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "SeqPro core-only install failed:\n${install_output}\n${install_error}")
endif()

if(EXISTS "${SEQPRO_TEST_ROOT}/prefix/include/seqpro/sequence_text_layout.h" OR
   EXISTS "${SEQPRO_TEST_ROOT}/prefix/include/seqpro/sequence_text_export.h")
  message(FATAL_ERROR "Core-only installation contains SequenceText public headers")
endif()

file(GLOB_RECURSE sequence_text_artifacts
  "${SEQPRO_TEST_ROOT}/prefix/*SeqProSequenceText*"
  "${SEQPRO_TEST_ROOT}/prefix/*seqpro_sequence_text*")
if(sequence_text_artifacts)
  message(FATAL_ERROR
    "Core-only installation contains SequenceText artifacts: ${sequence_text_artifacts}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${SEQPRO_TEST_ROOT}/optional-source"
          -B "${SEQPRO_TEST_ROOT}/optional-build"
          -G "${SEQPRO_GENERATOR}"
          -DCMAKE_PREFIX_PATH=${SEQPRO_TEST_ROOT}/prefix
  RESULT_VARIABLE optional_configure_result
  OUTPUT_VARIABLE optional_configure_output
  ERROR_VARIABLE optional_configure_error)
if(NOT optional_configure_result EQUAL 0)
  message(FATAL_ERROR
    "An optional missing SequenceText component rejected the core package:\n"
    "${optional_configure_output}\n${optional_configure_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${SEQPRO_TEST_ROOT}/source"
          -B "${SEQPRO_TEST_ROOT}/build"
          -G "${SEQPRO_GENERATOR}"
          -DCMAKE_PREFIX_PATH=${SEQPRO_TEST_ROOT}/prefix
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error)
if(configure_result EQUAL 0)
  message(FATAL_ERROR
    "A core-only installation unexpectedly satisfied the SequenceText component")
endif()

set(configure_diagnostics "${configure_output}\n${configure_error}")
if(NOT configure_diagnostics MATCHES "SequenceText")
  message(FATAL_ERROR
    "Missing-component failure did not identify SequenceText:\n${configure_diagnostics}")
endif()
