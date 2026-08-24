if(NOT DEFINED SEQPRO_ABIDIFF_EXECUTABLE OR NOT DEFINED SEQPRO_ABI_BASELINE OR
   NOT DEFINED SEQPRO_CURRENT_LIBRARY)
  message(FATAL_ERROR "SeqPro ABI compatibility inputs are required")
endif()
if(NOT EXISTS "${SEQPRO_ABI_BASELINE}")
  message(FATAL_ERROR "SeqPro ABI baseline is missing: ${SEQPRO_ABI_BASELINE}")
endif()

execute_process(
  COMMAND "${SEQPRO_ABIDIFF_EXECUTABLE}"
          --fail-no-debug-info
          --no-added-syms
          "${SEQPRO_ABI_BASELINE}"
          "${SEQPRO_CURRENT_LIBRARY}"
  RESULT_VARIABLE abi_diff_result
  OUTPUT_VARIABLE abi_diff_output
  ERROR_VARIABLE abi_diff_error)
if(NOT abi_diff_result EQUAL 0)
  message(FATAL_ERROR
    "ABI compatibility check failed for '${SEQPRO_CURRENT_LIBRARY}':\n"
    "${abi_diff_output}\n${abi_diff_error}")
endif()
