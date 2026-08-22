#!/usr/bin/env python3
"""DSU protocol integration test"""
import socket, struct, time, threading

def crc32(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF

results = []

def server():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', 26760))
    sock.settimeout(5.0)
    server_id = 99999
    print('[S] Ready on 26760', flush=True)
    
    for _ in range(3):
        try:
            data, addr = sock.recvfrom(512)
            if len(data) >= 20:
                msg_type = struct.unpack_from('<I', data, 16)[0]
                print(f'[S] Recv 0x{msg_type:08x}', flush=True)
                
                if msg_type == 0x100000:
                    payload = struct.pack('<II', 0x100000, 1001)
                    header = bytearray(16)
                    header[0:4] = b'DSUS'
                    struct.pack_into('<HH', header, 4, 1001, len(payload))
                    struct.pack_into('<I', header, 12, server_id)
                    crc_data = header[0:8] + header[12:16] + payload
                    struct.pack_into('<I', header, 8, crc32(crc_data))
                    sock.sendto(bytes(header) + payload, addr)
                    print('[S] -> VERSION', flush=True)
                    
                elif msg_type == 0x100001:
                    slot = data[20] if len(data) > 20 else 0
                    payload = bytearray(20)
                    struct.pack_into('<I', payload, 0, 0x100001)
                    payload[4] = slot
                    payload[5] = 2
                    payload[6] = 3
                    payload[7] = 2
                    payload[8:14] = b'\x01\x02\x03\x04\x05\x06'
                    payload[14] = 80
                    payload[15] = 1
                    header = bytearray(16)
                    header[0:4] = b'DSUS'
                    struct.pack_into('<HH', header, 4, 1001, len(payload))
                    struct.pack_into('<I', header, 12, server_id)
                    crc_data = header[0:8] + header[12:16] + bytes(payload)
                    struct.pack_into('<I', header, 8, crc32(crc_data))
                    sock.sendto(bytes(header) + bytes(payload), addr)
                    print('[S] -> CONTROLLER', flush=True)
                    
                elif msg_type == 0x100002:
                    payload = bytearray(104)
                    struct.pack_into('<I', payload, 0, 0x100002)
                    struct.pack_into('<I', payload, 4, 0)
                    struct.pack_into('<I', payload, 8, 0x1234)
                    payload[12] = 128
                    payload[13] = 128
                    payload[14] = 128
                    payload[15] = 128
                    header = bytearray(16)
                    header[0:4] = b'DSUS'
                    struct.pack_into('<HH', header, 4, 1001, len(payload))
                    struct.pack_into('<I', header, 12, server_id)
                    crc_data = header[0:8] + header[12:16] + bytes(payload)
                    struct.pack_into('<I', header, 8, crc32(crc_data))
                    sock.sendto(bytes(header) + bytes(payload), addr)
                    print('[S] -> PAD_DATA', flush=True)
        except socket.timeout:
            continue
    sock.close()

def client():
    time.sleep(0.3)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    cid = 12345
    
    def send(mt, extra=b''):
        p = struct.pack('<I', mt) + extra
        h = bytearray(16)
        h[0:4] = b'DSUC'
        struct.pack_into('<HH', h, 4, 1001, len(p))
        struct.pack_into('<I', h, 12, cid)
        cd = h[0:8] + h[12:16] + p
        c = 0xFFFFFFFF
        for b in cd:
            c ^= b
            for _ in range(8):
                c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        struct.pack_into('<I', h, 8, c ^ 0xFFFFFFFF)
        sock.sendto(bytes(h) + p, ('127.0.0.1', 26760))
    
    # Test 1: VERSION
    print('[C] Test 1: VERSION', flush=True)
    send(0x100000)
    try:
        d, _ = sock.recvfrom(512)
        m = d[0:4]
        t = struct.unpack_from('<I', d, 16)[0]
        v = struct.unpack_from('<I', d, 20)[0]
        ok = m == b'DSUS' and t == 0x100000 and v == 1001
        print(f'[C] magic={m} type=0x{t:08x} ver={v} -> PASS' if ok else f'[C] FAIL', flush=True)
        results.append(('VERSION', ok))
    except:
        print('[C] TIMEOUT', flush=True)
        results.append(('VERSION', False))
    
    # Test 2: CONTROLLER
    print('[C] Test 2: CONTROLLER', flush=True)
    send(0x100001, struct.pack('<B', 0))
    try:
        d, _ = sock.recvfrom(512)
        m = d[0:4]
        t = struct.unpack_from('<I', d, 16)[0]
        s = d[20]
        st = d[21]
        ok = m == b'DSUS' and t == 0x100001 and st == 2
        print(f'[C] magic={m} type=0x{t:08x} slot={s} state={st} -> PASS' if ok else f'[C] FAIL', flush=True)
        results.append(('CONTROLLER', ok))
    except:
        print('[C] TIMEOUT', flush=True)
        results.append(('CONTROLLER', False))
    
    # Test 3: PAD_DATA
    print('[C] Test 3: PAD_DATA', flush=True)
    send(0x100002, struct.pack('<BB', 1, 0))
    try:
        d, _ = sock.recvfrom(512)
        m = d[0:4]
        t = struct.unpack_from('<I', d, 16)[0]
        lx = d[28]
        ly = d[29]
        ok = m == b'DSUS' and t == 0x100002 and lx == 128
        print(f'[C] magic={m} type=0x{t:08x} lx={lx} ly={ly} -> PASS' if ok else f'[C] FAIL', flush=True)
        results.append(('PAD_DATA', ok))
    except:
        print('[C] TIMEOUT', flush=True)
        results.append(('PAD_DATA', False))
    
    sock.close()

t1 = threading.Thread(target=server)
t2 = threading.Thread(target=client)
t1.start()
t2.start()
t1.join(timeout=8)
t2.join(timeout=8)

print()
print('=' * 50)
print('  DSU Protocol Integration Test Results:')
print('=' * 50)
passed = 0
for name, ok in results:
    status = 'PASS' if ok else 'FAIL'
    print(f'  {name:15s} -> {status}')
    if ok:
        passed += 1
print(f'  {passed}/{len(results)} tests passed')
print('=' * 50)
