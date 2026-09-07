# Build for the Raspberry Pi Pico / Pico 2 (RP2040 / RP2350).
#
#   export PICO_SDK_PATH=/path/to/pico-sdk
#   cmake -B build -S . -DPICO_BOARD=pico       # or pico2 / pico_w
#   cmake --build build
#   # then copy build/drumlogue_midi_translator.uf2 to the Pico (BOOTSEL)
#
# Default target is DIN-only (one UART), which needs only the Pico SDK.
# To also host a USB-MIDI controller: set USE_USB_HOST_IN to 1 in config.h AND
# uncomment the TinyUSB section below.

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(drumlogue_midi_translator C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(drumlogue_midi_translator
    main.c
    translator.c
    midi_parse.c
    config.c
)

target_include_directories(drumlogue_midi_translator PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# Default (DIN only): just the standard library.
target_link_libraries(drumlogue_midi_translator pico_stdlib)

# ---- USB host (optional) --------------------------------------------------
# Uncomment to host a USB-MIDI controller (with USE_USB_HOST_IN = 1 in config.h).
# target_link_libraries(drumlogue_midi_translator tinyusb_host tinyusb_board)
# ---------------------------------------------------------------------------

# We drive DIN MIDI on a UART, not over USB-CDC stdio.
pico_enable_stdio_usb(drumlogue_midi_translator 0)
pico_enable_stdio_uart(drumlogue_midi_translator 0)

pico_add_extra_outputs(drumlogue_midi_translator)   # .uf2, .hex, .bin, .elf
