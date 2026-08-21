#include "bridge.h"
#include <coreinit/debug.h>
#include <coreinit/time.h>
#include <stdio.h>
#include <string.h>

// Global bridge state
BridgeState g_bridge_state = {
    .socket_fd = -1,
    .thread_running = FALSE,
    .should_stop = FALSE,
    .controller_data = {0},
    .server_ip = DEFAULT_SERVER_IP,
    .server_port = DEFAULT_SERVER_PORT,
};

void bridge_init(void) {
    OSReport("[Bridge] Initializing...\n");
    
    // Initialize mutex
    OSInitMutex(&g_bridge_state.mutex);
    
    // Initialize socket library
    socket_lib_init();
    
    // Reset controller data
    memset(&g_bridge_state.controller_data, 0, sizeof(BridgeControllerData));
    
    OSReport("[Bridge] Initialized\n");
}

void bridge_deinit(void) {
    OSReport("[Bridge] Deinitializing...\n");
    
    // Stop thread if running
    bridge_disconnect();
    
    // Cleanup socket library
    socket_lib_finish();
    
    OSReport("[Bridge] Deinitialized\n");
}

BOOL bridge_connect(const char *ip, uint16_t port) {
    OSReport("[Bridge] Connecting to %s:%d...\n", ip, port);
    
    // Disconnect existing connection
    bridge_disconnect();
    
    // Create socket
    g_bridge_state.socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_bridge_state.socket_fd < 0) {
        OSReport("[Bridge] Failed to create socket\n");
        return FALSE;
    }
    
    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    // Convert IP address
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        OSReport("[Bridge] Invalid IP address: %s\n", ip);
        socketclose(g_bridge_state.socket_fd);
        g_bridge_state.socket_fd = -1;
        return FALSE;
    }
    
    // Connect to server
    if (connect(g_bridge_state.socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        OSReport("[Bridge] Failed to connect\n");
        socketclose(g_bridge_state.socket_fd);
        g_bridge_state.socket_fd = -1;
        return FALSE;
    }
    
    OSReport("[Bridge] Connected to %s:%d\n", ip, port);
    
    // Start receiver thread
    g_bridge_state.should_stop = FALSE;
    
    // Allocate stack for thread
    static uint8_t thread_stack[8192];
    
    if (!OSCreateThread(&g_bridge_state.thread,
                        bridge_thread_func,
                        0, NULL,
                        thread_stack + sizeof(thread_stack),
                        sizeof(thread_stack),
                        1,  // Priority
                        OS_THREAD_ATTRIB_AFFINITY_CPU0)) {
        OSReport("[Bridge] Failed to create thread\n");
        socketclose(g_bridge_state.socket_fd);
        g_bridge_state.socket_fd = -1;
        return FALSE;
    }
    
    g_bridge_state.thread_running = TRUE;
    OSResumeThread(&g_bridge_state.thread);
    
    return TRUE;
}

void bridge_disconnect(void) {
    if (g_bridge_state.thread_running) {
        OSReport("[Bridge] Stopping thread...\n");
        g_bridge_state.should_stop = TRUE;
        
        // Wait for thread to finish
        OSJoinThread(&g_bridge_state.thread, NULL);
        g_bridge_state.thread_running = FALSE;
    }
    
    if (g_bridge_state.socket_fd >= 0) {
        OSReport("[Bridge] Closing socket\n");
        socketclose(g_bridge_state.socket_fd);
        g_bridge_state.socket_fd = -1;
    }
    
    // Clear controller data
    OSLockMutex(&g_bridge_state.mutex);
    g_bridge_state.controller_data.connected = FALSE;
    g_bridge_state.controller_data.data_valid = FALSE;
    OSUnlockMutex(&g_bridge_state.mutex);
}

int bridge_thread_func(int argc, const char **argv) {
    uint8_t buffer[PACKET_SIZE];
    
    OSReport("[Bridge] Receiver thread started\n");
    
    while (!g_bridge_state.should_stop) {
        // Receive data
        int bytes_received = recv(g_bridge_state.socket_fd, buffer, PACKET_SIZE, 0);
        
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                OSReport("[Bridge] Server closed connection\n");
            } else {
                OSReport("[Bridge] Receive error: %d\n", bytes_received);
            }
            break;
        }
        
        // Check if we received a complete packet
        if (bytes_received == PACKET_SIZE) {
            // Verify magic header
            uint32_t magic;
            memcpy(&magic, buffer, sizeof(uint32_t));
            
            if (magic == HEADER_MAGIC) {
                // Parse packet
                uint32_t version;
                int16_t left_x, left_y, right_x, right_y;
                uint32_t buttons;
                
                memcpy(&version, buffer + 4, sizeof(uint32_t));
                memcpy(&left_x, buffer + 8, sizeof(int16_t));
                memcpy(&left_y, buffer + 10, sizeof(int16_t));
                memcpy(&right_x, buffer + 12, sizeof(int16_t));
                memcpy(&right_y, buffer + 14, sizeof(int16_t));
                memcpy(&buttons, buffer + 16, sizeof(uint32_t));
                
                // Update controller data
                OSLockMutex(&g_bridge_state.mutex);
                g_bridge_state.controller_data.buttons = buttons;
                g_bridge_state.controller_data.left_stick_x = left_x;
                g_bridge_state.controller_data.left_stick_y = left_y;
                g_bridge_state.controller_data.right_stick_x = right_x;
                g_bridge_state.controller_data.right_stick_y = right_y;
                g_bridge_state.controller_data.connected = TRUE;
                g_bridge_state.controller_data.data_valid = TRUE;
                OSUnlockMutex(&g_bridge_state.mutex);
            } else {
                OSReport("[Bridge] Invalid packet magic: 0x%08X\n", magic);
            }
        }
    }
    
    OSReport("[Bridge] Receiver thread stopped\n");
    return 0;
}

BOOL bridge_get_controller_data(BridgeControllerData *data) {
    if (!data) {
        return FALSE;
    }
    
    OSLockMutex(&g_bridge_state.mutex);
    memcpy(data, &g_bridge_state.controller_data, sizeof(BridgeControllerData));
    OSUnlockMutex(&g_bridge_state.mutex);
    
    return data->data_valid;
}
