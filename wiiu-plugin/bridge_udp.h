#pragma once

#include <wut.h>
#include <coreinit/thread.h>
#include <coreinit/mutex.h>
#include <nsysnet/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>

// Protocol constants
#define BRIDGE_MAGIC 0x57425244  // "WBRD" - WiiU Bridge
#define BRIDGE_VERSION 1
#define BRIDGE_PACKET_SIZE 20

// Default settings
#define BRIDGE_DEFAULT_PORT 12345

// Wii U Pro Controller button definitions
#define WPAD_PRO_UP        0x00000001
#define WPAD_PRO_LEFT      0x00000002
#define WPAD_PRO_ZR        0x00000004
#define WPAD_PRO_X         0x00000008
#define WPAD_PRO_A         0x00000010
#define WPAD_PRO_Y         0x00000020
#define WPAD_PRO_B         0x00000040
#define WPAD_PRO_ZL        0x00000080
#define WPAD_PRO_R         0x00000200
#define WPAD_PRO_PLUS      0x00000400
#define WPAD_PRO_HOME      0x00000800
#define WPAD_PRO_MINUS     0x00001000
#define WPAD_PRO_L         0x00002000
#define WPAD_PRO_DOWN      0x00004000
#define WPAD_PRO_RIGHT     0x00008000
#define WPAD_PRO_STICK_R   0x00010000
#define WPAD_PRO_STICK_L   0x00020000

// Bridge controller data
typedef struct {
    uint32_t buttons;
    int16_t left_stick_x;
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;
    BOOL connected;
    uint32_t last_packet_time;
} BridgeControllerData;

// Bridge state
typedef struct {
    int socket_fd;
    OSThread thread;
    BOOL thread_running;
    BOOL should_stop;
    BridgeControllerData controller_data;
    OSMutex mutex;
    uint16_t port;
} BridgeUDPState;

// Global state
extern BridgeUDPState g_bridge_udp;

// Function declarations
void bridge_udp_init(void);
void bridge_udp_deinit(void);
BOOL bridge_udp_start(uint16_t port);
void bridge_udp_stop(void);
int bridge_udp_thread_func(int argc, const char **argv);
BOOL bridge_udp_get_controller_data(BridgeControllerData *data);
