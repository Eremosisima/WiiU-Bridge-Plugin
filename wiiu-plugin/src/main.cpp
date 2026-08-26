#include <coreinit/debug.h>
#include <coreinit/time.h>
#include <coreinit/energysaver.h>
#include <wups.h>
#include <wups/config/WUPSConfigItemStub.h>
#include <wups/config_api.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <sys/socket.h>
#include <nsysnet/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

WUPS_PLUGIN_NAME("WiiU Bridge");
WUPS_PLUGIN_DESCRIPTION("DSU bridge v23.10 - GamePad coexistence (merge mode) + D-pad filter + dim fix adapter ports");
WUPS_PLUGIN_VERSION("v23.11.0");
WUPS_PLUGIN_AUTHOR("Morgana");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();

#define MAX_CTRL 4
#define DSU_PORT 26760
#define DSU_MSG_VERSION 0x00100000
#define DSU_MSG_MULTI   0x00100003
#define DSU_MSG_PING    0x00100004
#define DSU_MSG_SINGLE  0x00100005

// ---- Wire format v23.1 (all little-endian) ----
// [0:4]  'DSUC'
// [4:6]  proto ver u16le
// [6:8]  payload len u16le
// [8:12] crc32
// [12:16] id
// [16:20] msg_type u32le
// [20+i*24] block i: btn u32le, lx i16le, ly i16le, gx f32le, gy f32le,
//                    irx u16le, iry u16le, d1x d1y d2x d2y u8
// block count derived from packet length
#define BLOCK_OFF 20

static int g_listen_sock = -1;
static int g_client_sock = -1;
static int g_udp_sock = -1;
static int g_log_sock = -1;

static uint32_t g_tcp_recv = 0;
static uint32_t g_udp_recv = 0;
static uint32_t g_packets = 0;
static uint32_t g_frames = 0;
static uint32_t g_inj_vpad = 0;
static uint32_t g_inj_kpad = 0;

struct ctrl_state {
    bool active;
    uint32_t buttons;
    int16_t lx, ly;
    float gyro_x, gyro_y;
    uint16_t ir_x, ir_y;
    uint8_t dot1_x, dot1_y, dot2_x, dot2_y;
};

static struct ctrl_state g_ctrl[MAX_CTRL];
static uint32_t g_last_btn[MAX_CTRL];       // last logged buttons
static uint32_t g_prev_vpad[MAX_CTRL];      // edge detect for VPAD slot
static uint32_t g_prev_kpad[MAX_CTRL];      // edge detect for KPAD slots
static uint64_t g_last_seen[MAX_CTRL];      // tick at last packet per ctrl

// a virtual controller is usable only while packets keep arriving (~0.8s window);
// after that we stop injecting so the REAL GamePad/Wiimotes regain control
static inline bool ctrl_fresh(int i) {
    if (!g_ctrl[i].active) return false;
    return OSTicksToMilliseconds(OSGetSystemTick() - g_last_seen[i]) < 800;
}

// ---- connection emulation state (chan-indexed, chan = idx-1) ----
static KPADConnectCallback g_conn_cb[MAX_CTRL]; // captured from game
static bool g_conn_fired[MAX_CTRL];             // "connected" event sent per chan

// ---- GameCube adapter (nn_hpad) ----
// local mirror of HPADStatus (avoiding the header's static-inline HPADRead wrapper)
struct gc_status {
    int32_t hold, trigger, release;
    int8_t stickX, stickY, substickX, substickY;
    uint8_t triggerL, triggerR, status;
    uint8_t pad_;
    int32_t error;
};
typedef void (*gc_conn_cb_t)(int32_t chan, int32_t status);
static gc_conn_cb_t g_hpad_cb[MAX_CTRL];
static bool g_hpad_fired[MAX_CTRL];
static uint32_t g_prev_hpad[MAX_CTRL];
static uint32_t g_inj_hpad = 0;

// WPAD_PRO -> HPAD button bits
static uint32_t pro_to_gc(uint32_t b, uint8_t *tl, uint8_t *tr) {
    uint32_t v = 0;
    *tl = *tr = 0;
    if (b & WPAD_PRO_BUTTON_A)    v |= 0x0001;   // A
    if (b & WPAD_PRO_BUTTON_B)    v |= 0x0002;   // B
    if (b & WPAD_PRO_BUTTON_X)    v |= 0x0004;   // X
    if (b & WPAD_PRO_BUTTON_Y)    v |= 0x0008;   // Y
    if (b & WPAD_PRO_BUTTON_UP)   v |= 0x0080;
    if (b & WPAD_PRO_BUTTON_DOWN) v |= 0x0040;
    if (b & WPAD_PRO_BUTTON_LEFT) v |= 0x0010;
    if (b & WPAD_PRO_BUTTON_RIGHT)v |= 0x0020;
    if (b & WPAD_PRO_BUTTON_PLUS) v |= 0x0100;   // START
    if (b & WPAD_PRO_TRIGGER_L) { v |= 0x0800; *tl = 150; }
    if (b & WPAD_PRO_TRIGGER_R) { v |= 0x0400; *tr = 150; }
    if (b & WPAD_PRO_TRIGGER_ZL || b & WPAD_PRO_TRIGGER_ZR) v |= 0x0200; // Z
    return v;
}

static int8_t gc_axis(int16_t raw) {
    int32_t v = ((int32_t)raw * 56) / 24000;
    if (v > 56) v = 56;
    if (v < -56) v = -56;
    return (int8_t)v;
}

// ---------- logging ----------
static void log_line(const char *msg) {
    OSReport("[Bridge] %s", msg);
    if (g_log_sock >= 0) {
        send(g_log_sock, msg, strlen(msg), 0);
        send(g_log_sock, "\n", 1, 0);
    }
}

static void log_fmt(const char *fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log_line(buf);
}

// ---------- little-endian readers (host-endian agnostic) ----------
static inline uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline int16_t rd16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline float rdf32(const uint8_t *p) {
    uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

static uint32_t crc32_calc(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

// ---------- button mapping WPAD_PRO -> VPAD ----------
static uint32_t pro_to_vpad(uint32_t b) {
    uint32_t v = 0;
    if (b & WPAD_PRO_BUTTON_A)           v |= VPAD_BUTTON_A;
    if (b & WPAD_PRO_BUTTON_B)           v |= VPAD_BUTTON_B;
    if (b & WPAD_PRO_BUTTON_X)           v |= VPAD_BUTTON_X;
    if (b & WPAD_PRO_BUTTON_Y)           v |= VPAD_BUTTON_Y;
    if (b & WPAD_PRO_BUTTON_UP)          v |= VPAD_BUTTON_UP;
    if (b & WPAD_PRO_BUTTON_DOWN)        v |= VPAD_BUTTON_DOWN;
    if (b & WPAD_PRO_BUTTON_LEFT)        v |= VPAD_BUTTON_LEFT;
    if (b & WPAD_PRO_BUTTON_RIGHT)       v |= VPAD_BUTTON_RIGHT;
    if (b & WPAD_PRO_TRIGGER_ZL)         v |= VPAD_BUTTON_ZL;
    if (b & WPAD_PRO_TRIGGER_ZR)         v |= VPAD_BUTTON_ZR;
    if (b & WPAD_PRO_TRIGGER_L)          v |= VPAD_BUTTON_L;
    if (b & WPAD_PRO_TRIGGER_R)          v |= VPAD_BUTTON_R;
    if (b & WPAD_PRO_BUTTON_PLUS)        v |= VPAD_BUTTON_PLUS;
    if (b & WPAD_PRO_BUTTON_MINUS)       v |= VPAD_BUTTON_MINUS;
    if (b & WPAD_PRO_BUTTON_HOME)        v |= VPAD_BUTTON_HOME;
    if (b & WPAD_PRO_BUTTON_STICK_L)     v |= VPAD_BUTTON_STICK_L;
    if (b & WPAD_PRO_BUTTON_STICK_R)     v |= VPAD_BUTTON_STICK_R;
    // stick emulation for games that read d-pad from stick
    if (b & WPAD_PRO_STICK_L_EMULATION_UP)    v |= VPAD_STICK_L_EMULATION_UP;
    if (b & WPAD_PRO_STICK_L_EMULATION_DOWN)  v |= VPAD_STICK_L_EMULATION_DOWN;
    if (b & WPAD_PRO_STICK_L_EMULATION_LEFT)  v |= VPAD_STICK_L_EMULATION_LEFT;
    if (b & WPAD_PRO_STICK_L_EMULATION_RIGHT) v |= VPAD_STICK_L_EMULATION_RIGHT;
    return v;
}

static float stick_norm(int16_t raw) {
    float f = raw / 1024.0f;
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    return f;
}

// ---------- packet processing ----------
// Physical D-pads cannot press opposite directions at once; games may glitch on
// such impossible states (e.g. LEFT+RIGHT). Keep UP over DOWN, LEFT over RIGHT.
static uint32_t dpad_sanitize(uint32_t b) {
    if ((b & WPAD_PRO_BUTTON_UP) && (b & WPAD_PRO_BUTTON_DOWN))
        b &= ~(uint32_t)WPAD_PRO_BUTTON_DOWN;
    if ((b & WPAD_PRO_BUTTON_LEFT) && (b & WPAD_PRO_BUTTON_RIGHT))
        b &= ~(uint32_t)WPAD_PRO_BUTTON_RIGHT;
    return b;
}

static void parse_block(const uint8_t *blk, struct ctrl_state *c) {
    c->buttons = dpad_sanitize(rd32(blk + 0));
    c->lx      = rd16(blk + 4);
    c->ly      = rd16(blk + 6);
    c->gyro_x  = rdf32(blk + 8);
    c->gyro_y  = rdf32(blk + 12);
    c->ir_x    = rd16(blk + 16);
    c->ir_y    = rd16(blk + 18);
    c->dot1_x  = blk[20]; c->dot1_y = blk[21];
    c->dot2_x  = blk[22]; c->dot2_y = blk[23];
}

static void process_packet(const uint8_t *buf, int n) {
    if (n < 20 || memcmp(buf, "DSUC", 4) != 0) {
        log_fmt("NOT DSU (%d bytes)", n);
        return;
    }
    uint32_t mt = rd32(buf + 16);

    if (mt == DSU_MSG_MULTI) {
        if (n < BLOCK_OFF + 24) {
            log_fmt("MULTI too short (%d bytes)", n);
            return;
        }
        uint32_t avail = (uint32_t)(n - BLOCK_OFF) / 24;
        uint32_t count = avail > MAX_CTRL ? MAX_CTRL : avail;
        bool changed = false;
        for (uint32_t i = 0; i < count && (BLOCK_OFF + (i + 1) * 24) <= (uint32_t)n; i++) {
            parse_block(buf + BLOCK_OFF + i * 24, &g_ctrl[i]);
            g_ctrl[i].active = true;
            g_last_seen[i] = OSGetSystemTick();
            if (g_ctrl[i].buttons != g_last_btn[i]) {
                log_fmt("C%u btn=0x%08x lx=%d ly=%d ir=(%u,%u)",
                        i, g_ctrl[i].buttons, g_ctrl[i].lx, g_ctrl[i].ly,
                        g_ctrl[i].ir_x, g_ctrl[i].ir_y);
                g_last_btn[i] = g_ctrl[i].buttons;
                changed = true;
            }
        }
        if (changed || (g_packets % 300) == 0)
            log_fmt("MULTI ok count=%u pkt=%u", count, g_packets);
        g_packets++;
    } else if (mt == DSU_MSG_SINGLE) {
        // single-slot packet from a dedicated client (e.g. one phone per player):
        // [20] slot u8, [21:24] pad, [24:48] block. Only that slot is updated,
        // so several clients can coexist without clobbering each other.
        if (n < 48) {
            log_fmt("SINGLE too short (%d bytes)", n);
            return;
        }
        uint32_t slot = buf[20];
        if (slot < MAX_CTRL) {
            parse_block(buf + 24, &g_ctrl[slot]);
            g_ctrl[slot].active = true;
            g_last_seen[slot] = OSGetSystemTick();
            if (g_ctrl[slot].buttons != g_last_btn[slot]) {
                log_fmt("S%u btn=0x%08x lx=%d ly=%d",
                        slot, g_ctrl[slot].buttons, g_ctrl[slot].lx, g_ctrl[slot].ly);
                g_last_btn[slot] = g_ctrl[slot].buttons;
            }
        }
        g_packets++;
    } else if (mt == DSU_MSG_PING) {
        log_line("PING received");
        g_packets++;
    }
    // VERSION handled in caller (needs reply addr)
}

static void send_version_reply(const struct sockaddr_in *cli) {
    if (g_udp_sock < 0 || !cli) return;
    // DSUS reply, fields written explicitly little-endian on wire
    uint8_t r[24] = {0};
    r[0]='D'; r[1]='S'; r[2]='U'; r[3]='S';
    wr32(r + 4, 1001);            // proto ver
    wr32(r + 8, crc32_calc(r, 8));  // placeholder crc (plugin never verifies)
    wr32(r + 12, 99999);          // id
    wr32(r + 16, DSU_MSG_VERSION);
    wr32(r + 20, 1001);           // version
    sendto(g_udp_sock, r, 24, 0, (const struct sockaddr *)cli, sizeof(*cli));
}

static void send_ping_reply(const struct sockaddr_in *cli, const uint8_t *in) {
    if (g_udp_sock < 0 || !cli) return;
    uint8_t r[24] = {0};
    r[0]='D'; r[1]='S'; r[2]='U'; r[3]='S';
    wr32(r + 4, 1001);
    wr32(r + 8, crc32_calc(r, 8));
    wr32(r + 12, 99999);
    wr32(r + 16, DSU_MSG_PING);
    memcpy(r + 20, in + 20, 4);
    sendto(g_udp_sock, r, 24, 0, (const struct sockaddr *)cli, sizeof(*cli));
}

static void poll_sockets() {
    // accept TCP client
    if (g_listen_sock >= 0 && g_client_sock < 0) {
        struct pollfd pfd = {g_listen_sock, POLLIN, 0};
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            struct sockaddr_in ca;
            socklen_t cl = sizeof(ca);
            g_client_sock = accept(g_listen_sock, (struct sockaddr *)&ca, &cl);
            if (g_client_sock >= 0) log_line("TCP client connected");
        }
    }
    // TCP data
    if (g_client_sock >= 0) {
        struct pollfd pfd = {g_client_sock, POLLIN, 0};
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[512];
            int n = recv(g_client_sock, buf, sizeof(buf), 0);
            g_tcp_recv++;
            if (n <= 0) {
                socketclose(g_client_sock);
                g_client_sock = -1;
                log_line("TCP client disconnected");
            } else {
                process_packet(buf, n);
            }
        }
    }
    // UDP data
    if (g_udp_sock >= 0) {
        struct pollfd pfd = {g_udp_sock, POLLIN, 0};
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[512];
            struct sockaddr_in cli;
            socklen_t cl = sizeof(cli);
            int n = recvfrom(g_udp_sock, buf, sizeof(buf), 0, (struct sockaddr *)&cli, &cl);
            if (n > 0) {
                g_udp_recv++;
                if (n >= 20 && memcmp(buf, "DSUC", 4) == 0) {
                    uint32_t mt = rd32(buf + 16);
                    if (mt == DSU_MSG_VERSION) send_version_reply(&cli);
                    else if (mt == DSU_MSG_PING) send_ping_reply(&cli, buf);
                    else process_packet(buf, n);
                } else {
                    process_packet(buf, n);
                }
            }
        }
    }
    // fire connect/disconnect events for virtual Pro Controllers (direct chan map)
    for (int ch = WPAD_CHAN_0; ch < MAX_CTRL; ch++) {
        if (g_ctrl[ch].active && !g_conn_fired[ch]) {
            g_conn_fired[ch] = true;
            if (g_conn_cb[ch]) {
                log_fmt("FIRE connect ch=%d", ch);
                g_conn_cb[ch]((KPADChan)ch, WPAD_ERROR_NONE);
            }
        } else if (!ctrl_fresh(ch) && g_conn_fired[ch]) {
            g_conn_fired[ch] = false;
            if (g_conn_cb[ch]) {
                log_fmt("FIRE disconnect ch=%d", ch);
                g_conn_cb[ch]((KPADChan)ch, WPAD_ERROR_NO_CONTROLLER);
            }
        }
    }
    // fire connect/disconnect for virtual GC ports (chan i <-> ctrl[i])
    for (int ch = 0; ch < MAX_CTRL; ch++) {
        if (ctrl_fresh(ch) && !g_hpad_fired[ch]) {
            g_hpad_fired[ch] = true;
            if (g_hpad_cb[ch]) {
                log_fmt("FIRE gc-connect port=%d", ch);
                g_hpad_cb[ch](ch, 0);
            }
        } else if (!ctrl_fresh(ch) && g_hpad_fired[ch]) {
            g_hpad_fired[ch] = false;
            if (g_hpad_cb[ch]) {
                log_fmt("FIRE gc-disconnect port=%d", ch);
                g_hpad_cb[ch](ch, -1);
            }
        }
    }
    // heartbeat ~10s; hook call rate measured ~1200/s (VPAD+KPAD combined)
    static uint64_t last_hb = 0;
    (void)last_hb;
    if (++g_frames % 12000 == 0) {
        log_fmt("SYS act=[%d %d %d %d] pkt=%u injV=%u injK=%u injH=%u tcp=%u udp=%u",
                ctrl_fresh(0) ? 1 : 0, ctrl_fresh(1) ? 1 : 0,
                ctrl_fresh(2) ? 1 : 0, ctrl_fresh(3) ? 1 : 0,
                g_packets, g_inj_vpad, g_inj_kpad, g_inj_hpad, g_tcp_recv, g_udp_recv);
    } else if (g_frames % 12000 == 6000) {
        // full connection-state telemetry for remote debugging
        log_fmt("STAT f=[%d %d %d %d] kfire=%d%d%d hfire=%d%d%d%d kcb=%p,%p,%p hcb=%p",
                ctrl_fresh(0) ? 1 : 0, ctrl_fresh(1) ? 1 : 0,
                ctrl_fresh(2) ? 1 : 0, ctrl_fresh(3) ? 1 : 0,
                g_conn_fired[0], g_conn_fired[1], g_conn_fired[2],
                g_hpad_fired[0], g_hpad_fired[1], g_hpad_fired[2], g_hpad_fired[3],
                (void *)g_conn_cb[0], (void *)g_conn_cb[1], (void *)g_conn_cb[2],
                (void *)g_hpad_cb[0]);
    }
}

// ---------- hooks ----------
DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer,
              uint32_t buffer_size, VPADReadError *error) {
    poll_sockets();
    int32_t result = real_VPADRead(chan, buffer, buffer_size, error);

    if (buffer && buffer_size > 0 && g_ctrl[0].active &&
        error && *error == VPAD_READ_SUCCESS && result > 0 && ctrl_fresh(0)) {
        // MERGE instead of overwrite: real GamePad keeps working alongside the
        // virtual input (buttons OR-ed; virtual stick only when actively used)
        uint32_t virt = pro_to_vpad(g_ctrl[0].buttons);
        uint32_t hold = virt | (uint32_t)buffer[0].hold;
        buffer[0].trigger |= virt & ~g_prev_vpad[0];
        buffer[0].release |= g_prev_vpad[0] & ~virt;
        g_prev_vpad[0]     = virt;
        buffer[0].hold     = hold;
        if (g_ctrl[0].lx != 0 || g_ctrl[0].ly != 0) {
            buffer[0].leftStick.x  = stick_norm(g_ctrl[0].lx);
            buffer[0].leftStick.y  = stick_norm(g_ctrl[0].ly);
            buffer[0].rightStick.x = stick_norm(g_ctrl[0].lx);
            buffer[0].rightStick.y = stick_norm(g_ctrl[0].ly);
        }
        if (g_ctrl[0].ir_x != 0 || g_ctrl[0].ir_y != 0) {
            buffer[0].tpNormal.x = (uint16_t)((g_ctrl[0].ir_x * 853) / 1023);
            buffer[0].tpNormal.y = (uint16_t)((g_ctrl[0].ir_y * 479) / 767);
            buffer[0].tpNormal.touched = 1;
            buffer[0].tpFiltered1 = buffer[0].tpNormal;
            buffer[0].tpFiltered2 = buffer[0].tpNormal;
        }
        g_inj_vpad++;
        static uint32_t once = 0;
        if (++once <= 3)
            log_fmt("VPAD INJECT hold=0x%08x", hold);
    }
    return result;
}

DECL_FUNCTION(uint32_t, KPADRead, KPADChan chan, KPADStatus *data, uint32_t size) {
    poll_sockets();
    uint32_t n = real_KPADRead(chan, data, size);

    // KPAD chans map directly: KPADRead(chan) <- ctrl[chan]
    int idx = (int)chan;
    static uint32_t diag_kr = 0;
    if (diag_kr < 5) { diag_kr++; log_fmt("DIAG KPADRead fired ch=%d n=%u", chan, n); }
    if (data && size > 0 && idx >= 0 && idx < MAX_CTRL && ctrl_fresh(idx)) {
        memset(&data[0], 0, sizeof(KPADStatus));
        data[0].extensionType = WPAD_EXT_PRO_CONTROLLER;
        data[0].error = KPAD_ERROR_OK;
        data[0].pro.hold    = g_ctrl[idx].buttons;
        data[0].pro.trigger = g_ctrl[idx].buttons & ~g_prev_kpad[idx];
        data[0].pro.release = g_prev_kpad[idx] & ~g_ctrl[idx].buttons;
        g_prev_kpad[idx]    = g_ctrl[idx].buttons;
        data[0].pro.leftStick.x  = stick_norm(g_ctrl[idx].lx);
        data[0].pro.leftStick.y  = stick_norm(g_ctrl[idx].ly);
        data[0].pro.rightStick.x = stick_norm(g_ctrl[idx].lx);
        data[0].pro.rightStick.y = stick_norm(g_ctrl[idx].ly);
        g_inj_kpad++;
        if (n == 0) n = 1;
        static uint32_t oncek = 0;
        if (++oncek <= 3)
            log_fmt("KPAD[%d] INJECT hold=0x%08x", idx, g_ctrl[idx].buttons);
    }
    return n;
}

DECL_FUNCTION(uint32_t, KPADReadEx, KPADChan chan, KPADStatus *data,
              uint32_t size, KPADError *error) {
    poll_sockets();
    uint32_t n = real_KPADReadEx(chan, data, size, error);

    int idx = (int)chan;
    static uint32_t diag_kre = 0;
    if (diag_kre < 5) { diag_kre++; log_fmt("DIAG KPADReadEx fired ch=%d n=%u", chan, n); }
    if (data && size > 0 && idx >= 0 && idx < MAX_CTRL && ctrl_fresh(idx)) {
        memset(&data[0], 0, sizeof(KPADStatus));
        data[0].extensionType = WPAD_EXT_PRO_CONTROLLER;
        data[0].error = KPAD_ERROR_OK;
        data[0].pro.hold    = g_ctrl[idx].buttons;
        data[0].pro.trigger = g_ctrl[idx].buttons & ~g_prev_kpad[idx];
        data[0].pro.release = g_prev_kpad[idx] & ~g_ctrl[idx].buttons;
        g_prev_kpad[idx]    = g_ctrl[idx].buttons;
        data[0].pro.leftStick.x  = stick_norm(g_ctrl[idx].lx);
        data[0].pro.leftStick.y  = stick_norm(g_ctrl[idx].ly);
        data[0].pro.rightStick.x = stick_norm(g_ctrl[idx].lx);
        data[0].pro.rightStick.y = stick_norm(g_ctrl[idx].ly);
        if (error) *error = KPAD_ERROR_OK;
        if (n == 0) n = 1;
    }
    return n;
}

// Games ask "is there a controller on this channel?" -> say YES for virtual Pros
DECL_FUNCTION(int32_t, WPADProbe, WPADChan channel, WPADExtensionType *outExtensionType) {
    poll_sockets();
    int32_t r = real_WPADProbe(channel, outExtensionType);
    static uint32_t diag_p = 0;
    if (diag_p < 12) {
        diag_p++;
        log_fmt("DIAG Probe ch=%d r=%d ext=0x%02x",
                channel, r, outExtensionType ? (uint32_t)*outExtensionType : 0xFF);
    }
    int idx = (int)channel;
    if (channel >= WPAD_CHAN_0 && idx < MAX_CTRL && ctrl_fresh(idx)) {
        if (outExtensionType) *outExtensionType = WPAD_EXT_PRO_CONTROLLER;
        g_inj_kpad++;
        return WPAD_ERROR_NONE;
    }
    return r;
}

// Many modern games read samples through the unified ring buffer
DECL_FUNCTION(void, KPADGetUnifiedWpadStatus, KPADChan chan,
              KPADUnifiedWpadStatus *buffer, uint32_t count) {
    poll_sockets();
    static uint32_t diag_u = 0;
    if (diag_u < 6) { diag_u++; log_fmt("DIAG Unified ch=%d cnt=%u", chan, count); }
    int idx = (int)chan;
    if (!buffer || count == 0 || chan < WPAD_CHAN_0 || idx >= MAX_CTRL || !ctrl_fresh(idx)) {
        real_KPADGetUnifiedWpadStatus(chan, buffer, count);
        return;
    }
    memset(buffer, 0, sizeof(KPADUnifiedWpadStatus) * count);
    buffer[0].format       = WPAD_FMT_PRO_CONTROLLER;
    buffer[0].pro.buttons  = g_ctrl[idx].buttons;
    buffer[0].pro.leftStick.x  = (int16_t)(stick_norm(g_ctrl[idx].lx) * 1400.0f);
    buffer[0].pro.leftStick.y  = (int16_t)(stick_norm(g_ctrl[idx].ly) * 1400.0f);
    buffer[0].pro.rightStick   = buffer[0].pro.leftStick;
    g_inj_kpad++;
}

// Capture the game's connection callback so we can fire connect/disconnect events
DECL_FUNCTION(KPADConnectCallback, KPADSetConnectCallback, KPADChan chan,
              KPADConnectCallback callback) {
    if (chan >= WPAD_CHAN_0 && chan < MAX_CTRL) {
        g_conn_cb[chan] = callback;
        static uint32_t d = 0;
        if (d < 6 && callback) { d++; log_fmt("DIAG SetConnectCB ch=%d cb=%p", chan, callback); }
    }
    return real_KPADSetConnectCallback(chan, callback);
}

// Game configures sampling per channel; real padscore rejects disconnected chans
DECL_FUNCTION(WPADError, WPADSetDataFormat, WPADChan channel, WPADDataFormat format) {
    int idx = (int)channel;
    WPADError r = real_WPADSetDataFormat(channel, format);
    static uint32_t d = 0;
    if (d < 10) { d++; log_fmt("DIAG SetDataFormat ch=%d fmt=0x%x -> %d", channel, format, r); }
    if (channel >= WPAD_CHAN_0 && idx < MAX_CTRL && ctrl_fresh(idx) && r != WPAD_ERROR_NONE) {
        return WPAD_ERROR_NONE;
    }
    return r;
}

// Battery: virtual Pros always report full charge
DECL_FUNCTION(uint8_t, WPADGetBatteryLevel, WPADChan channel) {
    static uint32_t d = 0;
    if (d < 8) { d++; log_fmt("DIAG Battery ch=%d real=%u", channel, real_WPADGetBatteryLevel(channel)); }
    int idx = (int)channel;
    if (channel >= WPAD_CHAN_0 && idx < MAX_CTRL && ctrl_fresh(idx)) return 4;
    return real_WPADGetBatteryLevel(channel);
}

// ---- GameCube adapter: pure polling, the easy door ----
DECL_FUNCTION(int32_t, HPADRead, int32_t chan, struct gc_status *buffers, int32_t count) {
    poll_sockets();
    int32_t n = real_HPADRead(chan, buffers, count);
    static uint32_t dh = 0;
    if (dh < 8) { dh++; log_fmt("DIAG HPADRead port=%d n=%d", chan, n); }

    int idx = chan;
    if (buffers && count > 0 && idx >= 0 && idx < MAX_CTRL && ctrl_fresh(idx)) {
        uint8_t tl = 0, tr = 0;
        uint32_t hold = pro_to_gc(g_ctrl[idx].buttons, &tl, &tr);
        int fill = count < 16 ? count : 16;
        for (int b = 0; b < fill; b++) {
            memset(&buffers[b], 0, sizeof(struct gc_status));
            buffers[b].hold    = hold;
            buffers[b].trigger = hold & ~g_prev_hpad[idx];
            buffers[b].release = g_prev_hpad[idx] & ~hold;
            buffers[b].stickX   = gc_axis(g_ctrl[idx].lx);
            buffers[b].stickY   = gc_axis(g_ctrl[idx].ly);
            buffers[b].substickX= gc_axis(g_ctrl[idx].lx);
            buffers[b].substickY= gc_axis(g_ctrl[idx].ly);
            buffers[b].triggerL = tl;
            buffers[b].triggerR = tr;
            buffers[b].status   = 0;
            buffers[b].error    = 0;
        }
        g_prev_hpad[idx] = hold;
        g_inj_hpad++;
        static uint32_t once_h = 0;
        if (++once_h <= 3)
            log_fmt("GC INJECT port=%d hold=0x%08x", idx, hold);
        if (n <= 0) n = 1;
    }
    return n;
}

DECL_FUNCTION(gc_conn_cb_t, HPADSetConnectCallback, int32_t chan, gc_conn_cb_t callback) {
    if (chan >= 0 && chan < MAX_CTRL) {
        g_hpad_cb[chan] = callback;
        static uint32_t d = 0;
        if (d < 6 && callback) { d++; log_fmt("DIAG HPADSetConnectCB port=%d cb=%p", chan, callback); }
    }
    return real_HPADSetConnectCallback(chan, callback);
}

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);
WUPS_MUST_REPLACE(KPADRead, WUPS_LOADER_LIBRARY_PADSCORE, KPADRead);
WUPS_MUST_REPLACE(KPADReadEx, WUPS_LOADER_LIBRARY_PADSCORE, KPADReadEx);
WUPS_MUST_REPLACE(WPADProbe, WUPS_LOADER_LIBRARY_PADSCORE, WPADProbe);
WUPS_MUST_REPLACE(KPADGetUnifiedWpadStatus, WUPS_LOADER_LIBRARY_PADSCORE, KPADGetUnifiedWpadStatus);
WUPS_MUST_REPLACE(KPADSetConnectCallback, WUPS_LOADER_LIBRARY_PADSCORE, KPADSetConnectCallback);
WUPS_MUST_REPLACE(WPADSetDataFormat, WUPS_LOADER_LIBRARY_PADSCORE, WPADSetDataFormat);
WUPS_MUST_REPLACE(WPADGetBatteryLevel, WUPS_LOADER_LIBRARY_PADSCORE, WPADGetBatteryLevel);
WUPS_MUST_REPLACE(HPADRead, WUPS_LOADER_LIBRARY_NN_HPAD, HPADRead);
WUPS_MUST_REPLACE(HPADSetConnectCallback, WUPS_LOADER_LIBRARY_NN_HPAD, HPADSetConnectCallback);

// ---------- config menu ----------
WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
    WUPSConfigCategory root = WUPSConfigCategory(rootHandle);
    char s[128];

    snprintf(s, sizeof(s), "v23.10 | LOG:%s TCPcli:%s",
             g_log_sock >= 0 ? "ON" : "off",
             g_client_sock >= 0 ? "YES" : "no");
    root.add(WUPSConfigItemStub::Create(s));

    snprintf(s, sizeof(s), "Pkt:%u InjVPAD:%u InjKPAD:%u", g_packets, g_inj_vpad, g_inj_kpad);
    root.add(WUPSConfigItemStub::Create(s));

    for (int i = 0; i < MAX_CTRL; i++) {
        snprintf(s, sizeof(s), "C%d [%s] btn=0x%08x L=(%d,%d)",
                 i, g_ctrl[i].active ? "ACTIVE" : "-----",
                 g_ctrl[i].buttons, g_ctrl[i].lx, g_ctrl[i].ly);
        root.add(WUPSConfigItemStub::Create(s));
    }

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
    if (ip) snprintf(s, sizeof(s), "IP: %u.%u.%u.%u :%d",
                     (ip>>24)&0xFF,(ip>>16)&0xFF,(ip>>8)&0xFF,ip&0xFF, DSU_PORT);
    else snprintf(s, sizeof(s), "IP: not connected");
    root.add(WUPSConfigItemStub::Create(s));

    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}
void ConfigMenuClosedCallback() {}

INITIALIZE_PLUGIN() {
    WUPSConfigAPIOptionsV1 o = {.name = "WiiU Bridge"};
    WUPSConfigAPI_Init(o, ConfigMenuOpenedCallback, ConfigMenuClosedCallback);
}

ON_APPLICATION_START() {
    // virtual players move constantly but the OS idle timer never sees real
    // input -> keep the screen alive while a game is running
    IMDisableDim();
    // TCP listener for DSU-over-TCP clients
    g_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen_sock >= 0) {
        int r = 1; setsockopt(g_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&r, sizeof(r));
        struct sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons(DSU_PORT);
        if (bind(g_listen_sock, (struct sockaddr*)&a, sizeof(a)) < 0) {
            socketclose(g_listen_sock); g_listen_sock = -1;
        } else if (listen(g_listen_sock, 1) < 0) {
            socketclose(g_listen_sock); g_listen_sock = -1;
        }
    }
    // UDP DSU socket
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp_sock >= 0) {
        int r = 1; setsockopt(g_udp_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&r, sizeof(r));
        int b = 1; setsockopt(g_udp_sock, SOL_SOCKET, SO_BROADCAST, (const char*)&b, sizeof(b));
        struct sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons(DSU_PORT);
        if (bind(g_udp_sock, (struct sockaddr*)&a, sizeof(a)) < 0) {
            socketclose(g_udp_sock); g_udp_sock = -1;
        }
    }
    // outbound log channel to PC
    g_log_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_log_sock >= 0) {
        struct sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(42069);
        inet_aton("192.168.100.61", &a.sin_addr);
        if (connect(g_log_sock, (struct sockaddr*)&a, sizeof(a)) == 0) {
            log_fmt("v23.10 up | TCP:%d UDP:%d", g_listen_sock, g_udp_sock);
        } else {
            socketclose(g_log_sock);
            g_log_sock = -1;
        }
    }
    memset(g_ctrl, 0, sizeof(g_ctrl));
    memset(g_last_btn, 0, sizeof(g_last_btn));
    memset(g_prev_vpad, 0, sizeof(g_prev_vpad));
    memset(g_prev_kpad, 0, sizeof(g_prev_kpad));
    // CRITICAL: callbacks belong to the PREVIOUS app's address space; keeping
    // them across app switches means calling unmapped code -> crash
    memset(g_conn_cb, 0, sizeof(g_conn_cb));
    memset(g_hpad_cb, 0, sizeof(g_hpad_cb));
    memset(g_conn_fired, 0, sizeof(g_conn_fired));
    memset(g_hpad_fired, 0, sizeof(g_hpad_fired));
    memset(g_prev_hpad, 0, sizeof(g_prev_hpad));
}

ON_APPLICATION_ENDS() {
    IMEnableDim();
    if (g_client_sock >= 0) socketclose(g_client_sock);
    if (g_listen_sock >= 0) socketclose(g_listen_sock);
    if (g_udp_sock >= 0) socketclose(g_udp_sock);
    if (g_log_sock >= 0) socketclose(g_log_sock);
    g_client_sock = g_listen_sock = g_udp_sock = g_log_sock = -1;
}
