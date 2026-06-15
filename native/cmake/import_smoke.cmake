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
file(READ "${pkgconfig_file}" pkgconfig_static)
string(FIND "${pkgconfig_static}" "-lxray_core" pkgconfig_static_lib_index)
if(pkgconfig_static_lib_index LESS 0)
  message(FATAL_ERROR "NumberXRay static pkg-config metadata does not link xray_core.")
endif()

set(sdk_manifest_file "${install_prefix}/${XRAY_INSTALL_DATADIR}/number-xray/number-xray-sdk.json")
if(NOT EXISTS "${sdk_manifest_file}")
  message(FATAL_ERROR "NumberXRay install smoke did not install SDK manifest: ${sdk_manifest_file}")
endif()
file(READ "${sdk_manifest_file}" sdk_manifest)
foreach(expected
    "\"public\": \"number_xray.h\""
    "\"functionReferenceHeader\": \"xray_workbench.h\""
    "\"catalog\": \"number-xray-api.json\""
    "\"catalogSchemaVersion\": 2"
    "\"referenceMarkdown\": \"number-xray-api.md\""
    "\"coverage\": \"all exported XRAY_API functions\""
    "\"versionFunction\": \"xray_version\""
    "\"abiVersionFunction\": \"xray_abi_version\""
    "\"bignumBackend\""
    "\"nameFunction\": \"xray_bignum_backend_name\""
    "\"versionFunction\": \"xray_bignum_backend_version\""
    "\"libraryFunction\": \"xray_bignum_backend_library\""
    "\"cmakeTarget\": \"NumberXRay::core\""
    "\"pkgConfig\": \"number-xray\""
    "\"freeFunction\": \"xray_free\""
    "\"cmakeTarget\": \"GMP::GMP\"")
  string(FIND "${sdk_manifest}" "${expected}" expected_index)
  if(expected_index LESS 0)
    message(FATAL_ERROR "NumberXRay SDK manifest is missing expected entry: ${expected}")
  endif()
endforeach()

set(api_catalog_file "${install_prefix}/${XRAY_INSTALL_DATADIR}/number-xray/number-xray-api.json")
if(NOT EXISTS "${api_catalog_file}")
  message(FATAL_ERROR "NumberXRay install smoke did not install API catalog: ${api_catalog_file}")
endif()
file(READ "${api_catalog_file}" api_catalog)
foreach(expected
    "\"schemaVersion\": 2"
    "\"generatedFrom\": \"xray_workbench.h\""
    "\"functionCount\":"
    "\"name\": \"xray_bigint_add_decimal\""
    "\"category\": \"scratch-bigint\""
    "\"summary\": \"Add two non-negative decimal integers and return a decimal string.\""
    "\"documentation\": \"Add two non-negative decimal integers and return a decimal string."
    "\"parameters\": [{\"name\": \"left_decimal\", \"type\": \"const char *\"}, {\"name\": \"right_decimal\", \"type\": \"const char *\"}]"
    "\"ownership\": \"caller-owned:xray_free\""
    "\"name\": \"xray_bignum_backend_name\""
    "\"ownership\": \"borrowed\""
    "\"parameters\": []"
    "\"name\": \"xray_factor_solve_json\""
    "\"name\": \"xray_benchmark_run\""
    "\"name\": \"xray_workbench_run_json\"")
  string(FIND "${api_catalog}" "${expected}" expected_index)
  if(expected_index LESS 0)
    message(FATAL_ERROR "NumberXRay API catalog is missing expected entry: ${expected}")
  endif()
endforeach()
file(READ "${XRAY_SOURCE_DIR}/include/xray_workbench.h" source_header)
string(REPLACE "\r\n" "\n" source_header "${source_header}")
string(REPLACE "\r" "\n" source_header "${source_header}")
string(REPLACE ";" "\\;" source_header "${source_header}")
string(REPLACE "\n" ";" source_header_lines "${source_header}")
set(api_count 0)
foreach(line IN LISTS source_header_lines)
  string(STRIP "${line}" declaration)
  if(declaration MATCHES "^XRAY_API[ \t].*\\(")
    string(REGEX REPLACE "^XRAY_API[ \t]+" "" signature "${declaration}")
    string(REGEX REPLACE "\\(.*$" "" prefix "${signature}")
    string(REGEX REPLACE ".*[ \t\\*]([A-Za-z_][A-Za-z0-9_]*)$" "\\1" name "${prefix}")
    if(name STREQUAL prefix)
      message(FATAL_ERROR "NumberXRay import smoke could not parse API name from: ${declaration}")
    endif()
    string(FIND "${api_catalog}" "\"name\": \"${name}\"" name_index)
    if(name_index LESS 0)
      message(FATAL_ERROR "NumberXRay API catalog is missing exported function: ${name}")
    endif()
    math(EXPR api_count "${api_count} + 1")
  endif()
endforeach()
string(FIND "${api_catalog}" "\"functionCount\": ${api_count}" count_index)
if(count_index LESS 0)
  message(FATAL_ERROR "NumberXRay API catalog functionCount does not match source header count: ${api_count}")
endif()

set(api_reference_file "${install_prefix}/${XRAY_INSTALL_DATADIR}/number-xray/number-xray-api.md")
if(NOT EXISTS "${api_reference_file}")
  message(FATAL_ERROR "NumberXRay install smoke did not install API reference: ${api_reference_file}")
endif()
file(READ "${api_reference_file}" api_reference)
foreach(expected
    "# Number X-Ray C API Reference"
    "Generated from `xray_workbench.h`"
    "## Runtime"
    "### `xray_bignum_backend_name`"
    "### `xray_bigint_add_decimal`"
    "| `left_decimal` | `const char *` |"
    "Ownership: `caller-owned:xray_free`"
    "### `xray_factor_solve_json`"
    "### `xray_benchmark_run`"
    "### `xray_workbench_run_json`"
    "Function count: ${api_count}")
  string(FIND "${api_reference}" "${expected}" expected_index)
  if(expected_index LESS 0)
    message(FATAL_ERROR "NumberXRay API reference is missing expected entry: ${expected}")
  endif()
endforeach()
foreach(line IN LISTS source_header_lines)
  string(STRIP "${line}" declaration)
  if(declaration MATCHES "^XRAY_API[ \t].*\\(")
    string(REGEX REPLACE "^XRAY_API[ \t]+" "" signature "${declaration}")
    string(REGEX REPLACE "\\(.*$" "" prefix "${signature}")
    string(REGEX REPLACE ".*[ \t\\*]([A-Za-z_][A-Za-z0-9_]*)$" "\\1" name "${prefix}")
    string(FIND "${api_reference}" "### `${name}`" name_index)
    if(name_index LESS 0)
      message(FATAL_ERROR "NumberXRay API reference is missing exported function: ${name}")
    endif()
  endif()
endforeach()

if(XRAY_EXPECT_SHARED)
  set(shared_pkgconfig_file "${install_prefix}/${XRAY_INSTALL_LIBDIR}/pkgconfig/number-xray-shared.pc")
  if(NOT EXISTS "${shared_pkgconfig_file}")
    message(FATAL_ERROR "NumberXRay install smoke did not install shared pkg-config metadata: ${shared_pkgconfig_file}")
  endif()
  file(READ "${shared_pkgconfig_file}" pkgconfig_shared)
  string(FIND "${pkgconfig_shared}" "-lnumber_xray" pkgconfig_shared_lib_index)
  if(pkgconfig_shared_lib_index LESS 0)
    message(FATAL_ERROR "NumberXRay shared pkg-config metadata does not link number_xray.")
  endif()

  foreach(expected
      "\"cmakeTarget\": \"NumberXRay::core_shared\""
      "\"pkgConfig\": \"number-xray-shared\""
      "\"libDir\": \"${XRAY_INSTALL_LIBDIR}\""
      "\"binDir\": \"${XRAY_INSTALL_BINDIR}\""
      "\"languageBindings\""
      "\"pythonCtypes\""
      "\"path\": \"${XRAY_INSTALL_DATADIR}/number-xray/python/number_xray_ctypes.py\""
      "\"smokeTest\": \"${XRAY_INSTALL_DATADIR}/number-xray/python/smoke.py\""
      "\"stdlibOnly\": true"
      "\"requiresSharedLibrary\": true")
    string(FIND "${sdk_manifest}" "${expected}" expected_index)
    if(expected_index LESS 0)
      message(FATAL_ERROR "NumberXRay SDK manifest is missing expected shared entry: ${expected}")
    endif()
  endforeach()

  set(python_ctypes_file "${install_prefix}/${XRAY_INSTALL_DATADIR}/number-xray/python/number_xray_ctypes.py")
  set(python_smoke_file "${install_prefix}/${XRAY_INSTALL_DATADIR}/number-xray/python/smoke.py")
  if(NOT EXISTS "${python_ctypes_file}")
    message(FATAL_ERROR "NumberXRay install smoke did not install Python ctypes loader: ${python_ctypes_file}")
  endif()
  if(NOT EXISTS "${python_smoke_file}")
    message(FATAL_ERROR "NumberXRay install smoke did not install Python ctypes smoke test: ${python_smoke_file}")
  endif()
else()
  set(shared_pkgconfig_file "${install_prefix}/${XRAY_INSTALL_LIBDIR}/pkgconfig/number-xray-shared.pc")
  if(EXISTS "${shared_pkgconfig_file}")
    message(FATAL_ERROR "NumberXRay static-only install unexpectedly installed shared pkg-config metadata: ${shared_pkgconfig_file}")
  endif()
  string(FIND "${sdk_manifest}" "\"coreShared\"" shared_manifest_index)
  if(NOT shared_manifest_index LESS 0)
    message(FATAL_ERROR "NumberXRay static-only SDK manifest unexpectedly advertises coreShared.")
  endif()
  string(FIND "${sdk_manifest}" "\"pythonCtypes\"" python_manifest_index)
  if(NOT python_manifest_index LESS 0)
    message(FATAL_ERROR "NumberXRay static-only SDK manifest unexpectedly advertises Python ctypes binding.")
  endif()
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

  find_program(PYTHON3_EXECUTABLE NAMES python3 python)
  if(PYTHON3_EXECUTABLE)
    if(WIN32)
      set(python_path "${install_prefix}/${XRAY_INSTALL_BINDIR}")
      set(python_dll_dirs "${install_prefix}/${XRAY_INSTALL_BINDIR}")
      if(DEFINED XRAY_GMP_RUNTIME_DIR AND XRAY_GMP_RUNTIME_DIR)
        set(python_path "${python_path};${XRAY_GMP_RUNTIME_DIR}")
        set(python_dll_dirs "${python_dll_dirs};${XRAY_GMP_RUNTIME_DIR}")
      endif()
      set(python_path "${python_path};$ENV{PATH}")
      execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
          "PATH=${python_path}"
          "NUMBER_XRAY_EXTRA_DLL_DIRS=${python_dll_dirs}"
          "${PYTHON3_EXECUTABLE}" "${python_smoke_file}" "${install_prefix}"
        RESULT_VARIABLE python_result
      )
    else()
      execute_process(
        COMMAND "${PYTHON3_EXECUTABLE}" "${python_smoke_file}" "${install_prefix}"
        RESULT_VARIABLE python_result
      )
    endif()
    if(NOT python_result EQUAL 0)
      message(FATAL_ERROR "NumberXRay Python ctypes smoke failed: ${python_result}")
    endif()
  else()
    message(WARNING "Python interpreter not found; skipping NumberXRay ctypes smoke.")
  endif()
endif()
