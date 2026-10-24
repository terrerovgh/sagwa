# M5Stack CoreS3 Hardware Specification & Integration

This document outlines the hardware architecture, peripheral mappings, and register-level integration for the **Sagwa** feline companion firmware on the **M5Stack CoreS3**.

---

## 🛠️ Hardware Overview

The M5Stack CoreS3 is a modular ESP32-S3 development kit equipped with rich multimedia, sensing, and power management capabilities.

| Subsystem | Component | Specifications |
|---|---|---|
| **SoC** | Espressif ESP32-S3FN8 | Xtensa 32-bit dual-core LX7 @ 240 MHz, 512 KB SRAM |
| **Storage** | SPI Flash | 16 MB High-speed SPI Flash (DIO, 80 MHz) |
| **RAM** | Octal PSRAM | 8 MB Octal PSRAM |
| **Display** | ILI9342C IPS LCD | 2.0-inch, 320×240 resolution, 16-bit RGB565, SPI DMA |
| **Touch Controller** | FocalTech FT6336U | Capacitive single/multi-touch, I2C0 (`0x38`) |
| **Audio Amp** | Awinic AW88298 | 16-bit high-efficiency Class-D I2S audio amplifier |
| **Microphones** | Dual Knowles SPM1423 | PDM stereo microphone array |
| **Proximity & ALS** | Lite-On LTR-553ALS-WA | Ambient light & digital proximity sensor, I2C0 (`0x23`) |
| **6-Axis IMU** | Bosch Sensortec BMI270 | 3-axis accelerometer + 3-axis gyroscope, I2C0 (`0x69`) |
| **Magnetometer** | Bosch Sensortec BMM150 | 3-axis geomagnetic sensor, I2C0 (`0x10`) |
| **PMIC** | X-Powers AXP2101 | Programmable power management unit, I2C0 (`0x34`) |
| **RTC** | NXP BM8563 | Real-time clock with battery backup, I2C0 (`0x51`) |
| **USB Interface** | Native USB Serial/JTAG | USB-C with automatic DTR/RTS bootloader switching (VID: `0x303A`, PID: `0x1001`) |

---

## 🔌 Pinout & Bus Allocation

### SPI Interface (Display & Camera)
- **LCD CS**: GPIO 3
- **LCD DC (Data/Command)**: GPIO 35
- **LCD SCLK**: GPIO 36
- **LCD MOSI**: GPIO 37
- **LCD Backlight PWM**: Driven via AXP2101 PMIC (`DLDO1` voltage regulator)
- **LCD Resolution**: $320 \times 240$ pixels, landscape orientation.
- **Draw Buffer**: Allocated in internal SRAM / DMA buffer (`CONFIG_BSP_LCD_DRAW_BUF_HEIGHT=30`).

### I2C0 Bus (Internal System Bus)
- **SDA**: GPIO 12
- **SCL**: GPIO 11
- **Frequency**: 400 kHz (Fast-mode I2C)

Devices sharing the I2C0 bus:
| Device Address | Part Number | Purpose in Sagwa |
|---|---|---|
| `0x23` | LTR-553ALS-WA | Proximity petting detection & auto-brightness |
| `0x69` | BMI270 | Motion-based wake, rotation detection, temperature |
| `0x10` | BMM150 | Magnetometer (auxiliary) |
| `0x34` | AXP2101 | PMIC voltage rails, battery fuel gauge, LCD backlight |
| `0x38` | FT6336U | Capacitive touch screen input |
| `0x51` | BM8563 | Real-Time Clock |

### I2S & PDM Audio
- **I2S Speaker Out (AW88298)**:
  - **BCLK**: GPIO 34
  - **LRCK / WS**: GPIO 33
  - **DOUT**: GPIO 13
  - **Sample Rate**: 16 kHz, 16-bit mono (software mixed into stereo I2S channel)
- **PDM Microphones (SPM1423)**:
  - **CLK**: GPIO 0
  - **DATA**: GPIO 47

---

## 🐾 Sensor Behavior in Sagwa

### 1. LTR-553ALS-WA (Proximity Petting & Light)
- **Proximity Petting**:
  - The LTR-553 proximity sensor is located near the top bezel.
  - When a user brings their hand within approximately 3–5 cm (`proximity >= 180`), Sagwa interprets this as gentle petting.
  - State reaction: If asleep, Sagwa wakes up; calls `muse_state_make_happy()`, and triggers `sagwa_sfx_pet_pulse()` to emit a continuous purring sound.
- **Dynamic Backlight Scaling**:
  - Measures ambient illuminance in lux.
  - An exponential smoothing filter adjusts the LCD backlight via AXP2101 ($20\% - 100\%$) to match ambient ambient room lighting.

### 2. BMI270 (IMU Motion Wake & Rotation)
- **Motion Wake**:
  - 100 Hz accelerometer sampling with 4-sample averaging ($\pm 8g$ range).
  - Acceleration delta threshold:
    $$\Delta a = |a_x - a_{x,\text{prev}}| + |a_y - a_{y,\text{prev}}| + |a_z - a_{z,\text{prev}}| > 0.32g$$
  - Debounced at 1.5 seconds. Shaking or tapping the CoreS3 gently awakens a sleeping Sagwa.
- **Sustained Rotation**:
  - 200 Hz gyroscope monitoring ($\pm 2000^\circ/\text{s}$).
  - Sustained rotational velocity ($> 40^\circ/\text{s}$ for $\ge 0.45\text{ s}$) triggers the animated stretch sequence, transitioning Sagwa into a standing alert hub state. This filtering prevents accidental triggers from desk bumps or speaker resonance.
- **Core Temperature**:
  - Reads onboard die temperature (offset-calibrated) available over the `sensors.read` RPC query.

---

## ⚡ Power Management & Battery

- **AXP2101 PMIC Configuration**:
  - Handled by the Board Support Package (`board_m5stack_cores3.c`).
  - Powers the CoreS3 core rails (VCC 3.3V, camera/sensor LDOs, audio amp supply).
  - Power button events are trapped to handle sleep toggles and push-to-talk.
