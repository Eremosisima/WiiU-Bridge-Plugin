#!/usr/bin/env python3
"""
DSU Server with Xbox 360 XInput support.
Reads real controller input and sends via DSU protocol to Wii U.
"""

import socket
import struct
import time
import threading
import sys

try:
    import pygame
    pygame.init()
    pygame.joystick.init()
    HAS_PYGAME = True
except:
    HAS_PYGAME = False
    print("pygame not available, using simulated input")

DSU_PORT = 26760
DISCOVERY_PORT = 26761
SERVER_NAME = "PC-DSU-XInput"

DSU_MSG_VERSION = 0x100000
DSU_MSG_CONTROLLER = 0x100001
DSU_MSG_PAD_DATA = 0x100002

controller_state = {
    'connected': True,
    'buttons': 0,
    'lx': 128, 'ly': 128,
    'rx': 128, 'ry': 128,
    'l2': 0, 'r2': 0,
}

def crc32(data):
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
    header = bytearray(16)
    header[0:4] = b'DSUS'
    struct.pack_into('<HH', header, 4, 1001, len(payload_data))
    struct.pack_into('<I', header, 12, server_id)
    crc_data = bytes(header[0:8]) + bytes(header[12:16]) + payload_data
    struct.pack_into('<I', header, 8, crc32(crc_data))
    return bytes(header) + payload_data

def build_version_response(server_id):
    payload = struct.pack('<II', DSU_MSG_VERSION, 1001)
    return build_dsu_header(DSU_MSG_VERSION, payload, server_id)

def build_controller_response(server_id, slot):
    payload = bytearray(20)
    struct.pack_into('<I', payload, 0, DSU_MSG_CONTROLLER)
    payload[4] = slot
    payload[5] = 2 if controller_state['connected'] else 0
    payload[6] = 3
    payload[7] = 2
    payload[8:14] = bytes([0x01, 0x02, 0x03, 0x04, 0x05, 0x06])
    payload[14] = 80
    payload[15] = 1 if controller_state['connected'] else 0
    return build_dsu_header(DSU_MSG_CONTROLLER, payload, server_id)

def build_pad_data_response(server_id, slot, packet_counter):
    payload = bytearray(104)
    struct.pack_into('<I', payload, 0, DSU_MSG_PAD_DATA)
    struct.pack_into('<I', payload, 4, packet_counter)
    struct.pack_into('<I', payload, 8, controller_state['buttons'])
    payload[12] = controller_state['lx']
    payload[13] = controller_state['ly']
    payload[14] = controller_state['rx']
    payload[15] = controller_state['ry']
    payload[16] = controller_state['l2']
    payload[17] = controller_state['r2']
    return build_dsu_header(DSU_MSG_PAD_DATA, payload, server_id)

def handle_client_message(data, addr, sock, server_id):
    if len(data) < 20:
        return
    if data[0:4] != b'DSUC':
        return
    msg_type = struct.unpack_from('<I', data, 16)[0]
    
    if msg_type == DSU_MSG_VERSION:
        sock.sendto(build_version_response(server_id), addr)
    elif msg_type == DSU_MSG_CONTROLLER:
        slot = data[20] if len(data) > 20 else 0
        sock.sendto(build_controller_response(server_id, slot), addr)
    elif msg_type == DSU_MSG_PAD_DATA:
        if len(data) > 21:
            slot_count = data[20]
            slots = list(data[21:21 + slot_count])
        else:
            slots = [0]
        for slot in slots:
            sock.sendto(build_pad_data_response(server_id, slot, packet_counter[0]), addr)
        packet_counter[0] += 1

def read_gamepad():
    """Read real gamepad input via pygame"""
    if not HAS_PYGAME:
        return
    
    pygame.joystick.quit()
    pygame.joystick.init()
    
    if pygame.joystick.get_count() > 0:
        js = pygame.joystick.Joystick(0)
        js.init()
        
        pygame.event.pump()
        
        # Read axes (0-255 range, 128=center)
        lx = int((js.get_axis(0) + 1) * 127.5)
        ly = int((js.get_axis(1) + 1) * 127.5)
        rx = int((js.get_axis(2) + 1) * 127.5)
        ry = int((js.get_axis(3) + 1) * 127.5)
        
        controller_state['lx'] = max(0, min(255, lx))
        controller_state['ly'] = max(0, min(255, ly))
        controller_state['rx'] = max(0, min(255, rx))
        controller_state['ry'] = max(0, min(255, ry))
        
        # Read triggers (axes 4 and 5)
        if js.get_numaxes() > 4:
            controller_state['l2'] = int((js.get_axis(4) + 1) * 127.5)
            controller_state['r2'] = int((js.get_axis(5) + 1) * 127.5)
        
        # Read buttons
        buttons = 0
        for i in range(min(js.get_numbuttons(), 16)):
            if js.get_button(i):
                buttons |= (1 << i)
        controller_state['buttons'] = buttons
        
        js.quit()

def dsu_server_thread():
    server_id = int(time.time()) & 0xFFFFFFFF
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', DSU_PORT))
    sock.settimeout(1.0)
    
    print("DSU Server on UDP {0}".format(DSU_PORT))
    
    while running:
        try:
            data, addr = sock.recvfrom(512)
            handle_client_message(data, addr, sock, server_id)
        except socket.timeout:
            pass
        except Exception as e:
            print("DSU error: {0}".format(e))

def discovery_thread():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', DISCOVERY_PORT))
    sock.settimeout(1.0)
    
    print("Discovery on UDP {0}".format(DISCOVERY_PORT))
    
    while running:
        try:
            data, addr = sock.recvfrom(512)
            if data == b'DSU_DISCOVER':
                local_ip = get_local_ip()
                response = "DSU_SERVER|{0}|{1}|{2}".format(local_ip, DSU_PORT, SERVER_NAME)
                sock.sendto(response.encode('ascii'), addr)
        except socket.timeout:
            pass

def gamepad_thread():
    while running:
        read_gamepad()
        time.sleep(0.016)  # ~60fps

def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return '127.0.0.1'

running = True
packet_counter = [0]

def main():
    global running
    
    print("=" * 50)
    print("  DSU Server with Xbox 360 XInput")
    print("=" * 50)
    print("  DSU Port:       {0}".format(DSU_PORT))
    print("  Discovery Port: {0}".format(DISCOVERY_PORT))
    print("  Local IP:       {0}".format(get_local_ip()))
    print("  Pygame:         {0}".format("OK" if HAS_PYGAME else "NOT AVAILABLE"))
    print("=" * 50)
    
    t1 = threading.Thread(target=dsu_server_thread, daemon=True)
    t2 = threading.Thread(target=discovery_thread, daemon=True)
    t3 = threading.Thread(target=gamepad_thread, daemon=True)
    
    t1.start()
    t2.start()
    t3.start()
    
    print("\nServer running! Press Ctrl+C to quit.\n")
    
    try:
        while running:
            time.sleep(0.1)
    except KeyboardInterrupt:
        running = False
    
    print("Shutting down...")
    time.sleep(0.5)
    print("Done!")

if __name__ == '__main__':
    main()
