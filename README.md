# WiiU Bridge Controller

Bridge external controllers to your Wii U via DSU protocol (CemuHook).

## Version

**v4.0.0** - Working build with lazy initialization

## Features

- **DSU Protocol**: Compatible with DSUController, Cemu, Dolphin, Yuzu
- **Lazy Init**: Plugin loads lightweight, DSU server starts on first game frame
- **Config Menu**: Simple Connect/Disconnect via Aroma config (L+DOWN+SELECT)
- **Direct Mode**: Phone hotspot connection without router
- **Auto-Discovery**: Broadcast on UDP 26761

## Architecture

```
[Android/PC] ──UDP DSU 26760──► [Wii U Plugin] ──► VPADRead hooks ──► [Game]
```

## Build

```bash
cd wiiu-plugin
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC
make
```

### Critical Build Requirements

- **LIBS must include `-lwups`** before `-lwut` (linking order matters)
- **`nn_ac` import**: Comes from WUPS library, cannot be removed. Empty header, harmless.
- **No `WUPS_USE_STORAGE`**: Causes hang on config menu exit

## Install

Copy `wiiu_bridge_plugin.wps` to:
```
sd:/wiiu/environments/aroma/plugins/
```

## Usage

1. Plugin loads with Aroma (lightweight, no networking)
2. Open any game
3. DSU server auto-starts on first VPADRead call
4. Open config: **L + DOWN + SELECT**
5. Connect DSUController app to Wii U IP:26760

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| Enable Bridge | true | Enable/disable the bridge |
| Input Mode | DSU | DSU protocol |
| DSU Port | 26760 | DSU server port |
| Direct Mode | true | Phone hotspot mode |

## DSU Protocol

- Port: 26760 UDP
- Version: 1001
- Supports: Buttons, Sticks, Triggers
- Compatible with: DSUController, DS4Windows, JoyShockMapper

### Button Mapping

| DSU | Wii U |
|-----|-------|
| Cross | A |
| Circle | B |
| Square | X |
| Triangle | Y |
| L1/R1 | L/R |
| L2/R2 | ZL/ZR |
| Share | Minus |
| Options | Plus |
| L3/R3 | Stick Press |

## Project Structure

```
WiiU-Bridge-Plugin/
├── wiiu-plugin/
│   ├── main_v2.cpp         # Plugin entry (VPADRead hook + config)
│   ├── dsu_server.h/c      # DSU protocol server
│   ├── dsu_crc32.h/c       # CRC32 for DSU packets
│   ├── discovery.h/c       # Device discovery
│   ├── bridge.h/c          # TCP bridge
│   ├── bridge_udp.h/c      # UDP bridge
│   └── Makefile
├── test-dsu-server.py      # DSU server emulator (PC)
├── test-dsu-client.py      # DSU client for testing
├── test-integration.py     # Automated protocol tests
└── README.md
```

## Debugging

### Enable OSReport logging

Plugin outputs debug via `OSReport()` which appears on Wii U console.

### Common Issues

| Issue | Solution |
|-------|----------|
| Aroma fails to load | Remove plugin from SD, check LIBS has `-lwups` |
| Config menu hangs | Remove `WUPS_USE_STORAGE` macro |
| DSU not responding | Check game is open (VPADRead must be called) |
| Different subnets | PC and Wii U must be on same network |

## Development History

### v4.0.0 (Current)
- Rebuilt from scratch with clean codebase
- Fixed: `-lwups` in LIBS (was missing, caused Aroma crash)
- Fixed: Removed `WUPS_USE_STORAGE` (caused config menu hang)
- Fixed: Lazy init (no networking at boot)
- Verified: VPADRead hook works
- Verified: DSU server compiles and links
- Pending: Live test with DSUController

### v3.x (Previous)
- Multiple iterations debugging Aroma load failures
- Root cause identified: `nn_ac` import from WUPS library (harmless, empty)
- Root cause identified: Missing `-lwups` in LIBS
- Root cause identified: Storage API causing hangs

### v1.0-v2.0 (Initial)
- Initial DSU protocol implementation
- TCP/UDP bridge support
- Device discovery
- Android server skeleton

## Credits

- [cemuhook-protocol](https://github.com/v1993/cemuhook-protocol) - DSU protocol
- [WUPS](https://github.com/wiiu-env/WiiUPluginSystem) - Wii U Plugin System
- [Bloopair](https://github.com/GaryOderNichts/Bloopair) - BT controller reference

## License

MIT License
