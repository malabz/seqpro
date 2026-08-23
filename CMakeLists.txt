cmake_minimum_required(VERSION 3.20)

project(SeqPro VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "SeqPro v0.1.0 supports 64-bit Linux and WSL only")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "SeqPro v0.1.0 requires a 64-bit platform")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  message(FATAL_ERROR "SeqPro requires GCC 9 or newer")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
  message(FATAL_ERROR "SeqPro requires Clang 10 or newer")
endif()

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(SEQPRO_IS_TOP_LEVEL ON)
  set(SEQPRO_TOOLS_DEFAULT ON)
else()
  set(SEQPRO_IS_TOP_LEVEL OFF)
  set(SEQPRO_TOOLS_DEFAULT OFF)
endif()

option(SEQPRO_BUILD_TOOLS "Build the seqpro-index command-line tool" ${SEQPRO_TOOLS_DEFAULT})
option(SEQPRO_BUILD_EXAMPLES "Build SeqPro examples" OFF)
option(SEQPRO_BUILD_BENCHMARKS "Build SeqPro benchmarks" OFF)
option(SEQPRO_ENABLE_CLANG_TIDY "Run clang-tidy while compiling SeqPro" OFF)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(seqpro
  src/error.cc
  src/fasta_index.cc
  src/indexed_fasta.cc
  src/mapped_file.cc
  third_party/xxhash/xxhash.c
)
add_library(SeqPro::seqpro ALIAS seqpro)

target_compile_features(seqpro PUBLIC cxx_std_17)
target_compile_definitions(seqpro PRIVATE SEQPRO_BUILDING_LIBRARY _FILE_OFFSET_BITS=64)
target_include_directories(seqpro
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xxhash
)
set_target_properties(seqpro PROPERTIES
  C_VISIBILITY_PRESET hidden
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS OFF
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  POSITION_INDEPENDENT_CODE YES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_compile_options(seqpro PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion>
  $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic>
)

if(SEQPRO_ENABLE_CLANG_TIDY)
  find_program(SEQPRO_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  set_target_properties(seqpro PROPERTIES CXX_CLANG_TIDY "${SEQPRO_CLANG_TIDY_EXECUTABLE}")
endif()

if(SEQPRO_BUILD_TOOLS)
  add_executable(seqpro-index tools/seqpro_index.cc)
  target_link_libraries(seqpro-index PRIVATE SeqPro::seqpro)
  target_compile_features(seqpro-index PRIVATE cxx_std_17)
  set_target_properties(seqpro-index PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
  )
  target_compile_options(seqpro-index PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SEQPRO_BUILD_EXAMPLES)
  add_executable(seqpro-basic-example examples/basic_usage.cc)
  target_link_libraries(seqpro-basic-example PRIVATE SeqPro::seqpro)
  target_compile_features(seqpro-basic-example PRIVATE cxx_std_17)
  set_target_properties(seqpro-basic-example PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
  )
endif()

if(SEQPRO_BUILD_BENCHMARKS)
  add_executable(seqpro-benchmark benchmarks/seqpro_benchmark.cc)
  target_link_libraries(seqpro-benchmark PRIVATE SeqPro::seqpro)
  target_compile_features(seqpro-benchmark PRIVATE cxx_std_17)
  set_target_properties(seqpro-benchmark PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
  )
  target_compile_options(seqpro-benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  add_executable(seqpro-tests tests/seqpro_test.cc)
  target_link_libraries(seqpro-tests PRIVATE SeqPro::seqpro Threads::Threads)
  target_compile_features(seqpro-tests PRIVATE cxx_std_17)
  set_target_properties(seqpro-tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
  )
  target_compile_options(seqpro-tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME seqpro.unit COMMAND seqpro-tests)

  if(SEQPRO_IS_TOP_LEVEL)
    add_test(
      NAME seqpro.consumer.add_subdirectory
      COMMAND ${CMAKE_COMMAND}
        -DSEQPRO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DSEQPRO_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/consumer-add-subdirectory
        -DSEQPRO_GENERATOR=${CMAKE_GENERATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_add_subdirectory_consumer.cmake
    )
    add_test(
      NAME seqpro.consumer.install
      COMMAND ${CMAKE_COMMAND}
        -DSEQPRO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DSEQPRO_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DSEQPRO_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/consumer-install
        -DSEQPRO_GENERATOR=${CMAKE_GENERATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_install_consumer.cmake
    )

    if(SEQPRO_BUILD_TOOLS)
      find_program(SEQPRO_SAMTOOLS_EXECUTABLE NAMES samtools)
      if(SEQPRO_SAMTOOLS_EXECUTABLE)
        add_test(
          NAME seqpro.integration.samtools
          COMMAND ${CMAKE_COMMAND}
            -DSEQPRO_INDEX_EXECUTABLE=$<TARGET_FILE:seqpro-index>
            -DSEQPRO_SAMTOOLS_EXECUTABLE=${SEQPRO_SAMTOOLS_EXECUTABLE}
            -DSEQPRO_FIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
            -DSEQPRO_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/samtools-interop
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_samtools_interop.cmake
        )
      endif()
    endif()
  endif()
endif()

install(TARGETS seqpro
  EXPORT SeqProTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(SEQPRO_BUILD_TOOLS)
  install(TARGETS seqpro-index RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE README.md CHANGELOG.md THIRD_PARTY_NOTICES.md
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/SeqPro
)
install(FILES docs/fai_contract.md
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/SeqPro/docs
)
install(EXPORT SeqProTargets
  FILE SeqProTargets.cmake
  NAMESPACE SeqPro::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SeqPro
)

configure_package_config_file(
  cmake/SeqProConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/SeqProConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SeqPro
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/SeqProConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/SeqProConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/SeqProConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SeqPro
)
