cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(treblesynth C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=2)

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        )

set(EXECUTABLE_OUTPUT_PATH "build")

# Add the examples
add_subdirectory(treblesynth)

set_target_properties(treblesynth PROPERTIES COMPILE_FLAGS "--save-temps")

pico_set_boot_stage2(treblesynth slower_boot2)
