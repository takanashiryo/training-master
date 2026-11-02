if(NOT DEFINED TRAINING_VERSION_HEADER OR TRAINING_VERSION_HEADER STREQUAL "")
  message(FATAL_ERROR "TRAINING_VERSION_HEADER is required")
endif()

# Local build date; CMake honours SOURCE_DATE_EPOCH for reproducible builds.
string(TIMESTAMP TRAINING_BUILD_DATE "%Y-%m-%d %H:%M")
if(TRAINING_BUILD_DATE STREQUAL "")
  message(FATAL_ERROR "Could not determine the build date")
endif()
get_filename_component(training_version_dir "${TRAINING_VERSION_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${training_version_dir}")
file(CONFIGURE OUTPUT "${TRAINING_VERSION_HEADER}" CONTENT
"#pragma once
namespace training::about {
inline constexpr char build_date[]=\"@TRAINING_BUILD_DATE@\";
}
"
  @ONLY NEWLINE_STYLE UNIX)
