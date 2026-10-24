#!/usr/bin/env python3
"""
Sagwa CoreS3 Flasher Tool
=========================
Easily flashes the complete Sagwa firmware and animation pack onto an
M5Stack CoreS3 (ESP32-S3) device.

Usage:
    python3 flash.py [options]

Options:
    -p, --port PORT       Serial port of M5Stack CoreS3 (auto-detected if omitted)
    -b, --baud BAUD       Baud rate for flashing (default: 1500000)
    --app-only            Flash only the main firmware binary (0x20000)
    --clips-only          Flash only the Sagwa animation clips pack (0x830000)
    --erase-all           Erase entire flash before writing
    --bin-dir DIR         Directory containing the binary files (default: bin/)
    -h, --help            Show this help message and exit
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

# Default flash layout for M5Stack CoreS3 (16MB SPI Flash)
FLASH_PARTS = [
    (0x000000, "bootloader.bin", "ESP32-S3 2nd Stage Bootloader"),
    (0x010000, "partition-table.bin", "Custom 16MB Partition Table"),
    (0x01D000, "ota_data_initial.bin", "OTA Initial Selection Data"),
    (0x020000, "sagwa-cores3.bin", "Sagwa CoreS3 Application Firmware"),
    (0x830000, "sagwa_clips.bin", "Sagwa 22-Clip Animation Pack (7.8MB)"),
]

def find_esptool():
    """Locate the esptool executable or module."""
    # Check if current Python environment has esptool module
    try:
        res = subprocess.run(
            [sys.executable, "-m", "esptool", "version"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        if res.returncode == 0:
            return [sys.executable, "-m", "esptool"]
    except Exception:
        pass

    # Check for esptool binary in PATH
    path_tool = shutil.which("esptool") or shutil.which("esptool.py")
    if path_tool:
        return [path_tool]

    # Check common esp-idf / virtualenv paths
    home = os.path.expanduser("~")
    candidate_paths = [
        os.path.join(home, "esp", "muse-tools", "bin", "esptool"),
        os.path.join(home, "esp", "muse-tools", "bin", "esptool.py"),
        os.path.join(home, ".espressif", "python_env", "*", "bin", "esptool"),
    ]
    for pattern in candidate_paths:
        matches = glob.glob(pattern)
        if matches and os.access(matches[0], os.X_OK):
            return [matches[0]]

    return None

def detect_serial_ports():
    """Detect available USB serial ports for M5Stack CoreS3."""
    ports = []

    # Use pyserial list_ports if available
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            # CoreS3 native USB has VID:PID 303A:1001 (Espressif USB Serial/JTAG)
            # or sometimes CH9102 / CP210x on certain base boards
            desc = p.description or ""
            is_esp = (p.vid == 0x303A and p.pid == 0x1001) or ("USB JTAG" in desc) or ("ESP32" in desc)
            ports.append((p.device, desc, is_esp))
        if ports:
            # Sort ESP devices first
            ports.sort(key=lambda x: not x[2])
            return ports
    except ImportError:
        pass

    # Fallback scanning /dev on Unix
    if sys.platform == "darwin":
        candidates = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.wchusbserial*")
        for dev in candidates:
            ports.append((dev, "USB Serial Device", True))
    elif sys.platform.startswith("linux"):
        candidates = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
        for dev in candidates:
            ports.append((dev, "USB Serial Device", True))
    elif sys.platform == "win32":
        for i in range(1, 32):
            ports.append((f"COM{i}", "Serial Port", False))

    return ports

def main():
    parser = argparse.ArgumentParser(
        description="Sagwa CoreS3 Flasher Tool - Flash Sagwa on M5Stack CoreS3"
    )
    parser.add_argument("-p", "--port", help="Serial port of M5Stack CoreS3")
    parser.add_argument("-b", "--baud", default="1500000", help="Flashing baud rate (default: 1500000)")
    parser.add_argument("--app-only", action="store_true", help="Flash only the firmware binary (0x20000)")
    parser.add_argument("--clips-only", action="store_true", help="Flash only the animation pack (0x830000)")
    parser.add_argument("--erase-all", action="store_true", help="Erase entire flash before writing")
    parser.add_argument(
        "--bin-dir",
        default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "bin"),
        help="Path to directory containing binaries (default: bin/)"
    )

    args = parser.parse_args()

    print("=" * 64)
    print("  Sagwa M5Stack CoreS3 Flasher")
    print("=" * 64)

    # 1. Locate esptool
    esptool_cmd = find_esptool()
    if not esptool_cmd:
        print("\n[!] Error: 'esptool' not found.")
        print("\nPlease install esptool via pip:")
        print("    pip install esptool")
        print("\nOr activate your ESP-IDF environment:")
        print("    . $IDF_PATH/export.sh")
        sys.exit(1)

    # 2. Select port
    port = args.port
    if not port:
        ports = detect_serial_ports()
        if not ports:
            print("\n[!] Error: No serial ports found. Please connect your M5Stack CoreS3 via USB-C.")
            sys.exit(1)
        elif len(ports) == 1:
            port = ports[0][0]
            print(f"[*] Auto-detected port: {port} ({ports[0][1]})")
        else:
            # Check if one is an Espressif device
            esp_ports = [p for p in ports if p[2]]
            if len(esp_ports) == 1:
                port = esp_ports[0][0]
                print(f"[*] Auto-detected CoreS3: {port} ({esp_ports[0][1]})")
            else:
                print("\nMultiple ports detected. Please choose one:")
                for idx, (dev, desc, _) in enumerate(ports):
                    print(f"  [{idx + 1}] {dev} - {desc}")
                try:
                    choice = int(input("Select port [1]: ") or "1")
                    port = ports[choice - 1][0]
                except (ValueError, IndexError):
                    print("Invalid selection.")
                    sys.exit(1)

    print(f"[*] Target Port: {port}")
    print(f"[*] Baud Rate:   {args.baud}")
    print(f"[*] Binary Dir:  {args.bin_dir}")

    # 3. Check binary files
    bin_dir = os.path.abspath(args.bin_dir)
    if not os.path.isdir(bin_dir):
        print(f"\n[!] Error: Binary directory not found at: {bin_dir}")
        sys.exit(1)

    flash_list = []
    if args.clips_only:
        flash_list = [(0x830000, "sagwa_clips.bin", "Sagwa Animation Pack")]
    elif args.app_only:
        flash_list = [(0x020000, "sagwa-cores3.bin", "Sagwa CoreS3 Application")]
    else:
        flash_list = FLASH_PARTS

    missing = []
    for offset, filename, desc in flash_list:
        file_path = os.path.join(bin_dir, filename)
        if not os.path.isfile(file_path):
            missing.append(filename)
        else:
            size_kb = os.path.getsize(file_path) / 1024
            print(f"  - [0x{offset:06X}] {filename:22} ({size_kb:7.1f} KB) - {desc}")

    if missing:
        print(f"\n[!] Error: Missing required binary files in {bin_dir}:")
        for m in missing:
            print(f"    - {m}")
        sys.exit(1)

    # 4. Optional erase
    if args.erase_all:
        print("\n[*] Erasing entire flash...")
        erase_cmd = esptool_cmd + ["--chip", "esp32s3", "-p", port, "-b", args.baud, "erase_flash"]
        res = subprocess.run(erase_cmd)
        if res.returncode != 0:
            print("[!] Error: Erase failed.")
            sys.exit(1)

    # 5. Build write_flash command
    cmd = esptool_cmd + [
        "--chip", "esp32s3",
        "-p", port,
        "-b", args.baud,
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "keep",
    ]

    for offset, filename, _ in flash_list:
        cmd.append(f"0x{offset:x}")
        cmd.append(os.path.join(bin_dir, filename))

    print("\n[*] Flashing Sagwa onto M5Stack CoreS3...")
    res = subprocess.run(cmd)

    if res.returncode == 0:
        print("\n" + "=" * 64)
        print(" SUCCESS: Sagwa is installed!")
        print(" The CoreS3 will reboot automatically.")
        print(" Watch Sagwa wake up, stretch, and greet you on the screen!")
        print("=" * 64)
    else:
        print("\n[!] Error: Flashing failed with return code:", res.returncode)
        print("Troubleshooting tips:")
        print("  1. Verify the USB-C cable supports data (not charge-only).")
        print("  2. If the board fails to enter download mode, hold the reset button")
        print("     (on the side/bottom) while connecting USB, then release.")
        print("  3. Ensure no other serial monitor (screen, minicom, platformio) is open.")
        sys.exit(res.returncode)

if __name__ == "__main__":
    main()
