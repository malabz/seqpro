if(NOT DEFINED SEQPRO_SOURCE_DIR OR NOT DEFINED SEQPRO_BINARY_DIR OR
   NOT DEFINED SEQPRO_TEST_ROOT)
  message(FATAL_ERROR "SeqPro install consumer test paths are required")
endif()

file(REMOVE_RECURSE "${SEQPRO_TEST_ROOT}")
file(MAKE_DIRECTORY "${SEQPRO_TEST_ROOT}/source")
configure_file(
  "${SEQPRO_SOURCE_DIR}/tests/consumer/find_package.CMakeLists.txt"
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

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${SEQPRO_TEST_ROOT}/source"
          -B "${SEQPRO_TEST_ROOT}/build"
          -G "${SEQPRO_GENERATOR}"
          -DSEQPRO_SOURCE_DIR=${SEQPRO_SOURCE_DIR}
          -DCMAKE_PREFIX_PATH=${SEQPRO_TEST_ROOT}/prefix
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
    "find_package consumer configure failed:\n${configure_output}\n${configure_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${SEQPRO_TEST_ROOT}/build" --parallel
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "find_package consumer build failed:\n${build_output}\n${build_error}")
endif()

foreach(consumer_executable IN ITEMS seqpro-consumer seqpro-consumer-cxx20)
  execute_process(
    COMMAND "${SEQPRO_TEST_ROOT}/build/${consumer_executable}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
  if(NOT run_result EQUAL 0)
    message(FATAL_ERROR
      "find_package consumer '${consumer_executable}' failed:\n"
      "${run_output}\n${run_error}")
  endif()
endforeach()
