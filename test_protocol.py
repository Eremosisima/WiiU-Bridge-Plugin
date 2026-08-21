#!/usr/bin/env python3
"""
WiiU Bridge Protocol Test
Tests the packet creation and parsing without actual controller input.
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

def create_test_packet(buttons: int, lx: int, ly: int, rx: int, ry: int) -> bytes:
    """Create a test packet."""
    packet = struct.pack('<IIhhi',
        HEADER_MAGIC,
        PROTOCOL_VERSION,
        lx, ly,
        rx, ry
    ) + struct.pack('<I', buttons)
    return packet

def parse_packet(packet: bytes) -> dict:
    """Parse a received packet."""
    if len(packet) != PACKET_SIZE:
        return None
    
    magic = struct.unpack('<I', packet[0:4])[0]
    version = struct.unpack('<I', packet[4:8])[0]
    lx = struct.unpack('<h', packet[8:10])[0]
    ly = struct.unpack('<h', packet[10:12])[0]
    rx = struct.unpack('<h', packet[12:14])[0]
    ry = struct.unpack('<h', packet[14:16])[0]
    buttons = struct.unpack('<I', packet[16:20])[0]
    
    return {
        'magic': magic,
        'version': version,
        'left_stick_x': lx,
        'left_stick_y': ly,
        'right_stick_x': rx,
        'right_stick_y': ry,
        'buttons': buttons
    }

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

def test_packet_creation():
    """Test packet creation and parsing."""
    print("Testing packet creation and parsing...")
    
    # Test cases
    test_cases = [
        (0, 0, 0, 0, 0, "No input"),
        (WPAD_PRO_BUTTON_A, 0, 0, 0, 0, "A button"),
        (WPAD_PRO_BUTTON_B, 0, 0, 0, 0, "B button"),
        (WPAD_PRO_BUTTON_UP, 0, 0, 0, 0, "D-Pad Up"),
        (WPAD_PRO_BUTTON_DOWN, 0, 0, 0, 0, "D-Pad Down"),
        (WPAD_PRO_BUTTON_LEFT, 0, 0, 0, 0, "D-Pad Left"),
        (WPAD_PRO_BUTTON_RIGHT, 0, 0, 0, 0, "D-Pad Right"),
        (WPAD_PRO_BUTTON_L, 0, 0, 0, 0, "Left Shoulder"),
        (WPAD_PRO_BUTTON_R, 0, 0, 0, 0, "Right Shoulder"),
        (WPAD_PRO_BUTTON_ZL, 0, 0, 0, 0, "Left Trigger"),
        (WPAD_PRO_BUTTON_ZR, 0, 0, 0, 0, "Right Trigger"),
        (WPAD_PRO_BUTTON_PLUS, 0, 0, 0, 0, "Plus button"),
        (WPAD_PRO_BUTTON_MINUS, 0, 0, 0, 0, "Minus button"),
        (WPAD_PRO_BUTTON_A | WPAD_PRO_BUTTON_B, 0, 0, 0, 0, "A+B"),
        (0, 1024, 0, 0, 0, "Left stick right"),
        (0, -1024, 0, 0, 0, "Left stick left"),
        (0, 0, 1024, 0, 0, "Left stick up"),
        (0, 0, -1024, 0, 0, "Left stick down"),
        (0, 0, 0, 1024, 0, "Right stick right"),
        (0, 0, 0, -1024, 0, "Right stick left"),
        (0, 0, 0, 0, 1024, "Right stick up"),
        (0, 0, 0, 0, -1024, "Right stick down"),
        (WPAD_PRO_BUTTON_A | WPAD_PRO_BUTTON_UP, 512, 512, -512, -512, "A+Up+Sticks"),
    ]
    
    all_passed = True
    for buttons, lx, ly, rx, ry, desc in test_cases:
        packet = create_test_packet(buttons, lx, ly, rx, ry)
        parsed = parse_packet(packet)
        
        if parsed is None:
            print(f"  FAIL: {desc} - Failed to parse packet")
            all_passed = False
            continue
        
        if parsed['magic'] != HEADER_MAGIC:
            print(f"  FAIL: {desc} - Invalid magic: 0x{parsed['magic']:08X}")
            all_passed = False
            continue
        
        if parsed['version'] != PROTOCOL_VERSION:
            print(f"  FAIL: {desc} - Invalid version: {parsed['version']}")
            all_passed = False
            continue
        
        if parsed['left_stick_x'] != lx:
            print(f"  FAIL: {desc} - Left X mismatch: {parsed['left_stick_x']} != {lx}")
            all_passed = False
            continue
        
        if parsed['left_stick_y'] != ly:
            print(f"  FAIL: {desc} - Left Y mismatch: {parsed['left_stick_y']} != {ly}")
            all_passed = False
            continue
        
        if parsed['right_stick_x'] != rx:
            print(f"  FAIL: {desc} - Right X mismatch: {parsed['right_stick_x']} != {rx}")
            all_passed = False
            continue
        
        if parsed['right_stick_y'] != ry:
            print(f"  FAIL: {desc} - Right Y mismatch: {parsed['right_stick_y']} != {ry}")
            all_passed = False
            continue
        
        if parsed['buttons'] != buttons:
            print(f"  FAIL: {desc} - Buttons mismatch: 0x{parsed['buttons']:08X} != 0x{buttons:08X}")
            all_passed = False
            continue
        
        print(f"  PASS: {desc} - Buttons: [{format_buttons(buttons)}] Sticks: L({lx},{ly}) R({rx},{ry})")
    
    return all_passed

def test_socket_server():
    """Test TCP server functionality."""
    print("\nTesting TCP server...")
    
    # Create server socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(('127.0.0.1', 12346))
    server_socket.listen(1)
    server_socket.settimeout(5.0)
    
    print("  Server listening on 127.0.0.1:12346")
    
    # Create client socket
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect(('127.0.0.1', 12346))
    
    print("  Client connected")
    
    # Accept connection
    conn, addr = server_socket.accept()
    print(f"  Server accepted connection from {addr}")
    
    # Send test packets
    test_packets = [
        create_test_packet(WPAD_PRO_BUTTON_A, 0, 0, 0, 0),
        create_test_packet(0, 1024, 0, 0, 0),
        create_test_packet(WPAD_PRO_BUTTON_UP | WPAD_PRO_BUTTON_A, 512, -512, 0, 0),
    ]
    
    for i, packet in enumerate(test_packets):
        client_socket.sendall(packet)
        print(f"  Sent packet {i+1}")
    
    # Receive and verify packets
    for i in range(len(test_packets)):
        received = conn.recv(PACKET_SIZE)
        if len(received) == PACKET_SIZE:
            parsed = parse_packet(received)
            if parsed:
                print(f"  Received packet {i+1}: Buttons=[{format_buttons(parsed['buttons'])}] "
                      f"L({parsed['left_stick_x']},{parsed['left_stick_y']}) "
                      f"R({parsed['right_stick_x']},{parsed['right_stick_y']})")
            else:
                print(f"  FAIL: Invalid packet received")
                return False
        else:
            print(f"  FAIL: Incomplete packet received ({len(received)} bytes)")
            return False
    
    # Clean up
    client_socket.close()
    conn.close()
    server_socket.close()
    
    print("  TCP test passed!")
    return True

def main():
    print("WiiU Bridge Protocol Test")
    print("=" * 40)
    
    # Test packet creation
    if not test_packet_creation():
        print("\nPacket test FAILED")
        return 1
    
    # Test TCP server
    if not test_socket_server():
        print("\nTCP test FAILED")
        return 1
    
    print("\n" + "=" * 40)
    print("All tests PASSED!")
    return 0

if __name__ == '__main__':
    sys.exit(main())
