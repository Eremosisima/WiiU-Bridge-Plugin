#include "bridge_udp.h"
#include <coreinit/debug.h>
#include <coreinit/time.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

// Global bridge UDP state
BridgeUDPState g_bridge_udp = {
    .socket_fd = -1,
    .thread_running = FALSE,
    .should_stop = FALSE,
    .controller_data = {0},
    .port = BRIDGE_DEFAULT_PORT,
};

void bridge_udp_init(void) {
    OSReport("[Bridge UDP] Initializing...\n");
    
    // Initialize mutex
    OSInitMutex(&g_bridge_udp.mutex);
    
    // Reset controller data
    memset(&g_bridge_udp.controller_data, 0, sizeof(BridgeControllerData));
    
    OSReport("[Bridge UDP] Initialized\n");
}

void bridge_udp_deinit(void) {
    OSReport("[Bridge UDP] Deinitializing...\n");
    
    // Stop if running
    bridge_udp_stop();
    
    OSReport("[Bridge UDP] Deinitialized\n");
}

BOOL bridge_udp_start(uint16_t port) {
    OSReport("[Bridge UDP] Starting on port %d...\n", port);
    
    // Stop existing
    bridge_udp_stop();
    
    g_bridge_udp.port = port;
    
    // Create UDP socket
    g_bridge_udp.socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_bridge_udp.socket_fd < 0) {
        OSReport("[Bridge UDP] Failed to create socket\n");
        return FALSE;
    }
    
    // Set socket options
    int reuse = 1;
    setsockopt(g_bridge_udp.socket_fd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *)&reuse, sizeof(reuse));
    
    // Bind to port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(g_bridge_udp.socket_fd, (struct sockaddr *)&server_addr, 
             sizeof(server_addr)) < 0) {
        OSReport("[Bridge UDP] Failed to bind port %d\n", port);
        socketclose(g_bridge_udp.socket_fd);
        g_bridge_udp.socket_fd = -1;
        return FALSE;
    }
    
    OSReport("[Bridge UDP] Bound to port %d\n", port);
    
    // Create receiver thread
    g_bridge_udp.should_stop = FALSE;
    
    static uint8_t thread_stack[8192];
    
    if (!OSCreateThread(&g_bridge_udp.thread,
                        bridge_udp_thread_func,
                        0, NULL,
                        thread_stack + sizeof(thread_stack),
                        sizeof(thread_stack),
                        1,  // Priority
                        OS_THREAD_ATTRIB_AFFINITY_CPU0)) {
        OSReport("[Bridge UDP] Failed to create thread\n");
        socketclose(g_bridge_udp.socket_fd);
        g_bridge_udp.socket_fd = -1;
        return FALSE;
    }
    
    g_bridge_udp.thread_running = TRUE;
    OSResumeThread(&g_bridge_udp.thread);
    
    OSReport("[Bridge UDP] Started successfully\n");
    return TRUE;
}

void bridge_udp_stop(void) {
    if (g_bridge_udp.thread_running) {
        OSReport("[Bridge UDP] Stopping...\n");
        g_bridge_udp.should_stop = TRUE;
        
        // Wait for thread
        OSJoinThread(&g_bridge_udp.thread, NULL);
        g_bridge_udp.thread_running = FALSE;
    }
    
    if (g_bridge_udp.socket_fd >= 0) {
        socketclose(g_bridge_udp.socket_fd);
        g_bridge_udp.socket_fd = -1;
    }
    
    // Clear controller data
    OSLockMutex(&g_bridge_udp.mutex);
    g_bridge_udp.controller_data.connected = FALSE;
    OSUnlockMutex(&g_bridge_udp.mutex);
    
    OSReport("[Bridge UDP] Stopped\n");
}

int bridge_udp_thread_func(int argc, const char **argv) {
    uint8_t buffer[BRIDGE_PACKET_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Set socket to non-blocking
    int nonblock = 1;
    setsockopt(g_bridge_udp.socket_fd, SOL_SOCKET, SO_NONBLOCK, 
               (const char *)&nonblock, sizeof(nonblock));
    
    OSReport("[Bridge UDP] Receiver thread started\n");
    
    while (!g_bridge_udp.should_stop) {
        // Receive UDP packet
        int bytes_received = recvfrom(g_bridge_udp.socket_fd, buffer, 
                                       BRIDGE_PACKET_SIZE, 0,
                                       (struct sockaddr *)&client_addr, 
                                       &client_len);
        
        if (bytes_received <= 0) {
            // No data available - sleep and try again
            OSSleepTicks(OSSecondsToTicks(0.01));  // 10ms
            continue;
        }
        
        if (bytes_received == BRIDGE_PACKET_SIZE) {
            // Parse packet
            uint32_t magic, version;
            int16_t left_x, left_y, right_x, right_y;
            uint32_t buttons;
            
            memcpy(&magic, buffer, sizeof(uint32_t));
            memcpy(&version, buffer + 4, sizeof(uint32_t));
            memcpy(&left_x, buffer + 8, sizeof(int16_t));
            memcpy(&left_y, buffer + 10, sizeof(int16_t));
            memcpy(&right_x, buffer + 12, sizeof(int16_t));
            memcpy(&right_y, buffer + 14, sizeof(int16_t));
            memcpy(&buttons, buffer + 16, sizeof(uint32_t));
            
            // Verify magic
            if (magic == BRIDGE_MAGIC) {
                // Update controller data
                OSLockMutex(&g_bridge_udp.mutex);
                g_bridge_udp.controller_data.buttons = buttons;
                g_bridge_udp.controller_data.left_stick_x = left_x;
                g_bridge_udp.controller_data.left_stick_y = left_y;
                g_bridge_udp.controller_data.right_stick_x = right_x;
                g_bridge_udp.controller_data.right_stick_y = right_y;
                g_bridge_udp.controller_data.connected = TRUE;
                g_bridge_udp.controller_data.last_packet_time = OSGetTime();
                OSUnlockMutex(&g_bridge_udp.mutex);
            }
        }
    }
    
    OSReport("[Bridge UDP] Receiver thread stopped\n");
    return 0;
}

BOOL bridge_udp_get_controller_data(BridgeControllerData *data) {
    if (!data) {
        return FALSE;
    }
    
    OSLockMutex(&g_bridge_udp.mutex);
    memcpy(data, &g_bridge_udp.controller_data, sizeof(BridgeControllerData));
    OSUnlockMutex(&g_bridge_udp.mutex);
    
    // Check if data is fresh (less than 500ms old)
    if (data->connected) {
        OSTime now = OSGetTime();
        OSTime age = now - data->last_packet_time;
        // OSGetTime is in microseconds, 500ms = 500000us
        if (age > 500000) {
            data->connected = FALSE;
        }
    }
    
    return data->connected;
}
