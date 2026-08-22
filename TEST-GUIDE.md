# WiiU Bridge Plugin - Test Guide

## Deployment Status
- **Plugin:** `L:\wiiu\environments\aroma\plugins\wiiu_bridge_plugin.wps` (286KB)
- **SD Card:** L: drive (31GB)
- **PC IP:** 192.168.168.100 (Ethernet 2)
- **DSU Server:** Running (PID from tasklist)

## Test Steps

### 1. Start DSU Server (PC)
```bash
python C:\WiiU-Bridge-Plugin\test-dsu-server.py
```
- DSU Port: 26760
- Discovery Port: 26761
- Server IP: 192.168.168.100

### 2. Boot Wii U
1. Power on Wii U
2. Launch Aroma environment
3. Plugin loads automatically from `/wiiu/environments/aroma/plugins/`
4. Check TV screen for "WiiU Bridge v3.0 ready" notification

### 3. Configure Plugin (Wii U)
1. Open Aroma config menu (L + DOWN + SELECT)
2. Navigate to "WiiU Bridge Controller"
3. Settings:
   - Enable Bridge: ON
   - Input Mode: DSU (Default)
   - Auto-discover: ON
4. Note the Wii U IP address displayed

### 4. Connect from DSUController (Android)
1. Install DSUController from Play Store
2. Open DSUController
3. Tap "Add new server"
4. Enter Wii U IP address (or use auto-discover)
5. Port: 26760
6. Connect

### 5. Test Controls
| Keyboard | DSU Button | Wii U Button |
|----------|------------|--------------|
| WASD | Left Stick | Left Stick |
| IJKL | Right Stick | Right Stick |
| Z | Cross | A |
| X | Circle | B |
| C | Square | X |
| V | Triangle | Y |
| Q | L1 | L |
| E | R1 | R |
| 1 | L2 | ZL |
| 2 | R2 | ZR |
| SPACE | Options | + |
| ENTER | Share | - |

## Troubleshooting

### No connection
- Check Wii U and PC are on same network (192.168.168.x)
- Verify Wii U IP address in plugin config
- Check firewall isn't blocking UDP 26760/26761

### Plugin not loading
- Verify `.wps` file is in correct directory
- Check Aroma environment is running
- Look for error messages in Wii U console output

### Input not responding
- Verify DSUController shows "Connected"
- Check button mappings in DSUController settings
- Try different controller mode in plugin config
