cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED XRAY_CLI OR NOT XRAY_CLI)
  message(FATAL_ERROR "XRAY_CLI is required.")
endif()

if(NOT EXISTS "${XRAY_CLI}")
  message(FATAL_ERROR "XRAY_CLI does not exist: ${XRAY_CLI}")
endif()

execute_process(
  COMMAND "${XRAY_CLI}" --help
  RESULT_VARIABLE help_result
  OUTPUT_VARIABLE help_stdout
  ERROR_VARIABLE help_stderr
)

if(NOT help_result EQUAL 0)
  message(FATAL_ERROR "xray_cli --help failed with exit code ${help_result}.")
endif()

set(help_text "${help_stdout}\n${help_stderr}")
set(required_focus_labels
  "--bench-focus"
  "mul-large"
  "mul-full-audit-pocket"
  "mul-backend-gap"
  "mul-toom4-top"
  "mul-toom5-smoke"
  "mul-toom-div-transition"
  "mul-combo-handoff-pocket"
  "mul-combo-handoff-boundary"
  "mul-sparse"
  "mul-novelty"
)

set(missing_labels "")
foreach(label IN LISTS required_focus_labels)
  string(FIND "${help_text}" "${label}" label_index)
  if(label_index LESS 0)
    list(APPEND missing_labels "${label}")
  endif()
endforeach()

list(LENGTH missing_labels missing_count)
if(missing_count GREATER 0)
  string(JOIN ", " missing_text ${missing_labels})
  message(FATAL_ERROR "xray_cli --help is missing benchmark focus label(s): ${missing_text}")
endif()

string(JOIN ", " label_text ${required_focus_labels})
message(STATUS "xray_cli --help lists benchmark focus labels: ${label_text}")
