cmake_minimum_required(VERSION 3.20)

project(NumberXRayWorkbench C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(XRAY_BUILD_GTK "Build the GTK4 desktop workbench when GTK4 headers are available" ON)
option(XRAY_BUILD_CLI "Build the command-line native workbench runner" ON)

find_path(XRAY_GMP_INCLUDE_DIR
  NAMES gmp.h
  HINTS
    "$ENV{GMP_ROOT}/include"
    "$ENV{VCPKG_ROOT}/installed/x64-windows/include"
    "C:/vcpkg/installed/x64-windows/include"
)

find_library(XRAY_GMP_LIBRARY
  NAMES gmp mpir
  HINTS
    "$ENV{GMP_ROOT}/lib"
    "$ENV{VCPKG_ROOT}/installed/x64-windows/lib"
    "C:/vcpkg/installed/x64-windows/lib"
)

if(NOT XRAY_GMP_INCLUDE_DIR OR NOT XRAY_GMP_LIBRARY)
  message(FATAL_ERROR "GMP/MPIR not found. Install GMP, or on Windows install mpir:x64-windows with vcpkg.")
endif()

add_library(xray_core STATIC
  src/core/xray_factor.c
  src/core/xray_cyclotomic.c
  src/core/xray_report.c
  src/core/xray_benchmark.c
)

target_include_directories(xray_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${XRAY_GMP_INCLUDE_DIR}
)

target_link_libraries(xray_core PUBLIC ${XRAY_GMP_LIBRARY})

if(MSVC)
  target_compile_options(xray_core PRIVATE /W4)
else()
  target_compile_options(xray_core PRIVATE -Wall -Wextra -Wpedantic)
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
