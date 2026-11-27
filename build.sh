#!/bin/bash
set -euo pipefail

echo "Building BERTIE Cardputer ADV firmware..."
.venv/bin/pio run -e cardputer-adv

# Application-only image: upload this through the OTA webpage.
cp .pio/build/cardputer-adv/firmware.bin bertie-v0.3-ota.bin

echo "Creating USB recovery image..."
"$HOME/.platformio/penv/bin/python" -m esptool \
  --chip esp32s3 merge-bin \
  -o bertie-v0.3-merged.bin \
  --flash-mode dio \
  --flash-freq 40m \
  --flash-size 8MB \
  0x0 .pio/build/cardputer-adv/bootloader.bin \
  0x8000 .pio/build/cardputer-adv/partitions.bin \
  0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 .pio/build/cardputer-adv/firmware.bin

echo "Ready: bertie-v0.3-ota.bin (browser OTA)"
echo "Ready: bertie-v0.3-merged.bin (USB recovery, offset 0x0)"
