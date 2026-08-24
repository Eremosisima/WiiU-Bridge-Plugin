#!/usr/bin/env python3
"""
DSU TCP Client for WiiU Bridge Plugin.
Connects to Wii U via TCP and sends controller input.
"""

import socket
import struct
import time
import sys

try:
    import pygame
    pygame.init()
    pygame.joystick.init()
    HAS_PYGAME = True
except:
    HAS_PYGAME = False

WIIU_IP = "192.168.100.42"
WIIU_PORT = 26760

def crc32(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF

def send_dsu(sock, msg_type, payload=b''):
    p = struct.pack('<I', msg_type) + payload
    h = bytearray(16)
    h[0:4] = b'DSUC'
    struct.pack_into('<HH', h, 4, 1001, len(p))
    struct.pack_into('<I', h, 12, 12345)
    cd = bytes(h[0:8]) + bytes(h[12:16]) + p
    struct.pack_into('<I', h, 8, crc32(cd))
    return bytes(h) + p

def connect():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect((WIIU_IP, WIIU_PORT))
        print("Connected to Wii U!")
        return sock
    except Exception as e:
        print("Connection failed: {0}".format(e))
        return None

def test_version(sock):
    print("Sending DSU version request...")
    sock.sendall(send_dsu(0x100000))
    try:
        data = sock.recv(512)
        if data[0:4] == b'DSUS':
            print("DSU version response received!")
            return True
    except:
        pass
    print("No response")
    return False

def read_gamepad():
    if not HAS_PYGAME:
        return 0, 0, 0, 0, 0
    
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
        
        buttons = 0
        for i in range(min(js.get_numbuttons(), 16)):
            if js.get_button(i):
                buttons |= (1 << i)
        
        js.quit()
        return lx, ly, rx, ry, buttons
    
    return 128, 128, 128, 128, 0

def main():
    print("=" * 50)
    print("  WiiU Bridge DSU TCP Client")
    print("=" * 50)
    print("  Wii U: {0}:{1}".format(WIIU_IP, WIIU_PORT))
    print("  Pygame: {0}".format("OK" if HAS_PYGAME else "NOT AVAILABLE"))
    print("=" * 50)
    
    sock = connect()
    if not sock:
        print("Failed to connect")
        return
    
    if not test_version(sock):
        print("DSU version test failed")
        sock.close()
        return
    
    print("\nConnected! Sending controller input...")
    print("Press Ctrl+C to quit\n")
    
    try:
        while True:
            lx, ly, rx, ry, buttons = read_gamepad()
            
            # Build pad data payload
            pad = bytearray(104)
            struct.pack_into('<I', pad, 0, 0x100002)  # msg_type
            struct.pack_into('<I', pad, 4, 0)  # packet counter
            struct.pack_into('<I', pad, 8, buttons)
            pad[12] = lx
            pad[13] = ly
            pad[14] = rx
            pad[15] = ry
            
            sock.sendall(send_dsu(0x100002, bytes(pad)))
            
            time.sleep(0.016)  # ~60fps
            
    except KeyboardInterrupt:
        print("\nQuitting...")
    finally:
        sock.close()

if __name__ == '__main__':
    main()
