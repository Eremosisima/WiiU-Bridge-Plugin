#include <coreinit/debug.h>
#include <coreinit/filesystem.h>
#include <malloc.h>
#include <stdarg.h>
#include <wups.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemMultipleValues.h>
#include <wups/config/WUPSConfigItemStub.h>
#include <wups/config_api.h>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <thread>
#include <arpa/inet.h>
#include "dsu_server.h"

#define DEBUG_FUNCTION_LINE(fmt, ...) OSReport("[WiiU-Bridge] " fmt "\n", ##__VA_ARGS__)

WUPS_PLUGIN_NAME("WiiU Bridge Controller");
WUPS_PLUGIN_DESCRIPTION("DSU controller bridge v4.0");
WUPS_PLUGIN_VERSION("v4.0.0");
WUPS_PLUGIN_AUTHOR("OpenCode");
WUPS_PLUGIN_LICENSE("MIT");

enum BridgeMode { MODE_DSU = 0, MODE_TCP = 1, MODE_UDP = 2 };

bool bridgeEnabled = true;
BridgeMode bridgeMode = MODE_DSU;
uint16_t dsuPort = 26760;
bool networkInitialized = false;

WUPS_USE_WUT_DEVOPTAB();

static bool startNetwork() {
    if (networkInitialized) return true;
    DEBUG_FUNCTION_LINE("startNetwork: initializing...");
    
    dsu_server_init(dsuPort);
    int r = dsu_server_start();
    DEBUG_FUNCTION_LINE("DSU server start: %d", r);
    if (r != 0) return false;
    
    networkInitialized = true;
    DEBUG_FUNCTION_LINE("startNetwork: OK");
    return true;
}

static uint32_t getLocalIp() {
    uint32_t ip = 0;
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s >= 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        inet_aton("8.8.8.8", &addr.sin_addr);
        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            struct sockaddr_in local;
            socklen_t len = sizeof(local);
            if (getsockname(s, (struct sockaddr *)&local, &len) == 0) {
                ip = ntohl(local.sin_addr.s_addr);
            }
        }
        socketclose(s);
    }
    return ip;
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer,
              uint32_t buffer_size, VPADReadError *error) {
    static int calls = 0;
    calls++;
    
    if (calls == 1) {
        DEBUG_FUNCTION_LINE("VPADRead #1 - starting network");
        startNetwork();
    }
    
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    
    if (bridgeEnabled && networkInitialized && result > 0 && real_error == VPAD_READ_SUCCESS) {
        DSUInputData dsuData;
        if (dsu_server_get_input(0, &dsuData)) {
            for (uint32_t i = 0; i < result; i++) {
                buffer[i].hold = dsuData.buttons;
                buffer[i].leftStick.x = dsuData.left_x;
                buffer[i].leftStick.y = dsuData.left_y;
                buffer[i].rightStick.x = dsuData.right_x;
                buffer[i].rightStick.y = dsuData.right_y;
            }
        }
    }
    
    if (error) *error = real_error;
    return result;
}

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);

WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    WUPSConfigCategory root = WUPSConfigCategory(rootHandle);
    
    char statusStr[128];
    snprintf(statusStr, sizeof(statusStr), "Status: %s", networkInitialized ? "CONNECTED" : "Disconnected");
    root.add(WUPSConfigItemStub::Create(statusStr));

    uint32_t ip = getLocalIp();
    char ipStr[64];
    if (ip != 0) {
        snprintf(ipStr, sizeof(ipStr), "IP: %u.%u.%u.%u Port: %d",
                 (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                 (ip >> 8) & 0xFF, (ip >> 0) & 0xFF, dsuPort);
    } else {
        snprintf(ipStr, sizeof(ipStr), "IP: Not connected");
    }
    root.add(WUPSConfigItemStub::Create(ipStr));

    root.add(WUPSConfigItemBoolean::Create("connect", "Connect to DSU",
                                           true, networkInitialized,
                                           [](ConfigItemBoolean *item, bool v) {
                                               if (v) startNetwork();
                                           }));

    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

void ConfigMenuClosedCallback() {
    DEBUG_FUNCTION_LINE("Config menu closed");
}

INITIALIZE_PLUGIN() {
    DEBUG_FUNCTION_LINE("INITIALIZE_PLUGIN");
    WUPSConfigAPIOptionsV1 configOptions = {.name = "WiiU Bridge Controller"};
    WUPSConfigAPI_Init(configOptions, ConfigMenuOpenedCallback, ConfigMenuClosedCallback);
}

ON_APPLICATION_START() {
    DEBUG_FUNCTION_LINE("ON_APPLICATION_START");
}

ON_APPLICATION_ENDS() {
    DEBUG_FUNCTION_LINE("ON_APPLICATION_ENDS");
    if (networkInitialized) {
        dsu_server_stop();
        networkInitialized = false;
    }
}
