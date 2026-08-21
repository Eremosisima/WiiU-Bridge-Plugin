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
#define HEADER_MAGIC 0x57425244  // "WBRD" - WiiU Bridge
#define PROTOCOL_VERSION 1
#define PACKET_SIZE 20

// Default server settings
#define DEFAULT_SERVER_IP "192.168.100.61"
#define DEFAULT_SERVER_PORT 12345

// Wii U Pro Controller button definitions (from wpad.h)
#define WPAD_PRO_BUTTON_UP        0x00000001
#define WPAD_PRO_BUTTON_LEFT      0x00000002
#define WPAD_PRO_BUTTON_ZR        0x00000004
#define WPAD_PRO_BUTTON_X         0x00000008
#define WPAD_PRO_BUTTON_A         0x00000010
#define WPAD_PRO_BUTTON_Y         0x00000020
#define WPAD_PRO_BUTTON_B         0x00000040
#define WPAD_PRO_BUTTON_ZL        0x00000080
#define WPAD_PRO_BUTTON_R         0x00000200
#define WPAD_PRO_BUTTON_PLUS      0x00000400
#define WPAD_PRO_BUTTON_HOME      0x00000800
#define WPAD_PRO_BUTTON_MINUS     0x00001000
#define WPAD_PRO_BUTTON_L         0x00002000
#define WPAD_PRO_BUTTON_DOWN      0x00004000
#define WPAD_PRO_BUTTON_RIGHT     0x00008000
#define WPAD_PRO_BUTTON_STICK_R   0x00010000
#define WPAD_PRO_BUTTON_STICK_L   0x00020000

// Bridge controller data structure
typedef struct {
    uint32_t buttons;
    int16_t left_stick_x;
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;
    BOOL connected;
    BOOL data_valid;
} BridgeControllerData;

// Bridge state
typedef struct {
    int socket_fd;
    OSThread thread;
    BOOL thread_running;
    BOOL should_stop;
    BridgeControllerData controller_data;
    OSMutex mutex;
    char server_ip[64];
    uint16_t server_port;
} BridgeState;

// Global state
extern BridgeState g_bridge_state;

// Function declarations
void bridge_init(void);
void bridge_deinit(void);
BOOL bridge_connect(const char *ip, uint16_t port);
void bridge_disconnect(void);
int bridge_thread_func(int argc, const char **argv);
BOOL bridge_get_controller_data(BridgeControllerData *data);
