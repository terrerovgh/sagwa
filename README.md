# Sagwa — Interactive Desktop Feline Companion for M5Stack CoreS3

[![Target](https://img.shields.io/badge/Target-ESP32--S3-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/ESP--IDF-v6.0.1-red.svg)](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/)
[![Board](https://img.shields.io/badge/Hardware-M5Stack%20CoreS3-orange.svg)](https://docs.m5stack.com/en/core/CoreS3)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Art License](https://img.shields.io/badge/Art%20%26%20Audio-CC%20BY--SA%20%2F%20OFL-yellow.svg)](ATTRIBUTION.md)

**Sagwa** is an expressive, interactive desktop AI feline companion running natively on the **M5Stack CoreS3** (ESP32-S3).

Combining real-time video animation graph playback, reactive feline acoustic vocalizations, multi-sensor gesture interactions, and a touch-driven LVGL interface, Sagwa brings an intelligent, responsive pet to your desk.

---

## Features

- **Real-Time Video Animation Graph**:
  Plays a directed graph of 22 video clips (MiniMax H3) seamlessly transitioning across 6 hub poses (*Sleep, Loaf, Sit, Stand, Stalk, Stretch*). Decoded on Core 0 via the ESP32-S3 ROM's hardware `TJpgDec` at 12 fps with zero cross-dissolves.
- **Natural Feline Acoustic Engine (SFX)**:
  Dedicated 16 kHz 16-bit PCM sound engine (`sagwa_sfx.c`) playing real cat recordings: meows, trills, chirps, yawns, hisses, hunting chatter and continuous rhythmic purring. Each kind of sound has several takes with a slight random pitch, so she never sounds exactly the same twice.
- **A Different Meow for Every Kind of Message**:
  Sagwa reads each message before she announces it (`sagwa_notify.c`): a soft *mrrow* when you are chatting with her, a short bright chirp for an ordinary notification, and a loud insistent double meow for something important ("urgente", "recordatorio", "!!"...), repeated until you notice her.
- **Lifelike Moods and Decisions**:
  Energy, sleepiness (the clock and the room light), boredom and contentment drive what she does on her own: when she naps, sits, stretches, plays in bouts, grooms or calls out. She avoids repeating herself, wakes when the lights go on, jumps at a sudden hand, and keeps her balance on a tilted desk.
- **Updates Herself from GitHub**:
  Every push to `main` is built and released by GitHub Actions. Sagwa checks for new releases, tells you on screen before she updates (touch her to wait), installs the firmware and new animations, and tells you when she is done.
- **Interactive Proximity Petting**:
  Holding your hand near the CoreS3's built-in LTR-553 proximity sensor initiates a petting response: Sagwa wakes up, makes a happy expression, and emits a soothing purr.
- **Motion-Aware Wake**:
  Onboard Bosch Sensortec BMI270 6-axis IMU detects taps, movement, or sustained rotation ($>40^\circ/\text{s}$) to wake Sagwa from deep sleep with an animated stretch sequence.
- **Daily Activity Tracking (NVS Persistence)**:
  Maintains persistent daily records in flash memory of Sagwa's wakeups, purr durations, meow counts, and device uptime, accessible from the settings dashboard.
- **Interactive Touch UI (LVGL)**:
  Features finger-scrollable chat reply bubbles, instant image thumbnail rendering for AI-generated images, live sensor telemetry gauges, and Wi-Fi/audio settings.
- **Encrypted Assistant Protocol**:
  Built on the Muse Home Link architecture, supporting encrypted Noise sessions over BLE and Wi-Fi to integrate with voice assistants and LLM agents.

---

## Quick Start: Flash Sagwa in 1 Minute

No development environment or compilation is needed! Pre-built, verified firmware binaries are included in the repository inside `bin/`.

### 1. Connect CoreS3
Plug your M5Stack CoreS3 into your computer using a USB-C data cable.

### 2. Run the Flasher

#### On macOS / Linux:
```bash
git clone https://github.com/terrerovgh/sagwa.git
cd sagwa
./flash.sh
```

#### On Windows / Any OS:
```bash
git clone https://github.com/terrerovgh/sagwa.git
cd sagwa
python3 flash.py
```

The script automatically detects the serial port, flashes all 5 binary partitions (`bootloader`, `partition-table`, `ota_data`, `sagwa-cores3`, and `sagwa_clips.bin`), and reboots your CoreS3. Sagwa will immediately appear on screen!

> For manual flashing with `esptool` or updating individual partitions, see [INSTALL.md](INSTALL.md).

---

## Repository Structure

```
sagwa/
├── README.md                      # Primary project documentation
├── INSTALL.md                     # Step-by-step installation & flashing guide
├── ARCHITECTURE.md                # System, animation & audio architecture
├── HARDWARE.md                    # M5Stack CoreS3 peripheral & sensor specs
├── ATTRIBUTION.md                 # Credits, provenance & asset licenses
├── flash.py                       # Automated cross-platform flasher script
├── flash.sh                       # 1-step flash launcher for macOS / Linux
├── bin/                           # Ready-to-flash binary images
│   ├── bootloader.bin             # ESP32-S3 2nd stage bootloader (0x000000)
│   ├── partition-table.bin        # 16 MB custom partition table (0x010000)
│   ├── ota_data_initial.bin       # OTA slot selection data (0x01D000)
│   ├── sagwa-cores3.bin           # Sagwa application firmware (0x020000)
│   └── sagwa_clips.bin            # 22-clip animation pack (0x830000, 7.8 MB)
├── muse-gadget-sdk/               # ESP32 C/C++ firmware & CoreS3 drivers
│   └── esp32/
│       ├── components/muse/       # Sagwa avatar, SFX, sensors, and UI
│       │   ├── avatar/            # sagwa_clips.c, sagwa_legacy.c, muse_pixel.c
│       │   ├── cores3_sensors.c   # LTR-553 & BMI270 sensor drivers
│       │   ├── sagwa_sfx.c        # 16 kHz feline audio synthesizer
│       │   ├── sagwa_stats.c      # NVS daily activity tracker
│       │   └── muse_ui.c          # Touch UI & chat reply views
│       ├── partitions_muse_cores3.csv
│       └── tools/muse/board.sh    # Build and flash automation tool
└── sagwa/                         # Asset pipeline & media sources
    ├── clips/                     # 22 MiniMax H3 1080p video clips
    ├── hubs/                      # 6 keyframe hub poses (sleep, loaf, sit, etc.)
    ├── sounds/                    # Original and converted PCM cat audio
    └── tools/pack_clips.py        # Python clip processor & JPEG packager
```

---

## Building from Source

If you want to modify the firmware or sensor behaviors:

### Requirements
- **ESP-IDF v6.0.1** ([Setup Guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/))

### Build & Flash Commands
```bash
# 1. Activate ESP-IDF
. $IDF_PATH/export.sh

# 2. Navigate to firmware directory
cd muse-gadget-sdk/esp32

# 3. Build firmware for M5Stack CoreS3
tools/muse/board.sh build cores3

# 4. Flash to attached device
tools/muse/board.sh flash cores3
```

---

## Asset Pipeline: Packaging Clips

Sagwa uses 22 transition clips rendered between 6 shared poses. If you create new clips or edit hub frames:

```bash
cd sagwa/tools
python3 pack_clips.py
```
This tool:
1. Normalizes bounding boxes and ground lines ($y = 900$).
2. Downsamples video to 12 fps.
3. Smoothly blends boundary frames with exact hub poses.
4. Generates baseline JPEGs at $240 \times 208$ pixels into `sagwa_clips.bin` ready for the flash partition at `0x830000`.

---

## Automatic Updates

Sagwa keeps herself up to date from this repository's [releases](https://github.com/terrerovgh/sagwa/releases):

1. Push to `main`. The **Sagwa release** workflow (`.github/workflows/release.yml`) builds and signs the firmware as `v1.0.<run>`, and publishes it with the animation pack and a `manifest.json`.
2. Within a few hours (every 3 h by default), Sagwa sees the new release. Once nobody is talking with her, she chirps and shows *"Hay una version nueva de mi..."* with a 30-second countdown. Touch her to put it off for 10 minutes.
3. She downloads and verifies the update, restarts, and greets you with a happy trill: *"Ya me actualice! v1.0.4 -> v1.0.5"*.

The firmware already on your board must include the updater, so flash it once by hand (`./flash.sh`) after pulling this change. Options (time zone, check interval, turning it off) are in [INSTALL.md](INSTALL.md#automatic-updates).

---

## In-Depth Documentation

- [Hardware Reference (`HARDWARE.md`)](HARDWARE.md) — Pinouts, I2C bus devices, power rails, and sensor thresholds.
- [Firmware Architecture (`ARCHITECTURE.md`)](ARCHITECTURE.md) — State machine graph, ROM TJpgDec decoder, audio pipeline, and flash layout.
- [Installation Guide (`INSTALL.md`)](INSTALL.md) — Complete flashing, driver installation, and troubleshooting guide.
- [Attribution & Licenses (`ATTRIBUTION.md`)](ATTRIBUTION.md) — Provenance and licenses for all art, audio, typography, and machine learning models.

---

## License

- **Firmware & Code**: Licensed under the [Apache License 2.0](LICENSE).
- **Art, Sound & Typography**: Licensed under CC BY 4.0, CC BY-SA 4.0, CC BY-SA 3.0, and SIL Open Font License 1.1. Full details in [ATTRIBUTION.md](ATTRIBUTION.md).
