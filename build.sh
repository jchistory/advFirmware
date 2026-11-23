
#!/bin/bash
set -e

echo "Building BERTIE firmware..."

# Compile the firmware
.venv/bin/pio run

echo "Merging firmware..."

# Combine components into one binary
.venv/bin/python \
  "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" \
  --chip esp32s3 merge_bin \
  -o bertie-v0.1-merged.bin \
  --flash_mode dio \
  --flash_freq 40m \
  --flash_size 8MB \
  0x0 .pio/build/cardputer-adv/bootloader.bin \
  0x8000 .pio/build/cardputer-adv/partitions.bin \
  0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 .pio/build/cardputer-adv/firmware.bin

echo "BERTIE firmware ready!"
