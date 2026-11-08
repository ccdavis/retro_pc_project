# RP2350-Zero HDMI VT100 display card (Video Option 3)
#   cmake -S . -B build -G Ninja -DPICO_SDK_PATH=../../tools/pico-sdk -DPICO_TOOLCHAIN_PATH=../../tools/arm-gnu-toolchain/bin && ninja -C build
#   -> build/rp2350_hdmi_vt100_card.uf2
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD waveshare_rp2350_zero CACHE STRING "")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "")
include(${CMAKE_CURRENT_LIST_DIR}/../../tools/pico-sdk/external/pico_sdk_import.cmake)
project(rp2350_hdmi_vt100_card C CXX ASM)
pico_sdk_init()
add_executable(rp2350_hdmi_vt100_card rp2350_hdmi_vt100_card.c ../../common/hstx_dvi.c)
target_include_directories(rp2350_hdmi_vt100_card PRIVATE ../../common)
target_compile_options(rp2350_hdmi_vt100_card PRIVATE -Wall -Wextra -O2)
target_link_libraries(rp2350_hdmi_vt100_card pico_stdlib hardware_dma hardware_irq hardware_uart)
pico_add_extra_outputs(rp2350_hdmi_vt100_card)
