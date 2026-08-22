#pragma once

#include <wut.h>
#include <coreinit/thread.h>
#include <coreinit/mutex.h>
#include <nsysnet/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DSU Protocol Constants
 * Based on cemuhook-protocol v1001
 */
#define DSU_PORT_DEFAULT    26760
#define DSU_VERSION         1001
#define DSU_HEADER_SIZE     16
#define DSU_MAX_PACKET      256
#define DSU_MAX_SLOTS       4
#define DSU_TIMEOUT_MS      5000
#define DSU_UPDATE_HZ       250
#define DSU_UPDATE_INTERVAL (1000000 / DSU_UPDATE_HZ)

/**
 * DSU Magic Values
 */
#define DSU_MAGIC_SERVER    0x53554344  // "DSUC" - server to client
#define DSU_MAGIC_CLIENT    0x44535543  // "DSUC" - client to server
#define DSU_MAGIC_SERVER_LE 0x43535544  // "DSUS" - server response (LE)
#define DSU_MAGIC_CLIENT_LE 0x43535544  // "DSUC" - client request (LE)

/**
 * DSU Message Types
 */
#define DSU_MSG_VERSION     0x100000
#define DSU_MSG_CONTROLLER  0x100001
#define DSU_MSG_PAD_DATA    0x100002
#define DSU_MSG_RUMBLE_MOTOR 0x110001
#define DSU_MSG_RUMBLE      0x110002

/**
 * DSU Controller States
 */
#define DSU_STATE_DISCONNECTED  0
#define DSU_STATE_RESERVED      1
#define DSU_STATE_CONNECTED     2

/**
 * DSU Controller Models
 */
#define DSU_MODEL_NONE          0
#define DSU_MODEL_PS3           1
#define DSU_MODEL_PS4           2
#define DSU_MODEL_GENERIC       3

/**
 * DSU Button Masks (PS4 layout)
 */
#define DSU_BUTTON_CROSS        0x2000
#define DSU_BUTTON_CIRCLE       0x4000
#define DSU_BUTTON_SQUARE       0x8000
#define DSU_BUTTON_TRIANGLE     0x1000
#define DSU_BUTTON_L1           0x0400
#define DSU_BUTTON_R1           0x0800
#define DSU_BUTTON_L2           0x0100
#define DSU_BUTTON_R2           0x0200
#define DSU_BUTTON_SHARE        0x0010
#define DSU_BUTTON_OPTIONS      0x0020
#define DSU_BUTTON_L3           0x0040
#define DSU_BUTTON_R3           0x0080
#define DSU_BUTTON_PS           0x0004
#define DSU_BUTTON_PAD           0x10000
#define DSU_BUTTON_DPAD_UP      0x0010
#define DSU_BUTTON_DPAD_DOWN    0x0040
#define DSU_BUTTON_DPAD_LEFT    0x0080
#define DSU_BUTTON_DPAD_RIGHT   0x0020

/**
 * Wii U Pro Controller Button Definitions
 */
#define WPAD_PRO_UP             0x00000001
#define WPAD_PRO_LEFT           0x00000002
#define WPAD_PRO_ZR             0x00000004
#define WPAD_PRO_X              0x00000008
#define WPAD_PRO_A              0x00000010
#define WPAD_PRO_Y              0x00000020
#define WPAD_PRO_B              0x00000040
#define WPAD_PRO_ZL             0x00000080
#define WPAD_PRO_R              0x00000200
#define WPAD_PRO_PLUS           0x00000400
#define WPAD_PRO_HOME           0x00000800
#define WPAD_PRO_MINUS          0x00001000
#define WPAD_PRO_L              0x00002000
#define WPAD_PRO_DOWN           0x00004000
#define WPAD_PRO_RIGHT          0x00008000
#define WPAD_PRO_STICK_R        0x00010000
#define WPAD_PRO_STICK_L        0x00020000

/**
 * DSU Header Structure
 */
typedef struct {
    uint8_t magic[4];       // "DSUS" or "DSUC"
    uint16_t version;       // Protocol version (1001)
    uint16_t length;        // Payload length
    uint32_t crc32;         // CRC32 checksum
    uint32_t id;            // Unique server/client ID
} __attribute__((packed)) DSUHeader;

/**
 * DSU Controller Info
 */
typedef struct {
    uint8_t slot;           // Controller slot (0-3)
    uint8_t state;          // Connection state
    uint8_t model;          // Controller model
    uint8_t connection;     // Connection type
    uint8_t mac[6];         // MAC address
    uint8_t battery;        // Battery level (0-100)
    uint8_t active;         // Is this controller active?
} __attribute__((packed)) DSUControllerInfo;

/**
 * DSU Pad Data (buttons + sticks)
 */
typedef struct {
    uint32_t packet_counter;
    uint8_t buttons[16];    // Button states (bitmask)
    uint8_t lx, ly;         // Left stick (0-255, 128=center)
    uint8_t rx, ry;         // Right stick (0-255, 128=center)
    uint8_t l2, r2;         // Analog triggers (0-255)
} __attribute__((packed)) DSUPadData;

/**
 * DSU Motion Data (accel + gyro)
 */
typedef struct {
    int32_t accel_x, accel_y, accel_z;  // Accelerometer (g * 65536)
    int32_t gyro_x, gyro_y, gyro_z;    // Gyroscope (deg/s * 65536)
} __attribute__((packed)) DSUMotionData;

/**
 * DSU Input Data (combined)
 */
typedef struct {
    uint32_t buttons;       // Wii U Pro Controller buttons
    int16_t left_x, left_y; // Left stick (-32768 to 32767)
    int16_t right_x, right_y; // Right stick (-32768 to 32767)
    uint8_t l2, r2;         // Triggers (0-255)
    BOOL valid;             // Is data valid?
    uint64_t timestamp;     // Last update time
} DSUInputData;

/**
 * DSU Client State
 */
typedef struct {
    struct sockaddr_in addr;
    socklen_t addr_len;
    uint32_t id;
    uint8_t subscribed_slot;
    BOOL subscribed;
    uint64_t last_seen;
} DSUClientState;

/**
 * DSU Server State
 */
typedef struct {
    int socket_fd;
    OSThread thread;
    BOOL thread_running;
    BOOL should_stop;
    OSMutex mutex;
    uint16_t port;
    uint32_t server_id;
    
    // Controller state
    DSUControllerInfo controllers[DSU_MAX_SLOTS];
    DSUInputData input_data[DSU_MAX_SLOTS];
    
    // Client management
    DSUClientState clients[4];
    int client_count;
    
    // Statistics
    uint32_t packets_received;
    uint32_t packets_sent;
    uint32_t errors;
} DSUServerState;

/**
 * Global DSU server state
 */
extern DSUServerState g_dsu_server;

/**
 * Initialize DSU server
 * @param port Port to listen on (default 26760)
 * @return 0 on success, -1 on error
 */
int dsu_server_init(uint16_t port);

/**
 * Start DSU server
 * @return 0 on success, -1 on error
 */
int dsu_server_start(void);

/**
 * Stop DSU server
 */
void dsu_server_stop(void);

/**
 * Get input data for a controller slot
 * @param slot Controller slot (0-3)
 * @param data Pointer to store input data
 * @return 1 if valid data, 0 otherwise
 */
int dsu_server_get_input(int slot, DSUInputData *data);

/**
 * Set controller connection state
 * @param slot Controller slot (0-3)
 * @param connected 1=connected, 0=disconnected
 */
void dsu_server_set_connected(int slot, int connected);

/**
 * Update controller input data
 * @param slot Controller slot (0-3)
 * @param buttons Wii U button mask
 * @param lx Left stick X (-32768 to 32767)
 * @param ly Left stick Y (-32768 to 32767)
 * @param rx Right stick X (-32768 to 32767)
 * @param ry Right stick Y (-32768 to 32767)
 * @param l2 Left trigger (0-255)
 * @param r2 Right trigger (0-255)
 */
void dsu_server_update_input(int slot, uint32_t buttons, 
                             int16_t lx, int16_t ly,
                             int16_t rx, int16_t ry,
                             uint8_t l2, uint8_t r2);

/**
 * DSU button to Wii U button conversion
 * @param dsu_buttons DSU button mask
 * @return Wii U Pro Controller button mask
 */
uint32_t dsu_to_wiiu_buttons(uint16_t dsu_buttons);

/**
 * Wii U button to DSU button conversion
 * @param wiiu_buttons Wii U button mask
 * @return DSU button mask
 */
uint16_t wiiu_to_dsu_buttons(uint32_t wiiu_buttons);

#ifdef __cplusplus
}
#endif
