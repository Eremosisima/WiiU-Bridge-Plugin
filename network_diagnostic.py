#!/usr/bin/env python3
"""
WiiU Network Diagnostic Tool
Helps find and connect to Wii U on the network
"""

import socket
import struct
import time
import sys
import subprocess
import platform

def get_local_ip():
    """Get local IP address"""
    try:
        # Create a socket to determine local IP
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"

def scan_network(subnet, port=12345, timeout=0.1):
    """Scan network for devices on specified port"""
    print(f"Scanning subnet {subnet}.0/24 on port {port}...")
    found_devices = []
    
    for i in range(1, 255):
        ip = f"{subnet}.{i}"
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            result = sock.connect_ex((ip, port))
            if result == 0:
                print(f"  Found: {ip}:{port}")
                found_devices.append(ip)
            sock.close()
        except:
            pass
    
    return found_devices

def test_connection(ip, port=12345):
    """Test connection to specified IP"""
    print(f"Testing connection to {ip}:{port}...")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        result = sock.connect_ex((ip, port))
        sock.close()
        
        if result == 0:
            print(f"  SUCCESS: {ip}:{port} is reachable")
            return True
        else:
            print(f"  FAILED: {ip}:{port} is not reachable")
            return False
    except Exception as e:
        print(f"  ERROR: {e}")
        return False

def send_test_packet(ip, port=12345):
    """Send a test packet to the specified IP"""
    print(f"Sending test packet to {ip}:{port}...")
    
    # Protocol constants
    HEADER_MAGIC = 0x57425244  # "WBRD" - WiiU Bridge
    PROTOCOL_VERSION = 1
    
    # Create test packet: A button pressed
    buttons = 0x1000  # A button
    lx, ly, rx, ry = 0, 0, 0, 0
    
    packet = struct.pack('<IIhhi',
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
        print(f"  Packet sent successfully")
        return True
    except Exception as e:
        print(f"  ERROR: {e}")
        return False

def main():
    print("WiiU Network Diagnostic Tool")
    print("=" * 50)
    
    # Get local IP
    local_ip = get_local_ip()
    print(f"Local IP: {local_ip}")
    
    # Extract subnet
    subnet = ".".join(local_ip.split(".")[:-1])
    print(f"Subnet: {subnet}.0/24")
    
    # Get Wii U IP from user
    print("\nEnter Wii U IP address (or press Enter to scan):")
    wiiu_ip = input("> ").strip()
    
    if not wiiu_ip:
        # Scan network
        print("\nScanning network...")
        found_devices = scan_network(subnet)
        
        if found_devices:
            print(f"\nFound {len(found_devices)} device(s) on port 12345:")
            for i, ip in enumerate(found_devices, 1):
                print(f"  {i}. {ip}")
            
            print("\nEnter device number to test:")
            try:
                choice = int(input("> ")) - 1
                if 0 <= choice < len(found_devices):
                    wiiu_ip = found_devices[choice]
                else:
                    print("Invalid choice")
                    return 1
            except ValueError:
                print("Invalid input")
                return 1
        else:
            print("No devices found on port 12345")
            print("\nTroubleshooting:")
            print("1. Make sure WiiU Bridge Controller plugin is running")
            print("2. Check Wii U is connected to same network")
            print("3. Try manual IP entry")
            return 1
    
    # Test connection
    print("\n" + "=" * 50)
    print(f"Testing connection to {wiiu_ip}:12345...")
    
    if test_connection(wiiu_ip):
        print("\nConnection successful!")
        
        # Send test packet
        print("\nSending test packet...")
        if send_test_packet(wiiu_ip):
            print("\nTest completed successfully!")
            print(f"\nUse this IP in your server: {wiiu_ip}")
        else:
            print("\nFailed to send test packet")
    else:
        print("\nConnection failed")
        print("\nTroubleshooting:")
        print("1. Verify Wii U IP address")
        print("2. Check firewall settings")
        print("3. Ensure Wii U and PC are on same network")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
