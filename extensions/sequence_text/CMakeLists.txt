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
  INSTALL_RPATH "$ORIGIN"
  POSITION_INDEPENDENT_CODE YES
  SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
  VERSION ${PROJECT_VERSION}
  VISIBILITY_INLINES_HIDDEN YES
)
seqpro_configure_owned_cxx_target(seqpro_sequence_text)

if(BUILD_SHARED_LIBS)
  target_link_options(seqpro_sequence_text PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/seqpro_sequence_text.map"
    "LINKER:--no-undefined"
    "LINKER:-z,relro"
    "LINKER:-z,now"
  )
  set_property(
    TARGET seqpro_sequence_text
    APPEND PROPERTY LINK_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/seqpro_sequence_text.map"
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
  seqpro_configure_owned_cxx_target(seqpro-sequence-text-tests)
  add_test(NAME seqpro.sequence_text.unit COMMAND seqpro-sequence-text-tests)

  if(BUILD_SHARED_LIBS)
    add_executable(
      seqpro-sequence-text-cross-dso-error-test
      tests/cross_dso_error_test.cc
    )
    target_link_libraries(
      seqpro-sequence-text-cross-dso-error-test
      PRIVATE SeqPro::sequence_text
    )
    seqpro_configure_owned_cxx_target(
      seqpro-sequence-text-cross-dso-error-test
    )
    add_test(
      NAME seqpro.sequence_text.cross_dso_error
      COMMAND seqpro-sequence-text-cross-dso-error-test
        ${PROJECT_SOURCE_DIR}/tests/fixtures/interop.fa
        ${PROJECT_SOURCE_DIR}/tests/fixtures/interop.fa.fai.expected
    )
  endif()

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
  seqpro_configure_owned_cxx_target(seqpro-sequence-text-example)

  add_executable(
    seqpro-sequence-text-coordinates-example
    examples/sequence_text_coordinates.cc
  )
  target_link_libraries(
    seqpro-sequence-text-coordinates-example
    PRIVATE SeqPro::sequence_text
  )
  seqpro_configure_owned_cxx_target(
    seqpro-sequence-text-coordinates-example
  )

  if(BUILD_TESTING)
    add_test(
      NAME seqpro.sequence_text.example.workflow
      COMMAND ${CMAKE_COMMAND}
        -DSEQPRO_EXAMPLE_EXECUTABLE=$<TARGET_FILE:seqpro-sequence-text-example>
        -DSEQPRO_EXAMPLE_MODE=sequence_text_workflow
        -DSEQPRO_FIXTURE_FASTA=${PROJECT_SOURCE_DIR}/tests/fixtures/interop.fa
        -DSEQPRO_FIXTURE_FAI=${PROJECT_SOURCE_DIR}/tests/fixtures/interop.fa.fai.expected
        -DSEQPRO_TEST_ROOT=${PROJECT_BINARY_DIR}/example-sequence-text-workflow
        -P ${PROJECT_SOURCE_DIR}/tests/run_example_smoke.cmake
    )
    add_test(
      NAME seqpro.sequence_text.example.coordinates
      COMMAND ${CMAKE_COMMAND}
        -DSEQPRO_EXAMPLE_EXECUTABLE=$<TARGET_FILE:seqpro-sequence-text-coordinates-example>
        -DSEQPRO_EXAMPLE_MODE=sequence_text_coordinates
        -DSEQPRO_FIXTURE_FASTA=${PROJECT_SOURCE_DIR}/tests/fixtures/interop.fa
        -DSEQPRO_FIXTURE_FAI=${PROJECT_SOURCE_DIR}/tests/fixtures/interop.fa.fai.expected
        -DSEQPRO_TEST_ROOT=${PROJECT_BINARY_DIR}/example-sequence-text-coordinates
        -P ${PROJECT_SOURCE_DIR}/tests/run_example_smoke.cmake
    )
  endif()
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
  seqpro_configure_owned_cxx_target(seqpro-sequence-text-benchmark)
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
