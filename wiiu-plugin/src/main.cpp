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
#include <padscore/wpad.h>

WUPS_PLUGIN_NAME("WiiU Bridge");
WUPS_PLUGIN_DESCRIPTION("DSU bridge v19.0 - all hooks");
WUPS_PLUGIN_VERSION("v19.0.0");
WUPS_PLUGIN_AUTHOR("Morgana");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();

static int g_listen_sock = -1;
static int g_client_sock = -1;
static uint32_t g_recv_count = 0;
static uint32_t g_packets = 0;

static uint32_t crc32_calc(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

// DSU packet processing (called from all hooks)
static void process_dsu(uint8_t *buf, int n) {
    if (n >= 20 && buf[0]=='D' && buf[1]=='S' && buf[2]=='U' && buf[3]=='C') {
        uint32_t mt;
        memcpy(&mt, buf + 16, 4);
        uint8_t resp[24] = {0};
        resp[0]='D'; resp[1]='S'; resp[2]='U'; resp[3]='S';
        uint32_t v=1001, l=8, id=99999;
        memcpy(resp+4,&v,2); memcpy(resp+6,&l,2);
        memcpy(resp+12,&id,4); memcpy(resp+16,&mt,4); memcpy(resp+20,&v,4);
        uint32_t crc=crc32_calc(resp,8); memcpy(resp+8,&crc,4);
        send(g_client_sock, resp, 24, 0);
        g_packets++;
    }
}

// Poll for DSU packets (called from all hooks)
static void poll_dsu() {
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
            g_recv_count++;
            if (n <= 0) {
                socketclose(g_client_sock);
                g_client_sock = -1;
            } else {
                process_dsu(buf, n);
            }
        }
    }
}

// Hook VPADRead (GamePad channel 0)
DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer,
              uint32_t buffer_size, VPADReadError *error) {
    poll_dsu();
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    if (error) *error = real_error;
    return result;
}
WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);

// Hook WPADRead (Wii Remote / Pro Controller)
DECL_FUNCTION(void, WPADRead, WPADChan chan, WPADStatusProController *data) {
    poll_dsu();
    real_WPADRead(chan, data);
}
WUPS_MUST_REPLACE(WPADRead, WUPS_LOADER_LIBRARY_PADSCORE, WPADRead);

// Config menu
WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    WUPSConfigCategory root = WUPSConfigCategory(rootHandle);
    char s[128];
    snprintf(s, sizeof(s), "Listen:%d Client:%d Recv:%u Pkts:%u", g_listen_sock, g_client_sock, g_recv_count, g_packets);
    root.add(WUPSConfigItemStub::Create(s));
    root.add(WUPSConfigItemStub::Create("TCP DSU Port: 26760"));
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
    g_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen_sock < 0) return;
    int r = 1; setsockopt(g_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&r, sizeof(r));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons(26760);
    if (bind(g_listen_sock, (struct sockaddr*)&a, sizeof(a)) < 0) { socketclose(g_listen_sock); g_listen_sock = -1; return; }
    if (listen(g_listen_sock, 1) < 0) { socketclose(g_listen_sock); g_listen_sock = -1; return; }
    OSReport("[Bridge] TCP DSU ready on 26760");
}
ON_APPLICATION_ENDS() {
    if (g_client_sock >= 0) socketclose(g_client_sock);
    if (g_listen_sock >= 0) socketclose(g_listen_sock);
    g_client_sock = g_listen_sock = -1;
}
