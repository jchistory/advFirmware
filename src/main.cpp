#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp32_cert_bundle.h>

#if __has_include("secrets.h")
  #include "secrets.h"
  #define HAS_LOCAL_OTA_SECRET 1
#endif
#if __has_include("build_version.h")
  #include "build_version.h"
#else
  #define BERTIE_BUILD_NUMBER 0
#endif

static constexpr const char* UPDATE_AP_SSID = "BERTIE-UPDATE";
static constexpr const char* GITHUB_RELEASE_API =
    "https://api.github.com/repos/jchistory/advFirmware/releases/latest";
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000;
static constexpr uint32_t AP_IDLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;
WebServer otaServer(80);
Preferences preferences;
String otaPassword, homeSSID, homePassword, offeredURL;
uint32_t offeredBuild = 0;
bool waitingForConfirm = false, uploadOK = false, rebootPending = false;
bool emergencyAPActive = false, uploadInProgress = false;
uint32_t rebootAt = 0, apLastActivity = 0;

// Screen state. Status is refreshed infrequently to conserve battery.
bool homeScreenActive = false;
String homeInput;
uint32_t lastStatusDrawMs = 0;
static constexpr int STATUS_BAR_HEIGHT = 18;

void screenMessage(const String& line) {
    M5Cardputer.Display.println(line);
    Serial.println(line);
}

// Header colours match BERTIE's navy/gold workshop palette.
void drawStatusBar() {
    auto& d = M5Cardputer.Display;
    const int w = d.width();
    const uint16_t navy = d.color565(37, 67, 107);
    const uint16_t gold = d.color565(201, 169, 107);
    const uint16_t dim = d.color565(155, 170, 188);
    const uint16_t good = d.color565(157, 224, 172);
    const int savedX = d.getCursorX();
    const int savedY = d.getCursorY();

    d.fillRect(0, 0, w, STATUS_BAR_HEIGHT, navy);
    d.drawFastHLine(0, STATUS_BAR_HEIGHT - 1, w, gold);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextWrap(false);

    // Battery estimate from M5Cardputer. Power.getBatteryLevel() returns 0-100.
    const int battery = M5Cardputer.Power.getBatteryLevel();
    const bool valid = battery >= 0 && battery <= 100;
    const uint16_t batteryColor = !valid ? dim : (battery <= 20 ? gold : good);
    d.drawRect(5, 4, 18, 9, TFT_WHITE);
    d.fillRect(23, 7, 2, 3, TFT_WHITE);
    if (valid && battery > 0)
        d.fillRect(7, 6, (battery * 14) / 100, 5, batteryColor);
    d.setTextColor(TFT_WHITE);
    d.setCursor(29, 5);
    d.print(valid ? String(battery) + "%" : String("--%"));

    // The emergency access point is distinct from a connected Wi-Fi station.
    const bool stationConnected = WiFi.status() == WL_CONNECTED;
    const bool active = stationConnected || emergencyAPActive;
    const uint16_t wifiColor = stationConnected ? good : (emergencyAPActive ? gold : dim);
    const int x = w / 2 - 18;
    d.drawLine(x - 7, 6, x, 2, wifiColor);
    d.drawLine(x, 2, x + 7, 6, wifiColor);
    d.drawLine(x - 4, 10, x, 7, wifiColor);
    d.drawLine(x, 7, x + 4, 10, wifiColor);
    d.fillCircle(x, 13, 1, wifiColor);
    if (!active) d.drawLine(x - 8, 15, x + 8, 1, dim);
    d.setTextColor(wifiColor);
    d.setCursor(x + 12, 5);
    d.print(stationConnected ? "ON" : (emergencyAPActive ? "AP" : "OFF"));

    // GitHub Actions supplies the build number. Local Codespaces builds are b0.
    const String version = String("v0.3 b") + String(BERTIE_BUILD_NUMBER);
    d.setTextColor(TFT_WHITE);
    d.setCursor(w - 5 - static_cast<int>(version.length()) * 6, 5);
    d.print(version);

    d.setCursor(savedX, savedY);
    d.setTextWrap(!homeScreenActive);
    lastStatusDrawMs = millis();
}

void statusScreen(const String& heading) {
    auto& d = M5Cardputer.Display;
    homeScreenActive = false;
    d.fillScreen(TFT_BLACK);
    drawStatusBar();
    d.setCursor(3, STATUS_BAR_HEIGHT + 6);
    d.setTextColor(TFT_GREEN);
    d.setTextSize(2);
    d.println(heading);
    d.setTextColor(TFT_WHITE);
    d.setTextSize(1);
    d.setTextWrap(true);
}

void drawHomeInput() {
    auto& d = M5Cardputer.Display;
    const int w = d.width();
    const uint16_t border = d.color565(69, 89, 112);
    d.fillRect(6, 87, w - 12, 26, TFT_BLACK);
    d.drawRect(6, 87, w - 12, 26, border);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.setTextWrap(false);
    d.setCursor(11, 96);
    const size_t maxChars = static_cast<size_t>((w - 30) / 6);
    const String visible = homeInput.length() > maxChars
        ? homeInput.substring(homeInput.length() - maxChars)
        : homeInput;
    d.print("> " + visible + "_");
}

void drawHomeScreen() {
    auto& d = M5Cardputer.Display;
    homeScreenActive = true;
    d.fillScreen(TFT_BLACK);
    drawStatusBar();
    d.setTextWrap(false);
    d.setTextFont(1);
    d.setTextSize(2);
    d.setTextColor(d.color565(242, 232, 213));
    d.setCursor(8, 26);
    d.print("BERTIE");

    d.setTextSize(1);
    d.setTextColor(d.color565(201, 169, 107));
    d.setCursor(8, 48);
    d.print("Bindery workshop terminal");
    d.drawFastHLine(8, 64, d.width() - 16, d.color565(70, 86, 108));

    d.setTextColor(TFT_WHITE);
    d.setCursor(8, 73);
    d.print("Ready for input");
    drawHomeInput();

    d.setTextColor(d.color565(155, 170, 188));
    d.setCursor(8, 123);
    d.print("Fn+U Update       Fn+P AP");
}

String generateAPPassword() {
    const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789";
    String result;
    for (int i = 0; i < 14; ++i)
        result += alphabet[esp_random() % (sizeof(alphabet) - 1)];
    return result;
}

void loadSettings() {
    preferences.begin("bertie", false);
    otaPassword = preferences.getString("apPass", "");
    if (otaPassword.length() < 8) {
#ifdef HAS_LOCAL_OTA_SECRET
        otaPassword = String(OTA_PASSWORD);
#endif
        if (otaPassword.length() < 8) otaPassword = generateAPPassword();
        preferences.putString("apPass", otaPassword);
    }
    homeSSID = preferences.getString("wifiSSID", "");
    homePassword = preferences.getString("wifiPass", "");
}

String escapeHTML(const String& input) {
    String s = input;
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\"", "&quot;");
    s.replace("'", "&#39;");
    return s;
}

void configureWebUpdater() {
    otaServer.on("/", HTTP_GET, []() {
        apLastActivity = millis();
        String page =
            "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>BERTIE Settings</title><h1>BERTIE</h1>"
            "<h2>Wi-Fi setup</h2><form action='/wifi' method='POST'>"
            "Network SSID:<br><input name='ssid' required maxlength='32' value='";
        page += escapeHTML(homeSSID);
        page += "'><br>Password:<br><input type='password' name='password' maxlength='63'>"
                "<br><button type='submit'>Save Wi-Fi</button></form><p>Saved Wi-Fi: ";
        page += homeSSID.length() ? escapeHTML(homeSSID) : "Not configured";
        page += "</p><h2>Emergency firmware upload</h2>"
                "<form action='/update' method='POST' enctype='multipart/form-data'>"
                "<input type='file' name='firmware' accept='.bin' required>"
                "<button type='submit'>Upload application firmware</button></form>"
                "<p>Use application-only .bin, NOT merged USB image.</p>"
                "<p>Normal updates: press Fn + U on the Cardputer.</p>";
        otaServer.send(200, "text/html", page);
    });
    otaServer.on("/wifi", HTTP_POST, []() {
        apLastActivity = millis();
        String ssid = otaServer.arg("ssid");
        String pass = otaServer.arg("password");
        if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 63 ||
            (pass.length() > 0 && pass.length() < 8)) {
            otaServer.send(400, "text/plain", "Invalid SSID/password length");
            return;
        }
        homeSSID = ssid;
        if (pass.length()) homePassword = pass;
        preferences.putString("wifiSSID", homeSSID);
        preferences.putString("wifiPass", homePassword);
        otaServer.send(200, "text/html", "<h2>Wi-Fi saved</h2><p>Press Fn + U.</p>");
    });
    otaServer.on("/update", HTTP_POST,
        []() {
            apLastActivity = millis();
            otaServer.send(uploadOK ? 200 : 500, "text/plain",
                           uploadOK ? "Firmware installed; restarting." : "Update failed.");
            if (uploadOK) { rebootPending = true; rebootAt = millis(); }
        },
        []() {
            apLastActivity = millis();
            HTTPUpload& upload = otaServer.upload();
            if (upload.status == UPLOAD_FILE_START) {
                uploadInProgress = true;
                uploadOK = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (uploadOK) {
                    uploadOK = Update.write(upload.buf, upload.currentSize) == upload.currentSize;
                    if (!uploadOK) Update.abort();
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                uploadOK = uploadOK && Update.end(true);
                uploadInProgress = false;
            } else if (upload.status == UPLOAD_FILE_ABORTED) {
                Update.abort(); uploadOK = false;
                uploadInProgress = false;
            }
        });
}

void showAPInformation() {
    statusScreen("BERTIE Wi-Fi");
    screenMessage("AP: BERTIE-UPDATE");
    screenMessage("Password:");
    screenMessage(otaPassword);
    screenMessage("192.168.4.1");
    screenMessage("Fn + P: AP off");
}

void stopEmergencyAP(bool showStatus = true) {
    if (!emergencyAPActive || uploadInProgress) return;
    otaServer.stop();
    WiFi.softAPdisconnect(false);
    emergencyAPActive = false;
    // Keep the station radio only if GitHub OTA is actively connected.
    WiFi.mode(WiFi.status() == WL_CONNECTED ? WIFI_STA : WIFI_OFF);
    if (showStatus) {
        statusScreen("BERTIE Wi-Fi");
        screenMessage("Emergency AP off");
        screenMessage("Fn + P: AP on");
    }
}

void toggleEmergencyAP() {
    if (emergencyAPActive) {
        if (uploadInProgress) {
            screenMessage("Upload in progress");
            return;
        }
        stopEmergencyAP();
        return;
    }
    WiFi.mode(WiFi.status() == WL_CONNECTED ? WIFI_AP_STA : WIFI_AP);
    if (!WiFi.softAP(UPDATE_AP_SSID, otaPassword.c_str())) {
        statusScreen("BERTIE Wi-Fi");
        screenMessage("OTA access point failed");
        WiFi.mode(WiFi.status() == WL_CONNECTED ? WIFI_STA : WIFI_OFF);
        return;
    }
    otaServer.begin();
    emergencyAPActive = true;
    apLastActivity = millis();
    showAPInformation();
}

bool connectToHomeWiFi() {
    if (homeSSID.isEmpty()) {
        statusScreen("Wi-Fi not set");
        screenMessage("Fn + P: enable AP");
        screenMessage("Then open 192.168.4.1");
        return false;
    }
    WiFi.mode(emergencyAPActive ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(homeSSID.c_str(), homePassword.c_str());
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        if (emergencyAPActive) otaServer.handleClient();
        delay(100);
    }
    if (WiFi.status() != WL_CONNECTED) {
        screenMessage("Wi-Fi connection failed");
        return false;
    }
    screenMessage("Wi-Fi connected");
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    const uint32_t ntpStart = millis();
    while (time(nullptr) < 1700000000 && millis() - ntpStart < 12000) {
        if (emergencyAPActive) otaServer.handleClient();
        delay(150);
    }
    if (time(nullptr) < 1700000000) {
        screenMessage("Clock sync failed");
        return false;
    }
    return true;
}

void disconnectHomeWiFi() {
    WiFi.disconnect(false, false);
    WiFi.mode(emergencyAPActive ? WIFI_AP : WIFI_OFF);
}

// Only invoked by Fn + U. Never polls GitHub in the background.
void checkForFirmwareUpdate() {
    waitingForConfirm = false;
    offeredURL = "";
    offeredBuild = 0;
    statusScreen("Check GitHub");
    screenMessage("Connecting...");
    if (!connectToHomeWiFi()) { disconnectHomeWiFi(); return; }


    WiFiClientSecure secure;

    secure.setCACertBundle(x509_crt_bundle, x509_crt_bundle_len);

    secure.setHandshakeTimeout(20);

    HTTPClient http;

    http.setTimeout(20000);
    if (!http.begin(secure, GITHUB_RELEASE_API)) {
        screenMessage("Cannot reach GitHub");
        disconnectHomeWiFi(); return;
    }
    http.addHeader("User-Agent", "BERTIE-Cardputer-ADV");
    http.addHeader("Accept", "application/vnd.github+json");
    

    const int code = http.GET();

    if (code != HTTP_CODE_OK) {
        screenMessage("GitHub HTTP: " + String(code));
        if (code < 0) screenMessage(http.errorToString(code));
        http.end(); disconnectHomeWiFi(); return;
    }
    const String body = http.getString();
    http.end();
    DynamicJsonDocument doc(24576);
    if (deserializeJson(doc, body)) {
        screenMessage("Invalid GitHub response");
        disconnectHomeWiFi(); return;
    }
    String tag = doc["tag_name"] | "";
    if (!tag.startsWith("build-")) {
        screenMessage("Unsupported release tag");
        disconnectHomeWiFi(); return;
    }
    uint32_t latestBuild = tag.substring(6).toInt();
    if (latestBuild == 0 || latestBuild <= BERTIE_BUILD_NUMBER) {
        screenMessage("Already up to date");
        disconnectHomeWiFi(); return;
    }
    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        String name = asset["name"] | "";
        if (name == "firmware.bin") {
            offeredURL = asset["browser_download_url"].as<String>();
            break;
        }
    }
    String expectedPrefix = "https://github.com/jchistory/advFirmware/releases/download/";
    if (offeredURL.isEmpty() || !offeredURL.startsWith(expectedPrefix) ||
        !offeredURL.endsWith("/firmware.bin")) {
        screenMessage("Release has no firmware");
        offeredURL = "";
        disconnectHomeWiFi(); return;
    }
    offeredBuild = latestBuild;
    statusScreen("Update available");
    screenMessage("Installed: " + String(BERTIE_BUILD_NUMBER));
    screenMessage("Latest: " + String(offeredBuild));
    screenMessage("Enter: install");
    screenMessage("Backspace: cancel");
    waitingForConfirm = true;
    // Enter reconnects; turn the radio off while awaiting confirmation.
    disconnectHomeWiFi();
}

void installFirmwareUpdate() {
    if (!waitingForConfirm || offeredURL.isEmpty()) return;
    waitingForConfirm = false;
    statusScreen("Installing update");
    screenMessage("Do not power off");
    if (WiFi.status() != WL_CONNECTED && !connectToHomeWiFi()) {
        disconnectHomeWiFi(); return;
    }
    WiFiClientSecure secure;
    secure.setCACertBundle(x509_crt_bundle, x509_crt_bundle_len);
    secure.setHandshakeTimeout(20);
    httpUpdate.rebootOnUpdate(false);
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const t_httpUpdate_return result = httpUpdate.update(secure, offeredURL);
    if (result == HTTP_UPDATE_OK) {
        screenMessage("Update installed!");
        delay(800);
        ESP.restart();
    } else {
        screenMessage("Update failed");
        screenMessage(httpUpdate.getLastErrorString());
        disconnectHomeWiFi();
    }
}

void setup() {
    Serial.begin(115200);
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    loadSettings();

    auto& d = M5Cardputer.Display;
    // Fixed screen orientation; no saved rotation preference or shortcut.
    d.setRotation(0);
    Serial.printf("Display: %dx%d, rotation 0\n", d.width(), d.height());
    configureWebUpdater();
    WiFi.mode(WIFI_OFF);
    drawHomeScreen();
    Serial.printf("BERTIE build %lu; Wi-Fi off; Fn+P emergency AP: %s\n",
                  (unsigned long)BERTIE_BUILD_NUMBER, UPDATE_AP_SSID);
}

void loop() {
    M5Cardputer.update();
    if (emergencyAPActive) {
        otaServer.handleClient();
        if (!uploadInProgress && !rebootPending &&
            millis() - apLastActivity >= AP_IDLE_TIMEOUT_MS) {
            stopEmergencyAP();
        }
    }
    if (rebootPending && millis() - rebootAt > 1500) ESP.restart();
    if (millis() - lastStatusDrawMs >= 5000) drawStatusBar();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto keys = M5Cardputer.Keyboard.keysState();
        if (keys.fn && M5Cardputer.Keyboard.isKeyPressed('u')) {
            checkForFirmwareUpdate();
        } else if (keys.fn && M5Cardputer.Keyboard.isKeyPressed('p')) {
            toggleEmergencyAP();

        } else if (waitingForConfirm && keys.enter && !keys.fn) {
            installFirmwareUpdate();
        } else if (waitingForConfirm && (keys.backspace || keys.esc)) {
            waitingForConfirm = false;
            offeredURL = "";
            disconnectHomeWiFi();
            statusScreen("Update cancelled");
        } else if (!waitingForConfirm && !keys.fn) {
            // Return to the home screen on ordinary typing after a status page.
            if (!homeScreenActive) drawHomeScreen();
            if (keys.backspace && !homeInput.isEmpty()) {
                homeInput.remove(homeInput.length() - 1);
            }
            if (keys.enter) homeInput = "";
            for (char c : keys.word) homeInput += c;
            if (homeInput.length() > 160) homeInput.remove(0, homeInput.length() - 160);
            drawHomeInput();
        }
    }
    delay(10);
}
