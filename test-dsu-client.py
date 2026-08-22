#!/usr/bin/env python3
"""
DSU Protocol Test Client - sends messages to verify the server emulator works.
"""

import socket
import struct
import time

DSU_IP = '127.0.0.1'
DSU_PORT = 26760
DISCOVERY_PORT = 26761

DSU_MSG_VERSION = 0x100000
DSU_MSG_CONTROLLER = 0x100001
DSU_MSG_PAD_DATA = 0x100002


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


def build_dsu_header(msg_type, payload_len, client_id):
    """Build DSU header"""
    header = bytearray(16)
    header[0:4] = b'DSUC'  # client magic
    struct.pack_into('<HH', header, 4, 1001, payload_len)
    # CRC32 at offset 8 will be filled later
    struct.pack_into('<I', header, 12, client_id)
    return header


def send_version_request(sock, client_id):
    """Send version request"""
    payload = struct.pack('<I', 0)  # empty payload
    header = build_dsu_header(DSU_MSG_VERSION, len(payload), client_id)
    crc_data = header[0:8] + header[12:16] + payload
    struct.pack_into('<I', header, 8, crc32(crc_data))
    msg = bytes(header) + payload
    sock.sendto(msg, (DSU_IP, DSU_PORT))
    print(f"Sent version request (client_id={client_id})")


def send_controller_request(sock, client_id, slot):
    """Send controller info request"""
    payload = struct.pack('<I', slot)
    header = build_dsu_header(DSU_MSG_CONTROLLER, len(payload), client_id)
    crc_data = header[0:8] + header[12:16] + payload
    struct.pack_into('<I', header, 8, crc32(crc_data))
    msg = bytes(header) + payload
    sock.sendto(msg, (DSU_IP, DSU_PORT))
    print(f"Sent controller request (slot={slot})")


def send_pad_data_request(sock, client_id, slots):
    """Send pad data request"""
    payload = bytearray()
    payload.extend(struct.pack('<H', 0))  # padding?
    payload.append(len(slots))
    for slot in slots:
        payload.append(slot)
    header = build_dsu_header(DSU_MSG_PAD_DATA, len(payload), client_id)
    crc_data = header[0:8] + header[12:16] + bytes(payload)
    struct.pack_into('<I', header, 8, crc32(crc_data))
    msg = bytes(header) + bytes(payload)
    sock.sendto(msg, (DSU_IP, DSU_PORT))
    print(f"Sent pad data request (slots={slots})")


def test_discovery():
    """Test discovery broadcast"""
    print("\n--- Testing Discovery ---")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(3.0)

    sock.sendto(b'DSU_DISCOVER', ('<broadcast>', DISCOVERY_PORT))
    print("Sent discovery broadcast")

    try:
        data, addr = sock.recvfrom(512)
        response = data.decode('ascii')
        print(f"Received: {response} from {addr}")
        return True
    except socket.timeout:
        print("Discovery timeout (expected if no network)")
        return False
    finally:
        sock.close()


def test_dsu_protocol():
    """Test DSU protocol handshake"""
    print("\n--- Testing DSU Protocol ---")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    client_id = 12345

    # Test 1: Version request
    print("\n[Test 1] Version request...")
    send_version_request(sock, client_id)
    try:
        data, addr = sock.recvfrom(512)
        magic = data[0:4]
        msg_type = struct.unpack_from('<H', data, 16)[0]
        version = struct.unpack_from('<I', data, 16)[0]
        print(f"  Response: magic={magic} msg_type=0x{msg_type:06x} version={version}")
        print("  PASS" if magic == b'DSUS' else "  FAIL")
    except socket.timeout:
        print("  TIMEOUT")
    except Exception as e:
        print(f"  ERROR: {e}")

    # Test 2: Controller request
    print("\n[Test 2] Controller request...")
    send_controller_request(sock, client_id, 0)
    try:
        data, addr = sock.recvfrom(512)
        magic = data[0:4]
        msg_type = struct.unpack_from('<H', data, 16)[0]
        slot = data[16] if len(data) > 16 else -1
        state = data[17] if len(data) > 17 else -1
        print(f"  Response: magic={magic} msg_type=0x{msg_type:06x} slot={slot} state={state}")
        print("  PASS" if magic == b'DSUS' else "  FAIL")
    except socket.timeout:
        print("  TIMEOUT")
    except Exception as e:
        print(f"  ERROR: {e}")

    # Test 3: Pad data request
    print("\n[Test 3] Pad data request...")
    send_pad_data_request(sock, client_id, [0])
    try:
        data, addr = sock.recvfrom(512)
        magic = data[0:4]
        msg_type = struct.unpack_from('<H', data, 16)[0]
        print(f"  Response: magic={magic} msg_type=0x{msg_type:06x} len={len(data)}")
        if len(data) > 20:
            lx = data[24]
            ly = data[25]
            print(f"  Sticks: lx={lx} ly={ly}")
        print("  PASS" if magic == b'DSUS' else "  FAIL")
    except socket.timeout:
        print("  TIMEOUT")
    except Exception as e:
        print(f"  ERROR: {e}")

    sock.close()


def main():
    print("=" * 50)
    print("  DSU Protocol Test Client")
    print("=" * 50)
    print(f"  Target: {DSU_IP}:{DSU_PORT}")
    print(f"  Discovery: {DSU_IP}:{DISCOVERY_PORT}")

    test_discovery()
    test_dsu_protocol()

    print("\n" + "=" * 50)
    print("  Tests complete!")
    print("=" * 50)


if __name__ == '__main__':
    main()
