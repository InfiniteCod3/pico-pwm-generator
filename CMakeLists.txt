cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(pico-notw C CXX ASM)

pico_sdk_init()

add_executable(pico-notw pico-notw.c oled.c)
pico_set_program_name(pico-notw "pico-notw")
pico_set_program_version(pico-notw "0.1")
pico_enable_stdio_uart(pico-notw 0)
pico_enable_stdio_usb(pico-notw 1)
target_link_libraries(pico-notw pico_stdlib hardware_adc hardware_pwm hardware_i2c)
pico_add_extra_outputs(pico-notw)
