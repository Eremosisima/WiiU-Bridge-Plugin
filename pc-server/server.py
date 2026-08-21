#!/usr/bin/env python3
"""
WiiU Bridge Server - PC Side
Reads Xbox 360 controller via XInput and sends data over TCP to Wii U plugin.
"""

import socket
import struct
import time
import sys
import argparse
from typing import Optional

try:
    import xinput
except ImportError:
    print("ERROR: xinput module not found. Install with: pip install xinput")
    print("Alternative: Use pygame for controller input")
    sys.exit(1)

# Protocol constants
HEADER_MAGIC = 0x57425244  # "WBRD" - WiiU Bridge
PROTOCOL_VERSION = 1
PACKET_SIZE = 20  # Header(8) + Buttons(4) + LeftStick(4) + RightStick(4) + Triggers(4)

# Xbox 360 button bitmasks (XInput format)
XINPUT_GAMEPAD_DPAD_UP = 0x0001
XINPUT_GAMEPAD_DPAD_DOWN = 0x0002
XINPUT_GAMEPAD_DPAD_LEFT = 0x0004
XINPUT_GAMEPAD_DPAD_RIGHT = 0x0008
XINPUT_GAMEPAD_START = 0x0010
XINPUT_GAMEPAD_BACK = 0x0020
XINPUT_GAMEPAD_LEFT_THUMB = 0x0040
XINPUT_GAMEPAD_RIGHT_THUMB = 0x0080
XINPUT_GAMEPAD_LEFT_SHOULDER = 0x0100
XINPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200
XINPUT_GAMEPAD_A = 0x1000
XINPUT_GAMEPAD_B = 0x2000
XINPUT_GAMEPAD_X = 0x4000
XINPUT_GAMEPAD_Y = 0x8000

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

def map_xinput_to_wiiu(xinput_buttons: int, left_trigger: int, right_trigger: int) -> tuple:
    """Map Xbox 360 buttons to Wii U Pro Controller format."""
    wiiu_buttons = 0
    
    # D-Pad
    if xinput_buttons & XINPUT_GAMEPAD_DPAD_UP:
        wiiu_buttons |= WPAD_PRO_BUTTON_UP
    if xinput_buttons & XINPUT_GAMEPAD_DPAD_DOWN:
        wiiu_buttons |= WPAD_PRO_BUTTON_DOWN
    if xinput_buttons & XINPUT_GAMEPAD_DPAD_LEFT:
        wiiu_buttons |= WPAD_PRO_BUTTON_LEFT
    if xinput_buttons & XINPUT_GAMEPAD_DPAD_RIGHT:
        wiiu_buttons |= WPAD_PRO_BUTTON_RIGHT
    
    # Face buttons
    if xinput_buttons & XINPUT_GAMEPAD_A:
        wiiu_buttons |= WPAD_PRO_BUTTON_A
    if xinput_buttons & XINPUT_GAMEPAD_B:
        wiiu_buttons |= WPAD_PRO_BUTTON_B
    if xinput_buttons & XINPUT_GAMEPAD_X:
        wiiu_buttons |= WPAD_PRO_BUTTON_X
    if xinput_buttons & XINPUT_GAMEPAD_Y:
        wiiu_buttons |= WPAD_PRO_BUTTON_Y
    
    # Shoulder buttons
    if xinput_buttons & XINPUT_GAMEPAD_LEFT_SHOULDER:
        wiiu_buttons |= WPAD_PRO_BUTTON_L
    if xinput_buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER:
        wiiu_buttons |= WPAD_PRO_BUTTON_R
    
    # Thumbstick clicks
    if xinput_buttons & XINPUT_GAMEPAD_LEFT_THUMB:
        wiiu_buttons |= WPAD_PRO_BUTTON_STICK_L
    if xinput_buttons & XINPUT_GAMEPAD_RIGHT_THUMB:
        wiiu_buttons |= WPAD_PRO_BUTTON_STICK_R
    
    # Start/Back -> Plus/Minus
    if xinput_buttons & XINPUT_GAMEPAD_START:
        wiiu_buttons |= WPAD_PRO_BUTTON_PLUS
    if xinput_buttons & XINPUT_GAMEPAD_BACK:
        wiiu_buttons |= WPAD_PRO_BUTTON_MINUS
    
    # Triggers as ZL/ZR (threshold at 30/255)
    if left_trigger > 30:
        wiiu_buttons |= WPAD_PRO_BUTTON_ZL
    if right_trigger > 30:
        wiiu_buttons |= WPAD_PRO_BUTTON_ZR
    
    return wiiu_buttons

def normalize_stick(value: int, deadzone: int = 8689) -> int:
    """Normalize Xbox 360 stick value (-32768..32767) to Wii U range (-2048..2047)."""
    if abs(value) < deadzone:
        return 0
    # Scale from Xbox range to Wii U range
    if value > 0:
        return int((value - deadzone) * 2047 / (32767 - deadzone))
    else:
        return int((value + deadzone) * 2048 / (-32768 + deadzone))

def create_packet(controller_state) -> bytes:
    """Create a data packet from controller state."""
    # Map buttons
    wiiu_buttons = map_xinput_to_wiiu(
        controller_state.Gamepad.wButtons,
        controller_state.Gamepad.bLeftTrigger,
        controller_state.Gamepad.bRightTrigger
    )
    
    # Normalize sticks
    left_x = normalize_stick(controller_state.Gamepad.sThumbLX)
    left_y = normalize_stick(controller_state.Gamepad.sThumbLY)
    right_x = normalize_stick(controller_state.Gamepad.sThumbRX)
    right_y = normalize_stick(controller_state.Gamepad.sThumbRY)
    
    # Pack packet: header(8) + buttons(4) + left_stick(4) + right_stick(4)
    packet = struct.pack('<IIhhi',
        HEADER_MAGIC,
        PROTOCOL_VERSION,
        left_x, left_y,
        right_x, right_y
    ) + struct.pack('<I', wiiu_buttons)
    
    return packet

class WiiUBridgeServer:
    def __init__(self, host: str = '0.0.0.0', port: int = 12345):
        self.host = host
        self.port = port
        self.server_socket: Optional[socket.socket] = None
        self.client_socket: Optional[socket.socket] = None
        self.running = False
    
    def start(self):
        """Start the TCP server."""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(1)
        self.server_socket.settimeout(1.0)
        
        print(f"[SERVER] Listening on {self.host}:{self.port}")
        print("[SERVER] Waiting for Wii U connection...")
        
        self.running = True
        
        while self.running:
            try:
                self.client_socket, addr = self.server_socket.accept()
                print(f"[SERVER] Wii U connected from {addr}")
                self.handle_client()
            except socket.timeout:
                continue
            except KeyboardInterrupt:
                print("\n[SERVER] Shutting down...")
                break
            except Exception as e:
                print(f"[SERVER] Error: {e}")
                continue
    
    def handle_client(self):
        """Handle connected Wii U client."""
        self.client_socket.settimeout(0.01)  # 10ms timeout for non-blocking reads
        
        controller = xinput.XInputGamepad(0)
        
        print("[SERVER] Starting controller stream...")
        last_packet_time = time.time()
        packet_count = 0
        
        while self.running:
            try:
                # Read controller state
                state = controller.get_state()
                
                # Create and send packet
                packet = create_packet(state)
                self.client_socket.sendall(packet)
                
                packet_count += 1
                current_time = time.time()
                
                # Print stats every second
                if current_time - last_packet_time >= 1.0:
                    print(f"[SERVER] Packets/sec: {packet_count}")
                    packet_count = 0
                    last_packet_time = current_time
                
                # Target ~60Hz update rate
                time.sleep(1/60)
                
            except ConnectionResetError:
                print("[SERVER] Wii U disconnected")
                break
            except ConnectionAbortedError:
                print("[SERVER] Connection aborted")
                break
            except Exception as e:
                print(f"[SERVER] Error: {e}")
                break
        
        if self.client_socket:
            self.client_socket.close()
            self.client_socket = None
        
        print("[SERVER] Client handler stopped")
    
    def stop(self):
        """Stop the server."""
        self.running = False
        if self.client_socket:
            self.client_socket.close()
        if self.server_socket:
            self.server_socket.close()

def main():
    parser = argparse.ArgumentParser(description='WiiU Bridge Server')
    parser.add_argument('--host', default='0.0.0.0', help='Host to bind to')
    parser.add_argument('--port', type=int, default=12345, help='Port to listen on')
    args = parser.parse_args()
    
    server = WiiUBridgeServer(args.host, args.port)
    
    try:
        server.start()
    except KeyboardInterrupt:
        print("\n[SERVER] Shutting down...")
    finally:
        server.stop()

if __name__ == '__main__':
    main()
