add_library(seqpro_sequence_text
  src/sequence_text_layout.cc
)
add_library(SeqPro::sequence_text ALIAS seqpro_sequence_text)

target_link_libraries(seqpro_sequence_text PUBLIC SeqPro::seqpro)
target_compile_features(seqpro_sequence_text PUBLIC cxx_std_17)
target_compile_definitions(
  seqpro_sequence_text
  PRIVATE SEQPRO_BUILDING_SEQUENCE_TEXT_LIBRARY
)
target_include_directories(seqpro_sequence_text
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(seqpro_sequence_text PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS OFF
  CXX_VISIBILITY_PRESET hidden
  EXPORT_NAME sequence_text
  POSITION_INDEPENDENT_CODE YES
  SOVERSION ${PROJECT_VERSION_MAJOR}
  VERSION ${PROJECT_VERSION}
  VISIBILITY_INLINES_HIDDEN YES
)
target_compile_options(seqpro_sequence_text PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wsign-conversion
)

if(SEQPRO_ENABLE_CLANG_TIDY)
  set_target_properties(
    seqpro_sequence_text
    PROPERTIES CXX_CLANG_TIDY "${SEQPRO_CLANG_TIDY_EXECUTABLE}"
  )
endif()

if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  add_executable(
    seqpro-sequence-text-tests
    tests/sequence_text_layout_test.cc
  )
  target_link_libraries(
    seqpro-sequence-text-tests
    PRIVATE SeqPro::sequence_text Threads::Threads
  )
  target_compile_features(seqpro-sequence-text-tests PRIVATE cxx_std_17)
  set_target_properties(seqpro-sequence-text-tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
  )
  target_compile_options(
    seqpro-sequence-text-tests
    PRIVATE -Wall -Wextra -Wpedantic
  )
  add_test(NAME seqpro.sequence_text.unit COMMAND seqpro-sequence-text-tests)

  if(SEQPRO_IS_TOP_LEVEL)
    add_test(
      NAME seqpro.sequence_text.consumer.add_subdirectory
      COMMAND ${CMAKE_COMMAND}
        -DSEQPRO_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DSEQPRO_SEQUENCE_TEXT_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DSEQPRO_TEST_ROOT=${PROJECT_BINARY_DIR}/sequence-text-consumer-add-subdirectory
        -DSEQPRO_GENERATOR=${CMAKE_GENERATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_add_subdirectory_consumer.cmake
    )
    add_test(
      NAME seqpro.sequence_text.consumer.install
      COMMAND ${CMAKE_COMMAND}
        -DSEQPRO_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DSEQPRO_BINARY_DIR=${PROJECT_BINARY_DIR}
        -DSEQPRO_SEQUENCE_TEXT_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DSEQPRO_TEST_ROOT=${PROJECT_BINARY_DIR}/sequence-text-consumer-install
        -DSEQPRO_GENERATOR=${CMAKE_GENERATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_install_consumer.cmake
    )
  endif()
endif()

if(SEQPRO_BUILD_EXAMPLES)
  add_executable(
    seqpro-sequence-text-example
    examples/sequence_text_usage.cc
  )
  target_link_libraries(
    seqpro-sequence-text-example
    PRIVATE SeqPro::sequence_text
  )
  target_compile_features(seqpro-sequence-text-example PRIVATE cxx_std_17)
  set_target_properties(seqpro-sequence-text-example PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
  )
  target_compile_options(
    seqpro-sequence-text-example
    PRIVATE -Wall -Wextra -Wpedantic
  )
endif()

if(SEQPRO_BUILD_BENCHMARKS)
  add_executable(
    seqpro-sequence-text-benchmark
    benchmarks/sequence_text_benchmark.cc
  )
  target_link_libraries(
    seqpro-sequence-text-benchmark
    PRIVATE SeqPro::sequence_text
  )
  target_compile_features(seqpro-sequence-text-benchmark PRIVATE cxx_std_17)
  set_target_properties(seqpro-sequence-text-benchmark PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
  )
  target_compile_options(
    seqpro-sequence-text-benchmark
    PRIVATE -Wall -Wextra -Wpedantic
  )
endif()

install(TARGETS seqpro_sequence_text
  EXPORT SeqProSequenceTextTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT SeqProSequenceTextTargets
  FILE SeqProSequenceTextTargets.cmake
  NAMESPACE SeqPro::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SeqPro
)
install(
  FILES ${PROJECT_SOURCE_DIR}/docs/sequence_text_layout.md
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/SeqPro/docs
)
