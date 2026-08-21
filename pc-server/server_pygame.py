#!/usr/bin/env python3
"""
WiiU Bridge Server - PC Side (Pygame version)
Reads Xbox 360 controller via pygame and sends data over TCP to Wii U plugin.
"""

import socket
import struct
import time
import sys
import argparse

try:
    import pygame
    from pygame.locals import *
except ImportError:
    print("ERROR: pygame not found. Install with: pip install pygame")
    sys.exit(1)

# Protocol constants
HEADER_MAGIC = 0x57425244  # "WBRD" - WiiU Bridge
PROTOCOL_VERSION = 1

# Wii U Pro Controller button mapping (WPADProButton)
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

def map_pygame_to_wiiu(joystick) -> tuple:
    """Map pygame joystick state to Wii U Pro Controller format."""
    wiiu_buttons = 0
    
    # Get axes (Xbox 360 layout: 0=LX, 1=LY, 2=RX, 3=RY, 4=LT, 5=RT)
    try:
        left_x = int(joystick.get_axis(0) * 32767)
        left_y = int(-joystick.get_axis(1) * 32767)  # Invert Y
        right_x = int(joystick.get_axis(2) * 32767)
        right_y = int(-joystick.get_axis(3) * 32767)  # Invert Y
        left_trigger = joystick.get_axis(4)  # -1 to 1
        right_trigger = joystick.get_axis(5)  # -1 to 1
    except:
        left_x = left_y = right_x = right_y = 0
        left_trigger = right_trigger = -1.0
    
    # Map buttons (Xbox 360: A=0, B=1, X=2, Y=3, LB=4, RB=5, Back=6, Start=7)
    for i in range(min(joystick.get_numbuttons(), 12)):
        if joystick.get_button(i):
            if i == 0:  # A
                wiiu_buttons |= WPAD_PRO_BUTTON_A
            elif i == 1:  # B
                wiiu_buttons |= WPAD_PRO_BUTTON_B
            elif i == 2:  # X
                wiiu_buttons |= WPAD_PRO_BUTTON_X
            elif i == 3:  # Y
                wiiu_buttons |= WPAD_PRO_BUTTON_Y
            elif i == 4:  # LB
                wiiu_buttons |= WPAD_PRO_BUTTON_L
            elif i == 5:  # RB
                wiiu_buttons |= WPAD_PRO_BUTTON_R
            elif i == 6:  # Back
                wiiu_buttons |= WPAD_PRO_BUTTON_MINUS
            elif i == 7:  # Start
                wiiu_buttons |= WPAD_PRO_BUTTON_PLUS
            elif i == 8:  # Left thumb
                wiiu_buttons |= WPAD_PRO_BUTTON_STICK_L
            elif i == 9:  # Right thumb
                wiiu_buttons |= WPAD_PRO_BUTTON_STICK_R
    
    # Map D-Pad (Xbox 360: POV hat or axes 6/7)
    try:
        # Try POV hat first
        hat = joystick.get_hat(0)
        if hat[0] == -1:
            wiiu_buttons |= WPAD_PRO_BUTTON_LEFT
        elif hat[0] == 1:
            wiiu_buttons |= WPAD_PRO_BUTTON_RIGHT
        if hat[1] == 1:
            wiiu_buttons |= WPAD_PRO_BUTTON_UP
        elif hat[1] == -1:
            wiiu_buttons |= WPAD_PRO_BUTTON_DOWN
    except:
        pass
    
    # Triggers as ZL/ZR (threshold at 30%)
    if left_trigger > -0.4:  # ~30% pressed
        wiiu_buttons |= WPAD_PRO_BUTTON_ZL
    if right_trigger > -0.4:  # ~30% pressed
        wiiu_buttons |= WPAD_PRO_BUTTON_ZR
    
    # Normalize sticks to Wii U range (-2048..2047)
    left_x = max(-2048, min(2047, int(left_x * 2048 / 32767)))
    left_y = max(-2048, min(2047, int(left_y * 2048 / 32767)))
    right_x = max(-2048, min(2047, int(right_x * 2048 / 32767)))
    right_y = max(-2048, min(2047, int(right_y * 2048 / 32767)))
    
    return wiiu_buttons, left_x, left_y, right_x, right_y

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
    parser = argparse.ArgumentParser(description='WiiU Bridge Server (Pygame)')
    parser.add_argument('--host', default='0.0.0.0', help='Host to bind to')
    parser.add_argument('--port', type=int, default=12345, help='Port to listen on')
    args = parser.parse_args()
    
    # Initialize pygame
    pygame.init()
    pygame.joystick.init()
    
    # Wait for joystick
    print("[SERVER] Waiting for controller...")
    while pygame.joystick.get_count() == 0:
        pygame.event.pump()
        pygame.time.wait(100)
    
    joystick = pygame.joystick.Joystick(0)
    joystick.init()
    print(f"[SERVER] Using controller: {joystick.get_name()}")
    
    # Create server socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind((args.host, args.port))
    server_socket.listen(1)
    server_socket.settimeout(1.0)
    
    print(f"[SERVER] Listening on {args.host}:{args.port}")
    print("[SERVER] Waiting for Wii U connection...")
    
    client_socket = None
    running = True
    
    while running:
        try:
            client_socket, addr = server_socket.accept()
            print(f"[SERVER] Wii U connected from {addr}")
            
            client_socket.settimeout(0.01)
            last_packet_time = time.time()
            packet_count = 0
            
            while running:
                # Handle pygame events
                for event in pygame.event.get():
                    if event.type == QUIT:
                        running = False
                    elif event.type == KEYDOWN:
                        if event.key == K_ESCAPE:
                            running = False
                
                # Read controller
                pygame.event.pump()
                buttons, lx, ly, rx, ry = map_pygame_to_wiiu(joystick)
                
                # Create and send packet
                packet = create_packet(buttons, lx, ly, rx, ry)
                try:
                    client_socket.sendall(packet)
                except:
                    print("[SERVER] Wii U disconnected")
                    break
                
                packet_count += 1
                current_time = time.time()
                
                # Print stats every second
                if current_time - last_packet_time >= 1.0:
                    print(f"[SERVER] Packets/sec: {packet_count}")
                    packet_count = 0
                    last_packet_time = current_time
                
                # Target ~60Hz
                pygame.time.Clock().tick(60)
            
            if client_socket:
                client_socket.close()
                client_socket = None
            
            if running:
                print("[SERVER] Waiting for new connection...")
        
        except socket.timeout:
            continue
        except KeyboardInterrupt:
            print("\n[SERVER] Shutting down...")
            running = False
        except Exception as e:
            print(f"[SERVER] Error: {e}")
            continue
    
    pygame.quit()
    server_socket.close()
    print("[SERVER] Stopped")

if __name__ == '__main__':
    main()
