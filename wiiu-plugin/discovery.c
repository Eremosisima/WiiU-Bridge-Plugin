#include "discovery.h"
#include <coreinit/debug.h>
#include <coreinit/time.h>

DiscoveryState g_discovery = {
    .socket_fd = -1,
    .thread_running = FALSE,
    .should_stop = FALSE,
    .server_count = 0,
    .scanning = FALSE,
};

int discovery_thread_func(int argc, const char **argv) {
    uint8_t buffer[256];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(g_discovery.socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    while (!g_discovery.should_stop) {
        int received = recvfrom(g_discovery.socket_fd, buffer, sizeof(buffer) - 1, 0,
                                (struct sockaddr *)&client_addr, &client_len);
        if (received <= 0) continue;
        buffer[received] = '\0';
        char *token = strtok((char *)buffer, "|");
        if (!token || strcmp(token, DISCOVERY_RESPONSE) != 0) continue;
        token = strtok(NULL, "|");
        if (!token) continue;
        char ip[16];
        strncpy(ip, token, 15);
        ip[15] = '\0';
        token = strtok(NULL, "|");
        if (!token) continue;
        uint16_t port = (uint16_t)atoi(token);
        token = strtok(NULL, "|");
        char name[64] = "Unknown";
        if (token) strncpy(name, token, 63);
        name[63] = '\0';
        OSLockMutex(&g_discovery.mutex);
        BOOL found = FALSE;
        for (int i = 0; i < g_discovery.server_count; i++) {
            if (strcmp(g_discovery.servers[i].ip, ip) == 0 && g_discovery.servers[i].port == port) {
                g_discovery.servers[i].last_seen = OSGetTime();
                strncpy(g_discovery.servers[i].name, name, 63);
                found = TRUE;
                break;
            }
        }
        if (!found && g_discovery.server_count < DISCOVERY_MAX_SERVERS) {
            int idx = g_discovery.server_count;
            strncpy(g_discovery.servers[idx].ip, ip, 15);
            g_discovery.servers[idx].ip[15] = '\0';
            g_discovery.servers[idx].port = port;
            strncpy(g_discovery.servers[idx].name, name, 63);
            g_discovery.servers[idx].name[63] = '\0';
            g_discovery.servers[idx].last_seen = OSGetTime();
            g_discovery.servers[idx].valid = TRUE;
            g_discovery.server_count++;
            DEBUG_FUNCTION_LINE("Discovery: Found server %s at %s:%d", name, ip, port);
        }
        OSUnlockMutex(&g_discovery.mutex);
    }
    g_discovery.thread_running = FALSE;
    return 0;
}

int discovery_init(void) {
    OSInitMutex(&g_discovery.mutex);
    socket_lib_init();
    for (int i = 0; i < DISCOVERY_MAX_SERVERS; i++) {
        g_discovery.servers[i].valid = FALSE;
        g_discovery.servers[i].name[0] = '\0';
        g_discovery.servers[i].ip[0] = '\0';
        g_discovery.servers[i].port = 0;
    }
    g_discovery.server_count = 0;
    return 0;
}

int discovery_start(void) {
    if (g_discovery.thread_running) return 0;
    g_discovery.socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_discovery.socket_fd < 0) return -1;
    int reuse = 1;
    setsockopt(g_discovery.socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    int broadcast = 1;
    setsockopt(g_discovery.socket_fd, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast));
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(DISCOVERY_PORT);
    if (bind(g_discovery.socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        socketclose(g_discovery.socket_fd);
        g_discovery.socket_fd = -1;
        return -1;
    }
    g_discovery.should_stop = FALSE;
    static uint8_t thread_stack[4096];
    if (!OSCreateThread(&g_discovery.thread, discovery_thread_func, 0, NULL,
                        thread_stack + sizeof(thread_stack), sizeof(thread_stack),
                        1, OS_THREAD_ATTRIB_AFFINITY_CPU0)) {
        socketclose(g_discovery.socket_fd);
        g_discovery.socket_fd = -1;
        return -1;
    }
    g_discovery.thread_running = TRUE;
    OSResumeThread(&g_discovery.thread);
    return 0;
}

void discovery_stop(void) {
    g_discovery.should_stop = TRUE;
    if (g_discovery.thread_running) {
        OSJoinThread(&g_discovery.thread, NULL);
        g_discovery.thread_running = FALSE;
    }
    if (g_discovery.socket_fd >= 0) {
        socketclose(g_discovery.socket_fd);
        g_discovery.socket_fd = -1;
    }
}

int discovery_send_broadcast(void) {
    if (g_discovery.socket_fd < 0) return -1;
    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
    broadcast_addr.sin_port = htons(DISCOVERY_PORT);
    int sent = sendto(g_discovery.socket_fd, DISCOVERY_MAGIC, DISCOVERY_MAGIC_LEN, 0,
                      (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
    if (sent > 0) {
        DEBUG_FUNCTION_LINE("Discovery: Broadcast sent");
        g_discovery.scanning = TRUE;
        return 0;
    }
    return -1;
}

int discovery_get_servers(DiscoveryServer *out, int max_count) {
    if (!out || max_count <= 0) return 0;
    OSLockMutex(&g_discovery.mutex);
    int count = 0;
    OSTime now = OSGetTime();
    for (int i = 0; i < g_discovery.server_count && count < max_count; i++) {
        if (g_discovery.servers[i].valid) {
            OSTime age = now - g_discovery.servers[i].last_seen;
            if (age < DISCOVERY_TIMEOUT_MS * 1000) {
                memcpy(&out[count], &g_discovery.servers[i], sizeof(DiscoveryServer));
                count++;
            } else {
                g_discovery.servers[i].valid = FALSE;
            }
        }
    }
    OSUnlockMutex(&g_discovery.mutex);
    return count;
}

int discovery_get_server_count(void) {
    return g_discovery.server_count;
}
