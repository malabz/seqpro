if(NOT DEFINED SEQPRO_SOURCE_DIR OR NOT DEFINED SEQPRO_BINARY_DIR OR
   NOT DEFINED SEQPRO_TEST_ROOT)
  message(FATAL_ERROR "SeqPro version compatibility test paths are required")
endif()

file(REMOVE_RECURSE "${SEQPRO_TEST_ROOT}")
file(MAKE_DIRECTORY "${SEQPRO_TEST_ROOT}/source")
configure_file(
  "${SEQPRO_SOURCE_DIR}/tests/consumer/version_compatibility.CMakeLists.txt"
  "${SEQPRO_TEST_ROOT}/source/CMakeLists.txt" COPYONLY)

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${SEQPRO_BINARY_DIR}"
          --prefix "${SEQPRO_TEST_ROOT}/prefix"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "SeqPro install failed:\n${install_output}\n${install_error}")
endif()

function(configure_version_case case_name find_arguments should_succeed)
  set(case_build_dir "${SEQPRO_TEST_ROOT}/${case_name}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SEQPRO_TEST_ROOT}/source" -B "${case_build_dir}"
            -G "${SEQPRO_GENERATOR}"
            "-DSEQPRO_FIND_ARGUMENTS=${find_arguments}"
            "-DCMAKE_PREFIX_PATH=${SEQPRO_TEST_ROOT}/prefix"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)

  if(should_succeed AND NOT configure_result EQUAL 0)
    message(FATAL_ERROR
      "Compatible version case '${case_name}' failed:\n"
      "${configure_output}\n${configure_error}")
  endif()
  if(NOT should_succeed AND configure_result EQUAL 0)
    message(FATAL_ERROR
      "Incompatible version case '${case_name}' unexpectedly configured")
  endif()
endfunction()

configure_version_case(same_minor "0.2" TRUE)
configure_version_case(exact_version "0.2.0;EXACT" TRUE)
configure_version_case(same_minor_range "0.2...<0.3" TRUE)
configure_version_case(previous_minor "0.1" FALSE)
configure_version_case(next_minor "0.3" FALSE)
