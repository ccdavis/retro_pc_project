# Host test harness for the Core 1 speech coprocessor and hypervisor screen reader.
#
#   make            build sim_core1 with every engine whose sources are present
#   make run        run it: tests + WAV files in out/
#   make ENGINES=   build with the tone voice only (no DECtalk / ESPK sources needed)
#
# Engine sources live under ../third_party/{dectalk,espk,klatt}:
DT_DIR   ?= $(abspath ../third_party/dectalk)
DT_UP    ?= $(DT_DIR)/dectalk-upstream/dectalk/src/dapi/src
ESPK_DIR ?= $(abspath ../third_party/espk)
KLATT    ?= $(abspath ../third_party/klatt)

.DEFAULT_GOAL := all
TOP   := ..
CORE  := $(TOP)/speech_core1
SR    := $(TOP)/screen_reader
B     := build
CC    ?= gcc
CFLAGS_BASE = -O2 -g -std=gnu11 -Wall -Wextra -funsigned-char -I$(CORE) -I$(SR)

HAVE_DT   := $(shell test -f $(DT_UP)/api/epsonapi.c && test -f $(DT_DIR)/build-gen/maindict.c && echo 1)
HAVE_ESPK := $(shell test -f $(ESPK_DIR)/espk/espk.c && test -f $(KLATT)/klatt_fx.c && echo 1)
ENGINES   ?= $(if $(HAVE_DT),dectalk) $(if $(HAVE_ESPK),espk)

OBJS = $(B)/doubletalk_core1.o $(B)/tdsr_hypervisor_reader.o $(B)/sim_core1.o
DEFS =

# ---- DECtalk ARM7 configuration (flags from accessible_os/freedos/src/dectalk/Makefile)
ifneq ($(filter dectalk,$(ENGINES)),)
DT_SRCS  = $(shell cat $(DT_DIR)/files.txt)
DT_DEFS  = -DARM7 -DENGLISH_US -DACNA -DARM7_NOSWI -DENGLISH -Di386 -DWIN32_TEST -DBLD_DECTALK_DLL -DCHEESY_DICT_COMPRESSION
DT_INC   = -I$(DT_UP)/api -I$(DT_UP)/cmd -I$(DT_UP)/lts -I$(DT_UP)/ph -I$(DT_UP)/vtm -I$(DT_UP)/kernel -I$(DT_UP)/nt -I$(DT_UP)/include -I$(DT_UP)/../.. -I$(DT_UP)/hlsyn -I$(DT_DIR)/glue
DT_CFLAGS = -O2 -g -w -fno-strict-aliasing -U__linux__ -Ulinux -U__gnu_linux__ $(DT_DEFS) $(DT_INC)
vpath %.c $(sort $(dir $(addprefix $(DT_UP)/,$(DT_SRCS)))) $(DT_DIR)/glue $(DT_DIR)/build-gen
DT_OBJS  = $(patsubst %.c,$(B)/dt/%.o,$(notdir $(DT_SRCS)) maindict.c) $(B)/dt/dt_stubs.o
OBJS += $(B)/engine_dectalk.o $(DT_OBJS)
DEFS += -DHAVE_DECTALK
$(B)/dt/%.o: %.c | $(B)/dt
	$(CC) $(DT_CFLAGS) -c $< -o $@
# the full dictionary from build-gen, not the 4809-word lts/maindict.c subset
$(B)/dt/maindict.o: $(DT_DIR)/build-gen/maindict.c | $(B)/dt
	$(CC) $(DT_CFLAGS) -c $< -o $@
$(B)/dt/dt_stubs.o: $(DT_DIR)/glue/stubs.c | $(B)/dt
	$(CC) $(DT_CFLAGS) -c $< -o $@
$(B)/engine_dectalk.o: $(CORE)/engine_dectalk.c $(CORE)/doubletalk_core1.h | $(B)
	$(CC) $(CFLAGS_BASE) -Wno-unused-parameter $(DT_DEFS) $(DT_INC) -c $< -o $@
endif

# ---- ESPK: eSpeak-NG English front end + klatt_fx (flags from vintage-pc-speech/src/espeak/Makefile)
ifneq ($(filter espk,$(ENGINES)),)
ESPK_LIB  = $(wildcard $(ESPK_DIR)/lib/*.c)
ESPK_SRC  = $(ESPK_DIR)/espk/wgout.c $(ESPK_DIR)/espk/espk.c $(ESPK_DIR)/espk/voices_txt.c $(ESPK_DIR)/espk/ucdmini.c $(KLATT)/klatt_fx.c
ESPK_INC  = -I$(ESPK_DIR)/lib -I$(ESPK_DIR)/lib/espeak-ng -I$(KLATT) -I$(ESPK_DIR)/espk
ESPK_CFLAGS = -O2 -g -w -fno-strict-aliasing $(ESPK_INC)
ESPK_OBJS = $(patsubst %.c,$(B)/espk/%.o,$(notdir $(ESPK_LIB) $(ESPK_SRC))) $(B)/espk/espk_stubs.o
vpath %.c $(ESPK_DIR)/lib $(ESPK_DIR)/espk $(KLATT)
OBJS += $(B)/engine_espk.o $(B)/espk_data_mem.o $(ESPK_OBJS)
DEFS += -DHAVE_ESPK -DESPK_DAT_PATH='"$(ESPK_DIR)/data/ESPK.DAT"'
$(B)/espk/%.o: %.c | $(B)/espk
	$(CC) $(ESPK_CFLAGS) -c $< -o $@
$(B)/espk/espk_stubs.o: $(ESPK_DIR)/espk/stubs.c | $(B)/espk
	$(CC) $(ESPK_CFLAGS) -c $< -o $@
$(B)/engine_espk.o: $(CORE)/engine_espk.c $(CORE)/doubletalk_core1.h | $(B)
	$(CC) $(CFLAGS_BASE) $(ESPK_INC) -DESPK_DAT_PATH='"$(ESPK_DIR)/data/ESPK.DAT"' -c $< -o $@
$(B)/espk_data_mem.o: $(CORE)/espk_data_mem.c | $(B)
	$(CC) $(CFLAGS_BASE) $(ESPK_INC) -c $< -o $@
endif

all: sim_core1

sim_core1: $(OBJS)
	$(CC) -o $@ $(OBJS) -lm

$(B)/doubletalk_core1.o: $(CORE)/doubletalk_core1.c $(CORE)/doubletalk_core1.h | $(B)
	$(CC) $(CFLAGS_BASE) -c $< -o $@
$(B)/tdsr_hypervisor_reader.o: $(SR)/tdsr_hypervisor_reader.c $(SR)/tdsr_hypervisor_reader.h $(CORE)/doubletalk_core1.h | $(B)
	$(CC) $(CFLAGS_BASE) -c $< -o $@
$(B)/sim_core1.o: sim_core1.c $(CORE)/doubletalk_core1.h $(SR)/tdsr_hypervisor_reader.h | $(B)
	$(CC) $(CFLAGS_BASE) $(DEFS) -c $< -o $@

$(B) $(B)/dt $(B)/espk:
	mkdir -p $@

run: sim_core1
	mkdir -p out && ./sim_core1 out

clean:
	rm -rf $(B) sim_core1 out
.PHONY: all run clean
