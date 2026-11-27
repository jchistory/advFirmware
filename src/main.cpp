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
WebServer otaServer(80);
Preferences preferences;
String otaPassword, homeSSID, homePassword, offeredURL;
uint32_t offeredBuild = 0;
bool waitingForConfirm = false, uploadOK = false, rebootPending = false;
uint32_t rebootAt = 0;

void screenMessage(const String& line) {
    M5Cardputer.Display.println(line);
    Serial.println(line);
}

void statusScreen(const String& heading) {
    auto& d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    d.setCursor(3, 3);
    d.setTextColor(TFT_GREEN);
    d.setTextSize(2);
    d.println(heading);
    d.setTextColor(TFT_WHITE);
    d.setTextSize(1);
    d.setTextWrap(true);
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

void startWebUpdater() {
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(UPDATE_AP_SSID, otaPassword.c_str())) {
        screenMessage("OTA access point failed");
        return;
    }
    otaServer.on("/", HTTP_GET, []() {
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
            otaServer.send(uploadOK ? 200 : 500, "text/plain",
                           uploadOK ? "Firmware installed; restarting." : "Update failed.");
            if (uploadOK) { rebootPending = true; rebootAt = millis(); }
        },
        []() {
            HTTPUpload& upload = otaServer.upload();
            if (upload.status == UPLOAD_FILE_START) {
                uploadOK = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (uploadOK) {
                    uploadOK = Update.write(upload.buf, upload.currentSize) == upload.currentSize;
                    if (!uploadOK) Update.abort();
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                uploadOK = uploadOK && Update.end(true);
            } else if (upload.status == UPLOAD_FILE_ABORTED) {
                Update.abort(); uploadOK = false;
            }
        });
    otaServer.begin();
}

void showAPInformation() {
    statusScreen("BERTIE Wi-Fi");
    screenMessage("AP: BERTIE-UPDATE");
    screenMessage("Password:");
    screenMessage(otaPassword);
    screenMessage("192.168.4.1");
    screenMessage("Fn + U: update");
}

bool connectToHomeWiFi() {
    if (homeSSID.isEmpty()) {
        statusScreen("Wi-Fi not set");
        screenMessage("Connect to BERTIE-UPDATE");
        screenMessage("192.168.4.1");
        screenMessage("Fn + P: AP password");
        return false;
    }
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(homeSSID.c_str(), homePassword.c_str());
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        otaServer.handleClient();
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
        otaServer.handleClient();
        delay(150);
    }
    if (time(nullptr) < 1700000000) {
        screenMessage("Clock sync failed");
        return false;
    }
    return true;
}

void disconnectHomeWiFi() {
    WiFi.disconnect(false, false); // Keep emergency access point active.
}

// Only invoked by Fn + U. Never polls GitHub in the background.
void checkForFirmwareUpdate() {
    waitingForConfirm = false;
    offeredURL = "";
    offeredBuild = 0;
    statusScreen("Check GitHub");
    screenMessage("Connecting...");
    if (!connectToHomeWiFi()) { disconnectHomeWiFi(); return; }

    Serial.println("DEBUG 1: Creating secure client");

    WiFiClientSecure secure;

    Serial.println("DEBUG 2: Setting CA bundle");
    secure.setCACertBundle(x509_crt_bundle, x509_crt_bundle_len);

    Serial.println("DEBUG 3: Setting handshake timeout");
    secure.setHandshakeTimeout(20);

    Serial.println("DEBUG 4: Creating HTTP client");
    HTTPClient http;

    http.setTimeout(20000);
    if (!http.begin(secure, GITHUB_RELEASE_API)) {
        screenMessage("Cannot reach GitHub");
        disconnectHomeWiFi(); return;
    }
    http.addHeader("User-Agent", "BERTIE-Cardputer-ADV");
    http.addHeader("Accept", "application/vnd.github+json");
    
    Serial.println("DEBUG 5: Starting GitHub GET");

    const int code = http.GET();

    Serial.printf("DEBUG 6: HTTP result = %d\n", code);
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
    d.setRotation(0);
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_GREEN);
    d.setTextSize(3);
    d.setCursor(8, 8);
    d.println("BERTIE");
    d.setTextSize(2);
    d.setTextColor(TFT_YELLOW);
    d.println("Bindery Executive for Robotics, Tools, Information & Equipment");
    d.setTextColor(TFT_WHITE);
    d.println();
    d.println("Terminal v0.3");
    d.println("Keyboard test:");

    startWebUpdater();
    Serial.printf("BERTIE build %lu; emergency AP %s\n",
                  (unsigned long)BERTIE_BUILD_NUMBER, UPDATE_AP_SSID);
}

void loop() {
    M5Cardputer.update();
    otaServer.handleClient();
    if (rebootPending && millis() - rebootAt > 1500) ESP.restart();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto keys = M5Cardputer.Keyboard.keysState();
        if (keys.fn && M5Cardputer.Keyboard.isKeyPressed('u')) {
            checkForFirmwareUpdate();
        } else if (keys.fn && M5Cardputer.Keyboard.isKeyPressed('p')) {
            showAPInformation();
        } else if (waitingForConfirm && keys.enter && !keys.fn) {
            installFirmwareUpdate();
        } else if (waitingForConfirm && (keys.backspace || keys.esc)) {
            waitingForConfirm = false;
            offeredURL = "";
            disconnectHomeWiFi();
            statusScreen("Update cancelled");
        } else if (!waitingForConfirm && !keys.fn) {
            for (char c : keys.word) M5Cardputer.Display.print(c);
            if (keys.enter) M5Cardputer.Display.println();
        }
    }
    delay(10);
}
