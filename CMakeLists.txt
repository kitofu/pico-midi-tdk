cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(pico-midi-tdk C CXX ASM)

pico_sdk_init()

add_executable(pico-midi-tdk
    pico-midi-tdk.c
    usb_descriptors.c
)

pico_set_program_name(pico-midi-tdk "pico-midi-tdk")
pico_set_program_version(pico-midi-tdk "0.1")

pico_enable_stdio_uart(pico-midi-tdk 0)
pico_enable_stdio_usb(pico-midi-tdk 0)

target_include_directories(pico-midi-tdk PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pico-midi-tdk
    pico_stdlib
    hardware_adc
    hardware_i2c
    pico_unique_id
    tinyusb_device
    tinyusb_board
)

pico_add_extra_outputs(pico-midi-tdk)


