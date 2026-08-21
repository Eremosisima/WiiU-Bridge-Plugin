#include "utils/logger.h"
#include <coreinit/filesystem.h>
#include <malloc.h>
#include <wups.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemIntegerRange.h>
#include <wups/config/WUPSConfigItemMultipleValues.h>
#include <wups/config/WUPSConfigItemStub.h>
#include <wups/config_api.h>
#include <string>
#include <bitset>
#include <notifications/notifications.h>
#include <sys/socket.h>
#include <thread>
#include <arpa/inet.h>
#include <nn/ac.h>

/**
    Plugin information
**/
WUPS_PLUGIN_NAME("WiiU Bridge Controller");
WUPS_PLUGIN_DESCRIPTION("Bridges PC controllers to Wii U as Pro Controller");
WUPS_PLUGIN_VERSION("v1.0.0");
WUPS_PLUGIN_AUTHOR("OpenCode");
WUPS_PLUGIN_LICENSE("MIT");

#define ENABLE_BRIDGE "enableBridge"
#define SERVER_IP "serverIP"
#define SERVER_PORT "serverPort"

// Bridge controller data
struct BridgeControllerData {
    uint32_t buttons;
    int16_t left_stick_x;
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;
};

// Global state
bool threadRunning = false;
bool bridgeEnabled = true;
char serverIP[64] = "192.168.100.61";
uint16_t serverPort = 12345;
BridgeControllerData bridgeData = {0};

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("wiiu_bridge_controller");

/**
 * Convert Xbox 360 buttons to Wii U Pro Controller format
 */
uint32_t convertButtons(uint32_t xboxButtons) {
    uint32_t wiiuButtons = 0;
    
    // D-Pad
    if (xboxButtons & 0x0001) wiiuButtons |= WPAD_PRO_BUTTON_UP;
    if (xboxButtons & 0x0002) wiiuButtons |= WPAD_PRO_BUTTON_DOWN;
    if (xboxButtons & 0x0004) wiiuButtons |= WPAD_PRO_BUTTON_LEFT;
    if (xboxButtons & 0x0008) wiiuButtons |= WPAD_PRO_BUTTON_RIGHT;
    
    // Face buttons
    if (xboxButtons & 0x1000) wiiuButtons |= WPAD_PRO_BUTTON_A;
    if (xboxButtons & 0x2000) wiiuButtons |= WPAD_PRO_BUTTON_B;
    if (xboxButtons & 0x4000) wiiuButtons |= WPAD_PRO_BUTTON_X;
    if (xboxButtons & 0x8000) wiiuButtons |= WPAD_PRO_BUTTON_Y;
    
    // Shoulders
    if (xboxButtons & 0x0100) wiiuButtons |= WPAD_PRO_BUTTON_L;
    if (xboxButtons & 0x0200) wiiuButtons |= WPAD_PRO_BUTTON_R;
    
    // Triggers (as buttons)
    if (xboxButtons & 0x0080) wiiuButtons |= WPAD_PRO_BUTTON_ZL;
    if (xboxButtons & 0x0040) wiiuButtons |= WPAD_PRO_BUTTON_ZR;
    
    // Thumbsticks
    if (xboxButtons & 0x0020) wiiuButtons |= WPAD_PRO_BUTTON_STICK_L;
    if (xboxButtons & 0x0010) wiiuButtons |= WPAD_PRO_BUTTON_STICK_R;
    
    // Start/Back
    if (xboxButtons & 0x0010) wiiuButtons |= WPAD_PRO_BUTTON_PLUS;
    if (xboxButtons & 0x0020) wiiuButtons |= WPAD_PRO_BUTTON_MINUS;
    
    return wiiuButtons;
}

/**
 * Socket thread to receive data from PC
 */
void socket_thread() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        DEBUG_FUNCTION_LINE_ERR("Failed to create socket");
        threadRunning = false;
        return;
    }
    
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(serverPort);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        DEBUG_FUNCTION_LINE_ERR("Failed to bind socket");
        close(server_fd);
        threadRunning = false;
        return;
    }
    
    // Listen for connections
    listen(server_fd, 1);
    DEBUG_FUNCTION_LINE("Bridge server listening on port %d", serverPort);
    NotificationModule_AddInfoNotification("Bridge server started on port %d", serverPort);
    
    // Accept client connection
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        DEBUG_FUNCTION_LINE_ERR("Failed to accept connection");
        close(server_fd);
        threadRunning = false;
        return;
    }
    
    DEBUG_FUNCTION_LINE("PC connected from %s", inet_ntoa(client_addr.sin_addr));
    NotificationModule_AddInfoNotification("PC connected!");
    
    // Receive loop
    uint8_t buffer[20];  // Protocol packet size
    while (true) {
        int bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
        
        if (bytes_received <= 0) {
            DEBUG_FUNCTION_LINE("PC disconnected");
            break;
        }
        
        if (bytes_received == 20) {
            // Parse packet: [Magic(4)][Version(4)][LeftX(2)][LeftY(2)][RightX(2)][RightY(2)][Buttons(4)]
            uint32_t magic;
            uint32_t version;
            int16_t left_x, left_y, right_x, right_y;
            uint32_t buttons;
            
            memcpy(&magic, buffer, sizeof(uint32_t));
            memcpy(&version, buffer + 4, sizeof(uint32_t));
            memcpy(&left_x, buffer + 8, sizeof(int16_t));
            memcpy(&left_y, buffer + 10, sizeof(int16_t));
            memcpy(&right_x, buffer + 12, sizeof(int16_t));
            memcpy(&right_y, buffer + 14, sizeof(int16_t));
            memcpy(&buttons, buffer + 16, sizeof(uint32_t));
            
            // Verify magic header (0x57425244 = "WBRD")
            if (magic == 0x57425244) {
                // Update bridge data
                bridgeData.buttons = convertButtons(buttons);
                bridgeData.left_stick_x = left_x;
                bridgeData.left_stick_y = left_y;
                bridgeData.right_stick_x = right_x;
                bridgeData.right_stick_y = right_y;
            }
        }
    }
    
    // Cleanup
    close(client_fd);
    close(server_fd);
    threadRunning = false;
    NotificationModule_AddInfoNotification("Bridge disconnected");
}

/**
 * Config menu callbacks
 */
void boolItemChanged(ConfigItemBoolean *item, bool newValue) {
    if (std::string_view(ENABLE_BRIDGE) == item->identifier) {
        bridgeEnabled = newValue;
        WUPSStorageAPI::Store(item->identifier, newValue);
    }
}

WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    WUPSConfigCategory root = WUPSConfigCategory(rootHandle);
    
    try {
        // Enable/disable bridge
        root.add(WUPSConfigItemBoolean::Create(ENABLE_BRIDGE, "Enable Bridge",
                                               true, bridgeEnabled,
                                               &boolItemChanged));
        
        // Show current IP
        uint32_t hostIpAddress = 0;
        nn::ac::GetAssignedAddress(&hostIpAddress);
        char ipSettings[100];
        
        if (hostIpAddress != 0) {
            snprintf(ipSettings, sizeof(ipSettings),
                     "Wii U IP: %u.%u.%u.%u (Port: %d)",
                     (hostIpAddress >> 24) & 0xFF,
                     (hostIpAddress >> 16) & 0xFF,
                     (hostIpAddress >> 8) & 0xFF,
                     (hostIpAddress >> 0) & 0xFF,
                     serverPort);
        } else {
            snprintf(ipSettings, sizeof(ipSettings), "Wii U not connected to network");
        }
        
        root.add(WUPSConfigItemStub::Create(ipSettings));
        
        // Server IP info
        char serverInfo[100];
        snprintf(serverInfo, sizeof(serverInfo), "PC Server: %s:%d", serverIP, serverPort);
        root.add(WUPSConfigItemStub::Create(serverInfo));
        
    } catch (std::exception &e) {
        DEBUG_FUNCTION_LINE_ERR("Config menu failed: %s", e.what());
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    }
    
    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

void ConfigMenuClosedCallback() {
    WUPSStorageAPI::SaveStorage();
}

/**
 * Plugin initialization
 */
INITIALIZE_PLUGIN() {
    initLogging();
    NotificationModule_InitLibrary();
    
    // Initialize bridge data
    memset(&bridgeData, 0, sizeof(bridgeData));
    
    // Setup config menu
    WUPSConfigAPIOptionsV1 configOptions = {.name = "WiiU Bridge Controller"};
    if (WUPSConfigAPI_Init(configOptions, ConfigMenuOpenedCallback, ConfigMenuClosedCallback) != WUPSCONFIG_API_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to init config api");
    }
    
    // Load settings from storage
    WUPSStorageError storageRes;
    if ((storageRes = WUPSStorageAPI::GetOrStoreDefault(ENABLE_BRIDGE, bridgeEnabled, true)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("GetOrStoreDefault failed: %s (%d)", WUPSStorageAPI_GetStatusStr(storageRes), storageRes);
    }
    
    if ((storageRes = WUPSStorageAPI::SaveStorage()) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("SaveStorage failed: %s (%d)", WUPSStorageAPI_GetStatusStr(storageRes), storageRes);
    }
    
    deinitLogging();
}

/**
 * Application start hook
 */
ON_APPLICATION_START() {
    initLogging();
    DEBUG_FUNCTION_LINE("WiiU Bridge Controller started");
}

/**
 * VPADRead hook - Inject bridge controller data
 */
DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer, uint32_t buffer_size, VPADReadError *error) {
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    
    // If bridge is enabled and we have valid data, inject it
    if (bridgeEnabled && result > 0 && real_error == VPAD_READ_SUCCESS) {
        // Start bridge thread if not running
        if (!threadRunning) {
            threadRunning = true;
            std::thread server(socket_thread);
            server.detach();
        }
        
        // Inject bridge data into gamepad buffer
        for (uint32_t i = 0; i < result; i++) {
            buffer[i].hold = bridgeData.buttons;
            buffer[i].leftStick.x = bridgeData.left_stick_x;
            buffer[i].leftStick.y = bridgeData.left_stick_y;
            buffer[i].rightStick.x = bridgeData.right_stick_x;
            buffer[i].rightStick.y = bridgeData.right_stick_y;
        }
    }
    
    if (error) {
        *error = real_error;
    }
    return result;
}

/**
 * WPADRead hook - Inject bridge controller data as Pro Controller
 */
DECL_FUNCTION(void, WPADRead, WPADChan chan, WPADStatusProController *data) {
    real_WPADRead(chan, data);
    
    // If bridge is enabled, inject as Pro Controller
    if (bridgeEnabled && data[0].err == 0) {
        // Override with bridge data
        data[0].extensionType = WPAD_EXT_PRO_CONTROLLER;
        data[0].buttons = bridgeData.buttons;
        data[0].leftStick.x = bridgeData.left_stick_x;
        data[0].leftStick.y = bridgeData.left_stick_y;
        data[0].rightStick.x = bridgeData.right_stick_x;
        data[0].rightStick.y = bridgeData.right_stick_y;
    }
}

// Register function replacements
WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);
WUPS_MUST_REPLACE(WPADRead, WUPS_LOADER_LIBRARY_PADSCORE, WPADRead);
