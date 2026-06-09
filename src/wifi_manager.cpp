#include "wifi_manager.h"
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <vector>

// TUGAS 4: PENYESUAIAN HEADER untuk WPA2-Enterprise
// Coba esp_wpa2.h dulu (Core 2.x), fallback ke esp_eap_client.h (Core 3.x)
#if __has_include("esp_eap_client.h")
    #include "esp_eap_client.h"
#elif __has_include("esp_wpa2.h")
    #include "esp_wpa2.h"
#endif

extern WiFiManager wifiManager;

// HTML page for WiFi configuration
const char HTML_PAGE[] PROGMEM = R"rawl(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; padding: 20px; }
        select, input { width: 100%; padding: 10px; margin: 5px 0; }
        button { width: 100%; padding: 15px; background: #4CAF50; color: white; border: none; }
        h2 { text-align: center; }
        .info { background: #e7f3fe; padding: 10px; margin: 10px 0; border-left: 4px solid #2196F3; font-size: 12px; }
    </style>
</head>
<body>
    <h2>Setup WiFi</h2>
    <form action="/connect" method="POST">
        <label>Pilih WiFi:</label>
        <select name="ssid">
            {SSIDS}
        </select>
        <div class="info">Untuk WiFi kampus (ITB Hotspot), isi Username dan Password.</div>
        <label>Username (Opsional - WPA2 Enterprise):</label>
        <input type="text" name="username" placeholder="Kosong untuk WPA2-Personal">
        <label>Password:</label>
        <input type="text" name="password" placeholder="Kosong jika tidak ada">
        <button type="submit">Connect</button>
    </form>
</body>
</html>
)rawl";

WiFiManager::WiFiManager() {
    apStarted = false;
    serverStarted = false;
    webServer = nullptr;
    dnsServer = nullptr;
    connecting = false;
    connectStartTime = 0;
    foundNetworks = 0;
    credentialsReceived = false;
    targetUsername = "";  // Untuk WPA2-Enterprise
}

WiFiManager::~WiFiManager() {
    stopConfigMode();
}

// ========== START CONFIG MODE ==========
bool WiFiManager::startConfigMode() {
    Serial.println("[WiFi] Starting AP mode...");

    // Start WiFi in AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID);

    delay(100);

    // Set IP address
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    Serial.println("[WiFi] AP started: " + String(WIFI_AP_SSID));
    Serial.println("[WiFi] IP: " + String(WIFI_AP_IP));

    // Scan for networks
    Serial.println("[WiFi] Scanning networks...");
    foundNetworks = WiFi.scanNetworks();
    Serial.println("[WiFi] Found " + String(foundNetworks) + " networks");

    for (int i = 0; i < foundNetworks && i < 20; i++) {
        scannedSSIDs[i] = WiFi.SSID(i);
        scannedRSSI[i] = WiFi.RSSI(i);
        Serial.println("  - " + scannedSSIDs[i] + " (" + String(scannedRSSI[i]) + "dBm)");
    }

    // Start DNS server for captive portal
    dnsServer = new DNSServer();
    dnsServer->start(53, "*", WiFi.softAPIP());

    // Start web server
    startWebServer();

    apStarted = true;
    return true;
}

// ========== GET SIGNAL ICON (Unicode Block - 100% supported) ==========
String WiFiManager::getSignalIcon(int rssi) {
    // Menggunakan Full Block (&#9608;) dan Light Shade (&#9617;)
    if (rssi > -60) {
        return "&#9608;&#9608;&#9608; "; // Sinyal Kuat (███)
    } else if (rssi > -80) {
        return "&#9608;&#9608;&#9617; "; // Sinyal Sedang (██░)
    } else {
        return "&#9608;&#9617;&#9617; "; // Sinyal Lemah (█░░)
    }
}

// ========== START WEB SERVER ==========
void WiFiManager::startWebServer() {
    webServer = new WebServer(80);

    // Root handler - show HTML form
    webServer->on("/", [this]() {
        String html = HTML_PAGE;

        // Build SSID options dengan ikon sinyal
        String options = "";
        for (int i = 0; i < foundNetworks && i < 20; i++) {
            String signalIcon = getSignalIcon(scannedRSSI[i]);
            options += "<option value=\"" + scannedSSIDs[i] + "\">" +
                       signalIcon + " " + scannedSSIDs[i] + "</option>";
        }

        html.replace("{SSIDS}", options);

        webServer->send(200, "text/html; charset=utf-8", html);
    });

    // Connect handler - SIMPAN CREDENTIALS DAN SET FLAG, JANGAN PANGGIL WiFi.begin() DISINI!
    webServer->on("/connect", [this]() {
        if (webServer->hasArg("ssid")) {
            // Simpan ke variabel class agar tidak hilang dari scope
            targetSSID = webServer->arg("ssid");
            targetPassword = webServer->arg("password");
            targetUsername = webServer->arg("username");

            Serial.print("[WiFi] Received SSID: ");
            Serial.println(targetSSID);
            Serial.print("[WiFi] Received Password: ");
            Serial.println(targetPassword.length() > 0 ? "[set]" : "[empty]");
            Serial.print("[WiFi] Received Username: ");
            Serial.println(targetUsername.length() > 0 ? targetUsername : "[empty - WPA2-Personal]");

            // ========== PERSIST KE wifi.csv SEKARANG (tepat saat diterima) ==========
            // Jalur save lama (state_machine STATE_WIFI_CONNECTING & main.cpp loop)
            // bisa ter-skip karena main.cpp memanggil sm.init() begitu WL_CONNECTED
            // tercapai, sehingga STATE_WIFI_CONNECTING tidak sempat menyimpan.
            // Simpan di sini -> kredensial selalu terjaga walau koneksi nantinya gagal,
            // dan langsung muncul di daftar "Setup WiFi" pada kunjungan berikutnya
            // (STATE_WIFI_BROWSE membaca wifi.csv setiap kali masuk).
            if (saveCredentials(targetSSID, targetPassword, targetUsername)) {
                Serial.println(F("[WiFi] Kredensial baru disimpan ke wifi.csv"));
            } else {
                Serial.println(F("[WiFi] GAGAL simpan kredensial baru ke wifi.csv"));
            }

            // Kirim response DULU ke browser
            webServer->send(200, "text/html; charset=utf-8",
                "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'></head>"
                "<body style='font-family:Arial;text-align:center;padding:50px;'>"
                "<h2>Menghubungkan ke WiFi...</h2>"
                "<p>Lihat layar alat untuk status koneksi.</p>"
                "<p>Jika tidak berhasil, coba lagi dari alat.</p>"
                "</body></html>");

            // Delay agar ESP32 sempat kirim response ke HP
            delay(500);

            // JANGAN panggil WiFi.begin() atau stop AP di sini!
            // Cukup set flag agar state machine yang tangani
            credentialsReceived = true;
            connecting = true;
            connectStartTime = millis();

            Serial.println("[WiFi] Credentials received, waiting for state machine to connect...");
        } else {
            webServer->send(400, "text/html", "<h2>Error! SSID required</h2>");
        }
    });

    // Captive Portal: Redirect semua request lain ke halaman utama
    webServer->onNotFound([this]() {
        webServer->sendHeader("Location", "http://192.168.4.1/", true);
        webServer->send(302, "text/plain", "");
    });

    webServer->begin();
    serverStarted = true;
    Serial.println("[WiFi] Web server started");
}

// ========== STOP WEB SERVER ==========
void WiFiManager::stopWebServer() {
    if (webServer) {
        webServer->stop();
        delete webServer;
        webServer = nullptr;
    }
    serverStarted = false;

    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
}

// ========== STOP CONFIG MODE ==========
void WiFiManager::stopConfigMode() {
    stopWebServer();
    WiFi.softAPdisconnect(true);
    apStarted = false;
    Serial.println("[WiFi] AP stopped");
}

// ========== PROCESS (non-blocking) ==========
void WiFiManager::process() {
    if (serverStarted && webServer) {
        webServer->handleClient();
    }
    if (dnsServer) {
        dnsServer->processNextRequest();
    }
}

// ========== START CONNECTION (DUAL-MODE: WPA2-Personal & WPA2-Enterprise) ==========
void WiFiManager::startConnection(const String& ssid, const String& password, const String& username) {
    targetSSID = ssid;
    targetPassword = password;
    targetUsername = username;
    connecting = true;
    connectStartTime = millis();

    Serial.println("[WiFi] Starting connection to: " + ssid);

    // Stop AP first
    stopConfigMode();

    // Start station mode
    WiFi.mode(WIFI_STA);

    // TUGAS 3: DUAL-MODE - Cek apakah username diisi (WPA2-Enterprise) atau tidak (WPA2-Personal)
    if (username.length() > 0) {
        // WPA2-Enterprise mode
        Serial.println("[WiFi] Mode: WPA2-Enterprise");
        Serial.println("[WiFi] Username: " + username);

        // Enable WPA2 Enterprise mode
        esp_wifi_sta_wpa2_ent_enable();

        // Set identity and credentials
        esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)username.c_str(), username.length());
        esp_wifi_sta_wpa2_ent_set_username((uint8_t*)username.c_str(), username.length());
        esp_wifi_sta_wpa2_ent_set_password((uint8_t*)password.c_str(), password.length());

        // Connect (no password needed for enterprise - credentials set separately)
        WiFi.begin(ssid.c_str());
    } else {
        // WPA2-Personal mode (standard)
        Serial.println("[WiFi] Mode: WPA2-Personal");
        WiFi.begin(ssid.c_str(), password.c_str());
    }
}

// ========== IS CONNECTING ==========
bool WiFiManager::isConnecting() {
    if (!connecting) return false;

    // Check timeout
    if (millis() - connectStartTime > WIFI_CONNECT_TIMEOUT) {
        Serial.println("[WiFi] Connection timeout");
        connecting = false;
        WiFi.disconnect();
        return false;
    }

    // Check connection status
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf_P(PSTR("[WiFi] Connected! IP: %s\n"), WiFi.localIP().toString().c_str());
        connecting = false;

        // ========== AUTO-SAVE CREDENTIALS (Jika belum ada) ==========
        // Simpan ke wifi.csv agar smart auto-connect bekerja di boot selanjutnya
        if (targetSSID.length() > 0) {
            Serial.println(F("[WiFi] Menyimpan kredensial ke SD Card..."));
            if (saveCredentials(targetSSID, targetPassword, targetUsername)) {
                Serial.println(F("[WiFi] Kredensial berhasil disimpan ke wifi.csv"));
            } else {
                Serial.println(F("[WiFi] Gagal menyimpan kredensial"));
            }
        }

        return false; // No longer connecting, now connected
    }

    return true;
}

// ========== IS CONNECTED ==========
bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

// ========== GET CONNECTION STATUS ==========
wl_status_t WiFiManager::getConnectionStatus() {
    return WiFi.status();
}

// ========== CANCEL CONNECTION ==========
void WiFiManager::cancelConnection() {
    connecting = false;
    WiFi.disconnect();
    Serial.println("[WiFi] Connection cancelled");
}

// ========== SAVE CREDENTIALS (Multi-Credential - support WPA2-Enterprise) ==========
bool WiFiManager::saveCredentials(const String& ssid, const String& password, const String& username) {
    // Load all existing credentials
    std::vector<WiFiCredential> creds = loadAllCredentials();

    // Check if SSID already exists
    bool found = false;
    for (auto& cred : creds) {
        if (cred.ssid == ssid) {
            // Update existing credential
            cred.password = password;
            cred.username = username;
            found = true;
            Serial.println("[WiFi] Updated existing SSID: " + ssid);
            break;
        }
    }

    // If not found, append new credential
    if (!found) {
        WiFiCredential newCred;
        newCred.ssid = ssid;
        newCred.password = password;
        newCred.username = username;
        creds.push_back(newCred);
        Serial.println("[WiFi] Added new SSID: " + ssid);
    }

    // Overwrite entire file with updated vector
    // Format CSV: ssid,password,username
    File f = SD.open("/wifi.csv", FILE_WRITE);
    if (!f) {
        Serial.println("[WiFi] Failed to save credentials");
        return false;
    }

    for (const auto& cred : creds) {
        f.print(cred.ssid);
        f.print(",");
        f.print(cred.password);
        f.print(",");
        f.println(cred.username);
    }
    f.close();

    Serial.println("[WiFi] Total credentials saved: " + String(creds.size()));
    return true;
}

// ========== LOAD ALL CREDENTIALS (support WPA2-Enterprise) ==========
std::vector<WiFiCredential> WiFiManager::loadAllCredentials() {
    std::vector<WiFiCredential> creds;

    File f = SD.open("/wifi.csv", FILE_READ);
    if (!f) {
        Serial.println("[WiFi] No saved credentials file");
        return creds;
    }

    // Parse CSV: each line is ssid,password,username
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();

        if (line.length() > 0) {
            // Format: ssid,password,username
            int firstComma = line.indexOf(',');
            int secondComma = line.indexOf(',', firstComma + 1);

            if (firstComma > 0) {
                WiFiCredential cred;
                cred.ssid = line.substring(0, firstComma);
                cred.ssid.trim();

                if (secondComma > 0) {
                    // New format: ssid,password,username
                    cred.password = line.substring(firstComma + 1, secondComma);
                    cred.username = line.substring(secondComma + 1);
                } else {
                    // Legacy format: ssid,password (no username)
                    cred.password = line.substring(firstComma + 1);
                    cred.username = "";
                }
                cred.password.trim();
                cred.username.trim();

                if (cred.ssid.length() > 0) {
                    creds.push_back(cred);
                    Serial.println("[WiFi] Loaded: " + cred.ssid + " (user: " + (cred.username.length() > 0 ? cred.username : "none") + ")");
                }
            }
        }
    }
    f.close();

    Serial.println("[WiFi] Total credentials loaded: " + String(creds.size()));
    return creds;
}

// ========== LOAD CREDENTIALS (Legacy) ==========
WiFiCredentials WiFiManager::loadCredentials() {
    WiFiCredentials cred;
    cred.saved = false;

    File f = SD.open("/wifi.csv", FILE_READ);
    if (!f) {
        Serial.println("[WiFi] No saved credentials");
        return cred;
    }

    // Parse CSV: ssid,password
    String line = f.readStringUntil('\n');
    f.close();

    int commaIndex = line.indexOf(',');
    if (commaIndex > 0) {
        cred.ssid = line.substring(0, commaIndex);
        cred.password = line.substring(commaIndex + 1);
        cred.ssid.trim();
        cred.password.trim();
    }

    if (cred.ssid.length() > 0) {
        cred.saved = true;
        Serial.println("[WiFi] Loaded credentials for: " + cred.ssid);
    }

    return cred;
}

// ========== SMART AUTO-CONNECT (DUAL-MODE: WPA2-Personal & WPA2-Enterprise) ==========
bool WiFiManager::smartAutoConnect() {
    Serial.println(F("[WiFi] Starting smart auto-connect..."));

    // Load all saved credentials
    std::vector<WiFiCredential> savedWiFi = loadAllCredentials();

    if (savedWiFi.empty()) {
        Serial.println(F("[WiFi] No saved credentials, skipping auto-connect"));
        return false;
    }

    Serial.println(F("[WiFi] Scanning for available networks..."));

    // Scan for available networks (blocks ~1-2 seconds)
    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println(F("[WiFi] No networks found"));
        return false;
    }

    Serial.printf_P(PSTR("[WiFi] Found %d networks\n"), n);

    // Find match between saved credentials and available networks
    for (int i = 0; i < n; i++) {
        String currentSSID = WiFi.SSID(i);

        for (const auto& saved : savedWiFi) {
            if (currentSSID == saved.ssid) {
                // Found match!
                Serial.printf_P(PSTR("[WiFi] MATCH FOUND: %s\n"), saved.ssid.c_str());

                // Store for later use
                targetSSID = saved.ssid;
                targetPassword = saved.password;
                targetUsername = saved.username;
                connecting = true;
                connectStartTime = millis();

                // Clear scan results and start connection
                WiFi.scanDelete();

                // Start WiFi connection in station mode
                WiFi.mode(WIFI_STA);

                // TUGAS 3: DUAL-MODE - Cek apakah username diisi (WPA2-Enterprise)
                if (saved.username.length() > 0) {
                    // WPA2-Enterprise mode
                    Serial.println(F("[WiFi] Auto-connect Mode: WPA2-Enterprise"));
                    Serial.printf_P(PSTR("[WiFi] Username: %s\n"), saved.username.c_str());

                    esp_wifi_sta_wpa2_ent_enable();
                    esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)saved.username.c_str(), saved.username.length());
                    esp_wifi_sta_wpa2_ent_set_username((uint8_t*)saved.username.c_str(), saved.username.length());
                    esp_wifi_sta_wpa2_ent_set_password((uint8_t*)saved.password.c_str(), saved.password.length());

                    WiFi.begin(saved.ssid.c_str());
                } else {
                    // WPA2-Personal mode
                    Serial.println(F("[WiFi] Auto-connect Mode: WPA2-Personal"));
                    WiFi.begin(saved.ssid.c_str(), saved.password.c_str());
                }

                Serial.printf_P(PSTR("[WiFi] Auto-connecting to: %s\n"), saved.ssid.c_str());
                return true;
            }
        }
    }

    // No match found
    Serial.println(F("[WiFi] No matching network found"));
    WiFi.scanDelete();
    return false;
}

// ========== GET CURRENT SSID ==========
String WiFiManager::getCurrentSSID() {
    if (isConnected()) {
        return WiFi.SSID();
    }
    return targetSSID;
}

// ========== GET TARGET PASSWORD ==========
String WiFiManager::getTargetPassword() {
    return targetPassword;
}

// ========== GET TARGET USERNAME (for WPA2-Enterprise) ==========
String WiFiManager::getTargetUsername() {
    return targetUsername;
}

// ========== HAS CREDENTIALS RECEIVED ==========
bool WiFiManager::hasCredentialsReceived() {
    return credentialsReceived;
}

// ========== CLEAR CREDENTIALS RECEIVED ==========
void WiFiManager::clearCredentialsReceived() {
    credentialsReceived = false;
}

// ========== CONNECT TO SAVED NETWORK ==========
bool WiFiManager::connectToSavedNetwork(const String& ssid) {
    Serial.println("[WiFi] connectToSavedNetwork: " + ssid);

    // Load all credentials
    std::vector<WiFiCredential> creds = loadAllCredentials();

    // Find matching SSID
    for (auto& cred : creds) {
        if (cred.ssid == ssid) {
            Serial.println("[WiFi] Found credentials for: " + ssid);

            // Mulai koneksi (non-blocking: WiFi.begin() dipanggil di dalam startConnection)
            // For WPA2-Personal: username kosong. For WPA2-Enterprise: username terisi.
            startConnection(cred.ssid, cred.password, cred.username);

            // ========== BLOCKING WAIT + TIMEOUT 15 detik ==========
            // WiFi.begin() non-blocking -> kita HARUS verifikasi WiFi.status() benar-benar
            // WL_CONNECTED sebelum mengembalikan true ke pemanggil (yang menampilkan "BERHASIL!").
            const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
            unsigned long startAttempt = millis();

            while (WiFi.status() != WL_CONNECTED &&
                   millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
                delay(500);
                Serial.print(F("."));
            }
            Serial.println();

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println(F("[WIFI] Koneksi Berhasil"));
                Serial.print(F("[WIFI] IP: "));
                Serial.println(WiFi.localIP());
                connecting = false;
                return true;
            } else {
                Serial.println(F("[WIFI] Koneksi Timeout/Gagal"));
                WiFi.disconnect();
                connecting = false;
                return false;
            }
        }
    }

    Serial.println("[WiFi] Credentials not found for: " + ssid);
    return false;
}