cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(treblesynth C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        )

set(EXECUTABLE_OUTPUT_PATH "build")

# Add the examples
add_subdirectory(treblesynth)

