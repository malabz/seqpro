if(NOT DEFINED SEQPRO_INDEX_EXECUTABLE OR NOT DEFINED SEQPRO_SAMTOOLS_EXECUTABLE OR
   NOT DEFINED SEQPRO_FIXTURE_DIR OR NOT DEFINED SEQPRO_TEST_ROOT)
  message(FATAL_ERROR "SeqPro Samtools interoperability paths are required")
endif()

file(REMOVE_RECURSE "${SEQPRO_TEST_ROOT}")
file(MAKE_DIRECTORY "${SEQPRO_TEST_ROOT}")
configure_file("${SEQPRO_FIXTURE_DIR}/interop.fa" "${SEQPRO_TEST_ROOT}/seqpro.fa" COPYONLY)
configure_file("${SEQPRO_FIXTURE_DIR}/interop.fa" "${SEQPRO_TEST_ROOT}/samtools.fa" COPYONLY)

execute_process(
  COMMAND "${SEQPRO_INDEX_EXECUTABLE}" build "${SEQPRO_TEST_ROOT}/seqpro.fa" --no-metadata
  RESULT_VARIABLE seqpro_build_result
  OUTPUT_VARIABLE seqpro_build_output
  ERROR_VARIABLE seqpro_build_error)
if(NOT seqpro_build_result EQUAL 0)
  message(FATAL_ERROR "SeqPro index build failed:\n${seqpro_build_output}\n${seqpro_build_error}")
endif()

execute_process(
  COMMAND "${SEQPRO_SAMTOOLS_EXECUTABLE}" faidx "${SEQPRO_TEST_ROOT}/samtools.fa"
  RESULT_VARIABLE samtools_build_result
  OUTPUT_VARIABLE samtools_build_output
  ERROR_VARIABLE samtools_build_error)
if(NOT samtools_build_result EQUAL 0)
  message(FATAL_ERROR
    "Samtools index build failed:\n${samtools_build_output}\n${samtools_build_error}")
endif()

file(READ "${SEQPRO_TEST_ROOT}/seqpro.fa.fai" seqpro_index)
file(READ "${SEQPRO_TEST_ROOT}/samtools.fa.fai" samtools_index)
file(READ "${SEQPRO_FIXTURE_DIR}/interop.fa.fai.expected" expected_index)
if(NOT seqpro_index STREQUAL samtools_index OR NOT seqpro_index STREQUAL expected_index)
  message(FATAL_ERROR "SeqPro and Samtools FAI fields differ")
endif()

execute_process(
  COMMAND "${SEQPRO_SAMTOOLS_EXECUTABLE}" faidx "${SEQPRO_TEST_ROOT}/seqpro.fa" "chr1:3-11"
  RESULT_VARIABLE extraction_result
  OUTPUT_VARIABLE extracted_fasta
  ERROR_VARIABLE extraction_error)
set(expected_extracted_fasta ">chr1:3-11\nGTACGTAC\nG\n")
if(NOT extraction_result EQUAL 0)
  message(FATAL_ERROR
    "Samtools could not use SeqPro FAI:\n${extracted_fasta}\n${extraction_error}")
endif()
if(NOT extracted_fasta STREQUAL expected_extracted_fasta)
  message(FATAL_ERROR
    "Samtools could not use SeqPro FAI:\n${extracted_fasta}\n${extraction_error}")
endif()

execute_process(
  COMMAND "${SEQPRO_INDEX_EXECUTABLE}" validate "${SEQPRO_TEST_ROOT}/samtools.fa" --full
  RESULT_VARIABLE validation_result
  OUTPUT_VARIABLE validation_output
  ERROR_VARIABLE validation_error)
if(NOT validation_result EQUAL 0 OR NOT validation_output MATCHES "external_standard_fai")
  message(FATAL_ERROR
    "SeqPro could not use Samtools FAI:\n${validation_output}\n${validation_error}")
endif()

set(crlf_fasta ">alpha description\r\nACGT\r\nTG\r\n>beta\r\nxyz")
file(WRITE "${SEQPRO_TEST_ROOT}/crlf-seqpro.fa" "${crlf_fasta}")
file(WRITE "${SEQPRO_TEST_ROOT}/crlf-samtools.fa" "${crlf_fasta}")
execute_process(
  COMMAND "${SEQPRO_INDEX_EXECUTABLE}" build
          "${SEQPRO_TEST_ROOT}/crlf-seqpro.fa" --no-metadata
  RESULT_VARIABLE crlf_seqpro_result
  ERROR_VARIABLE crlf_seqpro_error)
if(NOT crlf_seqpro_result EQUAL 0)
  message(FATAL_ERROR "SeqPro CRLF index build failed:\n${crlf_seqpro_error}")
endif()
execute_process(
  COMMAND "${SEQPRO_SAMTOOLS_EXECUTABLE}" faidx "${SEQPRO_TEST_ROOT}/crlf-samtools.fa"
  RESULT_VARIABLE crlf_samtools_result
  ERROR_VARIABLE crlf_samtools_error)
if(NOT crlf_samtools_result EQUAL 0)
  message(FATAL_ERROR "Samtools CRLF index build failed:\n${crlf_samtools_error}")
endif()
file(READ "${SEQPRO_TEST_ROOT}/crlf-seqpro.fa.fai" crlf_seqpro_index)
file(READ "${SEQPRO_TEST_ROOT}/crlf-samtools.fa.fai" crlf_samtools_index)
set(expected_crlf_index "alpha\t6\t20\t4\t6\nbeta\t3\t37\t3\t4\n")
if(NOT crlf_seqpro_index STREQUAL crlf_samtools_index OR
   NOT crlf_seqpro_index STREQUAL expected_crlf_index)
  message(FATAL_ERROR "SeqPro and Samtools CRLF FAI fields differ")
endif()
execute_process(
  COMMAND "${SEQPRO_INDEX_EXECUTABLE}" validate
          "${SEQPRO_TEST_ROOT}/crlf-samtools.fa" --full
  RESULT_VARIABLE crlf_validation_result
  ERROR_VARIABLE crlf_validation_error)
if(NOT crlf_validation_result EQUAL 0)
  message(FATAL_ERROR
    "SeqPro could not fully validate Samtools CRLF FAI:\n${crlf_validation_error}")
endif()
