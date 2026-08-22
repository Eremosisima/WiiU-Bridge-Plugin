#pragma once

#include <wut.h>
#include <coreinit/thread.h>
#include <coreinit/mutex.h>
#include <nsysnet/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISCOVERY_PORT         26761
#define DISCOVERY_MAGIC        "DSU_DISCOVER"
#define DISCOVERY_MAGIC_LEN    12
#define DISCOVERY_RESPONSE     "DSU_SERVER"
#define DISCOVERY_RESPONSE_LEN 10
#define DISCOVERY_MAX_SERVERS  8
#define DISCOVERY_TIMEOUT_MS   3000

typedef struct {
    char name[64];
    char ip[16];
    uint16_t port;
    uint64_t last_seen;
    BOOL valid;
} DiscoveryServer;

typedef struct {
    int socket_fd;
    OSThread thread;
    BOOL thread_running;
    BOOL should_stop;
    OSMutex mutex;
    DiscoveryServer servers[DISCOVERY_MAX_SERVERS];
    int server_count;
    BOOL scanning;
} DiscoveryState;

extern DiscoveryState g_discovery;

int discovery_init(void);
int discovery_start(void);
void discovery_stop(void);
int discovery_send_broadcast(void);
int discovery_get_servers(DiscoveryServer *out, int max_count);
int discovery_get_server_count(void);
int discovery_thread_func(int argc, const char **argv);

#ifdef __cplusplus
}
#endif
