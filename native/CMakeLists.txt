cmake_minimum_required(VERSION 3.20)
project(wave_collector LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(zw_acq src/main.cpp)
if(MSVC)
  target_compile_options(zw_acq PRIVATE /W4 /EHsc /utf-8 /O2)
  target_compile_definitions(zw_acq PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
else()
  target_compile_options(zw_acq PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()
if(WIN32)
  set(HANTEK_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/hantek")
  if(NOT EXISTS "${HANTEK_SDK_DIR}/include/HTHardDll.h" OR
     NOT EXISTS "${HANTEK_SDK_DIR}/include/DefMacro.h" OR
     NOT EXISTS "${HANTEK_SDK_DIR}/lib/x64/HTHardDll.lib" OR
     NOT EXISTS "${HANTEK_SDK_DIR}/lib/x64/HTSoftDll.lib")
    message(FATAL_ERROR "Missing HT6004BX SDK headers/import libraries: vendor/hantek/include and vendor/hantek/lib/x64")
  endif()
  target_include_directories(zw_acq PRIVATE "${HANTEK_SDK_DIR}/include")
  target_link_libraries(zw_acq PRIVATE
    kernel32
    "${HANTEK_SDK_DIR}/lib/x64/HTHardDll.lib"
    "${HANTEK_SDK_DIR}/lib/x64/HTSoftDll.lib")
endif()