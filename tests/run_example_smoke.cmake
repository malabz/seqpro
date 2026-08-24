if(NOT DEFINED SEQPRO_EXAMPLE_EXECUTABLE OR
   NOT DEFINED SEQPRO_EXAMPLE_MODE OR
   NOT DEFINED SEQPRO_FIXTURE_FASTA OR
   NOT DEFINED SEQPRO_FIXTURE_FAI OR
   NOT DEFINED SEQPRO_TEST_ROOT)
  message(FATAL_ERROR "The example smoke-test inputs are required")
endif()

file(REMOVE_RECURSE "${SEQPRO_TEST_ROOT}")
file(MAKE_DIRECTORY "${SEQPRO_TEST_ROOT}")
set(example_fasta_path "${SEQPRO_TEST_ROOT}/reference.fa")
configure_file("${SEQPRO_FIXTURE_FASTA}" "${example_fasta_path}" COPYONLY)
configure_file(
  "${SEQPRO_FIXTURE_FAI}"
  "${example_fasta_path}.fai"
  COPYONLY
)

if(SEQPRO_EXAMPLE_MODE STREQUAL "basic")
  set(example_arguments "${example_fasta_path}" chr1)
  set(expected_output "region_2_6\tGTAC")
elseif(SEQPRO_EXAMPLE_MODE STREQUAL "index")
  set(example_arguments
    "${example_fasta_path}"
    "${SEQPRO_TEST_ROOT}/custom.fai"
  )
  set(expected_output "full_content\tyes")
elseif(SEQPRO_EXAMPLE_MODE STREQUAL "read")
  set(example_arguments "${example_fasta_path}" chr1)
  set(expected_output "region\tGTACGT")
elseif(SEQPRO_EXAMPLE_MODE STREQUAL "concurrent")
  set(example_arguments "${example_fasta_path}" chr1)
  set(expected_output "threads\t4")
elseif(SEQPRO_EXAMPLE_MODE STREQUAL "sequence_text_workflow")
  set(example_arguments "${example_fasta_path}")
  set(expected_output "second_generation\t2")
elseif(SEQPRO_EXAMPLE_MODE STREQUAL "sequence_text_coordinates")
  set(example_arguments "${example_fasta_path}" chr1)
  set(expected_output "original_position\t4")
else()
  message(FATAL_ERROR "Unknown example mode: ${SEQPRO_EXAMPLE_MODE}")
endif()

execute_process(
  COMMAND "${SEQPRO_EXAMPLE_EXECUTABLE}" ${example_arguments}
  RESULT_VARIABLE example_exit_code
  OUTPUT_VARIABLE example_output
  ERROR_VARIABLE example_error
)
if(NOT example_exit_code EQUAL 0)
  message(FATAL_ERROR
    "Example '${SEQPRO_EXAMPLE_MODE}' failed with ${example_exit_code}:\n"
    "${example_error}"
  )
endif()

string(FIND "${example_output}" "${expected_output}" expected_output_position)
if(expected_output_position EQUAL -1)
  message(FATAL_ERROR
    "Example '${SEQPRO_EXAMPLE_MODE}' did not print '${expected_output}':\n"
    "${example_output}"
  )
endif()
