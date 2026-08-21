#include <wups.h>
#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <padscore/wpad.h>
#include <string.h>
#include "bridge.h"

// Plugin metadata
WUPS_PLUGIN_NAME("WiiU Bridge Controller")
WUPS_PLUGIN_AUTHOR("OpenCode")
WUPS_PLUGIN_VERSION("1.0.0")
WUPS_PLUGIN_LICENSE("MIT")
WUPS_PLUGIN_DESCRIPTION("Bridges PC controllers to Wii U as Pro Controller via TCP/IP")

// Global Pro Controller status for injection
static WPADStatusProController g_pro_status;
static BOOL g_pro_status_valid = FALSE;

// Hook: Plugin initialization
INITIALIZE_PLUGIN() {
    OSReport("[Bridge Plugin] Initializing plugin...\n");
    
    // Initialize bridge
    bridge_init();
    
    // Connect to server (default IP/port)
    // In a real implementation, this could be configurable via WUPS storage
    if (!bridge_connect(DEFAULT_SERVER_IP, DEFAULT_SERVER_PORT)) {
        OSReport("[Bridge Plugin] Failed to connect to server\n");
        // Continue anyway - might reconnect later
    }
    
    OSReport("[Bridge Plugin] Plugin initialized\n");
}

// Hook: Plugin deinitialization
DEINITIALIZE_PLUGIN() {
    OSReport("[Bridge Plugin] Deinitializing plugin...\n");
    
    // Disconnect bridge
    bridge_deinit();
    
    OSReport("[Bridge Plugin] Plugin deinitialized\n");
}

// Hook: Application starts
ON_APPLICATION_START() {
    OSReport("[Bridge Plugin] Application starting...\n");
    
    // Reconnect when game starts
    bridge_connect(DEFAULT_SERVER_IP, DEFAULT_SERVER_PORT);
}

// Hook: Application ends
ON_APPLICATION_ENDS() {
    OSReport("[Bridge Plugin] Application ending...\n");
    
    // Disconnect when game ends
    bridge_disconnect();
}

// Hook: Release foreground (e.g., going to system menu)
ON_RELEASE_FOREGROUND() {
    OSReport("[Bridge Plugin] Released foreground\n");
}

// Hook: Acquired foreground (returning from system menu)
ON_ACQUIRED_FOREGROUND() {
    OSReport("[Bridge Plugin] Acquired foreground\n");
}

// Function replacement: WPADRead
// This hooks into WPADRead to inject our controller data
DECL_FUNCTION(void, WPADRead, WPADChan channel, WPADStatus *status) {
    // Call original function
    real_WPADRead(channel, status);
    
    // If this is channel 0 and we have valid bridge data, inject it
    if (channel == WPAD_CHAN_0) {
        BridgeControllerData bridge_data;
        if (bridge_get_controller_data(&bridge_data) && bridge_data.data_valid) {
            // Convert to Pro Controller format
            memset(&g_pro_status, 0, sizeof(WPADStatusProController));
            
            // Set core status
            g_pro_status.core.extensionType = WPAD_EXT_PRO_CONTROLLER;
            g_pro_status.core.error = WPAD_ERROR_NONE;
            
            // Set buttons
            g_pro_status.buttons = bridge_data.buttons;
            
            // Set sticks (already in correct range from server)
            g_pro_status.leftSticks.x = bridge_data.left_stick_x;
            g_pro_status.leftSticks.y = bridge_data.left_stick_y;
            g_pro_status.rightSticks.x = bridge_data.right_stick_x;
            g_pro_status.rightSticks.y = bridge_data.right_stick_y;
            
            // Set power status
            g_pro_status.charging = FALSE;
            g_pro_status.wired = TRUE;  // Show as wired controller
            
            // Copy to output buffer
            memcpy(status, &g_pro_status, sizeof(WPADStatusProController));
            g_pro_status_valid = TRUE;
        }
    }
}

// Register function replacement
WUPS_MUST_REPLACE(WPADRead,
                   WUPS_LOADER_LIBRARY_PADSCORE,
                   WPADRead);
