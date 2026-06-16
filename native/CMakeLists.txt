cmake_minimum_required(VERSION 3.20)

project(NumberXRayWorkbench VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(XRAY_BUILD_GTK "Build the GTK4 desktop workbench when GTK4 headers are available" ON)
option(XRAY_BUILD_CLI "Build the command-line native workbench runner" ON)
option(XRAY_BUILD_SHARED "Build the shared Number X-Ray core library for language bindings and plugin-style consumers" ON)
option(XRAY_INSTALL "Install the reusable Number X-Ray C library package" ON)

include(GNUInstallDirs)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_path(XRAY_GMP_INCLUDE_DIR
  NAMES gmp.h
  HINTS
    "$ENV{GMP_ROOT}/include"
    "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/include"
    "$ENV{VCPKG_ROOT}/installed/x64-windows/include"
    "C:/vcpkg/installed/x64-windows/include"
)

set(XRAY_GMP_RELEASE_LIBRARY_HINTS
  "$ENV{GMP_ROOT}/lib"
  "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/lib"
  "$ENV{VCPKG_ROOT}/installed/x64-windows/lib"
  "C:/vcpkg/installed/x64-windows/lib"
)

set(XRAY_GMP_DEBUG_LIBRARY_HINTS
  "$ENV{GMP_ROOT}/debug/lib"
  "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/debug/lib"
  "$ENV{VCPKG_ROOT}/installed/x64-windows/debug/lib"
  "C:/vcpkg/installed/x64-windows/debug/lib"
)

find_library(XRAY_GMP_LIBRARY_RELEASE
  NAMES gmp mpir
  HINTS ${XRAY_GMP_RELEASE_LIBRARY_HINTS}
  NO_DEFAULT_PATH
)

if(NOT XRAY_GMP_LIBRARY_RELEASE)
  find_library(XRAY_GMP_LIBRARY_RELEASE NAMES gmp mpir)
endif()

if(WIN32 AND XRAY_GMP_LIBRARY_RELEASE)
  file(TO_CMAKE_PATH "${XRAY_GMP_LIBRARY_RELEASE}" XRAY_GMP_LIBRARY_RELEASE_NORMALIZED)
  if(XRAY_GMP_LIBRARY_RELEASE_NORMALIZED MATCHES "/debug/lib/")
    string(REPLACE "/debug/lib/" "/lib/" XRAY_GMP_LIBRARY_RELEASE_CANDIDATE "${XRAY_GMP_LIBRARY_RELEASE_NORMALIZED}")
    if(EXISTS "${XRAY_GMP_LIBRARY_RELEASE_CANDIDATE}")
      set(XRAY_GMP_LIBRARY_RELEASE "${XRAY_GMP_LIBRARY_RELEASE_CANDIDATE}" CACHE FILEPATH "Release GMP/MPIR library" FORCE)
    endif()
  endif()
endif()

find_library(XRAY_GMP_LIBRARY_DEBUG
  NAMES gmp mpir
  HINTS ${XRAY_GMP_DEBUG_LIBRARY_HINTS}
  NO_DEFAULT_PATH
)

set(XRAY_GMP_LIBRARY "${XRAY_GMP_LIBRARY_RELEASE}" CACHE FILEPATH "Release GMP/MPIR library" FORCE)

if(NOT XRAY_GMP_INCLUDE_DIR OR NOT XRAY_GMP_LIBRARY_RELEASE)
  message(FATAL_ERROR "GMP/MPIR not found. Install GMP, or on Windows install mpir:x64-windows with vcpkg.")
endif()

if(NOT TARGET GMP::GMP)
  add_library(GMP::GMP UNKNOWN IMPORTED)
  set_target_properties(GMP::GMP PROPERTIES
    IMPORTED_LOCATION "${XRAY_GMP_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_RELEASE "${XRAY_GMP_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_RELWITHDEBINFO "${XRAY_GMP_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_MINSIZEREL "${XRAY_GMP_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_NOCONFIG "${XRAY_GMP_LIBRARY_RELEASE}"
    INTERFACE_INCLUDE_DIRECTORIES "${XRAY_GMP_INCLUDE_DIR}"
  )
  if(XRAY_GMP_LIBRARY_DEBUG)
    set_target_properties(GMP::GMP PROPERTIES
      IMPORTED_LOCATION_DEBUG "${XRAY_GMP_LIBRARY_DEBUG}"
    )
  else()
    set_target_properties(GMP::GMP PROPERTIES
      IMPORTED_LOCATION_DEBUG "${XRAY_GMP_LIBRARY_RELEASE}"
    )
  endif()
endif()

set(XRAY_GMP_RUNTIME_DIR "")
set(XRAY_GMP_RUNTIME_DIR_RELEASE "")
set(XRAY_GMP_RUNTIME_DIR_DEBUG "")
if(WIN32)
  get_filename_component(XRAY_GMP_LIBRARY_DIR_RELEASE "${XRAY_GMP_LIBRARY_RELEASE}" DIRECTORY)
  get_filename_component(XRAY_GMP_PREFIX_DIR_RELEASE "${XRAY_GMP_LIBRARY_DIR_RELEASE}" DIRECTORY)
  set(XRAY_GMP_RUNTIME_DIR_RELEASE "${XRAY_GMP_PREFIX_DIR_RELEASE}/bin")
  set(XRAY_GMP_RUNTIME_DIR "${XRAY_GMP_RUNTIME_DIR_RELEASE}")
  if(XRAY_GMP_LIBRARY_DEBUG)
    get_filename_component(XRAY_GMP_LIBRARY_DIR_DEBUG "${XRAY_GMP_LIBRARY_DEBUG}" DIRECTORY)
    get_filename_component(XRAY_GMP_PREFIX_DIR_DEBUG "${XRAY_GMP_LIBRARY_DIR_DEBUG}" DIRECTORY)
    set(XRAY_GMP_RUNTIME_DIR_DEBUG "${XRAY_GMP_PREFIX_DIR_DEBUG}/bin")
  else()
    set(XRAY_GMP_RUNTIME_DIR_DEBUG "${XRAY_GMP_RUNTIME_DIR_RELEASE}")
  endif()
endif()

set(XRAY_GMP_BACKEND_NAME "GMP")
get_filename_component(XRAY_GMP_LIBRARY_BASENAME "${XRAY_GMP_LIBRARY_RELEASE}" NAME_WE)
get_filename_component(XRAY_GMP_LIBRARY_NAME "${XRAY_GMP_LIBRARY_RELEASE}" NAME)
string(TOLOWER "${XRAY_GMP_LIBRARY_BASENAME}" XRAY_GMP_LIBRARY_BASENAME_LOWER)
if(XRAY_GMP_LIBRARY_BASENAME_LOWER MATCHES "mpir")
  set(XRAY_GMP_BACKEND_NAME "MPIR")
endif()

set(XRAY_CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE 0)
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION OR CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE)
  set(XRAY_CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE 1)
endif()

set(XRAY_CORE_SOURCES
  src/core/xray_expression.c
  src/core/xray_memory.c
  src/core/xray_version.c
  src/core/xray_cpu.c
  src/core/xray_scratch_bigint.c
  src/core/xray_factor.c
  src/core/xray_cyclotomic.c
  src/core/xray_report.c
  src/core/xray_benchmark_compare.c
  src/core/xray_benchmark.c
  src/core/xray_gnfs.c
  src/core/xray_workspace.c
)

add_library(xray_core STATIC ${XRAY_CORE_SOURCES})
add_library(NumberXRay::core ALIAS xray_core)

set_target_properties(xray_core PROPERTIES
  EXPORT_NAME core
  OUTPUT_NAME xray_core
)

function(xray_configure_core_target target_name)
  target_include_directories(${target_name}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_link_libraries(${target_name} PUBLIC GMP::GMP)
  target_compile_definitions(${target_name} PRIVATE
    "XRAY_GMP_BACKEND_NAME=\"${XRAY_GMP_BACKEND_NAME}\""
    "XRAY_GMP_BACKEND_LIBRARY=\"${XRAY_GMP_LIBRARY_NAME}\""
    "XRAY_CMAKE_BUILD_CONFIG=\"$<CONFIG>\""
    "XRAY_CMAKE_GENERATOR=\"${CMAKE_GENERATOR}\""
    "XRAY_CMAKE_GENERATOR_PLATFORM=\"${CMAKE_GENERATOR_PLATFORM}\""
    "XRAY_CMAKE_GENERATOR_TOOLSET=\"${CMAKE_GENERATOR_TOOLSET}\""
    "XRAY_CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=${XRAY_CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE}"
  )

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

xray_configure_core_target(xray_core)

if(XRAY_BUILD_SHARED)
  add_library(xray_core_shared SHARED ${XRAY_CORE_SOURCES})
  add_library(NumberXRay::core_shared ALIAS xray_core_shared)
  set_target_properties(xray_core_shared PROPERTIES
    EXPORT_NAME core_shared
    OUTPUT_NAME number_xray
    C_VISIBILITY_PRESET hidden
  )
  target_compile_definitions(xray_core_shared
    PRIVATE XRAY_BUILDING_LIBRARY=1
    PUBLIC XRAY_SHARED=1
  )
  xray_configure_core_target(xray_core_shared)
endif()

if(XRAY_INSTALL)
  include(CMakePackageConfigHelpers)

  set(XRAY_INSTALL_TARGETS xray_core)
  if(TARGET xray_core_shared)
    list(APPEND XRAY_INSTALL_TARGETS xray_core_shared)
  endif()
  set(XRAY_INSTALL_EXPECT_SHARED OFF)
  if(TARGET xray_core_shared)
    set(XRAY_INSTALL_EXPECT_SHARED ON)
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/xray_workbench.h"
  )
  set(XRAY_API_CATALOG_FILE "number-xray-api.json")
  set(XRAY_API_CATALOG_PATH "${CMAKE_CURRENT_BINARY_DIR}/${XRAY_API_CATALOG_FILE}")
  set(XRAY_API_REFERENCE_FILE "number-xray-api.md")
  set(XRAY_API_REFERENCE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${XRAY_API_REFERENCE_FILE}")
  execute_process(
    COMMAND ${CMAKE_COMMAND}
      -DXRAY_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/xray_workbench.h
      -DXRAY_OUTPUT=${XRAY_API_CATALOG_PATH}
      -DXRAY_MARKDOWN_OUTPUT=${XRAY_API_REFERENCE_PATH}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_api_catalog.cmake
    RESULT_VARIABLE XRAY_API_CATALOG_RESULT
  )
  if(NOT XRAY_API_CATALOG_RESULT EQUAL 0)
    message(FATAL_ERROR "Number X-Ray API catalog generation failed: ${XRAY_API_CATALOG_RESULT}")
  endif()

  install(TARGETS ${XRAY_INSTALL_TARGETS}
    EXPORT NumberXRayTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  set(NumberXRay_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/NumberXRay")

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NumberXRayConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/NumberXRayConfig.cmake"
    INSTALL_DESTINATION "${NumberXRay_INSTALL_CMAKEDIR}"
  )

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/number-xray.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/number-xray.pc"
    @ONLY
  )

  set(XRAY_PKG_CONFIG_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/number-xray.pc"
  )

  set(XRAY_SDK_SHARED_LIBRARY_JSON "")
  set(XRAY_SDK_PYTHON_CTYPES_JSON "")
  if(TARGET xray_core_shared)
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/number-xray-shared.pc.in"
      "${CMAKE_CURRENT_BINARY_DIR}/number-xray-shared.pc"
      @ONLY
    )
    list(APPEND XRAY_PKG_CONFIG_FILES
      "${CMAKE_CURRENT_BINARY_DIR}/number-xray-shared.pc"
    )
    set(XRAY_SDK_SHARED_LIBRARY_JSON ",
    \"coreShared\": {
      \"name\": \"number_xray\",
      \"kind\": \"shared\",
      \"libDir\": \"${CMAKE_INSTALL_LIBDIR}\",
      \"binDir\": \"${CMAKE_INSTALL_BINDIR}\",
      \"cmakeTarget\": \"NumberXRay::core_shared\",
      \"pkgConfig\": \"number-xray-shared\"
    }")
    set(XRAY_SDK_PYTHON_CTYPES_JSON ",
  \"languageBindings\": {
    \"pythonCtypes\": {
      \"path\": \"${CMAKE_INSTALL_DATADIR}/number-xray/python/number_xray_ctypes.py\",
      \"smokeTest\": \"${CMAKE_INSTALL_DATADIR}/number-xray/python/smoke.py\",
      \"stdlibOnly\": true,
      \"requiresSharedLibrary\": true
    }
  }")
  endif()

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/number-xray-sdk.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/number-xray-sdk.json"
    @ONLY
  )

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/NumberXRayConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  install(EXPORT NumberXRayTargets
    NAMESPACE NumberXRay::
    DESTINATION "${NumberXRay_INSTALL_CMAKEDIR}"
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/NumberXRayConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/NumberXRayConfigVersion.cmake"
    DESTINATION "${NumberXRay_INSTALL_CMAKEDIR}"
  )

  install(FILES
    ${XRAY_PKG_CONFIG_FILES}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/number-xray-sdk.json"
    "${XRAY_API_CATALOG_PATH}"
    "${XRAY_API_REFERENCE_PATH}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/number-xray"
  )

  if(TARGET xray_core_shared)
    install(FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_ctypes/number_xray_ctypes.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_ctypes/smoke.py"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/number-xray/python"
    )
  endif()
endif()

if(XRAY_BUILD_CLI)
  add_executable(xray_cli src/cli/main.c)
  target_link_libraries(xray_cli PRIVATE xray_core)
endif()

include(CTest)
if(BUILD_TESTING)
  add_test(NAME xray_api_doc_coverage
    COMMAND ${CMAKE_COMMAND}
      -DXRAY_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/xray_workbench.h
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/api_doc_coverage.cmake
  )

  add_executable(xray_native_tests tests/native_tests.c)
  target_link_libraries(xray_native_tests PRIVATE xray_core)
  add_test(NAME xray_native_tests COMMAND xray_native_tests)
  if(TARGET xray_cli)
    add_test(NAME xray_cli_help COMMAND xray_cli --help)
    set_tests_properties(xray_cli_help PROPERTIES
      PASS_REGULAR_EXPRESSION "--bench-frontier"
    )
  endif()
  if(XRAY_INSTALL)
    add_test(NAME xray_import_consumer
      COMMAND ${CMAKE_COMMAND}
        -DXRAY_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DXRAY_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DXRAY_CONFIG=$<CONFIG>
        -DXRAY_GENERATOR=${CMAKE_GENERATOR}
        -DXRAY_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
        -DXRAY_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
        -DXRAY_EXPECT_SHARED=${XRAY_INSTALL_EXPECT_SHARED}
        -DXRAY_GMP_RUNTIME_DIR=${XRAY_GMP_RUNTIME_DIR}
        -DXRAY_INSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}
        -DXRAY_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DXRAY_INSTALL_DATADIR=${CMAKE_INSTALL_DATADIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/import_smoke.cmake
    )
  endif()
endif()

if(XRAY_BUILD_GTK)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GTK4 QUIET gtk4)
  endif()

  if(GTK4_FOUND)
    add_executable(xray_workbench
      src/gui/main.c
      src/gui/workbench.c
    )
    if(WIN32)
      set_target_properties(xray_workbench PROPERTIES WIN32_EXECUTABLE TRUE)
      target_link_libraries(xray_workbench PRIVATE shell32)
    endif()
    target_include_directories(xray_workbench PRIVATE ${GTK4_INCLUDE_DIRS})
    target_link_directories(xray_workbench PRIVATE ${GTK4_LIBRARY_DIRS})
    target_link_libraries(xray_workbench PRIVATE xray_core ${GTK4_LIBRARIES})
    target_compile_options(xray_workbench PRIVATE ${GTK4_CFLAGS_OTHER})
  else()
    message(WARNING "GTK4 was not found; xray_workbench GUI target will be skipped. Core, CLI, and tests still build.")
  endif()
endif()
