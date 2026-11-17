cmake_minimum_required(VERSION 3.20)
project(jam-midi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR})
endforeach()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# The driver, minus its entry point: the platform-neutral core plus the Windows HID and MIDI layers.
add_library(jamcore STATIC
  src/jam.cpp
  src/hid_win.cpp
  src/midi.cpp
  src/midi_tevirtual.cpp
  src/midi_winmm.cpp
)
target_include_directories(jamcore PUBLIC src)
target_link_libraries(jamcore PUBLIC setupapi hid winmm)

add_executable(jam-midi src/main.cpp)
target_link_libraries(jam-midi PRIVATE jamcore)

# Test tools, the same two as on macOS.
add_executable(jam-send tools/jam-send.cpp)
target_link_libraries(jam-send PRIVATE winmm)

add_executable(strip-test tools/strip-test.cpp)
target_link_libraries(strip-test PRIVATE jamcore)
