#!/usr/bin/env python3
"""
DSU Server with Xbox 360 XInput support - TCP Mode.
Reads real controller input and sends via DSU protocol to Wii U over TCP.
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
SERVER_NAME = "PC-DSU-XInput-TCP"

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
    payload = struct.pack('<II', 0x100000, 1001)
    return build_dsu_header(DSU_MSG_VERSION, payload, server_id)

def build_pad_data_response(server_id, packet_counter):
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

DSU_MSG_VERSION = 0x100000
DSU_MSG_CONTROLLER = 0x100001
DSU_MSG_PAD_DATA = 0x100002

def handle_client(client_sock, addr):
    print("Client connected from {0}:{1}".format(addr[0], addr[1]))
    server_id = int(time.time()) & 0xFFFFFFFF
    packet_counter = 0
    
    try:
        while running:
            data = client_sock.recv(512)
            if not data:
                break
            
            if len(data) >= 20 and data[0:4] == b'DSUC':
                msg_type = struct.unpack_from('<I', data, 16)[0]
                
                if msg_type == DSU_MSG_VERSION:
                    resp = build_version_response(server_id)
                    client_sock.sendall(resp)
                    print("Version response sent")
                elif msg_type == DSU_MSG_PAD_DATA:
                    resp = build_pad_data_response(server_id, packet_counter)
                    client_sock.sendall(resp)
                    packet_counter += 1
    except Exception as e:
        print("Client error: {0}".format(e))
    finally:
        client_sock.close()
        print("Client disconnected")

def read_gamepad():
    if not HAS_PYGAME:
        return
    
    pygame.joystick.quit()
    pygame.joystick.init()
    
    if pygame.joystick.get_count() > 0:
        js = pygame.joystick.Joystick(0)
        js.init()
        pygame.event.pump()
        
        lx = int((js.get_axis(0) + 1) * 127.5)
        ly = int((js.get_axis(1) + 1) * 127.5)
        rx = int((js.get_axis(2) + 1) * 127.5)
        ry = int((js.get_axis(3) + 1) * 127.5)
        
        controller_state['lx'] = max(0, min(255, lx))
        controller_state['ly'] = max(0, min(255, ly))
        controller_state['rx'] = max(0, min(255, rx))
        controller_state['ry'] = max(0, min(255, ry))
        
        if js.get_numaxes() > 4:
            controller_state['l2'] = int((js.get_axis(4) + 1) * 127.5)
            controller_state['r2'] = int((js.get_axis(5) + 1) * 127.5)
        
        buttons = 0
        for i in range(min(js.get_numbuttons(), 16)):
            if js.get_button(i):
                buttons |= (1 << i)
        controller_state['buttons'] = buttons
        
        js.quit()

def gamepad_thread():
    while running:
        read_gamepad()
        time.sleep(0.016)

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

def main():
    global running
    
    print("=" * 50)
    print("  DSU Server TCP with Xbox 360 XInput")
    print("=" * 50)
    print("  TCP Port:       {0}".format(DSU_PORT))
    print("  Local IP:       {0}".format(get_local_ip()))
    print("  Pygame:         {0}".format("OK" if HAS_PYGAME else "NOT AVAILABLE"))
    print("=" * 50)
    
    # Create TCP server
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind(('0.0.0.0', DSU_PORT))
    server_sock.listen(1)
    server_sock.settimeout(1.0)
    
    print("\nTCP server listening on port {0}".format(DSU_PORT))
    print("Waiting for Wii U connection...")
    
    # Start gamepad thread
    t = threading.Thread(target=gamepad_thread, daemon=True)
    t.start()
    
    try:
        while running:
            try:
                client_sock, addr = server_sock.accept()
                handle_client(client_sock, addr)
            except socket.timeout:
                pass
    except KeyboardInterrupt:
        running = False
    
    server_sock.close()
    print("Server stopped")

if __name__ == '__main__':
    main()
