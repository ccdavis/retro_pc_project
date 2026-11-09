# Talking retro computer firmware skeleton for the Waveshare RP2350-Zero.
#   Core 0: a stand-in for the emulated machine (writes a VT100 cell grid, sends
#           DoubleTalk bytes, forwards keys from the UART console)
#   Core 1: DECtalk / ESPK speech coprocessor + hypervisor screen reader,
#           11025 Hz PWM audio on GPIO 20 (left) and 21 (right)
# HDMI data-island audio is not implemented here (see PLAN.md).
#
#   cmake -S . -B build -G Ninja -DPICO_SDK_PATH=../../tools/pico-sdk \
#         -DPICO_TOOLCHAIN_PATH=../../tools/arm-gnu-toolchain/bin && ninja -C build
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD waveshare_rp2350_zero CACHE STRING "board")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "platform")
include(pico_sdk_import.cmake)
project(talking_retro C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

get_filename_component(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party" ABSOLUTE)
set(DT_DIR   "${THIRD_PARTY_DIR}/dectalk" CACHE PATH "DECtalk glue + build-gen")
set(DT_UP    "${DT_DIR}/dectalk-upstream/dectalk/src/dapi/src")
set(ESPK_DIR "${THIRD_PARTY_DIR}/espk" CACHE PATH "ESPK sources")
set(KLATT    "${THIRD_PARTY_DIR}/klatt" CACHE PATH "klatt_fx")
set(CORE     ${CMAKE_CURRENT_SOURCE_DIR}/../speech_core1)
set(SR       ${CMAKE_CURRENT_SOURCE_DIR}/../screen_reader)

# ---- DECtalk ARM7 engine (full dictionary) as a static library
file(STRINGS ${DT_DIR}/files.txt DT_FILES)
set(DT_SRCS "")
foreach(f ${DT_FILES})
  list(APPEND DT_SRCS ${DT_UP}/${f})
endforeach()
add_library(dectalk_arm7 STATIC ${DT_SRCS} ${DT_DIR}/glue/stubs.c ${DT_DIR}/build-gen/maindict.c)
target_compile_definitions(dectalk_arm7 PRIVATE ARM7 ENGLISH_US ACNA ARM7_NOSWI ENGLISH i386 WIN32_TEST BLD_DECTALK_DLL CHEESY_DICT_COMPRESSION)
target_include_directories(dectalk_arm7 PUBLIC ${DT_UP}/api ${DT_UP}/cmd ${DT_UP}/lts ${DT_UP}/ph ${DT_UP}/vtm ${DT_UP}/kernel ${DT_UP}/nt ${DT_UP}/include ${DT_UP}/../.. ${DT_UP}/hlsyn ${DT_DIR}/glue)
target_compile_options(dectalk_arm7 PRIVATE -w -fno-strict-aliasing -O2)

# ---- ESPK (eSpeak-NG English + klatt_fx) as a static library, data pack in flash
file(GLOB ESPK_LIB ${ESPK_DIR}/lib/*.c)
add_library(espk STATIC ${ESPK_LIB} ${ESPK_DIR}/espk/wgout.c ${ESPK_DIR}/espk/espk.c ${ESPK_DIR}/espk/stubs.c
            ${ESPK_DIR}/espk/voices_txt.c ${ESPK_DIR}/espk/ucdmini.c ${KLATT}/klatt_fx.c ${CORE}/espk_data_mem.c)
target_include_directories(espk PUBLIC ${ESPK_DIR}/lib ${ESPK_DIR}/lib/espeak-ng ${KLATT} ${ESPK_DIR}/espk ${CORE}/compat)
target_compile_options(espk PRIVATE -w -fno-strict-aliasing -O2)
target_compile_definitions(espk PRIVATE ESPK_NO_FILE_IO)

add_executable(talking_retro
  main.c
  espk_dat.S
  ${CORE}/doubletalk_core1.c
  ${CORE}/engine_dectalk.c
  ${CORE}/engine_espk.c
  ${SR}/tdsr_hypervisor_reader.c)
set_source_files_properties(espk_dat.S PROPERTIES COMPILE_DEFINITIONS "ESPK_DAT_FILE=\"${ESPK_DIR}/data/ESPK.DAT\"")
target_include_directories(talking_retro PRIVATE ${CORE} ${SR})
target_compile_definitions(talking_retro PRIVATE ARM7 ENGLISH_US ACNA ARM7_NOSWI ENGLISH i386 WIN32_TEST BLD_DECTALK_DLL CHEESY_DICT_COMPRESSION)
target_compile_options(talking_retro PRIVATE -O2 -Wall -Wno-unused-parameter)
target_link_libraries(talking_retro pico_stdlib pico_multicore hardware_pwm hardware_timer dectalk_arm7 espk)
pico_enable_stdio_uart(talking_retro 1)
pico_enable_stdio_usb(talking_retro 0)
pico_add_extra_outputs(talking_retro)
