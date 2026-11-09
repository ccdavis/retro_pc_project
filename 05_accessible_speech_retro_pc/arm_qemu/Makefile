# Core 1 speech coprocessor on QEMU's Cortex-M33 board (mps2-an505), built with the
# same CPU flags the Pico SDK uses for the RP2350 (rp2350-arm-s).
#   make        build sim_arm.elf and print section sizes per component
#   make run    run under QEMU with instruction counting; WAVs land in out/
TOOLS    ?= $(abspath ../../tools)
CC       := $(TOOLS)/arm-gnu-toolchain/bin/arm-none-eabi-gcc
SIZE     := $(TOOLS)/arm-gnu-toolchain/bin/arm-none-eabi-size
QEMU     := $(TOOLS)/qemu/usr/bin/qemu-system-arm -L $(TOOLS)/qemu/usr/share/qemu
DT_DIR   ?= $(abspath ../third_party/dectalk)
DT_UP    ?= $(DT_DIR)/dectalk-upstream/dectalk/src/dapi/src
ESPK_DIR ?= $(abspath ../third_party/espk)
KLATT    ?= $(abspath ../third_party/klatt)
CORE  := ../speech_core1
SR    := ../screen_reader
B     := build

ARCH   = -mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp
COMMON = $(ARCH) -O2 -g -ffunction-sections -fdata-sections -fno-strict-aliasing
CFLAGS = $(COMMON) -std=gnu11 -Wall -Wextra -I$(CORE) -I$(SR)
LDFLAGS = $(ARCH) -T mps2_an505.ld -nostartfiles --specs=rdimon.specs -Wl,--gc-sections -Wl,-Map=$(B)/sim_arm.map -Wl,--print-memory-usage

DT_SRCS  = $(shell cat $(DT_DIR)/files.txt)
DT_DEFS  = -DARM7 -DENGLISH_US -DACNA -DARM7_NOSWI -DENGLISH -Di386 -DWIN32_TEST -DBLD_DECTALK_DLL -DCHEESY_DICT_COMPRESSION
DT_INC   = -I$(DT_UP)/api -I$(DT_UP)/cmd -I$(DT_UP)/lts -I$(DT_UP)/ph -I$(DT_UP)/vtm -I$(DT_UP)/kernel -I$(DT_UP)/nt -I$(DT_UP)/include -I$(DT_UP)/../.. -I$(DT_UP)/hlsyn -I$(DT_DIR)/glue
DT_CFLAGS = $(COMMON) -w $(DT_DEFS) $(DT_INC)
vpath %.c $(sort $(dir $(addprefix $(DT_UP)/,$(DT_SRCS)))) $(ESPK_DIR)/lib $(ESPK_DIR)/espk $(KLATT) $(CORE) $(SR)
DT_OBJS  = $(patsubst %.c,$(B)/dt/%.o,$(notdir $(DT_SRCS))) $(B)/dt/maindict.o $(B)/dt/dt_stubs.o

ESPK_LIB  = $(wildcard $(ESPK_DIR)/lib/*.c)
ESPK_SRC  = $(ESPK_DIR)/espk/wgout.c $(ESPK_DIR)/espk/espk.c $(ESPK_DIR)/espk/voices_txt.c $(ESPK_DIR)/espk/ucdmini.c $(KLATT)/klatt_fx.c
ESPK_INC  = -I$(ESPK_DIR)/lib -I$(ESPK_DIR)/lib/espeak-ng -I$(KLATT) -I$(ESPK_DIR)/espk
ESPK_CFLAGS = $(COMMON) -w $(ESPK_INC) -I$(CORE)/compat
ESPK_OBJS = $(patsubst %.c,$(B)/espk/%.o,$(notdir $(ESPK_LIB) $(ESPK_SRC))) $(B)/espk/espk_stubs.o

CORE_OBJS = $(B)/core/doubletalk_core1.o $(B)/core/tdsr_hypervisor_reader.o $(B)/core/engine_dectalk.o $(B)/core/engine_espk.o $(B)/core/espk_data_mem.o
HARNESS_OBJS = $(B)/startup.o $(B)/main.o $(B)/espk_dat.o

all: $(B)/sim_arm.elf sizes

$(B)/sim_arm.elf: $(HARNESS_OBJS) $(CORE_OBJS) $(DT_OBJS) $(ESPK_OBJS) mps2_an505.ld
	$(CC) $(LDFLAGS) -o $@ $(HARNESS_OBJS) $(CORE_OBJS) $(DT_OBJS) $(ESPK_OBJS) -lm

$(B)/dt/%.o: %.c | $(B)/dt
	$(CC) $(DT_CFLAGS) -c $< -o $@
$(B)/dt/maindict.o: $(DT_DIR)/build-gen/maindict.c | $(B)/dt
	$(CC) $(DT_CFLAGS) -c $< -o $@
$(B)/dt/dt_stubs.o: $(DT_DIR)/glue/stubs.c | $(B)/dt
	$(CC) $(DT_CFLAGS) -c $< -o $@
$(B)/espk/%.o: %.c | $(B)/espk
	$(CC) $(ESPK_CFLAGS) -c $< -o $@
$(B)/espk/espk_stubs.o: $(ESPK_DIR)/espk/stubs.c | $(B)/espk
	$(CC) $(ESPK_CFLAGS) -c $< -o $@
$(B)/core/doubletalk_core1.o: $(CORE)/doubletalk_core1.c | $(B)/core
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/core/tdsr_hypervisor_reader.o: $(SR)/tdsr_hypervisor_reader.c | $(B)/core
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/core/engine_dectalk.o: $(CORE)/engine_dectalk.c | $(B)/core
	$(CC) $(CFLAGS) -Wno-unused-parameter $(DT_DEFS) $(DT_INC) -c $< -o $@
$(B)/core/engine_espk.o: $(CORE)/engine_espk.c | $(B)/core
	$(CC) $(CFLAGS) $(ESPK_INC) -c $< -o $@
$(B)/core/espk_data_mem.o: $(CORE)/espk_data_mem.c | $(B)/core
	$(CC) $(CFLAGS) $(ESPK_INC) -DESPK_NO_FILE_IO -c $< -o $@
$(B)/startup.o: startup.c | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/main.o: main.c | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/espk_dat.o: espk_dat.S $(ESPK_DIR)/data/ESPK.DAT | $(B)
	$(CC) $(ARCH) -DESPK_DAT_FILE='"$(ESPK_DIR)/data/ESPK.DAT"' -c $< -o $@

$(B) $(B)/dt $(B)/espk $(B)/core:
	mkdir -p $@

# per-component section totals: what the plan's flash and SRAM budget should say
sizes: $(B)/sim_arm.elf
	@echo "--- section sizes (bytes): text  data  bss ---"
	@echo -n "DECtalk engine code      : "; $(SIZE) -t $(filter-out $(B)/dt/maindict.o,$(DT_OBJS)) | tail -1 | awk '{printf "%8d %6d %8d\n",$$1,$$2,$$3}'
	@echo -n "DECtalk full dictionary  : "; $(SIZE) $(B)/dt/maindict.o | tail -1 | awk '{printf "%8d %6d %8d\n",$$1,$$2,$$3}'
	@echo -n "ESPK code + tables       : "; $(SIZE) -t $(ESPK_OBJS) | tail -1 | awk '{printf "%8d %6d %8d\n",$$1,$$2,$$3}'
	@echo -n "ESPK.DAT                 : "; $(SIZE) $(B)/espk_dat.o | tail -1 | awk '{printf "%8d %6d %8d\n",$$1,$$2,$$3}'
	@echo -n "Core 1 speech core + SR  : "; $(SIZE) -t $(CORE_OBJS) | tail -1 | awk '{printf "%8d %6d %8d\n",$$1,$$2,$$3}'
	@echo -n "whole image              : "; $(SIZE) $(B)/sim_arm.elf | tail -1 | awk '{printf "%8d %6d %8d\n",$$1,$$2,$$3}'

run: $(B)/sim_arm.elf
	mkdir -p out && cd out && timeout 600 $(QEMU) -M mps2-an505 -cpu cortex-m33 -nographic -monitor none -serial null \
	    -semihosting-config enable=on,target=native -icount shift=0,sleep=off -kernel $(abspath $(B)/sim_arm.elf)

clean:
	rm -rf $(B) out
.PHONY: all sizes run clean
