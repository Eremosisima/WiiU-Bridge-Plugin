#include <coreinit/debug.h>
#include <wups.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemStub.h>
#include <wups/config_api.h>
#include <sys/socket.h>
#include <nsysnet/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <string.h>

WUPS_PLUGIN_NAME("WiiU Bridge");
WUPS_PLUGIN_DESCRIPTION("DSU bridge v20.0 - UDP+TCP multi-controller");
WUPS_PLUGIN_VERSION("v20.0.0");
WUPS_PLUGIN_AUTHOR("Morgana");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();

#define MAX_CONTROLLERS 4
#define DSU_PORT 26760

// TCP server
static int g_listen_sock = -1;
static int g_client_sock = -1;

// UDP server
static int g_udp_sock = -1;

// Counters
static uint32_t g_tcp_recv = 0;
static uint32_t g_udp_recv = 0;
static uint32_t g_packets = 0;

// Controller state (4 slots)
static struct {
    bool connected;
    uint32_t buttons;
    int16_t left_x, left_y;
    int16_t right_x, right_y;
} g_controllers[MAX_CONTROLLERS] = {0};

static uint32_t crc32_calc(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

// Process DSU packet from any source
static void process_dsu_packet(uint8_t *buf, int n) {
    if (n < 20 || buf[0] != 'D' || buf[1] != 'S' || buf[2] != 'U' || buf[3] != 'C') return;
    
    uint32_t mt;
    memcpy(&mt, buf + 16, 4);
    
    // Build response
    uint8_t resp[24] = {0};
    resp[0]='D'; resp[1]='S'; resp[2]='U'; resp[3]='S';
    uint32_t v=1001, l=8, id=99999;
    memcpy(resp+4,&v,2); memcpy(resp+6,&l,2);
    memcpy(resp+12,&id,4); memcpy(resp+16,&mt,4); memcpy(resp+20,&v,4);
    uint32_t crc=crc32_calc(resp,8); memcpy(resp+8,&crc,4);
    
    // Send response via same socket
    if (g_client_sock >= 0) {
        send(g_client_sock, resp, 24, 0);
    } else if (g_udp_sock >= 0) {
        // UDP response - we'd need the client address
    }
    g_packets++;
}

// Poll TCP
static void poll_tcp() {
    if (g_listen_sock >= 0 && g_client_sock < 0) {
        struct pollfd pfd = {g_listen_sock, POLLIN, 0};
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            struct sockaddr_in ca; socklen_t cl = sizeof(ca);
            g_client_sock = accept(g_listen_sock, (struct sockaddr *)&ca, &cl);
        }
    }
    if (g_client_sock >= 0) {
        struct pollfd pfd = {g_client_sock, POLLIN, 0};
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[512];
            int n = recv(g_client_sock, buf, sizeof(buf), 0);
            g_tcp_recv++;
            if (n <= 0) { socketclose(g_client_sock); g_client_sock = -1; }
            else { process_dsu_packet(buf, n); }
        }
    }
}

// Poll UDP
static void poll_udp() {
    if (g_udp_sock >= 0) {
        struct pollfd pfd = {g_udp_sock, POLLIN, 0};
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[512];
            struct sockaddr_in cli;
            socklen_t cli_len = sizeof(cli);
            int n = recvfrom(g_udp_sock, buf, sizeof(buf), 0, (struct sockaddr *)&cli, &cli_len);
            if (n > 0) {
                g_udp_recv++;
                process_dsu_packet(buf, n);
                // UDP response needs client address
                if (n >= 20 && buf[0]=='D' && buf[1]=='S' && buf[2]=='U' && buf[3]=='C') {
                    uint32_t mt; memcpy(&mt, buf + 16, 4);
                    uint8_t resp[24] = {0};
                    resp[0]='D'; resp[1]='S'; resp[2]='U'; resp[3]='S';
                    uint32_t v=1001, l=8, id=99999;
                    memcpy(resp+4,&v,2); memcpy(resp+6,&l,2);
                    memcpy(resp+12,&id,4); memcpy(resp+16,&mt,4); memcpy(resp+20,&v,4);
                    uint32_t crc=crc32_calc(resp,8); memcpy(resp+8,&crc,4);
                    sendto(g_udp_sock, resp, 24, 0, (struct sockaddr *)&cli, cli_len);
                }
            }
        }
    }
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer,
              uint32_t buffer_size, VPADReadError *error) {
    poll_tcp();
    poll_udp();
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    if (error) *error = real_error;
    return result;
}

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);

WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    WUPSConfigCategory root = WUPSConfigCategory(rootHandle);
    char s[128];
    snprintf(s, sizeof(s), "TCP:%d UDP:%d Pkts:%u", g_listen_sock, g_udp_sock, g_packets);
    root.add(WUPSConfigItemStub::Create(s));
    char s2[64];
    snprintf(s2, sizeof(s2), "TCP recv:%u UDP recv:%u", g_tcp_recv, g_udp_recv);
    root.add(WUPSConfigItemStub::Create(s2));
    root.add(WUPSConfigItemStub::Create("Port: 26760 | 4 Controllers"));
    uint32_t ip = 0;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock >= 0) {
        struct sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_port = htons(80);
        inet_aton("8.8.8.8", &a.sin_addr);
        if (connect(sock, (struct sockaddr*)&a, sizeof(a)) == 0) {
            struct sockaddr_in l; socklen_t len = sizeof(l);
            if (getsockname(sock, (struct sockaddr*)&l, &len) == 0) ip = ntohl(l.sin_addr.s_addr);
        }
        socketclose(sock);
    }
    char ipStr[64];
    if (ip) snprintf(ipStr, sizeof(ipStr), "IP: %u.%u.%u.%u", (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, ip&0xFF);
    else snprintf(ipStr, sizeof(ipStr), "IP: Not connected");
    root.add(WUPSConfigItemStub::Create(ipStr));
    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}
void ConfigMenuClosedCallback() {}
INITIALIZE_PLUGIN() {
    WUPSConfigAPIOptionsV1 o = {.name = "WiiU Bridge"};
    WUPSConfigAPI_Init(o, ConfigMenuOpenedCallback, ConfigMenuClosedCallback);
}
ON_APPLICATION_START() {
    // TCP server
    g_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen_sock >= 0) {
        int r = 1; setsockopt(g_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&r, sizeof(r));
        struct sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons(DSU_PORT);
        if (bind(g_listen_sock, (struct sockaddr*)&a, sizeof(a)) < 0) { socketclose(g_listen_sock); g_listen_sock = -1; }
        else if (listen(g_listen_sock, 1) < 0) { socketclose(g_listen_sock); g_listen_sock = -1; }
    }
    
    // UDP server
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp_sock >= 0) {
        int r = 1; setsockopt(g_udp_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&r, sizeof(r));
        int br = 1; setsockopt(g_udp_sock, SOL_SOCKET, SO_BROADCAST, (const char*)&br, sizeof(br));
        struct sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons(DSU_PORT);
        if (bind(g_udp_sock, (struct sockaddr*)&a, sizeof(a)) < 0) {
            socketclose(g_udp_sock);
            g_udp_sock = -1;
        }
    }
    
    OSReport("[Bridge] TCP:%d UDP:%d", g_listen_sock, g_udp_sock);
}
ON_APPLICATION_ENDS() {
    if (g_client_sock >= 0) socketclose(g_client_sock);
    if (g_listen_sock >= 0) socketclose(g_listen_sock);
    if (g_udp_sock >= 0) socketclose(g_udp_sock);
    g_client_sock = g_listen_sock = g_udp_sock = -1;
}
