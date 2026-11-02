if(NOT DEFINED TRAINING_VERSION_SCRIPT OR NOT DEFINED TRAINING_TEST_HEADER)
  message(FATAL_ERROR "Version test paths are required")
endif()

function(generate_version epoch expected)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "TZ=UTC" "SOURCE_DATE_EPOCH=${epoch}"
    "${CMAKE_COMMAND}" "-DTRAINING_VERSION_HEADER=${TRAINING_TEST_HEADER}"
    -P "${TRAINING_VERSION_SCRIPT}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Version generation failed: ${error}")
  endif()
  file(READ "${TRAINING_TEST_HEADER}" header)
  if(NOT header MATCHES "build_date\\[\\]=\"${expected}\"")
    message(FATAL_ERROR "Expected ${expected} in generated header: ${header}")
  endif()
endfunction()

generate_version(1793534400 "2026-11-01 12:00")
file(TIMESTAMP "${TRAINING_TEST_HEADER}" before "%s" UTC)
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
generate_version(1793534400 "2026-11-01 12:00")
file(TIMESTAMP "${TRAINING_TEST_HEADER}" after "%s" UTC)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "An unchanged build minute rewrote the version header")
endif()
generate_version(1793534460 "2026-11-01 12:01")
generate_version(1793620800 "2026-11-02 12:00")
