add_executable(treblesynth
    src/main.cpp
    src/ssd1306_i2c.c
    src/buttons.c
    src/dsp.c
    src/ui.c
    src/pitch.c
    src/tinycl.cpp
    src/usbmain.c
    src/usb_descriptors.c
)

target_include_directories(treblesynth PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(treblesynth
    pico_stdlib
    hardware_pio
    hardware_dma
    pico_multicore
    hardware_interp
    hardware_pwm
    hardware_adc
    hardware_i2c
    hardware_flash
    tinyusb_device
    tinyusb_board
    )

# create map/bin/hex file etc.
pico_add_extra_outputs(treblesynth)

# Allow stdio to USB
pico_enable_stdio_usb(treblesynth 0)
