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
WUPS_PLUGIN_DESCRIPTION("DSU bridge v17.0 - full debug");
WUPS_PLUGIN_VERSION("v17.0.0");
WUPS_PLUGIN_AUTHOR("Morgana");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();

static int g_listen_sock = -1;
static int g_client_sock = -1;
static uint32_t g_packets = 0;
static uint32_t g_recv_count = 0;

static uint32_t crc32_calc(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer,
              uint32_t buffer_size, VPADReadError *error) {
    // Accept
    if (g_listen_sock >= 0 && g_client_sock < 0) {
        struct pollfd pfd = {g_listen_sock, POLLIN, 0};
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            struct sockaddr_in ca; socklen_t cl = sizeof(ca);
            g_client_sock = accept(g_listen_sock, (struct sockaddr *)&ca, &cl);
            if (g_client_sock >= 0) OSReport("[Bridge] Client connected from %d.%d.%d.%d",
                (ca.sin_addr.s_addr>>0)&0xFF, (ca.sin_addr.s_addr>>8)&0xFF,
                (ca.sin_addr.s_addr>>16)&0xFF, (ca.sin_addr.s_addr>>24)&0xFF);
        }
    }
    // Receive
    if (g_client_sock >= 0) {
        struct pollfd pfd = {g_client_sock, POLLIN, 0};
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[512];
            int n = recv(g_client_sock, buf, sizeof(buf), 0);
            g_recv_count++;
            if (n <= 0) {
                OSReport("[Bridge] Client disconnected (n=%d)", n);
                socketclose(g_client_sock);
                g_client_sock = -1;
            } else {
                // Log every received packet
                OSReport("[Bridge] Recv#%d: %dB magic=%c%c%c%c", g_recv_count, n, buf[0], buf[1], buf[2], buf[3]);
                
                if (n >= 20 && buf[0]=='D' && buf[1]=='S' && buf[2]=='U' && buf[3]=='C') {
                    uint32_t msg_type;
                    memcpy(&msg_type, buf + 16, 4);
                    OSReport("[Bridge] DSU msg_type=0x%08x", msg_type);
                    
                    if (msg_type == 0x100000) {
                        // Version request
                        uint8_t resp[24] = {0};
                        resp[0]='D'; resp[1]='S'; resp[2]='U'; resp[3]='S';
                        uint32_t v=1001, mt=0x100000, l=8, id=99999;
                        memcpy(resp+4,&v,2); memcpy(resp+6,&l,2);
                        memcpy(resp+12,&id,4); memcpy(resp+16,&v,4); memcpy(resp+20,&mt,4);
                        uint32_t crc=crc32_calc(resp,8); memcpy(resp+8,&crc,4);
                        send(g_client_sock, resp, 24, 0);
                        g_packets++;
                        OSReport("[Bridge] -> VERSION response sent (pkts=%u)", g_packets);
                    } else if (msg_type == 0x100001) {
                        // Controller info request
                        uint8_t resp[20] = {0};
                        resp[0]='D'; resp[1]='S'; resp[2]='U'; resp[3]='S';
                        uint32_t v=1001, mt=0x100001, l=12, id=99999;
                        memcpy(resp+4,&v,2); memcpy(resp+6,&l,2);
                        memcpy(resp+12,&id,4); memcpy(resp+16,&mt,4);
                        resp[20]=0; resp[21]=2; // slot 0, connected
                        uint32_t crc=crc32_calc(resp,8); memcpy(resp+8,&crc,4);
                        send(g_client_sock, resp, 24, 0);
                        g_packets++;
                        OSReport("[Bridge] -> CONTROLLER response sent");
                    } else if (msg_type == 0x100002) {
                        // Pad data request - just acknowledge
                        uint8_t resp[24] = {0};
                        resp[0]='D'; resp[1]='S'; resp[2]='U'; resp[3]='S';
                        uint32_t v=1001, mt=0x100002, l=4, id=99999;
                        memcpy(resp+4,&v,2); memcpy(resp+6,&l,2);
                        memcpy(resp+12,&id,4); memcpy(resp+16,&mt,4);
                        uint32_t crc=crc32_calc(resp,8); memcpy(resp+8,&crc,4);
                        send(g_client_sock, resp, 24, 0);
                        g_packets++;
                        OSReport("[Bridge] -> PAD_DATA ack sent");
                    } else {
                        OSReport("[Bridge] Unknown msg_type=0x%08x", msg_type);
                    }
                } else {
                    // Echo non-DSU
                    send(g_client_sock, buf, n, 0);
                    OSReport("[Bridge] Echo %dB", n);
                }
            }
        }
    }
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    if (error) *error = real_error;
    return result;
}

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);

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
    OSReport("[Bridge] v17.0 INIT");
    WUPSConfigAPIOptionsV1 o = {.name = "WiiU Bridge"};
    WUPSConfigAPI_Init(o, ConfigMenuOpenedCallback, ConfigMenuClosedCallback);
}
ON_APPLICATION_START() {
    g_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen_sock < 0) { OSReport("[Bridge] Socket failed"); return; }
    int r = 1; setsockopt(g_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&r, sizeof(r));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons(26760);
    if (bind(g_listen_sock, (struct sockaddr*)&a, sizeof(a)) < 0) { OSReport("[Bridge] Bind failed"); socketclose(g_listen_sock); g_listen_sock = -1; return; }
    if (listen(g_listen_sock, 1) < 0) { OSReport("[Bridge] Listen failed"); socketclose(g_listen_sock); g_listen_sock = -1; return; }
    OSReport("[Bridge] TCP DSU ready on 26760");
}
ON_APPLICATION_ENDS() {
    if (g_client_sock >= 0) socketclose(g_client_sock);
    if (g_listen_sock >= 0) socketclose(g_listen_sock);
    g_client_sock = g_listen_sock = -1;
}
