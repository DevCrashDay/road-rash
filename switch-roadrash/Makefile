ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := roadrash_switch
BUILD       := build
SOURCES     := source
INCLUDES    := include source
ROMFS       := romfs

APP_TITLE   := Road Rash (30th Anniversary)
APP_AUTHOR  := Homebrew
APP_VERSION := 1.0.0
APP_ICON    := icon.jpg

# Architecture flags for Tegra X1 (Cortex-A57 64-bit)
ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS      := -g -O2 -ffunction-sections $(ARCH) -D__SWITCH__ \
               -I$(DEVKITPRO)/libnx/include \
               -I$(DEVKITPRO)/portlibs/switch/include \
               -Isource -Iinclude

CXXFLAGS    := $(CFLAGS) -std=gnu++20 -fno-rtti

LIBPATHS    := -L$(DEVKITPRO)/libnx/lib -L$(DEVKITPRO)/portlibs/switch/lib
LIBS        := -lSDL2 -lEGL -lglapi -ldrm_nouveau -ldeko3d -lnx -lpthread -lm

OFILES      := $(BUILD)/switch_main.o $(BUILD)/switch_input.o $(BUILD)/graphics_backend.o $(BUILD)/splitscreen.o

all: $(TARGET).nro

$(BUILD):
	@mkdir -p $(BUILD)

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
	@elf2nro $< $@ --icon=$(APP_ICON) --nacp=$(TARGET).nacp --romfsdir=$(ROMFS)
	@echo "Built $(TARGET).nro successfully!"

$(TARGET).elf: $(BUILD) $(OFILES)
	@echo linking $(TARGET).elf
	@$(CXX) -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(BUILD)/%.o: source/%.cpp
	@echo compiling $<
	@$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp
