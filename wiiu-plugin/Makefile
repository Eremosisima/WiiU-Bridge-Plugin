ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO")
endif
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/wups/share/wups_rules
WUT_ROOT := $(DEVKITPRO)/wut
TARGET = wiiu_bridge_plugin
BUILD = build
SOURCES = .
CFILES := dsu_crc32.c dsu_server.c discovery.c bridge.c bridge_udp.c
CPPFILES := main_v2.cpp
CFLAGS := -g -Wall -O2 -ffunction-sections $(MACHDEP)
CFLAGS += $(INCLUDE) -D__WIIU__ -D__WUT__ -D__WUPS__
CXXFLAGS := $(CFLAGS) -std=c++20
LDFLAGS = -g $(MACHDEP) $(RPXSPECS) -Wl,-Map,$(notdir $*.map) $(WUPSSPECS)
LIBS := -lwups -lwut
LIBDIRS := $(PORTLIBS) $(WUPS_ROOT) $(WUT_ROOT)
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES := $(OFILES_SRC)
export INCLUDE := $(foreach dir,$(SOURCES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
.PHONY: $(BUILD) clean all
all: $(BUILD)
$(BUILD):
	@mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(TARGET).wps $(TARGET).elf
else
.PHONY: all
DEPENDS := $(OFILES:.o=.d)
all : $(OUTPUT).wps
$(OUTPUT).wps : $(OUTPUT).elf
$(OUTPUT).elf : $(OFILES)
-include $(DEPENDS)
endif
