#include "dsu_server.h"
#include "dsu_crc32.h"
#include <coreinit/debug.h>
#include <coreinit/time.h>
#include <coreinit/cache.h>
#include <stdlib.h>
#include <string.h>

DSUServerState g_dsu_server = {
    .socket_fd = -1,
    .thread_running = FALSE,
    .should_stop = FALSE,
    .port = DSU_PORT_DEFAULT,
    .server_id = 0,
    .packets_received = 0,
    .packets_sent = 0,
    .errors = 0,
};

uint32_t dsu_to_wiiu_buttons(uint16_t dsu_buttons) {
    uint32_t wiiu_buttons = 0;
    if (dsu_buttons & DSU_BUTTON_CROSS) wiiu_buttons |= WPAD_PRO_A;
    if (dsu_buttons & DSU_BUTTON_CIRCLE) wiiu_buttons |= WPAD_PRO_B;
    if (dsu_buttons & DSU_BUTTON_SQUARE) wiiu_buttons |= WPAD_PRO_X;
    if (dsu_buttons & DSU_BUTTON_TRIANGLE) wiiu_buttons |= WPAD_PRO_Y;
    if (dsu_buttons & DSU_BUTTON_L1) wiiu_buttons |= WPAD_PRO_L;
    if (dsu_buttons & DSU_BUTTON_R1) wiiu_buttons |= WPAD_PRO_R;
    if (dsu_buttons & DSU_BUTTON_L2) wiiu_buttons |= WPAD_PRO_ZL;
    if (dsu_buttons & DSU_BUTTON_R2) wiiu_buttons |= WPAD_PRO_ZR;
    if (dsu_buttons & DSU_BUTTON_SHARE) wiiu_buttons |= WPAD_PRO_MINUS;
    if (dsu_buttons & DSU_BUTTON_OPTIONS) wiiu_buttons |= WPAD_PRO_PLUS;
    if (dsu_buttons & DSU_BUTTON_L3) wiiu_buttons |= WPAD_PRO_STICK_L;
    if (dsu_buttons & DSU_BUTTON_R3) wiiu_buttons |= WPAD_PRO_STICK_R;
    if (dsu_buttons & DSU_BUTTON_PS) wiiu_buttons |= WPAD_PRO_HOME;
    if (dsu_buttons & 0x0010) wiiu_buttons |= WPAD_PRO_UP;
    if (dsu_buttons & 0x0020) wiiu_buttons |= WPAD_PRO_RIGHT;
    if (dsu_buttons & 0x0040) wiiu_buttons |= WPAD_PRO_DOWN;
    if (dsu_buttons & 0x0080) wiiu_buttons |= WPAD_PRO_LEFT;
    return wiiu_buttons;
}

uint16_t wiiu_to_dsu_buttons(uint32_t wiiu_buttons) {
    uint16_t dsu_buttons = 0;
    if (wiiu_buttons & WPAD_PRO_A) dsu_buttons |= DSU_BUTTON_CROSS;
    if (wiiu_buttons & WPAD_PRO_B) dsu_buttons |= DSU_BUTTON_CIRCLE;
    if (wiiu_buttons & WPAD_PRO_X) dsu_buttons |= DSU_BUTTON_SQUARE;
    if (wiiu_buttons & WPAD_PRO_Y) dsu_buttons |= DSU_BUTTON_TRIANGLE;
    if (wiiu_buttons & WPAD_PRO_L) dsu_buttons |= DSU_BUTTON_L1;
    if (wiiu_buttons & WPAD_PRO_R) dsu_buttons |= DSU_BUTTON_R1;
    if (wiiu_buttons & WPAD_PRO_ZL) dsu_buttons |= DSU_BUTTON_L2;
    if (wiiu_buttons & WPAD_PRO_ZR) dsu_buttons |= DSU_BUTTON_R2;
    if (wiiu_buttons & WPAD_PRO_MINUS) dsu_buttons |= DSU_BUTTON_SHARE;
    if (wiiu_buttons & WPAD_PRO_PLUS) dsu_buttons |= DSU_BUTTON_OPTIONS;
    if (wiiu_buttons & WPAD_PRO_STICK_L) dsu_buttons |= DSU_BUTTON_L3;
    if (wiiu_buttons & WPAD_PRO_STICK_R) dsu_buttons |= DSU_BUTTON_R3;
    if (wiiu_buttons & WPAD_PRO_HOME) dsu_buttons |= DSU_BUTTON_PS;
    if (wiiu_buttons & WPAD_PRO_UP) dsu_buttons |= 0x0010;
    if (wiiu_buttons & WPAD_PRO_RIGHT) dsu_buttons |= 0x0020;
    if (wiiu_buttons & WPAD_PRO_DOWN) dsu_buttons |= 0x0040;
    if (wiiu_buttons & WPAD_PRO_LEFT) dsu_buttons |= 0x0080;
    return dsu_buttons;
}

static void build_header(DSUHeader *header, uint32_t id, uint16_t payload_len) {
    header->magic[0] = 'D';
    header->magic[1] = 'S';
    header->magic[2] = 'U';
    header->magic[3] = 'S';
    header->version = DSU_VERSION;
    header->length = payload_len;
    header->id = id;
    header->crc32 = 0;
}

static void handle_version(DSUServerState *state, struct sockaddr_in *client, socklen_t len) {
    uint8_t packet[DSU_MAX_PACKET];
    DSUHeader *h = (DSUHeader *)packet;
    build_header(h, state->server_id, 6);  // 4 bytes msg_type + 2 bytes version
    uint8_t *p = packet + DSU_HEADER_SIZE;
    uint32_t msg_type = DSU_MSG_VERSION;
    memcpy(p, &msg_type, 4);
    p += 4;
    uint16_t ver = DSU_VERSION;
    memcpy(p, &ver, 2);
    h->crc32 = dsu_crc32_packet(packet, DSU_HEADER_SIZE + 6);
    sendto(state->socket_fd, packet, DSU_HEADER_SIZE + 6, 0, (struct sockaddr *)client, len);
    state->packets_sent++;
}

static void handle_controller_info(DSUServerState *state, struct sockaddr_in *client,
                                   socklen_t len, uint8_t *payload, uint16_t plen) {
    if (plen < 2) return;
    uint8_t flags = payload[0];
    uint8_t slot = payload[1];
    for (int i = 0; i < DSU_MAX_SLOTS; i++) {
        if ((flags & 0x01) && slot != i) continue;
        uint8_t pkt[DSU_MAX_PACKET];
        DSUHeader *h = (DSUHeader *)pkt;
        uint16_t psz = 15;  // 4 bytes msg_type + 11 bytes data
        build_header(h, state->server_id, psz);
        uint8_t *p = pkt + DSU_HEADER_SIZE;
        uint32_t msg_type = DSU_MSG_CONTROLLER;
        memcpy(p, &msg_type, 4);
        p += 4;
        *p++ = i;
        *p++ = state->controllers[i].state;
        *p++ = state->controllers[i].model;
        *p++ = 0;
        memcpy(p, state->controllers[i].mac, 6);
        p += 6;
        *p++ = state->controllers[i].battery;
        h->crc32 = dsu_crc32_packet(pkt, DSU_HEADER_SIZE + psz);
        sendto(state->socket_fd, pkt, DSU_HEADER_SIZE + psz, 0, (struct sockaddr *)client, len);
        state->packets_sent++;
    }
}

static void handle_pad_subscribe(DSUServerState *state, struct sockaddr_in *client,
                                 socklen_t len, uint8_t *payload, uint16_t plen) {
    if (plen < 2) return;
    uint8_t slot = payload[1];
    for (int i = 0; i < 4; i++) {
        if (state->clients[i].id != 0 &&
            state->clients[i].addr.sin_addr.s_addr == client->sin_addr.s_addr &&
            state->clients[i].addr.sin_port == client->sin_port) {
            state->clients[i].subscribed = TRUE;
            state->clients[i].subscribed_slot = slot;
            state->clients[i].last_seen = OSGetTime();
            return;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (state->clients[i].id == 0) {
            state->clients[i].id = state->server_id + 1;
            state->clients[i].addr = *client;
            state->clients[i].addr_len = len;
            state->clients[i].subscribed = TRUE;
            state->clients[i].subscribed_slot = slot;
            state->clients[i].last_seen = OSGetTime();
            state->client_count++;
            return;
        }
    }
}

static void send_pad_data_to_clients(DSUServerState *state) {
    uint8_t pkt[DSU_MAX_PACKET];
    for (int c = 0; c < 4; c++) {
        if (!state->clients[c].subscribed) continue;
        OSTime now = OSGetTime();
        if ((now - state->clients[c].last_seen) > DSU_TIMEOUT_MS * 1000) {
            state->clients[c].subscribed = FALSE;
            state->client_count--;
            continue;
        }
        uint8_t slot = state->clients[c].subscribed_slot;
        if (slot >= DSU_MAX_SLOTS) continue;
        if (state->controllers[slot].state != DSU_STATE_CONNECTED) continue;
        DSUHeader *h = (DSUHeader *)pkt;
        uint16_t psz = 26;  // 4 bytes msg_type + 22 bytes data
        build_header(h, state->server_id, psz);
        uint8_t *p = pkt + DSU_HEADER_SIZE;
        uint32_t msg_type = DSU_MSG_PAD_DATA;
        memcpy(p, &msg_type, 4);
        p += 4;
        uint32_t pc = state->packets_sent;
        memcpy(p, &pc, 4);
        p += 4;
        uint16_t dsu_btn = wiiu_to_dsu_buttons(state->input_data[slot].buttons);
        memset(p, 0, 16);
        p[0] = (uint8_t)(dsu_btn & 0xFF);
        p[1] = (uint8_t)((dsu_btn >> 8) & 0xFF);
        p += 16;
        *p++ = (uint8_t)((state->input_data[slot].left_x + 32768) >> 8);
        *p++ = (uint8_t)((state->input_data[slot].left_y + 32768) >> 8);
        *p++ = (uint8_t)((state->input_data[slot].right_x + 32768) >> 8);
        *p++ = (uint8_t)((state->input_data[slot].right_y + 32768) >> 8);
        h->crc32 = dsu_crc32_packet(pkt, DSU_HEADER_SIZE + psz);
        sendto(state->socket_fd, pkt, DSU_HEADER_SIZE + psz, 0,
               (struct sockaddr *)&state->clients[c].addr, state->clients[c].addr_len);
        state->packets_sent++;
    }
}

int dsu_server_thread(int argc, const char **argv) {
    DSUServerState *state = &g_dsu_server;
    uint8_t buffer[DSU_MAX_PACKET];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Set socket to non-blocking
    int nonblock = 1;
    setsockopt(state->socket_fd, SOL_SOCKET, SO_NONBLOCK, 
               (const char *)&nonblock, sizeof(nonblock));
    
    while (!state->should_stop) {
        int received = recvfrom(state->socket_fd, buffer, DSU_MAX_PACKET, 0,
                                (struct sockaddr *)&client_addr, &client_len);
        if (received <= 0) {
            // No data available - sleep and try again
            OSSleepTicks(OSSecondsToTicks(0.001));  // 1ms
            continue;
        }
        state->packets_received++;
        if (received < DSU_HEADER_SIZE) continue;
        DSUHeader *h = (DSUHeader *)buffer;
        if (h->magic[0] != 'D' || h->magic[1] != 'S' || h->magic[2] != 'U' || h->magic[3] != 'C') continue;
        uint32_t msg_type;
        memcpy(&msg_type, buffer + DSU_HEADER_SIZE, 4);
        uint8_t *payload = buffer + DSU_HEADER_SIZE + 4;
        uint16_t payload_len = received - DSU_HEADER_SIZE - 4;
        switch (msg_type) {
            case DSU_MSG_VERSION:
                handle_version(state, &client_addr, client_len);
                break;
            case DSU_MSG_CONTROLLER:
                handle_controller_info(state, &client_addr, client_len, payload, payload_len);
                break;
            case DSU_MSG_PAD_DATA:
                handle_pad_subscribe(state, &client_addr, client_len, payload, payload_len);
                break;
        }
        send_pad_data_to_clients(state);
    }
    state->thread_running = FALSE;
    return 0;
}

int dsu_server_init(uint16_t port) {
    DSUServerState *state = &g_dsu_server;
    state->port = port;
    state->server_id = (uint32_t)OSGetTime();
    OSInitMutex(&state->mutex);
    for (int i = 0; i < DSU_MAX_SLOTS; i++) {
        state->controllers[i].state = DSU_STATE_DISCONNECTED;
        state->controllers[i].model = DSU_MODEL_GENERIC;
        state->controllers[i].battery = 0;
        state->controllers[i].active = 0;
        memset(state->controllers[i].mac, 0, 6);
    }
    for (int i = 0; i < 4; i++) {
        state->clients[i].id = 0;
        state->clients[i].subscribed = FALSE;
    }
    state->client_count = 0;
    return 0;
}

int dsu_server_start(void) {
    DSUServerState *state = &g_dsu_server;
    if (state->socket_fd >= 0) dsu_server_stop();
    state->socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (state->socket_fd < 0) return -1;
    int reuse = 1;
    setsockopt(state->socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(state->port);
    if (bind(state->socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        socketclose(state->socket_fd);
        state->socket_fd = -1;
        return -1;
    }
    state->should_stop = FALSE;
    static uint8_t thread_stack[8192];
    if (!OSCreateThread(&state->thread, dsu_server_thread, 0, NULL,
                        thread_stack + sizeof(thread_stack), sizeof(thread_stack),
                        1, OS_THREAD_ATTRIB_AFFINITY_CPU0)) {
        socketclose(state->socket_fd);
        state->socket_fd = -1;
        return -1;
    }
    state->thread_running = TRUE;
    OSResumeThread(&state->thread);
    for (int i = 0; i < DSU_MAX_SLOTS; i++) {
        state->controllers[i].state = DSU_STATE_CONNECTED;
        state->controllers[i].model = DSU_MODEL_GENERIC;
        state->controllers[i].battery = 50;
        state->controllers[i].active = 1;
    }
    return 0;
}

void dsu_server_stop(void) {
    DSUServerState *state = &g_dsu_server;
    state->should_stop = TRUE;
    if (state->thread_running) {
        OSJoinThread(&state->thread, NULL);
        state->thread_running = FALSE;
    }
    if (state->socket_fd >= 0) {
        socketclose(state->socket_fd);
        state->socket_fd = -1;
    }
    for (int i = 0; i < DSU_MAX_SLOTS; i++) {
        state->controllers[i].state = DSU_STATE_DISCONNECTED;
    }
}

int dsu_server_get_input(int slot, DSUInputData *data) {
    if (slot < 0 || slot >= DSU_MAX_SLOTS || !data) return 0;
    DSUServerState *state = &g_dsu_server;
    OSLockMutex(&state->mutex);
    memcpy(data, &state->input_data[slot], sizeof(DSUInputData));
    OSUnlockMutex(&state->mutex);
    if (data->valid) {
        OSTime age = OSGetTime() - data->timestamp;
        if (age > DSU_TIMEOUT_MS * 1000) {
            data->valid = FALSE;
        }
    }
    return data->valid;
}

void dsu_server_set_connected(int slot, int connected) {
    if (slot < 0 || slot >= DSU_MAX_SLOTS) return;
    DSUServerState *state = &g_dsu_server;
    state->controllers[slot].state = connected ? DSU_STATE_CONNECTED : DSU_STATE_DISCONNECTED;
    state->controllers[slot].active = connected;
}

void dsu_server_update_input(int slot, uint32_t buttons, int16_t lx, int16_t ly,
                             int16_t rx, int16_t ry, uint8_t l2, uint8_t r2) {
    if (slot < 0 || slot >= DSU_MAX_SLOTS) return;
    DSUServerState *state = &g_dsu_server;
    OSLockMutex(&state->mutex);
    state->input_data[slot].buttons = buttons;
    state->input_data[slot].left_x = lx;
    state->input_data[slot].left_y = ly;
    state->input_data[slot].right_x = rx;
    state->input_data[slot].right_y = ry;
    state->input_data[slot].l2 = l2;
    state->input_data[slot].r2 = r2;
    state->input_data[slot].valid = TRUE;
    state->input_data[slot].timestamp = OSGetTime();
    OSUnlockMutex(&state->mutex);
}
