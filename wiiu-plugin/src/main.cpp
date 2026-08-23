#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <wups.h>
#include <sys/socket.h>
#include <nsysnet/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <string.h>

WUPS_PLUGIN_NAME("WiiU Bridge");
WUPS_PLUGIN_DESCRIPTION("DSU bridge v6.3");
WUPS_PLUGIN_VERSION("v6.3.0");
WUPS_PLUGIN_AUTHOR("OpenCode");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();

static int g_sock = -1;
static volatile int g_running = 0;
static OSThread g_thread;
static uint8_t g_stack[8192];

static uint32_t crc32_calc(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

static int dsu_thread_func(int argc, const char **argv) {
    OSReport("[Bridge] Thread started, sock=%d", g_sock);
    uint8_t buf[512];
    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);
    struct pollfd pfd;
    pfd.fd = g_sock;
    pfd.events = POLLIN;
    int count = 0;
    
    while (g_running) {
        count++;
        if (count % 100 == 0) {
            OSReport("[Bridge] Thread alive, count=%d", count);
        }
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 100);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            int n = recvfrom(g_sock, buf, sizeof(buf), 0, (struct sockaddr *)&cli, &cli_len);
            OSReport("[Bridge] Received %d bytes", n);
            if (n >= 20 && memcmp(buf, "DSUC", 4) == 0) {
                uint32_t msg_type;
                memcpy(&msg_type, buf + 16, 4);
                if (msg_type == 0x100000) {
                    uint8_t resp[24] = {0};
                    resp[0] = 'D'; resp[1] = 'S'; resp[2] = 'U'; resp[3] = 'S';
                    uint32_t v = 1001, mt = 0x100000, len = 8, id = 99999;
                    memcpy(resp + 4, &v, 2);
                    memcpy(resp + 6, &len, 2);
                    memcpy(resp + 12, &id, 4);
                    memcpy(resp + 16, &v, 4);
                    memcpy(resp + 20, &mt, 4);
                    uint32_t crc = crc32_calc(resp, 8);
                    memcpy(resp + 8, &crc, 4);
                    sendto(g_sock, resp, 24, 0, (struct sockaddr *)&cli, cli_len);
                    OSReport("[Bridge] DSU response sent!");
                }
            }
        }
    }
    OSReport("[Bridge] Thread exiting");
    return 0;
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer,
              uint32_t buffer_size, VPADReadError *error) {
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    if (error) *error = real_error;
    return result;
}

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);

INITIALIZE_PLUGIN() {
    OSReport("[Bridge] v6.3 INIT");
    
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    OSReport("[Bridge] Socket: %d", g_sock);
    if (g_sock < 0) return;
    
    int reuse = 1;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(26760);
    
    int br = bind(g_sock, (struct sockaddr *)&addr, sizeof(addr));
    OSReport("[Bridge] Bind: %d", br);
    if (br < 0) {
        socketclose(g_sock);
        g_sock = -1;
        return;
    }
    
    g_running = 1;
    BOOL created = OSCreateThread(&g_thread, dsu_thread_func, 0, NULL,
                        g_stack + sizeof(g_stack), sizeof(g_stack),
                        1, OS_THREAD_ATTRIB_AFFINITY_CPU0);
    OSReport("[Bridge] Thread created: %d", created);
    if (created) {
        OSResumeThread(&g_thread);
        OSReport("[Bridge] Thread resumed");
    }
}

ON_APPLICATION_START() {
    OSReport("[Bridge] App started, sock=%d, running=%d", g_sock, g_running);
}

ON_APPLICATION_ENDS() {
    g_running = 0;
    if (g_sock >= 0) { socketclose(g_sock); g_sock = -1; }
}
