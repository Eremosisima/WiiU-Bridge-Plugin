# WiiU Bridge Controller

Bridge external controllers to your Wii U via DSU protocol (CemuHook), TCP, or UDP.

## Features

- **DSU Protocol (Default)**: Compatible with DSUController, Cemu, Dolphin, Yuzu and more
- **TCP Bridge**: Direct connection from PC server
- **UDP Bridge**: Android phone as Bluetooth controller bridge
- **Device Discovery**: Auto-find DSU servers on local network
- **Motion Data Support**: (Coming in v3.1)
- **Multi-controller**: Up to 4 controllers via DSU slots

## Architecture

```
[Android/PC] ──UDP/DSU──► [Wii U Plugin] ──► VPADRead/WPADRead ──► [Game]
```

The plugin hooks into Wii U's input system and injects controller data from external sources.

## Installation

### Prerequisites

- Wii U with Aroma custom firmware
- devkitPPC toolchain
- WUPS (Wii U Plugin System)

### Build

```bash
cd wiiu-plugin
make
```

### Install

Copy `wiiu_bridge_plugin.wps` to:
```
sd:/wiiu/environments/aroma/plugins/
```

## Usage

### DSU Mode (Recommended)

1. Install DSUController on your Android phone
2. Connect Bluetooth controller to phone
3. Launch the plugin on Wii U
4. The plugin auto-discovers DSU servers on your network
5. Select your device in the config menu

### TCP Mode

1. Run PC server: `python pc-server/server_simple.py`
2. Set Wii U IP in config menu
3. Select TCP mode in config menu

### UDP Mode

1. Run Android server app
2. Set Wii U IP in the app
3. Select UDP mode in config menu

## Configuration

Access the plugin config menu from Aroma's plugin settings.

| Setting | Default | Description |
|---------|---------|-------------|
| Enable Bridge | true | Enable/disable the bridge |
| Input Mode | DSU | DSU, TCP, UDP, or WBRD |
| DSU Port | 26760 | DSU server port |
| TCP Port | 12345 | TCP server port |
| UDP Port | 12345 | UDP server port |
| Auto-discover | true | Auto-find DSU servers |

## Protocol

### DSU (DualShock UDP)

Standard protocol used by Cemu, Dolphin, Yuzu for motion controller input.

- Port: 26760 UDP
- Version: 1001
- Supports: Buttons, Sticks, Triggers, Motion (gyro/accel)
- Compatible with: DSUController, DS4Windows, JoyShockMapper

### Button Mapping (DSU → Wii U)

| DSU | Wii U |
|-----|-------|
| Cross | A |
| Circle | B |
| Square | X |
| Triangle | Y |
| L1 | L |
| R1 | R |
| L2 | ZL |
| R2 | ZR |
| Share | Minus |
| Options | Plus |
| L3 | Stick L Press |
| R3 | Stick R Press |
| PS | Home |

## Project Structure

```
WiiU-Bridge-Plugin/
├── wiiu-plugin/          # Wii U plugin (Aroma)
│   ├── main_v2.cpp       # Plugin entry point
│   ├── dsu_server.h/c    # DSU protocol server
│   ├── dsu_crc32.h/c     # CRC32 for DSU packets
│   ├── discovery.h/c     # Device discovery
│   ├── bridge_udp.h/c    # UDP bridge (legacy)
│   ├── bridge.h/c        # TCP bridge (legacy)
│   └── Makefile
├── android-server/       # Android Bluetooth bridge app
├── pc-server/            # PC controller server
└── README.md
```

## Compatible Apps

| App | Platform | Protocol |
|-----|----------|----------|
| DSUController | Android/iOS | DSU |
| DS4Windows | Windows | DSU |
| JoyShockMapper | Windows/Linux | DSU |
| Phone2Pad | Android/Windows | DSU |
| PadConnect | Android | Custom |

## Credits

- [cemuhook-protocol](https://github.com/v1993/cemuhook-protocol) - DSU protocol specification
- [WUPS](https://github.com/wiiu-env/WiiUPluginSystem) - Wii U Plugin System
- [Network-Input-Server](https://github.com/Teotia444/Network-Input-Server) - Reference implementation
- [Bloopair](https://github.com/GaryOderNichts/Bloopair) - Bluetooth controller support

## License

MIT License
