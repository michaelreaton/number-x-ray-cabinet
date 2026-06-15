if(NOT DEFINED XRAY_SOURCE_DIR OR NOT DEFINED XRAY_BINARY_DIR OR NOT DEFINED XRAY_CONFIG)
  message(FATAL_ERROR "XRAY_SOURCE_DIR, XRAY_BINARY_DIR, and XRAY_CONFIG are required.")
endif()

if(NOT XRAY_CONFIG)
  set(XRAY_CONFIG Release)
endif()

if(WIN32 AND XRAY_GENERATOR MATCHES "Visual Studio")
  # Visual Studio generators select the matching toolset; stale shell state can
  # force an incompatible VCToolsVersion into the external consumer configure.
  set(ENV{VCToolsVersion})
endif()

set(smoke_root "${XRAY_BINARY_DIR}/import-smoke")
set(install_prefix "${smoke_root}/install")
set(consumer_build "${smoke_root}/consumer-build")

file(REMOVE_RECURSE "${smoke_root}")
file(MAKE_DIRECTORY "${smoke_root}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${XRAY_BINARY_DIR}" --config "${XRAY_CONFIG}" --prefix "${install_prefix}"
  RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "NumberXRay install smoke failed during install step: ${install_result}")
endif()

set(configure_command
  "${CMAKE_COMMAND}"
  -S "${XRAY_SOURCE_DIR}/examples/import_consumer"
  -B "${consumer_build}"
  -DCMAKE_PREFIX_PATH=${install_prefix}
  -DCMAKE_BUILD_TYPE=${XRAY_CONFIG}
)

if(XRAY_GENERATOR)
  list(APPEND configure_command -G "${XRAY_GENERATOR}")
endif()
if(XRAY_GENERATOR_PLATFORM)
  list(APPEND configure_command -A "${XRAY_GENERATOR_PLATFORM}")
endif()
if(XRAY_GENERATOR_TOOLSET)
  list(APPEND configure_command -T "${XRAY_GENERATOR_TOOLSET}")
endif()

execute_process(
  COMMAND ${configure_command}
  RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "NumberXRay import smoke failed during configure step: ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" --config "${XRAY_CONFIG}"
  RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "NumberXRay import smoke failed during build step: ${build_result}")
endif()

if(WIN32)
  set(consumer_exe_candidates
    "${consumer_build}/${XRAY_CONFIG}/number_xray_import_consumer.exe"
    "${consumer_build}/number_xray_import_consumer.exe"
  )
else()
  set(consumer_exe_candidates
    "${consumer_build}/number_xray_import_consumer"
  )
endif()

set(consumer_exe "")
foreach(candidate IN LISTS consumer_exe_candidates)
  if(EXISTS "${candidate}")
    set(consumer_exe "${candidate}")
    break()
  endif()
endforeach()

if(NOT consumer_exe)
  message(FATAL_ERROR "NumberXRay import smoke could not find the consumer executable.")
endif()

execute_process(
  COMMAND "${consumer_exe}"
  RESULT_VARIABLE run_result
)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "NumberXRay import smoke failed during run step: ${run_result}")
endif()
