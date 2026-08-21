#!/usr/bin/env python3
"""
WiiU Auto Scanner - Automatically scans network for Wii U
"""

import socket
import struct
import time
import sys

def get_local_ip():
    """Get local IP address"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"

def scan_port(ip, port, timeout=0.1):
    """Scan specific port on IP"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        result = sock.connect_ex((ip, port))
        sock.close()
        return result == 0
    except:
        return False

def main():
    print("WiiU Auto Scanner")
    print("=" * 50)
    
    # Get local IP
    local_ip = get_local_ip()
    print(f"Local IP: {local_ip}")
    
    # Extract subnet
    subnet = ".".join(local_ip.split(".")[:-1])
    print(f"Scanning subnet: {subnet}.0/24")
    
    # Scan for devices on common ports
    ports_to_scan = [12345, 56709, 80, 443]  # Bridge port, NIS port, HTTP, HTTPS
    
    found_devices = []
    
    print("\nScanning...")
    for i in range(1, 255):
        ip = f"{subnet}.{i}"
        for port in ports_to_scan:
            if scan_port(ip, port):
                print(f"  Found: {ip}:{port}")
                found_devices.append((ip, port))
                break  # Don't check other ports if one is found
    
    print("\n" + "=" * 50)
    print(f"Found {len(found_devices)} device(s):")
    
    for i, (ip, port) in enumerate(found_devices, 1):
        print(f"  {i}. {ip}:{port}")
    
    if found_devices:
        print("\nTrying to send test packet to each device...")
        
        # Protocol constants
        HEADER_MAGIC = 0x57425244
        PROTOCOL_VERSION = 1
        
        for ip, port in found_devices:
            print(f"\nTesting {ip}:{port}...")
            
            # Create test packet
            buttons = 0x1000  # A button
            lx, ly, rx, ry = 0, 0, 0, 0
            
            packet = struct.pack('<IIhhhh',
                HEADER_MAGIC,
                PROTOCOL_VERSION,
                lx, ly,
                rx, ry
            ) + struct.pack('<I', buttons)
            
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(2)
                sock.connect((ip, port))
                sock.sendall(packet)
                sock.close()
                print(f"    SUCCESS: Packet sent to {ip}:{port}")
                print(f"    Use this IP in your server: {ip}")
            except Exception as e:
                print(f"    FAILED: {e}")
    
    print("\n" + "=" * 50)
    print("Summary:")
    print("1. WiiU Bridge Controller plugin must be running on Wii U")
    print("2. Plugin listens on port 12345")
    print("3. Use the IP address found above in your server")
    print("4. Run: python server_simple.py")

if __name__ == '__main__':
    main()
