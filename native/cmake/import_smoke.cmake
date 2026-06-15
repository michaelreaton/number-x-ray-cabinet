if(NOT DEFINED XRAY_SOURCE_DIR OR NOT DEFINED XRAY_BINARY_DIR OR NOT DEFINED XRAY_CONFIG)
  message(FATAL_ERROR "XRAY_SOURCE_DIR, XRAY_BINARY_DIR, and XRAY_CONFIG are required.")
endif()

if(NOT XRAY_CONFIG)
  set(XRAY_CONFIG Release)
endif()
if(NOT DEFINED XRAY_EXPECT_SHARED)
  set(XRAY_EXPECT_SHARED ON)
endif()
if(NOT DEFINED XRAY_INSTALL_LIBDIR OR NOT XRAY_INSTALL_LIBDIR)
  set(XRAY_INSTALL_LIBDIR lib)
endif()
if(NOT DEFINED XRAY_INSTALL_BINDIR OR NOT XRAY_INSTALL_BINDIR)
  set(XRAY_INSTALL_BINDIR bin)
endif()
if(NOT DEFINED XRAY_INSTALL_DATADIR OR NOT XRAY_INSTALL_DATADIR)
  set(XRAY_INSTALL_DATADIR share)
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

set(pkgconfig_file "${install_prefix}/${XRAY_INSTALL_LIBDIR}/pkgconfig/number-xray.pc")
if(NOT EXISTS "${pkgconfig_file}")
  message(FATAL_ERROR "NumberXRay install smoke did not install pkg-config metadata: ${pkgconfig_file}")
endif()

set(sdk_manifest_file "${install_prefix}/${XRAY_INSTALL_DATADIR}/number-xray/number-xray-sdk.json")
if(NOT EXISTS "${sdk_manifest_file}")
  message(FATAL_ERROR "NumberXRay install smoke did not install SDK manifest: ${sdk_manifest_file}")
endif()
file(READ "${sdk_manifest_file}" sdk_manifest)
foreach(expected
    "\"public\": \"number_xray.h\""
    "\"functionReferenceHeader\": \"xray_workbench.h\""
    "\"coverage\": \"all exported XRAY_API functions\""
    "\"versionFunction\": \"xray_version\""
    "\"abiVersionFunction\": \"xray_abi_version\""
    "\"cmakeTarget\": \"NumberXRay::core\""
    "\"pkgConfig\": \"number-xray\""
    "\"freeFunction\": \"xray_free\""
    "\"cmakeTarget\": \"GMP::GMP\"")
  string(FIND "${sdk_manifest}" "${expected}" expected_index)
  if(expected_index LESS 0)
    message(FATAL_ERROR "NumberXRay SDK manifest is missing expected entry: ${expected}")
  endif()
endforeach()

if(XRAY_EXPECT_SHARED)
  foreach(expected
      "\"cmakeTarget\": \"NumberXRay::core_shared\""
      "\"libDir\": \"${XRAY_INSTALL_LIBDIR}\""
      "\"binDir\": \"${XRAY_INSTALL_BINDIR}\"")
    string(FIND "${sdk_manifest}" "${expected}" expected_index)
    if(expected_index LESS 0)
      message(FATAL_ERROR "NumberXRay SDK manifest is missing expected shared entry: ${expected}")
    endif()
  endforeach()
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

function(xray_run_consumer executable_name)
  if(WIN32)
    set(consumer_exe_candidates
      "${consumer_build}/${XRAY_CONFIG}/${executable_name}.exe"
      "${consumer_build}/${executable_name}.exe"
    )
  else()
    set(consumer_exe_candidates
      "${consumer_build}/${executable_name}"
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
    message(FATAL_ERROR "NumberXRay import smoke could not find ${executable_name}.")
  endif()

  if(WIN32)
    set(consumer_path "${install_prefix}/${XRAY_INSTALL_BINDIR}")
    if(DEFINED XRAY_GMP_RUNTIME_DIR AND XRAY_GMP_RUNTIME_DIR)
      set(consumer_path "${consumer_path};${XRAY_GMP_RUNTIME_DIR}")
    endif()
    set(consumer_path "${consumer_path};$ENV{PATH}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "PATH=${consumer_path}" "${consumer_exe}"
      RESULT_VARIABLE run_result
    )
  else()
    execute_process(
      COMMAND "${consumer_exe}"
      RESULT_VARIABLE run_result
    )
  endif()

  if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "NumberXRay import smoke failed while running ${executable_name}: ${run_result}")
  endif()
endfunction()

xray_run_consumer(number_xray_import_consumer)
if(XRAY_EXPECT_SHARED)
  xray_run_consumer(number_xray_import_consumer_shared)
endif()
