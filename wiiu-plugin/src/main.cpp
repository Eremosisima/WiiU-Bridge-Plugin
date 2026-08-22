#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <wups.h>
#include <sys/socket.h>
#include <nsysnet/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

WUPS_PLUGIN_NAME("WiiU Bridge");
WUPS_PLUGIN_DESCRIPTION("DSU bridge v5.2");
WUPS_PLUGIN_VERSION("v5.2.0");
WUPS_PLUGIN_AUTHOR("OpenCode");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();

static int g_dsu_socket = -1;
static volatile bool g_dsu_running = false;

static uint32_t dsu_crc32(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

static void dsu_respond(struct sockaddr_in *client, int sock, uint32_t msg_type, uint8_t *payload, int payload_len) {
    uint8_t packet[256];
    memset(packet, 0, sizeof(packet));
    memcpy(packet + 0, "DSUS", 4);
    uint16_t ver = 1001;
    memcpy(packet + 4, &ver, 2);
    uint16_t plen = payload_len + 4;
    memcpy(packet + 6, &plen, 2);
    uint32_t id = 99999;
    memcpy(packet + 12, &id, 4);
    memcpy(packet + 16, &msg_type, 4);
    if (payload_len > 0 && payload)
        memcpy(packet + 20, payload, payload_len);
    uint32_t crc = dsu_crc32(packet, 8);
    memcpy(packet + 8, &crc, 4);
    sendto(sock, packet, 20 + payload_len, 0, (struct sockaddr *)client, sizeof(*client));
}

static int dsu_thread_func(int argc, const char **argv) {
    uint8_t buffer[512];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    OSReport("[Bridge] DSU thread running");
    while (g_dsu_running) {
        int received = recvfrom(g_dsu_socket, buffer, sizeof(buffer), 0,
                                (struct sockaddr *)&client_addr, &client_len);
        if (received <= 0) continue;
        if (received < 20) continue;
        if (memcmp(buffer, "DSUC", 4) != 0) continue;
        uint32_t msg_type;
        memcpy(&msg_type, buffer + 16, 4);
        if (msg_type == 0x100000) {
            uint32_t ver = 1001;
            dsu_respond(&client_addr, g_dsu_socket, 0x100000, (uint8_t *)&ver, 4);
        }
    }
    return 0;
}

static bool dsu_start() {
    if (g_dsu_running) return true;
    g_dsu_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_dsu_socket < 0) return false;
    int reuse = 1;
    setsockopt(g_dsu_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(26760);
    if (bind(g_dsu_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        socketclose(g_dsu_socket);
        g_dsu_socket = -1;
        return false;
    }
    static uint8_t stack[8192];
    OSThread thread;
    if (!OSCreateThread(&thread, dsu_thread_func, 0, NULL,
                        stack + sizeof(stack), sizeof(stack),
                        1, OS_THREAD_ATTRIB_AFFINITY_CPU0)) {
        socketclose(g_dsu_socket);
        g_dsu_socket = -1;
        return false;
    }
    g_dsu_running = true;
    OSResumeThread(&thread);
    OSReport("[Bridge] DSU started on port 26760");
    return true;
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer,
              uint32_t buffer_size, VPADReadError *error) {
    static bool checked = false;
    if (!checked) {
        checked = true;
        FILE *f = fopen("sd:/wiiu/bridge_on.txt", "r");
        if (f) {
            fclose(f);
            remove("sd:/wiiu/bridge_on.txt");
            dsu_start();
        }
    }
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    if (error) *error = real_error;
    return result;
}

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);

INITIALIZE_PLUGIN() {
    OSReport("[Bridge] Loaded v5.2");
}

ON_APPLICATION_START() {}
ON_APPLICATION_ENDS() {
    g_dsu_running = false;
    if (g_dsu_socket >= 0) { socketclose(g_dsu_socket); g_dsu_socket = -1; }
}
