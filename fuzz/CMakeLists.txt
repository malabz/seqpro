if(NOT SEQPRO_IS_TOP_LEVEL)
  message(FATAL_ERROR "SEQPRO_BUILD_FUZZERS is supported only for a top-level SeqPro build")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "SEQPRO_BUILD_FUZZERS requires Clang libFuzzer support")
endif()
if(BUILD_SHARED_LIBS)
  message(FATAL_ERROR "SEQPRO_BUILD_FUZZERS requires BUILD_SHARED_LIBS=OFF")
endif()
if(NOT SEQPRO_BUILD_SEQUENCE_TEXT)
  message(FATAL_ERROR "SEQPRO_BUILD_FUZZERS requires SEQPRO_BUILD_SEQUENCE_TEXT=ON")
endif()

set(SEQPRO_FUZZ_SANITIZERS -fsanitize=address,undefined)
target_compile_options(seqpro PRIVATE ${SEQPRO_FUZZ_SANITIZERS} -fno-omit-frame-pointer)
target_compile_options(
  seqpro_sequence_text
  PRIVATE ${SEQPRO_FUZZ_SANITIZERS} -fno-omit-frame-pointer
)

function(seqpro_add_fuzzer target_name source_file linked_library)
  add_executable(${target_name} ${source_file})
  target_link_libraries(${target_name} PRIVATE ${linked_library})
  target_include_directories(${target_name} PRIVATE "${PROJECT_SOURCE_DIR}/src")
  seqpro_configure_owned_cxx_target(${target_name})
  target_compile_options(
    ${target_name}
    PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
  )
  target_link_options(${target_name} PRIVATE -fsanitize=fuzzer,address,undefined)
endfunction()

seqpro_add_fuzzer(seqpro-fuzz-fasta-index fasta_index_fuzzer.cc SeqPro::seqpro)
seqpro_add_fuzzer(seqpro-fuzz-metadata metadata_fuzzer.cc SeqPro::seqpro)
seqpro_add_fuzzer(seqpro-fuzz-fasta-scanner fasta_scanner_fuzzer.cc SeqPro::seqpro)
seqpro_add_fuzzer(
  seqpro-fuzz-sequence-text
  sequence_text_layout_fuzzer.cc
  SeqPro::sequence_text
)
