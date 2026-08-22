#!/usr/bin/env python3
"""Simple DSU server debug test"""
import socket, struct, time

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('0.0.0.0', 26760))
sock.settimeout(2.0)

print(f"Listening on UDP 26760...")
print("Waiting for data...")

try:
    while True:
        try:
            data, addr = sock.recvfrom(512)
            print(f"\nReceived {len(data)} bytes from {addr}")
            print(f"  Raw: {data.hex()}")
            if len(data) >= 16:
                magic = data[0:4]
                print(f"  Magic: {magic}")
                print(f"  Version: {struct.unpack_from('<H', data, 4)[0]}")
                print(f"  Length: {struct.unpack_from('<H', data, 6)[0]}")
                print(f"  CRC: {struct.unpack_from('<I', data, 8)[0]:08x}")
                print(f"  ID: {struct.unpack_from('<I', data, 12)[0]}")
                if len(data) >= 18:
                    msg_type = struct.unpack_from('<H', data, 16)[0]
                    print(f"  MsgType: 0x{msg_type:06x}")
        except socket.timeout:
            continue
except KeyboardInterrupt:
    pass
finally:
    sock.close()
