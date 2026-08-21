cmake_minimum_required(VERSION 3.10)

# Set compiler
set(CMAKE_C_COMPILER powerpc-eabi-gcc)
set(CMAKE_CXX_COMPILER powerpc-eabi-g++)

# Plugin settings
set(PLUGIN_NAME "wiiu_bridge_plugin")
set(PLUGIN_VERSION "1.0.0")
set(PLUGIN_AUTHOR "OpenCode")

# Source files
set(SOURCES
    main.c
    bridge.c
)

# Include directories
set(INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} \
    -mogc -mcpu=750 -meabi -msdata=eabi \
    -fno-exceptions -fno-rtti \
    -g -O2 -Wall")

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} \
    -nodefaultlibs -nostartfiles")

# Find devkitPPC
if(NOT DEFINED ENV{DEVKITPPC})
    set(DEVKITPPC "C:/msys64/opt/devkitpro/devkitPPC")
else()
    set(DEVKITPPC $ENV{DEVKITPPC})
endif()

# Find WUPS
find_library(WUPS_LIB wups PATHS ${DEVKITPPC}/../wut/lib)
find_library(WUT_LIB wut PATHS ${DEVKITPPC}/../wut/lib)

# Create executable
add_executable(${PLUGIN_NAME} ${SOURCES})

# Include directories
target_include_directories(${PLUGIN_NAME} PRIVATE
    ${DEVKITPPC}/../wut/include
    ${INCLUDES}
)

# Link libraries
target_link_libraries(${PLUGIN_NAME}
    ${WUPS_LIB}
    ${WUT_LIB}
    nsysnet
    padscore
    coreinit
)

# Set output extension
set_target_properties(${PLUGIN_NAME} PROPERTIES
    SUFFIX ".wps"
    PREFIX ""
)

# Install target
install(TARGETS ${PLUGIN_NAME}
    DESTINATION ${CMAKE_INSTALL_PREFIX}/wiiu/environments/aroma/plugins)
