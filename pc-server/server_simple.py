#!/usr/bin/env python3
"""
WiiU Bridge Server - Simple version for testing
Sends controller data to Wii U plugin
"""

import socket
import struct
import time
import sys

# Protocol constants
HEADER_MAGIC = 0x57425244  # "WBRD" - WiiU Bridge
PROTOCOL_VERSION = 1
PACKET_SIZE = 20

def create_packet(buttons: int, lx: int, ly: int, rx: int, ry: int) -> bytes:
    """Create a data packet."""
    packet = struct.pack('<IIhhi',
        HEADER_MAGIC,
        PROTOCOL_VERSION,
        lx, ly,
        rx, ry
    ) + struct.pack('<I', buttons)
    return packet

def main():
    print("WiiU Bridge Server (Simple)")
    print("=" * 40)
    
    # Get Wii U IP from user
    wiiu_ip = input("Enter Wii U IP address: ").strip()
    if not wiiu_ip:
        wiiu_ip = "192.168.100.100"
    
    port = 12345
    print(f"Connecting to Wii U at {wiiu_ip}:{port}...")
    
    # Create socket and connect to Wii U
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    try:
        client_socket.connect((wiiu_ip, port))
        print("Connected to Wii U!")
    except ConnectionRefusedError:
        print("ERROR: Could not connect to Wii U")
        print("Make sure the WiiU Bridge Controller plugin is running")
        return 1
    except Exception as e:
        print(f"ERROR: {e}")
        return 1
    
    # Send test packets
    print("\nSending test controller data...")
    print("Press Ctrl+C to stop")
    
    test_buttons = [
        (0x1000, "A button"),
        (0x2000, "B button"),
        (0x4000, "X button"),
        (0x8000, "Y button"),
        (0x0001, "D-Pad Up"),
        (0x0002, "D-Pad Down"),
        (0x0004, "D-Pad Left"),
        (0x0008, "D-Pad Right"),
        (0x0100, "Left Shoulder"),
        (0x0200, "Right Shoulder"),
    ]
    
    try:
        packet_count = 0
        last_time = time.time()
        
        while True:
            # Simulate button presses
            for buttons, name in test_buttons:
                packet = create_packet(buttons, 0, 0, 0, 0)
                client_socket.sendall(packet)
                print(f"Sent: {name} (0x{buttons:04X})")
                time.sleep(0.2)
            
            # Simulate stick movement
            for i in range(-1000, 1001, 200):
                packet = create_packet(0, i, 0, 0, 0)
                client_socket.sendall(packet)
                print(f"Sent: Left Stick X = {i}")
                time.sleep(0.05)
            
            for i in range(-1000, 1001, 200):
                packet = create_packet(0, 0, i, 0, 0)
                client_socket.sendall(packet)
                print(f"Sent: Left Stick Y = {i}")
                time.sleep(0.05)
            
            # Print stats
            current_time = time.time()
            if current_time - last_time >= 1.0:
                print(f"Packets sent: {packet_count}")
                packet_count = 0
                last_time = current_time
            
            packet_count += 1
            
    except KeyboardInterrupt:
        print("\nStopped by user")
    finally:
        client_socket.close()
    
    print("Disconnected")
    return 0

if __name__ == '__main__':
    sys.exit(main())
