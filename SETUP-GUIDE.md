# WiiU Bridge Plugin - Setup Guide

## Direct Mode (Phone Hotspot)

This guide shows how to connect your phone's controllers to Wii U **without a router**.

### Architecture

```
Phone (WiFi Hotspot) ←→ Wii U (DSU Server Plugin)
         |                        |
   DSUController app        Receives input
   Sends controller data    Emulates Pro Controller
```

---

## Step 1: Enable Phone WiFi Hotspot

### Android
1. Settings → Network & Internet → Hotspot & tethering
2. Turn on WiFi hotspot
3. Note the hotspot name and password
4. **Important:** Use 2.4GHz band (not 5GHz)

### iPhone
1. Settings → Personal Hotspot
2. Turn on "Allow Others to Join"
3. Note the password
4. **Important:** Use "Maximize Compatibility" mode

---

## Step 2: Connect Wii U to Phone Hotspot

1. On Wii U, go to **System Settings** (wrench icon)
2. Select **Internet**
3. Select **Connection Settings**
4. Select **Search for WiFi**
5. Find your phone's hotspot in the list
6. Enter the password
7. Save and test connection

---

## Step 3: Install DSUController App

### Android
1. Install from Google Play Store: **DSUController**
2. Or download from: https://github.com/MiyooCFW/dsucontroller

### iPhone
1. Install **DSU Controller** from App Store

---

## Step 4: Configure Plugin

1. On Wii U, open the plugin config menu:
   - Press **L + DOWN + SELECT** simultaneously
   - Or access via Aroma config menu

2. Check the following:
   - **Enable Bridge**: ON
   - **Input Mode**: DSU (Default)
   - **Direct Mode (Phone Hotspot)**: ON

3. Note the **Wii U IP Address** shown in the config menu
   - Example: `192.168.43.100`

---

## Step 5: Connect DSUController to Wii U

1. Make sure phone is connected to the same WiFi (your hotspot)
2. Open DSUController app
3. Enter the Wii U IP address from Step 4
4. Enter port: **26760**
5. Tap **Connect**

---

## Step 6: Test

1. Press buttons on your phone
2. Check if the Wii U responds
3. If using VPADRead mode (GamePad), the input should override the GamePad
4. If using WPADRead mode (Pro Controller), the input should appear as a Pro Controller

---

## Troubleshooting

### "No connection"
- Verify phone hotspot is 2.4GHz (not 5GHz)
- Check Wii U is connected to the hotspot
- Verify IP address is correct in DSUController

### "Input lag"
- Use 2.4GHz band (not 5GHz)
- Close other apps on phone
- Reduce DSUController update rate

### "Controller not recognized"
- Make sure plugin is loaded (check Aroma plugins list)
- Verify DSU mode is enabled in config
- Restart Wii U and try again

---

## Advanced: Static IP Configuration

For more reliable connections, set a static IP on the Wii U:

1. In plugin config menu, enable **Static IP**
2. Enter the IP address (e.g., `192.168.43.100`)
3. Save and restart

---

## Button Mapping (DSU → Wii U Pro Controller)

| DSU Button | Wii U Button |
|------------|--------------|
| Cross (X) | A |
| Circle (O) | B |
| Square | X |
| Triangle | Y |
| L1 | L |
| R1 | R |
| L2 | ZL |
| R2 | ZR |
| Share | Minus (-) |
| Options | Plus (+) |
| L3 | Stick L press |
| R3 | Stick R press |
| PS | Home |
| D-Pad | D-Pad |
| Left Stick | Left Stick |
| Right Stick | Right Stick |

---

## Support

- GitHub: https://github.com/Eremosisima/WiiU-Bridge-Plugin
- Issues: https://github.com/Eremosisima/WiiU-Bridge-Plugin/issues
