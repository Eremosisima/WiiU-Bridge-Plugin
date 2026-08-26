#!/usr/bin/env python3
"""
DSU Server -> WiiU Bridge plugin v23 (wire format LE, blocks @ offset 20).

Reads a real gamepad via pygame (if available), otherwise simulates input.
Sends MULTI packets at ~60Hz over UDP.

Usage:
    python dsu-server-wiiu.py [--ip 192.168.100.42] [--port 26760] [--simulate]
"""

import socket
import struct
import time
import argparse

DSU_MSG_VERSION = 0x00100000
DSU_MSG_MULTI = 0x00100003
DSU_MSG_PING = 0x00100004

# WPAD Pro Controller button masks (what the plugin expects)
PRO_A = 0x00000010
PRO_B = 0x00000040
PRO_X = 0x00000008
PRO_Y = 0x00000020
PRO_UP = 0x00000001
PRO_DOWN = 0x00004000
PRO_LEFT = 0x00000002
PRO_RIGHT = 0x00008000
PRO_L = 0x00002000
PRO_R = 0x00000200
PRO_ZL = 0x00000080
PRO_ZR = 0x00000004
PRO_PLUS = 0x00000400
PRO_MINUS = 0x00001000
PRO_HOME = 0x00000800
PRO_SL = 0x00020000
PRO_SR = 0x00010000


def crc32(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def build_multi(blocks):
    """blocks: list of packed 24-byte LE controller blocks (count derived from length)."""
    payload = b''
    for blk in blocks:
        payload += blk
    p = struct.pack('<I', DSU_MSG_MULTI) + payload
    h = bytearray(16)
    h[0:4] = b'DSUC'
    struct.pack_into('<HH', h, 4, 1001, len(p))
    struct.pack_into('<I', h, 12, 12345)
    cd = bytes(h[0:8]) + bytes(h[12:16]) + p
    struct.pack_into('<I', h, 8, crc32(cd))
    return bytes(h) + p


def ctrl_block(buttons=0, lx=0, ly=0, gx=0.0, gy=0.0, irx=0, iry=0,
               d1=(0, 0), d2=(0, 0)):
    return struct.pack('<Ihh ff HH BBBB',
                       buttons & 0xFFFFFFFF,
                       max(-2048, min(2047, lx)),
                       max(-2048, min(2047, ly)),
                       gx, gy,
                       irx & 0xFFFF, iry & 0xFFFF,
                       d1[0], d1[1], d2[0], d2[1])


class RealInput:
    """pygame gamepad reader -> pro controller state."""

    def __init__(self):
        import pygame
        self.pg = pygame
        pygame.init()
        pygame.joystick.init()
        if pygame.joystick.get_count() == 0:
            raise RuntimeError('no joystick')
        self.js = pygame.joystick.Joystick(0)
        self.js.init()
        print('gamepad:', self.js.get_name())

    def read(self):
        self.pg.event.pump()
        js = self.js
        btns = 0

        def b(i):
            return js.get_button(i) if i < js.get_numbuttons() else False

        # generic mapping (works for xbox360 / generic usb)
        if b(0): btns |= PRO_A
        if b(1): btns |= PRO_B
        if b(2): btns |= PRO_X
        if b(3): btns |= PRO_Y
        if b(4): btns |= PRO_L
        if b(5): btns |= PRO_R
        if b(6): btns |= PRO_MINUS
        if b(7): btns |= PRO_PLUS
        if b(8): btns |= PRO_HOME
        if b(9): btns |= PRO_SL
        if b(10): btns |= PRO_SR
        if js.get_numhats() > 0:
            hat = js.get_hat(0)
            if hat[0] == -1: btns |= PRO_LEFT
            if hat[0] == 1: btns |= PRO_RIGHT
            if hat[1] == 1: btns |= PRO_UP
            if hat[1] == -1: btns |= PRO_DOWN
        ax = [js.get_axis(i) for i in range(js.get_numaxes())]
        lx = int(ax[0] * 1024) if len(ax) > 0 else 0
        ly = int(-ax[1] * 1024) if len(ax) > 1 else 0
        rx = int(ax[2] * 1024) if len(ax) > 2 else 0
        ry = int(-ax[3] * 1024) if len(ax) > 3 else 0
        zl = int((ax[4] + 1) * 128) if len(ax) > 4 else 0
        zr = int((ax[5] + 1) * 128) if len(ax) > 5 else 0
        if zl > 60: btns |= PRO_ZL
        if zr > 60: btns |= PRO_ZR
        return btns, lx, ly, rx, ry


class SimulatedInput:
    """auto-presses buttons so you can verify injection without hardware."""

    def __init__(self):
        self.t0 = time.time()
        self.seq = [
            (PRO_A, 500, 0),
            (PRO_B, -500, 300),
            (PRO_X | PRO_R, 800, -800),
            (PRO_Y | PRO_ZR, -800, -500),
            (PRO_PLUS, 0, 900),
        ]

    def read(self):
        t = time.time() - self.t0
        idx = int(t / 2) % len(self.seq)
        btns, lx, ly = self.seq[idx]
        irx = int(512 + 400 * ((t % 10) / 10))
        iry = int(383 + 200 * (((t * 2) % 10) / 10))
        return btns, lx, ly, lx // 2, ly // 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ip', default='192.168.100.42')
    ap.add_argument('--port', type=int, default=26760)
    ap.add_argument('--simulate', action='store_true',
                    help='ignore gamepad and send scripted inputs')
    args = ap.parse_args()

    try:
        inp = None if args.simulate else RealInput()
    except Exception as e:
        print('sin gamepad ({0}), usando simulacion'.format(e))
        inp = None
    if inp is None:
        inp = SimulatedInput()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dst = (args.ip, args.port)
    print('enviando MULTI a {0}:{1} @60Hz - Ctrl+C para salir'.format(*dst))

    n = 0
    t0 = time.time()
    try:
        while True:
            btns, lx, ly, rx, ry = inp.read()
            # right stick not in protocol yet; mirror left is done plugin-side.
            pkt = build_multi([ctrl_block(btns, lx, ly)])
            sock.sendto(pkt, dst)
            n += 1
            if n % 60 == 0:
                rate = n / (time.time() - t0)
                print('pkt={0} rate={1:.0f}/s btns=0x{2:08x}'.format(n, rate, btns))
            time.sleep(1 / 60)
    except KeyboardInterrupt:
        print('\nlisto')


if __name__ == '__main__':
    main()
