cmake_minimum_required(VERSION 3.20)

project(NumberXRayWorkbench VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(XRAY_BUILD_GTK "Build the GTK4 desktop workbench when GTK4 headers are available" ON)
option(XRAY_BUILD_CLI "Build the command-line native workbench runner" ON)
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

find_library(XRAY_GMP_LIBRARY
  NAMES gmp mpir
  HINTS
    "$ENV{GMP_ROOT}/lib"
    "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/lib"
    "$ENV{VCPKG_ROOT}/installed/x64-windows/lib"
    "C:/vcpkg/installed/x64-windows/lib"
)

if(NOT XRAY_GMP_INCLUDE_DIR OR NOT XRAY_GMP_LIBRARY)
  message(FATAL_ERROR "GMP/MPIR not found. Install GMP, or on Windows install mpir:x64-windows with vcpkg.")
endif()

if(NOT TARGET GMP::GMP)
  add_library(GMP::GMP UNKNOWN IMPORTED)
  set_target_properties(GMP::GMP PROPERTIES
    IMPORTED_LOCATION "${XRAY_GMP_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${XRAY_GMP_INCLUDE_DIR}"
  )
endif()

add_library(xray_core STATIC
  src/core/xray_expression.c
  src/core/xray_cpu.c
  src/core/xray_scratch_bigint.c
  src/core/xray_factor.c
  src/core/xray_cyclotomic.c
  src/core/xray_report.c
  src/core/xray_benchmark.c
  src/core/xray_gnfs.c
  src/core/xray_workspace.c
)

add_library(NumberXRay::core ALIAS xray_core)

set_target_properties(xray_core PROPERTIES
  EXPORT_NAME core
  OUTPUT_NAME xray_core
)

target_include_directories(xray_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(xray_core PUBLIC GMP::GMP)

if(MSVC)
  target_compile_options(xray_core PRIVATE /W4)
else()
  target_compile_options(xray_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(XRAY_INSTALL)
  include(CMakePackageConfigHelpers)

  install(TARGETS xray_core
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
endif()

if(XRAY_BUILD_CLI)
  add_executable(xray_cli src/cli/main.c)
  target_link_libraries(xray_cli PRIVATE xray_core)
endif()

include(CTest)
if(BUILD_TESTING)
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
