# BERTIE v0.3: one-time upgrade to GitHub-pull OTA

This overlay upgrades the existing Cardputer ADV firmware, preserving the
Cardputer's startup acronym, rotation 0 and larger fonts.

## Preparation and first installation

1. Extract the overlay into the ROOT of `advFirmware` in Codespaces.
   It replaces `src/main.cpp`, `platformio.ini`, `build.sh`, `.gitignore`, and adds
   `.github/workflows/publish-firmware.yml` and `include/github_ca.h`.
   Keep your existing `include/secrets.h` locally and DO NOT commit it.
2. Run `bash build.sh` in Codespaces. This compiles the upgraded firmware.
3. Download `bertie-v0.3-ota.bin` from the Codespaces repository root.
4. Connect to your *existing* BERTIE-UPDATE network and open http://192.168.4.1.
   Upload this **application-only bertie-v0.3-ota.bin**, not the merged image.
5. Once the Cardputer reboots, rejoin its BERTIE-UPDATE network. The saved
   original AP password should be retained by the new firmware if you built
   with your local `secrets.h`. If not, press Fn + P to see the newly
   generated AP password on the Cardputer display.
6. Open http://192.168.4.1 again. Enter your normal 2.4 GHz Wi-Fi SSID and
   password, then Save Wi-Fi. Credentials are stored only on the Cardputer
   (NVS), not in the public GitHub repository.
7. Commit and push **source changes** to GitHub. This triggers a GitHub Actions
   build and publishes firmware.bin under Releases. Check the Actions tab;
   the first build must succeed before Fn + U can find a release.
8. Press **Fn + U** on the Cardputer to check for a newer release. Press Enter
   to install or Backspace to cancel. No background update checks run.

## Going forward

Edit code, commit and push to `main`, wait until the GitHub Actions release
finishes, then press Fn + U and Enter on the Cardputer. Do not upload a
merged bootloader image through the web OTA page.

Fn + P displays the emergency AP password and address (192.168.4.1).
The older web uploader remains available as a fallback.

## Important caveats

- This source has not been compiled or tested on your Cardputer in this
  environment. Verify `pio run` in Codespaces before flashing.
- GitHub Actions requires repository Actions enabled and permission for
  GitHub Actions to write repository contents (for Releases).
- Requires internet access via a 2.4 GHz Wi-Fi network and the HTTPS root
  certificates in include/github_ca.h; certificate chains can change.
- On first installation, save Wi-Fi credentials with the web page once.
- The AP stays enabled as an emergency fallback. It uses Wi-Fi power even
  when no GitHub check is running.
- The TLS trust bundle and GitHub-host URL check protect transport and
  destination; for stronger software supply-chain protection, a future
  version can add Ed25519 signatures and downgrade protection.
- Existing firmware can be recovered using the USB flashing process.
