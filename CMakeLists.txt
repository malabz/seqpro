cmake_minimum_required(VERSION 3.20)

project(SeqPro VERSION 0.2.0 LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "SeqPro v0.2.0 supports 64-bit Linux and WSL only")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "SeqPro v0.2.0 requires a 64-bit platform")
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" SEQPRO_SYSTEM_PROCESSOR)
if(NOT SEQPRO_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
  message(FATAL_ERROR "SeqPro v0.2.0 release support is limited to x86_64 Linux/WSL")
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
option(SEQPRO_BUILD_SEQUENCE_TEXT "Build the optional SequenceText component" OFF)
option(SEQPRO_BUILD_DOCUMENTATION "Build strict Doxygen API documentation" OFF)
option(SEQPRO_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)
option(SEQPRO_ENABLE_CLANG_TIDY "Run clang-tidy while compiling SeqPro" OFF)
option(SEQPRO_ENABLE_ABI_CHECKS "Compare shared libraries with the v0.2.0 ABI baseline" OFF)
option(SEQPRO_WARNINGS_AS_ERRORS "Treat SeqPro-owned compiler warnings as errors" OFF)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(SEQPRO_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
file(MAKE_DIRECTORY "${SEQPRO_GENERATED_INCLUDE_DIR}/seqpro")
configure_file(
  include/seqpro/version.h.in
  "${SEQPRO_GENERATED_INCLUDE_DIR}/seqpro/version.h"
  @ONLY
)

function(seqpro_configure_owned_cxx_target target_name)
  target_compile_features(${target_name} PRIVATE cxx_std_17)
  set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
  )
  target_compile_options(${target_name} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wformat=2
    -Wundef
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -fstack-protector-strong
  )
  if(SEQPRO_WARNINGS_AS_ERRORS)
    target_compile_options(${target_name} PRIVATE -Werror)
  endif()
  if(SEQPRO_ENABLE_CLANG_TIDY)
    set_target_properties(
      ${target_name}
      PROPERTIES CXX_CLANG_TIDY "${SEQPRO_CLANG_TIDY_EXECUTABLE}"
    )
  endif()
endfunction()

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
    $<BUILD_INTERFACE:${SEQPRO_GENERATED_INCLUDE_DIR}>
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
  SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)
target_compile_options(seqpro PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion;-Wshadow;-Wformat=2;-Wundef;-Wnon-virtual-dtor;-Woverloaded-virtual;-fstack-protector-strong>
  $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic>
)
if(SEQPRO_WARNINGS_AS_ERRORS)
  target_compile_options(seqpro PRIVATE -Werror)
endif()

if(BUILD_SHARED_LIBS)
  target_link_options(seqpro PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/seqpro_core.map"
    "LINKER:--no-undefined"
    "LINKER:-z,relro"
    "LINKER:-z,now"
  )
  set_property(
    TARGET seqpro
    APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/seqpro_core.map"
  )
endif()

if(SEQPRO_ENABLE_CLANG_TIDY)
  find_program(SEQPRO_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  set_target_properties(seqpro PROPERTIES CXX_CLANG_TIDY "${SEQPRO_CLANG_TIDY_EXECUTABLE}")
endif()

if(SEQPRO_BUILD_SEQUENCE_TEXT)
  add_subdirectory(extensions/sequence_text)
endif()

if(SEQPRO_BUILD_DOCUMENTATION)
  find_package(Doxygen 1.9 REQUIRED)
  set(SEQPRO_DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/api-docs")
  set(SEQPRO_DOXYGEN_INPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/seqpro ${SEQPRO_GENERATED_INCLUDE_DIR}/seqpro"
  )
  if(SEQPRO_BUILD_SEQUENCE_TEXT)
    string(APPEND SEQPRO_DOXYGEN_INPUTS
      " ${CMAKE_CURRENT_SOURCE_DIR}/extensions/sequence_text/include/seqpro"
    )
  endif()
  configure_file(
    docs/Doxyfile.in
    "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    @ONLY
  )
  add_custom_target(seqpro-docs ALL
    COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    BYPRODUCTS "${SEQPRO_DOXYGEN_OUTPUT_DIR}/html/index.html"
    COMMENT "Generating strict SeqPro API documentation"
    VERBATIM
  )
endif()

if(SEQPRO_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()

if(SEQPRO_BUILD_TOOLS)
  add_executable(seqpro-index tools/seqpro_index.cc)
  target_link_libraries(seqpro-index PRIVATE SeqPro::seqpro)
  seqpro_configure_owned_cxx_target(seqpro-index)
endif()

if(SEQPRO_BUILD_EXAMPLES)
  add_executable(seqpro-basic-example examples/basic_usage.cc)
  target_link_libraries(seqpro-basic-example PRIVATE SeqPro::seqpro)
  seqpro_configure_owned_cxx_target(seqpro-basic-example)
endif()

if(SEQPRO_BUILD_BENCHMARKS)
  add_executable(seqpro-benchmark benchmarks/seqpro_benchmark.cc)
  target_link_libraries(seqpro-benchmark PRIVATE SeqPro::seqpro)
  seqpro_configure_owned_cxx_target(seqpro-benchmark)
endif()

if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  add_executable(seqpro-tests tests/seqpro_test.cc)
  target_link_libraries(seqpro-tests PRIVATE SeqPro::seqpro Threads::Threads)
  target_include_directories(seqpro-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(
      seqpro-tests
      PRIVATE SEQPRO_TEST_SHARED_LIBRARY
    )
  endif()
  seqpro_configure_owned_cxx_target(seqpro-tests)
  add_test(NAME seqpro.unit COMMAND seqpro-tests)
  add_test(
    NAME seqpro.source_conventions
    COMMAND ${CMAKE_COMMAND}
      -DSEQPRO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckSourceConventions.cmake
  )

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
      NAME seqpro.consumer.fetch_content_offline
      COMMAND ${CMAKE_COMMAND}
        -DSEQPRO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DSEQPRO_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/consumer-fetch-content
        -DSEQPRO_GENERATOR=${CMAKE_GENERATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_fetch_content_consumer.cmake
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
    add_test(
      NAME seqpro.consumer.relocated_install
      COMMAND ${CMAKE_COMMAND}
        -DSEQPRO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DSEQPRO_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DSEQPRO_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/consumer-relocated-install
        -DSEQPRO_GENERATOR=${CMAKE_GENERATOR}
        -DSEQPRO_SEQUENCE_TEXT_ENABLED=${SEQPRO_BUILD_SEQUENCE_TEXT}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_relocated_install_consumer.cmake
    )
    add_test(
      NAME seqpro.consumer.version_compatibility
      COMMAND ${CMAKE_COMMAND}
        -DSEQPRO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DSEQPRO_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DSEQPRO_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/consumer-version-compatibility
        -DSEQPRO_GENERATOR=${CMAKE_GENERATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_version_compatibility.cmake
    )

    if(NOT SEQPRO_BUILD_SEQUENCE_TEXT)
      add_test(
        NAME seqpro.consumer.missing_sequence_text_component
        COMMAND ${CMAKE_COMMAND}
          -DSEQPRO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
          -DSEQPRO_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
          -DSEQPRO_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/consumer-missing-sequence-text
          -DSEQPRO_GENERATOR=${CMAKE_GENERATOR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_missing_sequence_text_component.cmake
      )
    endif()

    if(SEQPRO_BUILD_TOOLS)
      add_test(NAME seqpro.cli.version COMMAND seqpro-index --version)
      set_tests_properties(
        seqpro.cli.version
        PROPERTIES PASS_REGULAR_EXPRESSION "^seqpro-index 0\\.2\\.0"
      )
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

    if(BUILD_SHARED_LIBS)
      find_program(SEQPRO_NM_EXECUTABLE NAMES nm REQUIRED)
      find_program(SEQPRO_CXXFILT_EXECUTABLE NAMES c++filt REQUIRED)
      find_program(SEQPRO_READELF_EXECUTABLE NAMES readelf REQUIRED)
      set(SEQPRO_SHARED_AUDIT_COMMAND
        ${CMAKE_COMMAND}
        -DSEQPRO_CORE_LIBRARY=$<TARGET_FILE:seqpro>
        -DSEQPRO_NM_EXECUTABLE=${SEQPRO_NM_EXECUTABLE}
        -DSEQPRO_CXXFILT_EXECUTABLE=${SEQPRO_CXXFILT_EXECUTABLE}
        -DSEQPRO_READELF_EXECUTABLE=${SEQPRO_READELF_EXECUTABLE}
      )
      if(SEQPRO_BUILD_SEQUENCE_TEXT)
        list(APPEND SEQPRO_SHARED_AUDIT_COMMAND
          -DSEQPRO_SEQUENCE_TEXT_LIBRARY=$<TARGET_FILE:seqpro_sequence_text>
        )
      endif()
      list(APPEND SEQPRO_SHARED_AUDIT_COMMAND
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_shared_library_audit.cmake
      )
      add_test(NAME seqpro.shared_library.audit COMMAND ${SEQPRO_SHARED_AUDIT_COMMAND})

      if(SEQPRO_ENABLE_ABI_CHECKS)
        find_program(SEQPRO_ABIDIFF_EXECUTABLE NAMES abidiff REQUIRED)
        add_test(
          NAME seqpro.abi.core
          COMMAND ${CMAKE_COMMAND}
            -DSEQPRO_ABIDIFF_EXECUTABLE=${SEQPRO_ABIDIFF_EXECUTABLE}
            -DSEQPRO_ABI_BASELINE=${CMAKE_CURRENT_SOURCE_DIR}/abi/v0.2.0/seqpro.abi
            -DSEQPRO_CURRENT_LIBRARY=$<TARGET_FILE:seqpro>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckAbiCompatibility.cmake
        )
        if(SEQPRO_BUILD_SEQUENCE_TEXT)
          add_test(
            NAME seqpro.abi.sequence_text
            COMMAND ${CMAKE_COMMAND}
              -DSEQPRO_ABIDIFF_EXECUTABLE=${SEQPRO_ABIDIFF_EXECUTABLE}
              -DSEQPRO_ABI_BASELINE=${CMAKE_CURRENT_SOURCE_DIR}/abi/v0.2.0/sequence_text.abi
              -DSEQPRO_CURRENT_LIBRARY=$<TARGET_FILE:seqpro_sequence_text>
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckAbiCompatibility.cmake
          )
        endif()
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
install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  PATTERN "version.h.in" EXCLUDE
)
install(
  FILES "${SEQPRO_GENERATED_INCLUDE_DIR}/seqpro/version.h"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/seqpro
)
install(FILES
  LICENSE
  README.md
  README.zh-CN.md
  CHANGELOG.md
  CONTRIBUTING.md
  SECURITY.md
  DEPENDENCIES.md
  THIRD_PARTY_NOTICES.md
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/SeqPro
)
install(FILES
  docs/fai_contract.md
  docs/sequence_text_layout.md
  docs/releasing.md
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/SeqPro/docs
)
install(
  DIRECTORY docs/zh-CN/
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/SeqPro/docs/zh-CN
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
  COMPATIBILITY SameMinorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/SeqProConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/SeqProConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SeqPro
)
