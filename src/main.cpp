#include <M5Cardputer.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "secrets.h"


WebServer otaServer(80);
bool otaSuccess = false;
bool rebootPending = false;
unsigned long rebootTime = 0;

void startOTA() {
    WiFi.mode(WIFI_AP);

    if (!WiFi.softAP("BERTIE-UPDATE", OTA_PASSWORD)) {
        M5Cardputer.Display.println("OTA Wi-Fi failed");
        return;
    }

    otaServer.on("/", HTTP_GET, []() {
        otaServer.send(200, "text/html",
            "<h2>BERTIE Firmware Update</h2>"
            "<form method='POST' action='/update' "
            "enctype='multipart/form-data'>"
            "<input type='file' name='firmware' "
            "accept='.bin' required>"
            "<input type='submit' value='Upload Firmware'>"
            "</form>");
    });

    otaServer.on("/update", HTTP_POST,
        []() {
            otaServer.send(
                otaSuccess ? 200 : 500,
                "text/plain",
                otaSuccess
                    ? "Update successful. Restarting..."
                    : "Firmware update failed."
            );

            if (otaSuccess) {
                rebootPending = true;
                rebootTime = millis();
            }
        },
        []() {
            HTTPUpload& upload = otaServer.upload();

            if (upload.status == UPLOAD_FILE_START) {
                otaSuccess = Update.begin(
                    UPDATE_SIZE_UNKNOWN
                );
            }
            else if (upload.status == UPLOAD_FILE_WRITE) {
                if (otaSuccess) {
                    otaSuccess =
                        Update.write(
                            upload.buf,
                            upload.currentSize
                        ) == upload.currentSize;
                }
            }
            else if (upload.status == UPLOAD_FILE_END) {
                otaSuccess =
                    otaSuccess && Update.end(true);
            }
            else if (upload.status == UPLOAD_FILE_ABORTED) {
                Update.abort();
                otaSuccess = false;
            }
        }
    );

    otaServer.begin();
    M5Cardputer.Display.println("OTA: 192.168.4.1");
}


void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  // Set Up the Display
  M5Cardputer.Display.setRotation(0);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextColor(TFT_GREEN);
  M5Cardputer.Display.setTextSize(3);
  M5Cardputer.Display.setCursor(8, 8);

  M5Cardputer.Display.println("BERTIE");

  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(TFT_YELLOW);
  M5Cardputer.Display.println("Bindery Executive for Robotics, Tools, Information & Equipment");
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.println();
  M5Cardputer.Display.println("Terminal v0.1");
  M5Cardputer.Display.println("Keyboard test:");

  startOTA();
}

void loop() {
    M5Cardputer.update();

    otaServer.handleClient();

    if (rebootPending && millis() - rebootTime > 1500) {
        ESP.restart();
    }
    //Read keyboard input
    if (M5Cardputer.Keyboard.isChange() &&
        M5Cardputer.Keyboard.isPressed()) {

            auto keys = M5Cardputer.Keyboard.keysState();
        
            for (char c : keys.word) {
                M5Cardputer.Display.print(c);
            }

            if (keys.enter) {
                M5Cardputer.Display.println();
            }
        }   

        delay(10);
}
