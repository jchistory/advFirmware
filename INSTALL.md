# BERTIE certificate-bundle OTA repair

This package replaces `src/main.cpp` and `platformio.ini` only. All existing keyboard shortcuts, local settings, and the emergency web updater remain.

## Installation

1. **Back up or commit your existing project first.** Extract this archive at the root of `advFirmware` in Codespaces; allow both included files to replace the old ones.
2. Keep your `include/secrets.h` local and ignored by Git. Do not put Wi-Fi passwords in the public repository.
3. Run `bash build.sh` and require `[SUCCESS]` and `BERTIE firmware ready!`.
4. Download `bertie-v0.3-ota.bin` from the Codespaces Explorer (application-only image). Do **not** upload the merged image over OTA.
5. Connect to `BERTIE-UPDATE`, open `http://192.168.4.1`, upload the OTA image, and restart. Saved home Wi-Fi settings are stored in Preferences/NVS and should remain in place.
6. Press Fn+U. The device should now validate GitHub's HTTPS certificate using a maintained Mozilla-root certificate bundle. Do not disable HTTPS verification.
7. If a release is offered, press Enter to install, or Backspace to cancel. Preserve your browser OTA fallback until a complete download and reboot have been verified.

## Notes / limitations

- The project continues to use Arduino-ESP32 2.0.16 / PlatformIO Espressif32 6.7.0. That release supports `WiFiClientSecure::setCACertBundle(const uint8_t*)`.
- This change depends on an external ESP32CertBundle library. We have checked its documented API, but **this ZIP has not been compiled or run on your hardware**. The Codespaces build is mandatory.
- A certificate bundle makes trust-store maintenance easier, but doesn't itself guarantee that HTTPS redirect handling or firmware download works. GitHub Release asset URLs often redirect between HTTPS hosts; verify a complete actual update before relying on Fn+U.
- GitHub Actions needs to compile and publish a `firmware.bin` Release asset; the existing workflow already does so. This archive deliberately does not overwrite the workflow or build script.
- The AP remains active for emergency OTA, as in your current firmware, and consumes some power. Future work can make emergency AP activation conditional.
- This initial revision still has no firmware signature validation independent of TLS. For stronger update assurance, add signed-release verification.
