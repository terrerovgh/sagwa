#!/usr/bin/env bash
#
# Quick 1-step installer script for Sagwa on M5Stack CoreS3
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=================================================="
echo " 🐾 Sagwa Installer for M5Stack CoreS3"
echo "=================================================="

# Ensure python3 exists
if ! command -v python3 &>/dev/null; then
    echo "❌ Error: python3 is required but not found."
    exit 1
fi

# Run the python flasher
exec python3 flash.py "$@"
