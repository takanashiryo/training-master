if(NOT DEFINED TRAINING_VERSION_SCRIPT OR NOT DEFINED TRAINING_TEST_HEADER OR NOT DEFINED TRAINING_TEST_RESOURCE)
  message(FATAL_ERROR "Version test paths are required")
endif()

function(generate_version epoch expected_date)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "TZ=UTC" "SOURCE_DATE_EPOCH=${epoch}"
    "${CMAKE_COMMAND}" "-DTRAINING_VERSION_HEADER=${TRAINING_TEST_HEADER}"
    "-DTRAINING_VERSION_RESOURCE=${TRAINING_TEST_RESOURCE}"
    -P "${TRAINING_VERSION_SCRIPT}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Version generation failed: ${error}")
  endif()
  file(READ "${TRAINING_TEST_HEADER}" header)
  if(NOT header MATCHES "build_date\\[\\]=\"${expected_date}\"")
    message(FATAL_ERROR "Expected ${expected_date} in generated header: ${header}")
  endif()
  file(READ "${TRAINING_TEST_RESOURCE}" resource)
  foreach(field IN ITEMS
      "FILEVERSION 0,1,67,0\n"
      "PRODUCTVERSION 0,1,67,0\n"
      "VALUE \"CompanyName\", \"Takanashi Ryo\\0\""
      "VALUE \"FileVersion\", \"0.1.67\\0\""
      "VALUE \"LegalCopyright\", L\"Copyright \\x00A9 2026 Takanashi Ryo\\0\""
      "VALUE \"ProductVersion\", \"0.1.67\\0\""
      "VALUE \"Comments\", \"Built ${expected_date}\\0\"")
    string(FIND "${resource}" "${field}" position)
    if(position EQUAL -1)
      message(FATAL_ERROR "Missing ${field} in generated DLL version resource")
    endif()
  endforeach()
endfunction()

generate_version(1793534400 "2026-11-01 12:00")
file(TIMESTAMP "${TRAINING_TEST_HEADER}" before "%s" UTC)
file(TIMESTAMP "${TRAINING_TEST_RESOURCE}" resource_before "%s" UTC)
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
generate_version(1793534400 "2026-11-01 12:00")
file(TIMESTAMP "${TRAINING_TEST_HEADER}" after "%s" UTC)
file(TIMESTAMP "${TRAINING_TEST_RESOURCE}" resource_after "%s" UTC)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "An unchanged build minute rewrote the version header")
endif()
if(NOT resource_before STREQUAL resource_after)
  message(FATAL_ERROR "An unchanged build minute rewrote the DLL version resource")
endif()
generate_version(1793534460 "2026-11-01 12:01")
generate_version(1793620800 "2026-11-02 12:00")
generate_version(1786233600 "2026-08-09 00:00")
