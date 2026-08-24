if(NOT DEFINED SEQPRO_SOURCE_DIR OR NOT DEFINED SEQPRO_BINARY_DIR OR
   NOT DEFINED SEQPRO_TEST_ROOT OR NOT DEFINED SEQPRO_SEQUENCE_TEXT_ENABLED)
  message(FATAL_ERROR "SeqPro relocated-install consumer inputs are required")
endif()

file(REMOVE_RECURSE "${SEQPRO_TEST_ROOT}")
file(MAKE_DIRECTORY "${SEQPRO_TEST_ROOT}/source")

if(SEQPRO_SEQUENCE_TEXT_ENABLED)
  configure_file(
    "${SEQPRO_SOURCE_DIR}/extensions/sequence_text/tests/consumer/find_package.CMakeLists.txt"
    "${SEQPRO_TEST_ROOT}/source/CMakeLists.txt"
    COPYONLY
  )
else()
  configure_file(
    "${SEQPRO_SOURCE_DIR}/tests/consumer/find_package.CMakeLists.txt"
    "${SEQPRO_TEST_ROOT}/source/CMakeLists.txt"
    COPYONLY
  )
endif()

set(initial_install_prefix "${SEQPRO_TEST_ROOT}/initial prefix")
set(relocated_install_prefix "${SEQPRO_TEST_ROOT}/relocated prefix")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${SEQPRO_BINARY_DIR}"
          --prefix "${initial_install_prefix}"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR
    "SeqPro relocation-test install failed:\n${install_output}\n${install_error}"
  )
endif()

file(RENAME "${initial_install_prefix}" "${relocated_install_prefix}")

file(GLOB_RECURSE installed_cmake_files LIST_DIRECTORIES FALSE
  "${relocated_install_prefix}/*.cmake")
foreach(installed_cmake_file IN LISTS installed_cmake_files)
  file(READ "${installed_cmake_file}" installed_cmake_contents)
  string(FIND "${installed_cmake_contents}" "${SEQPRO_SOURCE_DIR}" source_path_offset)
  if(NOT source_path_offset EQUAL -1)
    message(FATAL_ERROR
      "Installed CMake package contains a source-tree path: ${installed_cmake_file}"
    )
  endif()
endforeach()

set(consumer_configure_command
  "${CMAKE_COMMAND}"
  -S "${SEQPRO_TEST_ROOT}/source"
  -B "${SEQPRO_TEST_ROOT}/build"
  -G "${SEQPRO_GENERATOR}"
  -DSEQPRO_SOURCE_DIR=${SEQPRO_SOURCE_DIR}
  -DCMAKE_PREFIX_PATH=${relocated_install_prefix}
)
if(SEQPRO_SEQUENCE_TEXT_ENABLED)
  list(APPEND consumer_configure_command
    -DSEQPRO_SEQUENCE_TEXT_DIR=${SEQPRO_SOURCE_DIR}/extensions/sequence_text)
endif()

execute_process(
  COMMAND ${consumer_configure_command}
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
    "relocated find_package consumer configure failed:\n"
    "${configure_output}\n${configure_error}"
  )
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${SEQPRO_TEST_ROOT}/build" --parallel
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR
    "relocated find_package consumer build failed:\n"
    "${build_output}\n${build_error}"
  )
endif()

if(SEQPRO_SEQUENCE_TEXT_ENABLED)
  set(relocated_consumer_executables
    seqpro-sequence-text-consumer
    seqpro-sequence-text-consumer-cxx20)
else()
  set(relocated_consumer_executables
    seqpro-consumer
    seqpro-consumer-cxx20)
endif()

foreach(consumer_executable IN LISTS relocated_consumer_executables)
  execute_process(
    COMMAND "${SEQPRO_TEST_ROOT}/build/${consumer_executable}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
  if(NOT run_result EQUAL 0)
    message(FATAL_ERROR
      "relocated consumer '${consumer_executable}' failed:\n"
      "${run_output}\n${run_error}")
  endif()
endforeach()
