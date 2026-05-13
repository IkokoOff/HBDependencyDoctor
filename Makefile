#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
TARGET          := HBDependencyDoctor
BUILD           := build
SOURCES         := source
DATA            :=
INCLUDES        :=
ROMFS           :=

RES             := $(TOPDIR)/res
OUT             := $(TOPDIR)/out

APP_TITLE       := HB Dependency Doctor
APP_DESCRIPTION := 3DS Homebrew Health Analyser
APP_AUTHOR      := Ikoko

#---------------------------------------------------------------------------------
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS := -g -Wall -Wextra -O2 -mword-relocations \
           -fomit-frame-pointer -ffunction-sections \
           $(ARCH)
CFLAGS += $(INCLUDE) -D__3DS__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT  := $(CURDIR)/out/$(TARGET)
export TOPDIR  := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD := $(CC)

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
    export APP_ICON := $(RES)/icon.png
else
    export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
    export _3DSXFLAGS += --smdh=$(OUT)/$(TARGET).smdh
endif

.PHONY: $(BUILD) clean all cia

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@mkdir -p $(OUT)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# ── CIA build ────────────────────────────────────────────────────────────────
BANNERTOOL := bannertool
MAKEROM    := makerom
RSF        := $(RES)/$(TARGET).rsf

APP_PRODUCT_CODE := CTR-H-HBDD
APP_UNIQUE_ID    := 0xFBDD0

$(OUT)/$(TARGET).bnr: $(RES)/banner.png $(RES)/audio.wav
	@echo "  Creating banner..."
	@$(BANNERTOOL) makebanner -i $(RES)/banner.png -a $(RES)/audio.wav -o $(OUT)/$(TARGET).bnr

$(OUT)/$(TARGET).icn: $(RES)/icon.png
	@echo "  Creating icon..."
	@$(BANNERTOOL) makesmdh \
		-s "$(APP_TITLE)" \
		-l "$(APP_DESCRIPTION)" \
		-p "$(APP_AUTHOR)" \
		-i $(RES)/icon.png \
		-o $(OUT)/$(TARGET).icn

cia: all $(OUT)/$(TARGET).bnr $(OUT)/$(TARGET).icn
	@echo "  Building CIA..."
	@$(MAKEROM) -f cia \
		-o $(OUT)/$(TARGET).cia \
		-rsf $(RSF) \
		-target t \
		-exefslogo \
		-elf $(OUTPUT).elf \
		-icon $(OUT)/$(TARGET).icn \
		-banner $(OUT)/$(TARGET).bnr \
		-major 1 -minor 0 -micro 0 \
		-DAPP_TITLE="$(APP_TITLE)" \
		-DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" \
		-DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)" \
		-DAPP_ENCRYPTED=false \
		-DAPP_SYSTEM_MODE=64MB \
		-DAPP_SYSTEM_MODE_EXT=Legacy \
		-DAPP_CATEGORY=Application \
		-DAPP_USE_ON_SD=true \
		-DAPP_MEMORY_TYPE=Application \
		-DAPP_CPU_SPEED=268MHz \
		-DAPP_ENABLE_L2_CACHE=false \
		-DAPP_VERSION_MAJOR=1
	@echo "  Done: $(OUT)/$(TARGET).cia"

clean:
	@echo Cleaning...
	@rm -fr $(BUILD) $(OUT)

else
#---------------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).3dsx

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUT)/$(TARGET).smdh
$(OUTPUT).elf:  $(OFILES)
$(OFILES_SRC):  $(HFILES_BIN)

-include $(DEPENDS)

endif
