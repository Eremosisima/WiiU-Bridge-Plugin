#!/usr/bin/env python3
"""
WiiU Bridge Client Test
Simulates the Wii U plugin connecting to the server and receiving data.
"""

import socket
import struct
import time
import sys

# Protocol constants
HEADER_MAGIC = 0x57425244  # "WBRD" - WiiU Bridge
PROTOCOL_VERSION = 1
PACKET_SIZE = 20

# Wii U Pro Controller button definitions
WPAD_PRO_BUTTON_UP = 0x00000001
WPAD_PRO_BUTTON_LEFT = 0x00000002
WPAD_PRO_BUTTON_ZR = 0x00000004
WPAD_PRO_BUTTON_X = 0x00000008
WPAD_PRO_BUTTON_A = 0x00000010
WPAD_PRO_BUTTON_Y = 0x00000020
WPAD_PRO_BUTTON_B = 0x00000040
WPAD_PRO_BUTTON_ZL = 0x00000080
WPAD_PRO_BUTTON_R = 0x00000200
WPAD_PRO_BUTTON_PLUS = 0x00000400
WPAD_PRO_BUTTON_HOME = 0x00000800
WPAD_PRO_BUTTON_MINUS = 0x00001000
WPAD_PRO_BUTTON_L = 0x00002000
WPAD_PRO_BUTTON_DOWN = 0x00004000
WPAD_PRO_BUTTON_RIGHT = 0x00008000
WPAD_PRO_BUTTON_STICK_R = 0x00010000
WPAD_PRO_BUTTON_STICK_L = 0x00020000

def format_buttons(buttons: int) -> str:
    """Format button flags as readable string."""
    btn_names = []
    if buttons & WPAD_PRO_BUTTON_UP:
        btn_names.append("UP")
    if buttons & WPAD_PRO_BUTTON_DOWN:
        btn_names.append("DOWN")
    if buttons & WPAD_PRO_BUTTON_LEFT:
        btn_names.append("LEFT")
    if buttons & WPAD_PRO_BUTTON_RIGHT:
        btn_names.append("RIGHT")
    if buttons & WPAD_PRO_BUTTON_A:
        btn_names.append("A")
    if buttons & WPAD_PRO_BUTTON_B:
        btn_names.append("B")
    if buttons & WPAD_PRO_BUTTON_X:
        btn_names.append("X")
    if buttons & WPAD_PRO_BUTTON_Y:
        btn_names.append("Y")
    if buttons & WPAD_PRO_BUTTON_L:
        btn_names.append("L")
    if buttons & WPAD_PRO_BUTTON_R:
        btn_names.append("R")
    if buttons & WPAD_PRO_BUTTON_ZL:
        btn_names.append("ZL")
    if buttons & WPAD_PRO_BUTTON_ZR:
        btn_names.append("ZR")
    if buttons & WPAD_PRO_BUTTON_PLUS:
        btn_names.append("PLUS")
    if buttons & WPAD_PRO_BUTTON_MINUS:
        btn_names.append("MINUS")
    if buttons & WPAD_PRO_BUTTON_HOME:
        btn_names.append("HOME")
    if buttons & WPAD_PRO_BUTTON_STICK_L:
        btn_names.append("STICK_L")
    if buttons & WPAD_PRO_BUTTON_STICK_R:
        btn_names.append("STICK_R")
    
    return ", ".join(btn_names) if btn_names else "NONE"

def main():
    print("WiiU Bridge Client Test")
    print("=" * 40)
    
    # Connect to server
    print("Connecting to server at 127.0.0.1:12345...")
    
    try:
        client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket.connect(('127.0.0.1', 12345))
        print("Connected!")
    except ConnectionRefusedError:
        print("ERROR: Could not connect to server")
        print("Make sure the server is running: python server_pygame.py")
        return 1
    
    # Receive packets for 5 seconds
    print("\nReceiving packets for 5 seconds...")
    print("Press Ctrl+C to stop")
    
    start_time = time.time()
    packet_count = 0
    last_print_time = start_time
    
    try:
        while time.time() - start_time < 5.0:
            # Receive packet
            received = client_socket.recv(PACKET_SIZE)
            
            if len(received) == PACKET_SIZE:
                # Parse packet
                magic = struct.unpack('<I', received[0:4])[0]
                version = struct.unpack('<I', received[4:8])[0]
                lx = struct.unpack('<h', received[8:10])[0]
                ly = struct.unpack('<h', received[10:12])[0]
                rx = struct.unpack('<h', received[12:14])[0]
                ry = struct.unpack('<h', received[14:16])[0]
                buttons = struct.unpack('<I', received[16:20])[0]
                
                packet_count += 1
                current_time = time.time()
                
                # Print status every second
                if current_time - last_print_time >= 1.0:
                    print(f"  Packets/sec: {packet_count}")
                    print(f"  Last packet: Buttons=[{format_buttons(buttons)}] "
                          f"L({lx},{ly}) R({rx},{ry})")
                    packet_count = 0
                    last_print_time = current_time
            elif len(received) > 0:
                print(f"  WARNING: Incomplete packet ({len(received)} bytes)")
    
    except KeyboardInterrupt:
        print("\nStopped by user")
    
    finally:
        client_socket.close()
    
    print("\nTest completed")
    return 0

if __name__ == '__main__':
    sys.exit(main())
