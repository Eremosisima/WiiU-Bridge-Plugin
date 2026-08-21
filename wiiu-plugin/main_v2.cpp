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
#include "bridge_udp.h"
#include "dsu_server.h"
#include "discovery.h"

WUPS_PLUGIN_NAME("WiiU Bridge Controller");
WUPS_PLUGIN_DESCRIPTION("Bridges controllers to Wii U via DSU/TCP/UDP");
WUPS_PLUGIN_VERSION("v3.0.0");
WUPS_PLUGIN_AUTHOR("OpenCode");
WUPS_PLUGIN_LICENSE("MIT");

#define ENABLE_BRIDGE   "enableBridge"
#define BRIDGE_MODE     "bridgeMode"
#define TCP_PORT        "tcpPort"
#define UDP_PORT        "udpPort"
#define DSU_PORT        "dsuPort"
#define AUTO_DISCOVER   "autoDiscover"

enum BridgeMode {
    MODE_DSU = 0,
    MODE_TCP = 1,
    MODE_UDP = 2,
    MODE_WBRD = 3
};

bool bridgeEnabled = true;
BridgeMode bridgeMode = MODE_DSU;
uint16_t tcpPort = 12345;
uint16_t udpPort = 12345;
uint16_t dsuPort = DSU_PORT_DEFAULT;
bool autoDiscover = true;

bool tcpThreadRunning = false;
bool tcpShouldStop = false;

struct BridgeInput {
    uint32_t buttons;
    int16_t left_x, left_y;
    int16_t right_x, right_y;
    bool valid;
    uint64_t timestamp;
};

BridgeInput lastInput = {0};

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("wiiu_bridge_controller");

void tcp_thread_func() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        tcpThreadRunning = false;
        return;
    }
    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(tcpPort);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        socketclose(server_fd);
        tcpThreadRunning = false;
        return;
    }
    listen(server_fd, 1);
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        socketclose(server_fd);
        tcpThreadRunning = false;
        return;
    }
    NotificationModule_AddInfoNotification("PC connected via TCP!");
    uint8_t buffer[20];
    while (!tcpShouldStop) {
        int received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        if (received == 20) {
            uint32_t magic;
            memcpy(&magic, buffer, sizeof(uint32_t));
            if (magic == 0x57425244) {
                uint32_t version;
                int16_t lx, ly, rx, ry;
                uint32_t buttons;
                memcpy(&version, buffer + 4, sizeof(uint32_t));
                memcpy(&lx, buffer + 8, sizeof(int16_t));
                memcpy(&ly, buffer + 10, sizeof(int16_t));
                memcpy(&rx, buffer + 12, sizeof(int16_t));
                memcpy(&ry, buffer + 14, sizeof(int16_t));
                memcpy(&buttons, buffer + 16, sizeof(uint32_t));
                lastInput.buttons = buttons;
                lastInput.left_x = lx;
                lastInput.left_y = ly;
                lastInput.right_x = rx;
                lastInput.right_y = ry;
                lastInput.valid = true;
                lastInput.timestamp = OSGetTime();
            }
        }
    }
    close(client_fd);
    close(server_fd);
    tcpThreadRunning = false;
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer,
              uint32_t buffer_size, VPADReadError *error) {
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    if (bridgeEnabled && result > 0 && real_error == VPAD_READ_SUCCESS) {
        if (bridgeMode == MODE_TCP) {
            if (!tcpThreadRunning) {
                tcpThreadRunning = true;
                tcpShouldStop = false;
                std::thread(tcp_thread_func).detach();
            }
        }

        DSUInputData dsuData;
        if (dsu_server_get_input(0, &dsuData)) {
            for (uint32_t i = 0; i < result; i++) {
                buffer[i].hold = dsuData.buttons;
                buffer[i].leftStick.x = dsuData.left_x;
                buffer[i].leftStick.y = dsuData.left_y;
                buffer[i].rightStick.x = dsuData.right_x;
                buffer[i].rightStick.y = dsuData.right_y;
            }
        } else if (lastInput.valid) {
            OSTime age = OSGetTime() - lastInput.timestamp;
            if (age < 200000) {
                for (uint32_t i = 0; i < result; i++) {
                    buffer[i].hold = lastInput.buttons;
                    buffer[i].leftStick.x = lastInput.left_x;
                    buffer[i].leftStick.y = lastInput.left_y;
                    buffer[i].rightStick.x = lastInput.right_x;
                    buffer[i].rightStick.y = lastInput.right_y;
                }
            }
        }

        if (bridgeMode == MODE_UDP || bridgeMode == MODE_WBRD) {
            BridgeControllerData udpData;
            if (bridge_udp_get_controller_data(&udpData)) {
                for (uint32_t i = 0; i < result; i++) {
                    buffer[i].hold = udpData.buttons;
                    buffer[i].leftStick.x = udpData.left_stick_x;
                    buffer[i].leftStick.y = udpData.left_stick_y;
                    buffer[i].rightStick.x = udpData.right_stick_x;
                    buffer[i].rightStick.y = udpData.right_stick_y;
                }
            }
        }
    }
    if (error) *error = real_error;
    return result;
}

DECL_FUNCTION(void, WPADRead, WPADChan chan, WPADStatusProController *data) {
    real_WPADRead(chan, data);
    if (bridgeEnabled && data[0].err == 0) {
        DSUInputData dsuData;
        if (dsu_server_get_input(0, &dsuData)) {
            data[0].extensionType = WPAD_EXT_PRO_CONTROLLER;
            data[0].buttons = dsuData.buttons;
            data[0].leftStick.x = dsuData.left_x;
            data[0].leftStick.y = dsuData.left_y;
            data[0].rightStick.x = dsuData.right_x;
            data[0].rightStick.y = dsuData.right_y;
        } else if (lastInput.valid) {
            OSTime age = OSGetTime() - lastInput.timestamp;
            if (age < 200000) {
                data[0].extensionType = WPAD_EXT_PRO_CONTROLLER;
                data[0].buttons = lastInput.buttons;
                data[0].leftStick.x = lastInput.left_x;
                data[0].leftStick.y = lastInput.left_y;
                data[0].rightStick.x = lastInput.right_x;
                data[0].rightStick.y = lastInput.right_y;
            }
        }
    }
}

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);
WUPS_MUST_REPLACE(WPADRead, WUPS_LOADER_LIBRARY_PADSCORE, WPADRead);

WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    WUPSConfigCategory root = WUPSConfigCategory(rootHandle);
    try {
        root.add(WUPSConfigItemBoolean::Create(ENABLE_BRIDGE, "Enable Bridge",
                                               true, bridgeEnabled,
                                               [](ConfigItemBoolean *item, bool v) {
                                                   bridgeEnabled = v;
                                                   WUPSStorageAPI::Store(item->identifier, v);
                                               }));

        const char *modeNames[] = {"DSU (Default)", "TCP", "UDP", "WBRD"};
        root.add(WUPSConfigItemMultipleValues::Create(BRIDGE_MODE, "Input Mode",
                                                      modeNames, 4,
                                                      (int32_t)bridgeMode,
                                                      [](ConfigItemMultipleValues *item, int32_t v) {
                                                          bridgeMode = (BridgeMode)v;
                                                          WUPSStorageAPI::Store(item->identifier, v);
                                                      }));

        uint32_t hostIp = 0;
        nn::ac::GetAssignedAddress(&hostIp);
        char ipStr[128];
        if (hostIp != 0) {
            snprintf(ipStr, sizeof(ipStr),
                     "Wii U IP: %u.%u.%u.%u | DSU:%d",
                     (hostIp >> 24) & 0xFF, (hostIp >> 16) & 0xFF,
                     (hostIp >> 8) & 0xFF, (hostIp >> 0) & 0xFF,
                     dsuPort);
        } else {
            snprintf(ipStr, sizeof(ipStr), "Wii U not connected to network");
        }
        root.add(WUPSConfigItemStub::Create(ipStr));

        char statusStr[128];
        snprintf(statusStr, sizeof(statusStr), "DSU: %s | Clients: %d | Packets: %u/%u",
                 g_dsu_server.thread_running ? "ACTIVE" : "inactive",
                 g_dsu_server.client_count,
                 g_dsu_server.packets_sent,
                 g_dsu_server.packets_received);
        root.add(WUPSConfigItemStub::Create(statusStr));

        char discStr[128];
        snprintf(discStr, sizeof(discStr), "Servers found: %d", discovery_get_server_count());
        root.add(WUPSConfigItemStub::Create(discStr));

        root.add(WUPSConfigItemBoolean::Create(AUTO_DISCOVER, "Auto-discover devices",
                                               true, autoDiscover,
                                               [](ConfigItemBoolean *item, bool v) {
                                                   autoDiscover = v;
                                                   WUPSStorageAPI::Store(item->identifier, v);
                                               }));

        char tcpStr[64];
        snprintf(tcpStr, sizeof(tcpStr), "TCP Port: %d", tcpPort);
        root.add(WUPSConfigItemStub::Create(tcpStr));

        char udpStr[64];
        snprintf(udpStr, sizeof(udpStr), "UDP Port: %d", udpPort);
        root.add(WUPSConfigItemStub::Create(udpStr));

    } catch (std::exception &e) {
        DEBUG_FUNCTION_LINE_ERR("Config failed: %s", e.what());
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    }
    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

void ConfigMenuClosedCallback() {
    WUPSStorageAPI::SaveStorage();
}

INITIALIZE_PLUGIN() {
    initLogging();
    NotificationModule_InitLibrary();

    WUPSConfigAPIOptionsV1 configOptions = {.name = "WiiU Bridge Controller"};
    WUPSConfigAPI_Init(configOptions, ConfigMenuOpenedCallback, ConfigMenuClosedCallback);

    dsu_server_init(dsuPort);
    dsu_server_start();

    discovery_init();
    discovery_start();
    if (autoDiscover) {
        discovery_send_broadcast();
    }

    bridge_udp_init();
    bridge_udp_start(udpPort);

    NotificationModule_AddInfoNotification("WiiU Bridge v3.0 ready (DSU port: %d)", dsuPort);

    deinitLogging();
}

ON_APPLICATION_START() {
    initLogging();
    DEBUG_FUNCTION_LINE("WiiU Bridge Controller v3.0 started (DSU mode)");
}
