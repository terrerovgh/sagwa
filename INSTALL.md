# Sagwa Installation & Flashing Guide

This guide provides simple, step-by-step instructions to install and run **Sagwa** on an **M5Stack CoreS3** (ESP32-S3).

---

## Method 1: 1-Minute Quick Flash (Recommended)

No compiler or ESP-IDF setup is required! Pre-built, verified binaries are included in the repository inside `bin/`.

### Prerequisites
1. **M5Stack CoreS3** connected to your computer with a high-quality USB-C data cable.
2. **Python 3.8+** installed on your computer.
3. Install `esptool` (if not already installed):
   ```bash
   pip install esptool
   ```

### 1-Step Flashing

#### On macOS / Linux:
```bash
./flash.sh
```

#### On Windows / Any OS:
```bash
python3 flash.py
```

### What Happens Automatically:
1. The script auto-detects your CoreS3 serial port (`/dev/cu.usbmodem*`, `/dev/ttyACM*`, or `COM*`).
2. It verifies the 5 binary partitions in `bin/`:
   - `0x000000`: `bootloader.bin` (ESP32-S3 bootloader)
   - `0x010000`: `partition-table.bin` (Custom 16MB partition table)
   - `0x01D000`: `ota_data_initial.bin` (OTA data)
   - `0x020000`: `sagwa-cores3.bin` (Main application firmware)
   - `0x830000`: `sagwa_clips.bin` (7.8 MB 22-clip animation pack)
3. It flashes everything at 1.5 Mbps baud rate.
4. The board automatically resets and boots into Sagwa!

---

## Method 2: Manual Flashing with `esptool`

If you prefer to flash using standard command-line tools:

```bash
# Replace <PORT> with your serial port (e.g., /dev/cu.usbmodem1101 or COM3)
python3 -m esptool --chip esp32s3 -p <PORT> -b 1500000 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size keep \
  0x000000 bin/bootloader.bin \
  0x010000 bin/partition-table.bin \
  0x01D000 bin/ota_data_initial.bin \
  0x020000 bin/sagwa-cores3.bin \
  0x830000 bin/sagwa_clips.bin
```

### Updating Only the Animation Clips
If you modify or generate new clips, you only need to flash the `sagwa` partition:

```bash
python3 flash.py --clips-only
# Or manually:
python3 -m esptool --chip esp32s3 -p <PORT> -b 1500000 write_flash 0x830000 bin/sagwa_clips.bin
```

### Updating Only the Application Code
```bash
python3 flash.py --app-only
# Or manually:
python3 -m esptool --chip esp32s3 -p <PORT> -b 1500000 write_flash 0x020000 bin/sagwa-cores3.bin
```

---

## Method 3: Building from Source

For developers who want to modify firmware, sensor logic, or the UI.

### 1. Requirements
- **ESP-IDF v6.0.1** (Exact version required).
  Follow Espressif's [ESP-IDF Installation Guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/).
- Python 3 with `Pillow`, `numpy`, and `imageio-ffmpeg` (for asset packaging).

### 2. Activate the Environment
```bash
. $IDF_PATH/export.sh
```

### 3. Build CoreS3 Firmware
From the `muse-gadget-sdk/esp32` directory:

```bash
cd muse-gadget-sdk/esp32

# Build the firmware image for CoreS3
tools/muse/board.sh build cores3
```

This compiles the firmware and generates the build directory `build-muse-m5stack-cores3/`.

### 4. Flash from the Build System
```bash
tools/muse/board.sh flash cores3
```

### 5. (Optional) Rebuilding the Animation Pack
If you edit hub poses in `sagwa/hubs` or video clips in `sagwa/clips`:

```bash
cd sagwa/tools
python3 pack_clips.py
```
This writes the freshly packed `sagwa_clips.bin` directly into the firmware avatar component directory.

---

## Automatic Updates

Sagwa updates herself from GitHub releases (see [ARCHITECTURE.md](ARCHITECTURE.md#self-update-from-github-mainsagwa_updatec)):

- **First time**: flash a build that contains the updater (`./flash.sh` with the current `bin/`, or `tools/muse/board.sh flash cores3`). From then on she updates on her own over Wi-Fi.
- **Publishing**: every push to `main` (except changes to Markdown files or `bin/`) runs the *Sagwa release* workflow. You can also run it by hand from the Actions tab. Edited clips (`sagwa/clips/`, `sagwa/hubs/`) are repacked automatically.
- **Signing**: the board only accepts images signed with the key its firmware was built with. The workflow uses the repository's development key (`muse-gadget-sdk/esp32/dev_signing_key.pem`) unless you add your own as the `SAGWA_SIGNING_KEY` repository secret; if you do, flash one build made with that key by hand first.
- **Settings** (`idf.py menuconfig`, *ESP32 Device SDK*):
  - `CONFIG_SAGWA_AUTO_UPDATE`: turn self-updates off.
  - `CONFIG_SAGWA_UPDATE_INTERVAL_MIN`: minutes between checks (default 180).
  - `CONFIG_SAGWA_UPDATE_REPO`: the `owner/name` she updates from.
  - `CONFIG_SAGWA_TZ`: your POSIX time zone, so her day and night follow your clock (default `UTC0`; for example `CST6CDT,M3.2.0,M11.1.0` or `CET-1CEST,M3.5.0,M10.5.0/3`).
- **microSD card (optional)**: format it **FAT32** (exFAT and NTFS aren't supported by ESP-IDF; on macOS, Disk Utility > Erase > "MS-DOS (FAT)"). Sagwa keeps `/sagwa/cache/` (weather) and `/sagwa/update/` (downloads staged during an update, cleared once it succeeds) on it. Settings > Sagwa shows the free space, or "Not FAT32".
- **Offline info**: tap Sagwa twice. The time zone is taken from the weather service (by your IP's location) unless you set `CONFIG_SAGWA_TZ`.
- **Sounds**: the cat sounds are generated from `sagwa/sounds/` with `python3 sagwa/tools/make_sfx.py` (needs `numpy` and `imageio-ffmpeg`); add `--wav DIR` to listen to every take.

---

## Troubleshooting & FAQ

### Board Not Detected / No Serial Port Appears
- **Data Cable Check**: Ensure your USB-C cable supports data transfer and is not a charging-only cable.
- **Port Naming**:
  - **macOS**: Look for `/dev/cu.usbmodem*` (native USB-C).
  - **Linux**: Look for `/dev/ttyACM0` or `/dev/ttyUSB0`.
  - **Windows**: Look for `COM3`, `COM4`, etc. in Device Manager.
- **Linux Permissions**: Add your user to the `dialout` group:
  ```bash
  sudo usermod -aG dialout $USER
  ```
  Then log out and log back in.

### Board Fails to Enter Bootloader / Download Mode
The CoreS3's native USB Serial/JTAG handles reset automatically. If a handshake error occurs:
1. Unplug the USB cable.
2. Hold down the reset/power button on the bottom of the CoreS3.
3. Plug the USB cable back in while continuing to hold the button for 2 seconds, then release.
4. Run `./flash.sh` again.

### Black Screen on First Boot
- Make sure all 5 partitions were flashed, including `0x830000` (`sagwa_clips.bin`). If the animation partition is missing or corrupted, the firmware falls back to legacy pose mode.
- Check serial logs at 115200 baud:
  ```bash
  python3 -m serial.tools.miniterm <PORT> 115200
  ```
  A healthy boot will log: `Muse Gadget starting` and initialize the LTR-553 & BMI270 sensors.
