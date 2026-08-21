# WiiU Bridge Plugin Makefile
# Requires devkitPPC and WUPS

TARGET = wiiu_bridge_plugin

SOURCES = main_v2.cpp bridge.c bridge_udp.c dsu_server.c dsu_crc32.c discovery.c

INCLUDES = -I.

LIBS = -lwups -lwut -lnsysnet -lpadscore -lcoreinit

CXX = powerpc-eabi-g++
CC = powerpc-eabi-gcc

CFLAGS = -mogc -mcpu=750 -meabi -msdata=eabi \
         -fno-exceptions -fno-rtti \
         -g -O2 -Wall \
         $(INCLUDES)

CXXFLAGS = $(CFLAGS)

LDFLAGS = -nodefaultlibs -nostartfiles \
          -T $(DEVKITPPC)/powerpc-eabi/lib/link.ld

BUILD_DIR = build

OBJECTS_C = $(addprefix $(BUILD_DIR)/, $(filter %.c, $(SOURCES:.c=.o)))
OBJECTS_CPP = $(addprefix $(BUILD_DIR)/, $(filter %.cpp, $(SOURCES:.cpp=.o)))
OBJECTS = $(OBJECTS_C) $(OBJECTS_CPP)

all: $(TARGET).wps

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET).wps: $(OBJECTS)
	$(CXX) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) -o $@
	@echo "Built: $@"

clean:
	rm -rf $(BUILD_DIR) $(TARGET).wps

install: $(TARGET).wps
	@echo "Copy $(TARGET).wps to your SD card:"
	@echo "  /wiiu/environments/aroma/plugins/"

.PHONY: all clean install
