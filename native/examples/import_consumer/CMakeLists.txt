cmake_minimum_required(VERSION 3.20)

project(NumberXRayImportConsumer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(NumberXRay CONFIG REQUIRED)

add_executable(number_xray_import_consumer main.c)
target_link_libraries(number_xray_import_consumer PRIVATE NumberXRay::core)
