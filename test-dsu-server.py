#!/usr/bin/env python3
"""
DSU (CemuHook) Protocol Server Emulator for testing WiiU Bridge Plugin.
Listens on UDP 26760, responds to version/controller/pad-data messages.
Also handles discovery on UDP 26761.
"""

import socket
import struct
import time
import threading
import sys

DSU_PORT = 26760
DISCOVERY_PORT = 26761
SERVER_NAME = "PC-DSU-Emulator"

# DSU Protocol constants
DSU_MSG_VERSION = 0x100000
DSU_MSG_CONTROLLER = 0x100001
DSU_MSG_PAD_DATA = 0x100002

# Controller state (simulated Xbox 360 gamepad)
controller_state = {
    'connected': True,
    'buttons': 0,
    'lx': 128, 'ly': 128,  # center
    'rx': 128, 'ry': 128,  # center
    'l2': 0, 'r2': 0,
}


def crc32(data):
    """CRC32 with polynomial 0xEDB88320"""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF


def build_dsu_header(msg_type, payload_data, server_id):
    """Build DSU header (16 bytes) + payload"""
    header = bytearray(16)
    header[0:4] = b'DSUS'  # magic (server)
    struct.pack_into('<HH', header, 4, 1001, len(payload_data))
    struct.pack_into('<I', header, 12, server_id)
    # CRC covers header[0:8] + header[12:16] + payload
    crc_data = bytes(header[0:8]) + bytes(header[12:16]) + bytes(payload_data)
    struct.pack_into('<I', header, 8, crc32(crc_data))
    return bytes(header) + bytes(payload_data)


def build_version_response(server_id):
    """Build version response"""
    payload = struct.pack('<II', DSU_MSG_VERSION, 1001)  # msg_type + version
    return build_dsu_header(DSU_MSG_VERSION, payload, server_id)


def build_controller_response(server_id, slot):
    """Build controller info response"""
    payload = bytearray(20)
    struct.pack_into('<I', payload, 0, DSU_MSG_CONTROLLER)  # msg_type
    payload[4] = slot  # slot
    payload[5] = 2 if controller_state['connected'] else 0  # state (2=connected)
    payload[6] = 3  # model (generic)
    payload[7] = 2  # connection type (USB)
    payload[8:14] = bytes([0x01, 0x02, 0x03, 0x04, 0x05, 0x06])  # MAC
    payload[14] = 80  # battery
    payload[15] = 1 if controller_state['connected'] else 0  # active
    return build_dsu_header(DSU_MSG_CONTROLLER, payload, server_id)


def build_pad_data_response(server_id, slot, packet_counter):
    """Build pad data response"""
    payload = bytearray(104)
    struct.pack_into('<I', payload, 0, DSU_MSG_PAD_DATA)  # msg_type
    struct.pack_into('<I', payload, 4, packet_counter)

    # Buttons (bitmask, PS4 layout)
    buttons = controller_state['buttons']
    struct.pack_into('<I', payload, 8, buttons)

    # Analog sticks (0-255, 128=center)
    payload[12] = controller_state['lx']  # left stick X
    payload[13] = controller_state['ly']  # left stick Y
    payload[14] = controller_state['rx']  # right stick X
    payload[15] = controller_state['ry']  # right stick Y

    # Triggers
    payload[16] = controller_state['l2']  # L2
    payload[17] = controller_state['r2']  # R2

    return build_dsu_header(DSU_MSG_PAD_DATA, payload, server_id)


def handle_client_message(data, addr, sock, server_id):
    """Handle incoming DSU message from client"""
    if len(data) < 20:
        print(f"  Message too short: {len(data)} bytes")
        return

    # Check magic
    magic = data[0:4]
    if magic != b'DSUC':
        print(f"  Bad magic: {magic}")
        return

    # Parse header
    version, payload_len = struct.unpack_from('<HH', data, 4)
    stored_crc = struct.unpack_from('<I', data, 8)[0]
    client_id = struct.unpack_from('<I', data, 12)[0]

    # Parse message type (first 4 bytes of payload)
    msg_type = struct.unpack_from('<I', data, 16)[0]

    print(f"  From {addr}: type=0x{msg_type:08x} client_id={client_id} payload_len={payload_len}")

    if msg_type == DSU_MSG_VERSION:
        print(f"  -> Version response")
        resp = build_version_response(server_id)
        sock.sendto(resp, addr)

    elif msg_type == DSU_MSG_CONTROLLER:
        # Read slot from payload
        slot = data[20] if len(data) > 20 else 0
        print(f"  -> Controller info (slot {slot})")
        resp = build_controller_response(server_id, slot)
        sock.sendto(resp, addr)

    elif msg_type == DSU_MSG_PAD_DATA:
        # Read which slots are requested
        if len(data) > 21:
            slot_count = data[20]
            slots = list(data[21:21 + slot_count])
        else:
            slots = [0]
        print(f"  -> Pad data for slots: {slots}")
        for slot in slots:
            resp = build_pad_data_response(server_id, slot, packet_counter[0])
            sock.sendto(resp, addr)
        packet_counter[0] += 1


def dsu_server_thread():
    """Main DSU server thread"""
    global server_running
    server_id = int(time.time()) & 0xFFFFFFFF

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', DSU_PORT))
    sock.settimeout(1.0)

    print(f"DSU Server listening on UDP {DSU_PORT}", flush=True)
    print(f"Server ID: {server_id}", flush=True)

    while server_running:
        try:
            data, addr = sock.recvfrom(512)
            handle_client_message(data, addr, sock, server_id)
        except socket.timeout:
            continue
        except Exception as e:
            print(f"DSU server error: {e}", flush=True)

    sock.close()
    print("DSU server stopped", flush=True)


def discovery_server_thread():
    """Discovery server thread - responds to broadcast queries"""
    global server_running

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(('0.0.0.0', DISCOVERY_PORT))
    sock.settimeout(1.0)

    print(f"Discovery server listening on UDP {DISCOVERY_PORT}", flush=True)

    while server_running:
        try:
            data, addr = sock.recvfrom(512)
            msg = data.decode('ascii', errors='ignore').strip()
            if msg == 'DSU_DISCOVER':
                local_ip = get_local_ip()
                response = f"DSU_SERVER|{local_ip}|{DSU_PORT}|{SERVER_NAME}"
                sock.sendto(response.encode('ascii'), addr)
                print(f"Discovery response to {addr}", flush=True)
        except socket.timeout:
            continue
        except Exception as e:
            print(f"Discovery error: {e}", flush=True)

    sock.close()


def get_local_ip():
    """Get local IP address"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return '127.0.0.1'


def keyboard_input_thread():
    """Thread to read keyboard input for controlling the emulated gamepad"""
    global server_running

    print("\n--- Keyboard Controls ---", flush=True)
    print("  WASD = Left Stick", flush=True)
    print("  Arrow Keys = D-Pad", flush=True)
    print("  IJKL = Right Stick", flush=True)
    print("  Z = Cross, X = Circle, C = Square, V = Triangle", flush=True)
    print("  Q = L1, E = R1, 1 = L2, 2 = R2", flush=True)
    print("  SPACE = Options, ENTER = Share", flush=True)
    print("  ESC = Quit", flush=True)
    print("-------------------------\n", flush=True)

    try:
        import msvcrt
        while server_running:
            if msvcrt.kbhit():
                key = msvcrt.getch()
                if key == b'\x1b':  # ESC
                    server_running = False
                    break
                handle_key_press(key)
            time.sleep(0.01)
    except ImportError:
        print("msvcrt not available", flush=True)
        while server_running:
            time.sleep(0.1)


def handle_key_press(key):
    """Handle keyboard input for gamepad emulation"""
    # Button bitmasks (PS4 layout)
    CROSS = 0x2000
    CIRCLE = 0x4000
    SQUARE = 0x8000
    TRIANGLE = 0x1000
    L1 = 0x0400
    R1 = 0x0800
    L2_BTN = 0x0100
    R2_BTN = 0x0200
    SHARE = 0x0010
    OPTIONS = 0x0020
    L3 = 0x0040
    R3 = 0x0080
    DPAD_UP = 0x0010
    DPAD_DOWN = 0x0040
    DPAD_LEFT = 0x0080
    DPAD_RIGHT = 0x0020

    key_map = {
        b'z': CROSS, b'Z': CROSS,
        b'x': CIRCLE, b'X': CIRCLE,
        b'c': SQUARE, b'C': SQUARE,
        b'v': TRIANGLE, b'V': TRIANGLE,
        b'q': L1, b'Q': L1,
        b'e': R1, b'E': R1,
        b'1': L2_BTN,
        b'2': R2_BTN,
        b' ': OPTIONS,
        b'\r': SHARE,
    }

    if key in key_map and key_map[key] != 0:
        controller_state['buttons'] |= key_map[key]
        print(f"  Button ON: 0x{controller_state['buttons']:04x}", flush=True)
    elif key == b'w':
        controller_state['ly'] = min(255, controller_state['ly'] + 20)
        print(f"  Left stick Y: {controller_state['ly']}", flush=True)
    elif key == b's':
        controller_state['ly'] = max(0, controller_state['ly'] - 20)
        print(f"  Left stick Y: {controller_state['ly']}", flush=True)
    elif key == b'a':
        controller_state['lx'] = max(0, controller_state['lx'] - 20)
        print(f"  Left stick X: {controller_state['lx']}", flush=True)
    elif key == b'd':
        controller_state['lx'] = min(255, controller_state['lx'] + 20)
        print(f"  Left stick X: {controller_state['lx']}", flush=True)
    elif key == b'i':
        controller_state['ry'] = min(255, controller_state['ry'] + 20)
        print(f"  Right stick Y: {controller_state['ry']}", flush=True)
    elif key == b'k':
        controller_state['ry'] = max(0, controller_state['ry'] - 20)
        print(f"  Right stick Y: {controller_state['ry']}", flush=True)
    elif key == b'j':
        controller_state['rx'] = max(0, controller_state['rx'] - 20)
        print(f"  Right stick X: {controller_state['rx']}", flush=True)
    elif key == b'l':
        controller_state['rx'] = min(255, controller_state['rx'] + 20)
        print(f"  Right stick X: {controller_state['rx']}", flush=True)
    elif key in (b'w', b'a', b's', b'd'):
        pass  # handled above


# Global state
server_running = True
packet_counter = [0]


def main():
    global server_running

    print("=" * 60, flush=True)
    print("  DSU Protocol Server Emulator for WiiU Bridge Plugin", flush=True)
    print("=" * 60, flush=True)
    print(f"  DSU Port:       {DSU_PORT}", flush=True)
    print(f"  Discovery Port: {DISCOVERY_PORT}", flush=True)
    print(f"  Local IP:       {get_local_ip()}", flush=True)
    print("=" * 60, flush=True)

    dsu_thread = threading.Thread(target=dsu_server_thread, daemon=True)
    discovery_thread = threading.Thread(target=discovery_server_thread, daemon=True)
    input_thread = threading.Thread(target=keyboard_input_thread, daemon=True)

    dsu_thread.start()
    discovery_thread.start()
    input_thread.start()

    print("\nServer running! Press ESC to quit.\n", flush=True)

    try:
        while server_running:
            time.sleep(0.1)
    except KeyboardInterrupt:
        server_running = False

    print("\nShutting down...", flush=True)
    server_running = False
    time.sleep(0.5)
    print("Done!", flush=True)


if __name__ == '__main__':
    main()
