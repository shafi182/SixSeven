#include "api_manager.h"
#include "lcd.h"
#include "sdcard.h"
#include "user_manager.h"
#include "fingerprint.h"
#include <ArduinoJson.h>
#include <SD.h>

// ========== TUGAS 3: KONSOLIDASI STRING (PROGMEM) - api_manager ==========
const char MSG_API_LOADING[] PROGMEM = "Menghubungi API...";
const char MSG_API_SYNC[] PROGMEM = "Sinkronisasi...";
const char MSG_API_BERHASIL[] PROGMEM = "BERHASIL!";
const char MSG_API_GAGAL[] PROGMEM = "GAGAL!";
const char MSG_API_ERROR[] PROGMEM = "Error";
const char MSG_API_LOGIN[] PROGMEM = "Login Berhasil!";
const char MSG_API_WAIT[] PROGMEM = "Mohon Tunggu";
const char MSG_API_DATA[] PROGMEM = "Memuat Data...";

// ========== KONFIGURASI BATCH PUSH ==========
const int PUSH_BATCH_SIZE = 3;  // Maksimal data per batch

extern UserManager userManager;
extern FingerprintManager fingerprintManager;
APIManager apiManager;

// ========== CONTEXT-AWARE SYNC STATUS (3 FASE) ==========
// true bila fase masing-masing sukses (HTTP 200 / hasil parsing valid)
bool syncStatusUsers     = false;  // /api/device/users
bool syncStatusDashboard = false;  // /api/device/dashboard
bool syncStatusFeature   = false;  // /api/device/dpk, /api/device/fingerprint, dll

// Snapshot state machine saat ini (di-update di StateMachine::update()).
// Implementasi getCurrentSyncStatus() berada di state_machine.cpp agar
// enum StateMachineState bisa dipakai langsung tanpa hardcode nilai.
int g_currentState = 0;

// ========== TUGAS 2: GLOBAL JWT TOKEN FOR PUSH QUEUE ==========
// Disimpan setelah login berhasil, digunakan saat Push Queue transmisi
String globalJwtToken = "";

// ========== FLAG: STATUS SINKRONISASI USERS (untuk Retry di layar Login) ==========
// true bila fetchUsersAndSync() gagal (HTTP error/timeout), false bila sukses (HTTP 200)
bool isSyncUserFailed = false;

// ========== RAM CACHE FINGERPRINT MAPPING ==========
// Index = Slot ID (1-200), Value = user_id (NIP/NIM)
// Diisi saat boot dari fingerprint.csv, di-update saat enrollment baru
String fingerMap[201];

// ========== TUGAS 4: MAP UNTUK DATA MAHASISWA DARI API ==========
// Digunakan untuk logika "tambal data" saat sinkronisasi
#include <map>
std::map<String, String> apiMahasiswaData;

// ========== TUGAS 1: HELPER BINARY-SAFE - Hex ke Raw Bytes ==========
size_t hexToBytes(String hex, uint8_t *out) {
    if (hex.length() == 0 || out == NULL) return 0;

    size_t len = hex.length() / 2;
    for (size_t i = 0; i < len; i++) {
        out[i] = (uint8_t)strtol(hex.substring(i * 2, i * 2 + 2).c_str(), NULL, 16);
    }
    return len;
}

// ========== TUGAS 1: HELPER BINARY-SAFE - Raw Bytes ke Hex ==========
String bytesToHex(uint8_t *data, size_t len) {
    if (data == NULL || len == 0) return "";

    String hex = "";
    for (size_t i = 0; i < len; i++) {
        char buf[3];
        sprintf(buf, "%02X", data[i]);
        hex += buf;
    }
    return hex;
}

// ========== TUGAS 1: FUNGSI TRIM_HEX - Hapus padding dari akhir Hex String ==========
String trimHex(String hex) {
    // Hapus karakter 'F' atau '0' (padding) dari bagian paling belakang
    // Selama panjangnya lebih dari batas minimum (1024 = 512 bytes)
    // Kita hapus pasangan byte padding (2 karakter hex per byte)
    // Hanya hapus jika berakhir dengan "00" atau "FF" (padding yang valid)
    while (hex.length() > 1024 &&
           (hex.endsWith("00") || hex.endsWith("FF"))) {
        // Hapus 2 karakter terakhir (1 byte)
        hex.remove(hex.length() - 2);
    }
    return hex;
}

APIManager::APIManager() {
    isFetching = false;
    timeSynced = false;
}

bool APIManager::isProcessing() {
    return isFetching;
}

bool APIManager::isTimeSynced() {
    return timeSynced;
}

// ========== SYNC TIME VIA NTP (ESP32 Built-in) ==========
bool APIManager::syncTimeFromAPI() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[TIME] WiFi not connected, skipping time sync"));
        return false;
    }

    if (timeSynced) {
        Serial.println(F("[TIME] Time already synced, skipping"));
        return true;
    }

    Serial.println(F("[TIME] ============================"));
    Serial.println(F("[TIME] Syncing time via NTP..."));

    // Set timezone to WIB (UTC+7)
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

    // Wait max 10 seconds for time to synchronize
    int retry = 0;
    struct tm timeinfo;

    Serial.print(F("[TIME] Waiting for NTP sync"));

    while (!getLocalTime(&timeinfo) && retry < 10) {
        Serial.print(".");
        delay(1000);
        retry++;
    }

    Serial.println();

    if (retry < 10) {
        timeSynced = true;

        char buffer[50];
        strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &timeinfo);
        Serial.println(F("[TIME] Waktu berhasil disinkronisasi!"));
        Serial.printf_P(PSTR("[TIME] Waktu lokal: %s\n"), buffer);

        return true;
    } else {
        Serial.println(F("[TIME] Gagal sinkronisasi NTP setelah 10 percobaan."));
        return false;
    }
}

// ========== TUGAS 1, 2, 3: SYNC USERS FROM API ==========
// Dipanggil SATU KALI saat WiFi terhubung dan SNTP tersinkronisasi
void APIManager::syncUsersFromAPI() {
    Serial.println(F("[SYNC_USERS] ============================"));
    Serial.println(F("[SYNC_USERS] Starting automatic user sync..."));

    // Cek WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[SYNC_USERS] WiFi not connected, skipping sync"));
        logSystemActivity("API_SYNC", "GAGAL - WiFi tidak terhubung");
        return;
    }

    // Cek SNTP time sync
    if (!timeSynced) {
        Serial.println(F("[SYNC_USERS] SNTP not synced yet, skipping sync"));
        logSystemActivity("API_SYNC", "GAGAL - SNTP belum tersinkronisasi");
        return;
    }

    // TUGAS 1: HTTP GET dengan custom headers (sudah ada di fetchUsersAndSync)
    // TUGAS 2: Parsing JSON & Delta sync ke users.csv (sudah ada di fetchUsersAndSync)
    // TUGAS 3: Refresh RAM & Blackbox logging

    Serial.println(F("[SYNC_USERS] Calling fetchUsersAndSync()..."));
    bool syncResult = fetchUsersAndSync();

    if (syncResult) {
        // TUGAS 3: Refresh RAM - reload users.csv ke struct di RAM
        // Re-initialize userManager untuk memuat data terbaru
        Serial.println(F("[SYNC_USERS] Reloading user data to RAM..."));
        userManager.init();

        // TUGAS 3: Blackbox logging
        logSystemActivity("API_SYNC", "Sinkronisasi tabel users.csv berhasil");
        Serial.println(F("[SYNC_USERS] Sync completed successfully!"));
    } else {
        logSystemActivity("API_SYNC", "GAGAL - Sinkronisasi user gagal");
        Serial.println(F("[SYNC_USERS] Sync failed!"));
    }
}

// ========== DELTA SYNC HELPERS (file-local) ==========

// Ambil nilai variant JSON sebagai String dengan aman (int64/long/string).
// ArduinoJson v7 menyimpan integer besar (NIP 18 digit) sebagai int64_t, jadi
// as<String>() mengembalikan digit penuh tanpa overflow.
static String jvToStr(JsonVariantConst v) {
    if (v.isNull()) return String("");
    return v.as<String>();
}

// Pad/trim hex template ke PERSIS 3072 char (1536 byte) untuk R503.
// Backend memangkas trailing zeros -> hex menyusut; sensor menolak template
// yang panjangnya bukan 3072 char (Error 24). Kembalikan "" bila bukan template.
static String padFpHex(String h) {
    const int FP_HEX_LEN = 3072;
    h.trim();
    if (h.length() <= 100) return String("");   // bukan template valid / null
    if (h.length() < FP_HEX_LEN) {
        h.reserve(FP_HEX_LEN);                   // 1x alloc, hindari fragmentasi
        while (h.length() < FP_HEX_LEN) h += '0';
    } else if (h.length() > FP_HEX_LEN) {
        h = h.substring(0, FP_HEX_LEN);
    }
    return h;
}

// Cek apakah sebuah NIP termasuk dalam vector target (untuk filter rewrite FP).
static bool nipInList(const std::vector<String>& list, const String& nip) {
    for (const auto& n : list) if (n == nip) return true;
    return false;
}

// ========== TUGAS 1: FETCH USERS & DELTA SYNC (2 FASE) ==========
// FASE 1: GET /api/device/users  -> metadata ringan (nip,pin,role,updated_at).
//         Bandingkan updated_at vs users.csv lokal; kumpulkan NIP baru/berubah,
//         upsert metadata ke users.csv (skema: id,nip,pin,role,fingerprint_id,updated_at).
// FASE 2: Bila ada NIP yang perlu di-update, GET /api/device/user-fingerprints?nip=...
//         lalu upsert fp_1_user + fp_2_user (padding 3072) ke fingerprint_users.csv.
bool APIManager::fetchUsersAndSync() {
    // Anggap GAGAL dulu; baru di-set sukses saat seluruh alur tuntas.
    isSyncUserFailed = true;
    syncStatusUsers  = false;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[API] WiFi not connected, skipping API fetch"));
        return false;
    }

    Serial.println(F("[API] ============================"));
    Serial.println(F("[API] Delta Sync: FASE 1 (tarik metadata users)..."));
    isFetching = true;

    lcd.clear();
    lcd.printLine(0, F("Sinkronisasi Users"));
    lcd.printLine(1, MSG_API_LOADING);
    lcd.printLine(3, F("*:Batal"));

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    // ===================== FASE 1: METADATA + DELTA CHECK =====================
    HTTPClient http;
    String url = String(API_BASE_URL) + "/api/device/users";
    Serial.printf_P(PSTR("[API] URL: %s\n"), url.c_str());

    http.begin(secureClient, url);
    http.addHeader("x-device-code",   DEVICE_CODE);
    http.addHeader("x-device-secret", DEVICE_SECRET);

    int httpCode = http.GET();
    Serial.printf_P(PSTR("[API] FASE 1 HTTP Code: %d\n"), httpCode);

    if (httpCode != 200) {
        Serial.printf("[API] FASE 1 gagal: %s\n",
            httpCode > 0 ? String(httpCode).c_str() : http.errorToString(httpCode).c_str());
        logSystemActivity("API_SYNC_ERROR", "FASE1 HTTP: " + String(httpCode));
        lcd.printLine(2, httpCode > 0 ? ("HTTP Error: " + String(httpCode)) : String("Connection Error"));
        delay(2000);
        http.end();
        isFetching = false;
        return false;
    }

    String payload = http.getString();
    http.end();   // tutup TLS lebih awal: bebaskan heap radio sebelum parse + SD

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    payload = String();   // bebaskan buffer mentah; doc sudah menyalin yang perlu

    if (error) {
        Serial.printf_P(PSTR("[API] FASE 1 JSON Parse Error: %s\n"), error.c_str());
        lcd.printLine(2, F("JSON Parse Error"));
        delay(2000);
        isFetching = false;
        return false;
    }

    JsonArray users;
    if (doc["users"].is<JsonArray>()) {
        users = doc["users"].as<JsonArray>();
    } else if (doc["data"].is<JsonArray>()) {
        users = doc["data"].as<JsonArray>();
    } else {
        Serial.println(F("[API] FASE 1: key 'users'/'data' tidak ada"));
        lcd.printLine(2, F("Invalid Response"));
        delay(2000);
        isFetching = false;
        return false;
    }

    Serial.printf_P(PSTR("[API] FASE 1: %d users metadata dari server\n"), (int)users.size());

    // --- Muat users.csv lama ke RAM (metadata ringan, aman di heap) ---
    // Skema: id,nip,pin,role,fingerprint_id,updated_at
    struct LU { String id, nip, pin, role, fpId, updatedAt; };
    std::vector<LU> local;
    int maxId = 0;
    {
        File f = SD.open("/users.csv", FILE_READ);
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() < 2) continue;
                if (line.startsWith("id,")) continue;   // skip header

                String c[6]; int idx = 0, start = 0;
                for (int i = 0; i <= (int)line.length() && idx < 6; i++) {
                    if (i == (int)line.length() || line.charAt(i) == ',') {
                        c[idx++] = line.substring(start, i);
                        start = i + 1;
                    }
                }
                LU u;
                u.id = c[0]; u.nip = c[1]; u.pin = c[2];
                u.role = c[3]; u.fpId = c[4]; u.updatedAt = c[5];
                u.id.trim(); u.nip.trim(); u.updatedAt.trim();
                u.updatedAt.replace("\r", "");
                int v = u.id.toInt(); if (v > maxId) maxId = v;
                local.push_back(u);
            }
            f.close();
        }
    }

    auto findLocal = [&](const String& nip) -> int {
        for (size_t i = 0; i < local.size(); i++) if (local[i].nip == nip) return (int)i;
        return -1;
    };

    // --- Loop metadata server: kumpulkan NIP baru/berubah + upsert ke RAM ---
    String nipsToFetch = "";
    std::vector<String> targetNips;
    bool dirty = false;

    for (JsonObject user : users) {
        String nip = jvToStr(user["nip"]);  nip.trim();
        if (nip.length() == 0 || nip == "null" || nip == "0") continue;

        String pin = jvToStr(user["pin"]);  pin.trim();
        String role = jvToStr(user["role"]); role.trim();
        if (role.length() == 0) role = "dosen";
        String updServer = jvToStr(user["updated_at"]); updServer.trim();

        int li = findLocal(nip);
        bool isNew     = (li < 0);
        bool isChanged = (!isNew) && (local[li].updatedAt != updServer);

        if (!isNew && !isChanged) continue;   // up-to-date -> lewati

        // Tandai untuk pull FP (FASE 2)
        if (nipsToFetch.length()) nipsToFetch += ",";
        nipsToFetch += nip;
        targetNips.push_back(nip);

        // Upsert metadata ke RAM
        if (isNew) {
            LU nu;
            nu.id = String(++maxId);
            nu.nip = nip; nu.pin = pin; nu.role = role;
            nu.fpId = "0"; nu.updatedAt = updServer;
            local.push_back(nu);
            Serial.printf_P(PSTR("[API] NEW user NIP=%s role=%s\n"), nip.c_str(), role.c_str());
        } else {
            local[li].pin = pin;
            local[li].role = role;
            local[li].updatedAt = updServer;   // fpId dipertahankan
            Serial.printf_P(PSTR("[API] UPDATED user NIP=%s\n"), nip.c_str());
        }
        dirty = true;
    }

    doc.clear();   // bebaskan metadata JSON sebelum FASE 2

    // --- Tulis ulang users.csv SEKALI bila ada perubahan (atomic swap) ---
    if (dirty) {
        SD.remove("/temp_users.csv");
        File tw = SD.open("/temp_users.csv", FILE_WRITE);
        if (!tw) {
            Serial.println(F("[API] ERROR: gagal buat temp_users.csv"));
            lcd.printLine(2, F("Error: temp file"));
            delay(2000);
            isFetching = false;
            return false;
        }
        tw.println("id,nip,pin,role,fingerprint_id,updated_at");
        for (auto& u : local) {
            tw.printf("%s,%s,%s,%s,%s,%s\n",
                u.id.c_str(), u.nip.c_str(), u.pin.c_str(), u.role.c_str(),
                u.fpId.length() ? u.fpId.c_str() : "0", u.updatedAt.c_str());
        }
        tw.close();
        SD.remove("/users.csv");
        SD.rename("/temp_users.csv", "/users.csv");
        Serial.println(F("[API] users.csv di-upsert (metadata + updated_at)"));
    }

    // ===================== FASE 2: PULL FINGERPRINT ON-DEMAND =================
    if (nipsToFetch.length() == 0) {
        Serial.println(F("[API] Delta Sync: Semua data up-to-date, skip pull FP."));
        logSystemActivity("API_SYNC", "Delta Sync: up-to-date, skip pull FP");
        lcd.clear();
        lcd.printLine(0, MSG_API_BERHASIL);
        lcd.printLine(1, F("Up-to-date"));
        delay(1500);
        isFetching      = false;
        isSyncUserFailed = false;
        syncStatusUsers  = true;
        return true;
    }

    Serial.printf_P(PSTR("[API] Delta Sync: FASE 2 pull FP utk NIP: %s\n"), nipsToFetch.c_str());
    lcd.printLine(1, MSG_API_SYNC);

    HTTPClient http2;
    String url2 = String(API_BASE_URL) + "/api/device/user-fingerprints?nip=" + nipsToFetch;
    Serial.printf_P(PSTR("[API] FASE 2 URL: %s\n"), url2.c_str());

    http2.begin(secureClient, url2);
    http2.addHeader("x-device-code",   DEVICE_CODE);
    http2.addHeader("x-device-secret", DEVICE_SECRET);

    int httpCode2 = http2.GET();
    Serial.printf_P(PSTR("[API] FASE 2 HTTP Code: %d\n"), httpCode2);

    if (httpCode2 != 200) {
        // Metadata sudah tersimpan; FP gagal -> laporkan gagal agar bisa retry.
        Serial.println(F("[API] FASE 2 gagal menarik fingerprint"));
        logSystemActivity("API_SYNC_ERROR", "FASE2 HTTP: " + String(httpCode2));
        lcd.printLine(2, "FP Error: " + String(httpCode2));
        delay(2000);
        http2.end();
        isFetching = false;
        return false;
    }

    String fpPayload = http2.getString();
    http2.end();

    JsonDocument fpDoc;
    error = deserializeJson(fpDoc, fpPayload);
    fpPayload = String();

    if (error) {
        Serial.printf_P(PSTR("[API] FASE 2 JSON Parse Error: %s\n"), error.c_str());
        lcd.printLine(2, F("FP Parse Error"));
        delay(2000);
        isFetching = false;
        return false;
    }

    JsonArray fps;
    if      (fpDoc["data"].is<JsonArray>())          fps = fpDoc["data"].as<JsonArray>();
    else if (fpDoc["users"].is<JsonArray>())         fps = fpDoc["users"].as<JsonArray>();
    else if (fpDoc["fingerprints"].is<JsonArray>())  fps = fpDoc["fingerprints"].as<JsonArray>();
    else if (fpDoc.is<JsonArray>())                  fps = fpDoc.as<JsonArray>();
    else {
        Serial.println(F("[API] FASE 2: array fingerprint tidak ditemukan"));
        lcd.printLine(2, F("FP: Invalid Resp"));
        delay(2000);
        isFetching = false;
        return false;
    }

    Serial.printf_P(PSTR("[API] FASE 2: %d fingerprint records\n"), (int)fps.size());

    // --- Upsert fingerprint_users.csv (atomic swap) ---
    // Strategi: tulis ulang seluruh file dengan id (=slot sensor) dinomori ulang
    // dari 1 secara berurutan -> slot selalu rapat 1..N, tidak pernah drift ke >200.
    // Baris user_id yg TIDAK di-update disalin apa adanya (hanya id-nya diganti);
    // baris untuk NIP target ditulis ulang dengan fp_1 + fp_2 yang baru.
    SD.remove("/temp_fingerprint_users.csv");
    File tf = SD.open("/temp_fingerprint_users.csv", FILE_WRITE);
    if (!tf) {
        Serial.println(F("[API] ERROR: gagal buat temp_fingerprint_users.csv"));
        lcd.printLine(2, F("Error: temp FP"));
        delay(2000);
        isFetching = false;
        return false;
    }
    tf.println("id,user_id,role,data_jari,created_at");

    int slotCounter = 0;   // id berurutan mulai dari 1
    // 1) Salin baris lama yg user_id-nya BUKAN target (id dinomori ulang)
    {
        File of = SD.open("/fingerprint_users.csv", FILE_READ);
        if (of) {
            while (of.available()) {
                String line = of.readStringUntil('\n');
                line.trim();
                if (line.length() < 5) continue;
                if (line.startsWith("id,")) continue;

                int p1 = line.indexOf(',');
                int p2 = line.indexOf(',', p1 + 1);
                if (p1 < 0 || p2 < 0) continue;
                String userId = line.substring(p1 + 1, p2);
                userId.trim();

                if (nipInList(targetNips, userId)) continue;  // akan ditulis ulang

                // Tulis ulang dgn id baru; pertahankan sisa kolom (user_id,role,data_jari,created_at)
                tf.print(++slotCounter);
                tf.print(',');
                tf.println(line.substring(p1 + 1));
            }
            of.close();
        }
    }

    // 2) Tulis fp_1 + fp_2 baru untuk tiap NIP target
    String timestamp = isTimeSynced() ? getFormattedTime() : "API_SYNC";
    int writtenFps = 0;
    for (JsonObject fp : fps) {
        String nip = jvToStr(fp["nip"]); nip.trim();
        if (nip.length() == 0) { nip = jvToStr(fp["user_id"]); nip.trim(); }  // fallback key
        if (nip.length() == 0) continue;

        String role = jvToStr(fp["role"]); role.trim();
        if (role.length() == 0) role = "dosen";

        String fp1 = padFpHex(jvToStr(fp["fp_1_user"]));
        String fp2 = padFpHex(jvToStr(fp["fp_2_user"]));
        if (fp1.length() == 0) fp1 = padFpHex(jvToStr(fp["fp_user"]));  // fallback skema lama

        if (fp1.length() > 0) {
            tf.printf("%d,%s,%s,%s,%s\n", ++slotCounter, nip.c_str(), role.c_str(), fp1.c_str(), timestamp.c_str());
            writtenFps++;
        }
        if (fp2.length() > 0) {
            tf.printf("%d,%s,%s,%s,%s\n", ++slotCounter, nip.c_str(), role.c_str(), fp2.c_str(), timestamp.c_str());
            writtenFps++;
        }
        Serial.printf_P(PSTR("[API] FP upsert NIP=%s fp1=%dB fp2=%dB\n"),
            nip.c_str(), fp1.length(), fp2.length());
    }
    tf.close();
    fpDoc.clear();

    SD.remove("/fingerprint_users.csv");
    SD.rename("/temp_fingerprint_users.csv", "/fingerprint_users.csv");
    Serial.printf_P(PSTR("[API] fingerprint_users.csv di-upsert: %d FP baru\n"), writtenFps);

    // ===================== RELOAD RAM / SENSOR =====================
    Serial.println(F("[API] Mereload data fingerprint ke RAM/sensor..."));
    fingerprintManager.syncFromCSV(&userManager);
    Serial.println(F("[API] Reload RAM selesai"));

    logSystemActivity("API_SYNC",
        "Delta Sync OK. NIP berubah: " + String((int)targetNips.size()) + ", FP: " + String(writtenFps));

    lcd.clear();
    lcd.printLine(0, MSG_API_BERHASIL);
    lcd.printLine(1, "Update: " + String((int)targetNips.size()));
    lcd.printLine(2, "FP: " + String(writtenFps));
    delay(2000);

    isFetching      = false;
    isSyncUserFailed = false;   // SUKSES -> sembunyikan opsi retry
    syncStatusUsers  = true;    // FASE 1 SUKSES -> ikon centang di layar Login
    return true;
}

// ========== TUGAS 2: API LOGIN ==========
String APIManager::apiLogin(int finger_id) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[API_LOGIN] WiFi not connected");
        return "";
    }

    Serial.println("[API_LOGIN] ============================");
    Serial.println("[API_LOGIN] Attempting login for finger_id: " + String(finger_id));

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    String url = String(API_BASE_URL) + "/api/device/login";

    http.begin(secureClient, url);

    // Add headers - Hanya x-device-code dan x-device-secret
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-device-code", DEVICE_CODE);
    http.addHeader("x-device-secret", DEVICE_SECRET);

    // Create JSON body
    JsonDocument doc;
    doc["finger_id"] = finger_id;

    String jsonBody;
    serializeJson(doc, jsonBody);

    Serial.println("[API_LOGIN] Request body: " + jsonBody);

    int httpCode = http.POST(jsonBody);
    Serial.println("[API_LOGIN] HTTP Code: " + String(httpCode));

    String token = "";

    if (httpCode > 0) {
        String payload = http.getString();

        if (httpCode == 200) {
            JsonDocument responseDoc;
            DeserializationError error = deserializeJson(responseDoc, payload);

            if (error) {
                Serial.println("[API_LOGIN] JSON Parse Error: " + String(error.c_str()));
            } else {
                // Extract token from response
                if (responseDoc["token"].is<const char*>()) {
                    token = responseDoc["token"].as<const char*>();
                    Serial.println("[API_LOGIN] Token received: " + token.substring(0, 20) + "...");

                    logSystemActivity("API_LOGIN", "Berhasil mendapat token untuk finger_id: " + String(finger_id));
                } else if (responseDoc["data"]["token"].is<const char*>()) {
                    token = responseDoc["data"]["token"].as<const char*>();
                    Serial.println("[API_LOGIN] Token received (data): " + token.substring(0, 20) + "...");

                    logSystemActivity("API_LOGIN", "Berhasil mendapat token untuk finger_id: " + String(finger_id));
                } else {
                    Serial.println("[API_LOGIN] No token in response");
                    logSystemActivity("API_LOGIN_ERROR", "finger_id: " + String(finger_id) + " - No token");
                }
            }
        } else {
            Serial.println("[API_LOGIN] HTTP Error: " + String(httpCode));
            Serial.println(payload);
            logSystemActivity("API_LOGIN_ERROR", "HTTP: " + String(httpCode));
        }
    } else {
        Serial.println("[API_LOGIN] Request failed: " + http.errorToString(httpCode));
    }

    http.end();
    return token;
}

// ========== TUGAS 2: FETCH STUDENT FINGERPRINT INDIVIDUAL (BINARY-SAFE) ==========
// Pull fingerprint template untuk mahasiswa tertentu dari server
// Menggunakan alur Raw Hex Langsung (tidak ada Base64 decode)
// Server mengirim Hex String langsung - tidak perlu decode
String APIManager::fetchStudentFingerprint(String nim, String token) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[API_PULL] WiFi not connected");
        return "";
    }

    Serial.println("[API_PULL] ============================");
    Serial.println("[API_PULL] Fetching fingerprint for NIM: " + nim);

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    String url = String(API_BASE_URL) + "/api/device/fingerprint?nim=" + nim;

    Serial.printf("[API_PULL] URL: %s\n", url.c_str());

    http.begin(secureClient, url);
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("x-device-code", DEVICE_CODE);
    http.addHeader("x-device-secret", DEVICE_SECRET);

    String hexData = "";
    int httpCode = http.GET();

    Serial.printf("[API_PULL] HTTP Code: %d\n", httpCode);

    if (httpCode == 200) {
        Serial.println("[API_PULL] HTTP 200 OK");
        String response = http.getString();

        Serial.printf("[API_PULL] Payload: %s\n", response.c_str());

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);

        if (error) {
            Serial.printf_P(PSTR("[API_PULL] JSON Parse Error: %s\n"), error.c_str());
        } else {
            // Navigasi ke array 'data' lalu elemen ke-0
            // Struktur: {"ok":true, "data":[{"nim":"...", "fp_mhsw":"..."}]}
            if (doc["data"].is<JsonArray>()) {
                JsonArray arr = doc["data"].as<JsonArray>();
                if (arr.size() > 0) {
                    if (!arr[0]["fp_mhsw"].isNull()) {
                        hexData = arr[0]["fp_mhsw"].as<String>();
                        Serial.printf_P(PSTR("[API_PULL] Data ditemukan! Hex Len: %d\n"), hexData.length());
                    } else if (!arr[0]["template_data"].isNull()) {
                        hexData = arr[0]["template_data"].as<String>();
                        Serial.printf_P(PSTR("[API_PULL] Data ditemukan (template_data)! Len: %d\n"), hexData.length());
                    } else {
                        Serial.println(F("[API_PULL] Error: fp_mhsw/template_data tidak ditemukan dalam array"));
                    }
                } else {
                    Serial.println(F("[API_PULL] Error: Array data kosong"));
                }
            }
            // Fallback: Jika response langsung object
            else if (!doc["fp_mhsw"].isNull()) {
                hexData = doc["fp_mhsw"].as<String>();
                Serial.printf_P(PSTR("[API_PULL] Data ditemukan (direct)! Hex Len: %d\n"), hexData.length());
            } else if (!doc["template_data"].isNull()) {
                hexData = doc["template_data"].as<String>();
            } else if (!doc["data"]["fp_mhsw"].isNull()) {
                hexData = doc["data"]["fp_mhsw"].as<String>();
            } else {
                Serial.println(F("[API_PULL] Error: Key fp_mhsw tidak ditemukan"));
            }
        }
    } else {
        Serial.printf("[API_PULL] HTTP Error: %d\n", httpCode);
    }

    http.end();

    // ========== TUGAS 2: SIMPAN LANGSUNG RAW HEX ==========
    // Server mengirim Hex String langsung - tidak perlu decode
    if (hexData.length() == 0) return "";

    Serial.printf_P(PSTR("[API_PULL] Data ditemukan! Hex Len: %d\n"), hexData.length());

    return hexData;
}

// ========== BATCH PULL: Fetch multiple student fingerprints in one API call ==========
// nimList: comma-separated NIMs (e.g., "18022054,18022051,18022055")
// Returns: JsonArray with {nim, fp_mhsw} objects
JsonArray APIManager::fetchBatchStudentFingerprints(String nimList, String token, JsonDocument& doc) {
    JsonArray emptyArray;
    syncStatusFeature = false;   // Anggap GAGAL dulu; sukses di-set true saat HTTP 200 + parsing OK.
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[BATCH_PULL] WiFi not connected");
        return emptyArray;
    }

    if (nimList.length() == 0) {
        Serial.println("[BATCH_PULL] nimList kosong");
        return emptyArray;
    }

    Serial.println("[BATCH_PULL] ============================");
    Serial.printf("[BATCH_PULL] Fetching fingerprints untuk NIMs: %s\n", nimList.c_str());

    // ========== LAZY INIT PERSISTENT TLS SESSION (Keep-Alive) ==========
    // Setup HANYA SEKALI di pemanggilan pertama. Sesi ditutup oleh state machine
    // via endBatchPullSession() setelah seluruh batch selesai. Dengan pola ini,
    // 9 request batch berbagi 1 TCP+TLS handshake (hemat ratusan ms per request
    // dan menghindari beban kriptografi asimetris yang berulang di ESP32).
    if (!_batchPullActive) {
        _batchPullClient.setInsecure();
        _batchPullHttp.setReuse(true);   // jaga koneksi TCP/TLS lintas request
        _batchPullActive = true;
        Serial.println(F("[BATCH_PULL] Persistent TLS session DIBUKA (Keep-Alive ON)"));
    }

    // Endpoint 2-jari: kembalikan fp_1_mhsw + fp_2_mhsw per NIM
    String url = String(API_BASE_URL) + "/api/device/fingerprint-mahasiswa?nim=" + nimList;
    Serial.printf("[BATCH_PULL] URL: %s\n", url.c_str());

    _batchPullHttp.begin(_batchPullClient, url);
    _batchPullHttp.addHeader("Connection",      "keep-alive");  // eksplisit minta keep-alive
    _batchPullHttp.addHeader("Authorization",   "Bearer " + token);
    _batchPullHttp.addHeader("x-device-code",   DEVICE_CODE);
    _batchPullHttp.addHeader("x-device-secret", DEVICE_SECRET);

    int httpCode = _batchPullHttp.GET();
    Serial.printf("[BATCH_PULL] HTTP Code: %d\n", httpCode);

    JsonArray resultArray;

    if (httpCode == 200) {
        // getString() men-handle Transfer-Encoding: chunked dengan benar.
        String payload = _batchPullHttp.getString();
        Serial.printf_P(PSTR("[BATCH_PULL] Body bytes: %d\n"), payload.length());

        // DEBUG: cetak 200 char pertama payload mentah utk lihat struktur JSON asli
        Serial.println("[BATCH_PULL] RAW JSON: " +
            payload.substring(0, payload.length() < 200 ? payload.length() : 200));

        doc.clear();
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
            Serial.printf_P(PSTR("[BATCH_PULL] JSON Parse Error: %s\n"), error.c_str());
        } else {
            // ========== PENCARIAN KEY DINAMIS (SMART FALLBACK) ==========
            // Endpoint berbeda bisa membungkus array di key berbeda. Cek beberapa
            // kemungkinan, termasuk root array langsung.
            const char* foundKey = nullptr;
            if (doc["data"].is<JsonArray>()) {
                resultArray = doc["data"].as<JsonArray>();          foundKey = "data";
            } else if (doc["students"].is<JsonArray>()) {
                resultArray = doc["students"].as<JsonArray>();      foundKey = "students";
            } else if (doc["mahasiswa"].is<JsonArray>()) {
                resultArray = doc["mahasiswa"].as<JsonArray>();     foundKey = "mahasiswa";
            } else if (doc["fingerprints"].is<JsonArray>()) {
                resultArray = doc["fingerprints"].as<JsonArray>();  foundKey = "fingerprints";
            } else if (doc.is<JsonArray>()) {
                resultArray = doc.as<JsonArray>();                  foundKey = "<root>";
            }

            if (foundKey == nullptr) {
                Serial.println(F("[BATCH_PULL] ERROR: Array sidik jari tidak ditemukan pada key manapun!"));
            } else {
                Serial.printf_P(PSTR("[BATCH_PULL] Array di key '%s', %d fingerprints\n"),
                    foundKey, (int)resultArray.size());
                syncStatusFeature = true;   // FASE 3 SUKSES
            }
        }
        // JANGAN doc.clear() di sini - 'doc' dimiliki pemanggil; resultArray masih
        // menunjuk ke isinya. Membersihkan di sini = dangling reference.
    } else {
        Serial.printf("[BATCH_PULL] HTTP Error: %d\n", httpCode);
    }

    // ========== KURAS SISA BYTE DI STREAM (jaga socket bersih utk request berikut) ==========
    // Jika ada chunked-trailer / body sisa yang belum ter-consume, koneksi Keep-Alive
    // bisa di-drop oleh HTTPClient ESP32. Kuras eksplisit di sini.
    Stream* stream = _batchPullHttp.getStreamPtr();
    if (stream) {
        while (stream->available()) stream->read();
    }

    // DILARANG: jangan panggil _batchPullHttp.end() atau _batchPullClient.stop() di sini.
    // Sesi persistent ditutup oleh endBatchPullSession() setelah seluruh batch beres.
    return resultArray;
}

// ========== TUTUP PERSISTENT TLS SESSION SETELAH SELURUH BATCH SELESAI ==========
// Dipanggil oleh state machine di akhir alur PULL_FP (pullIndex >= targetCount).
void APIManager::endBatchPullSession() {
    if (_batchPullActive) {
        _batchPullHttp.end();             // close TCP/TLS connection
        _batchPullActive = false;
        Serial.println(F("[BATCH_PULL] Persistent TLS session DITUTUP"));
    }
}

// ========== LANGKAH 3: FETCH DASHBOARD DATA ==========
// NOTE: Untuk saat ini, HANYA cetak respons JSON ke Serial Monitor.
// Jangan melakukan parsing atau menyimpan ke SD Card terlebih dahulu.
void APIManager::fetchDashboardData(String token) {
    // Anggap GAGAL dulu; baru di-set true di akhir processDashboardData saat sukses.
    syncStatusDashboard = false;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[API-Langkah3] WiFi not connected"));
        return;
    }

    if (token.length() == 0) {
        Serial.println(F("[API-Langkah3] No token provided"));
        return;
    }

    Serial.println(F("[API-Langkah3] ============================"));
    Serial.println(F("[API-Langkah3] Fetching dashboard data..."));

    // ========== TUGAS 3: UPDATE LCD ==========
    // Row 0: Login Berhasil!
    // Row 1: Memuat Data...
    // Row 2: Mohon Tunggu
    lcd.clear();
    lcd.printLine(0, MSG_API_LOGIN);
    lcd.printLine(1, MSG_API_DATA);
    lcd.printLine(2, MSG_API_WAIT);

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    String url = "https://presensi-elektronik-ta-2526-016.vercel.app/api/device/dashboard";

    http.begin(secureClient, url);

    // ========== TUGAS 2: SET 3 HEADERS ==========
    // x-device-code dan x-device-secret dari konstanta DEVICE_CODE/DEVICE_SECRET
    // Authorization: Bearer <token> (PENTING: ada spasi setelah kata Bearer)
    http.addHeader("x-device-code", DEVICE_CODE);
    http.addHeader("x-device-secret", DEVICE_SECRET);
    http.addHeader("Authorization", "Bearer " + token);

    Serial.println("[API-Langkah3] URL: " + url);
    Serial.println("[API-Langkah3] Headers:");
    Serial.println("  - x-device-code: " + String(DEVICE_CODE));
    Serial.println("  - x-device-secret: " + String(DEVICE_SECRET));
    Serial.println("  - Authorization: Bearer " + token.substring(0, 20) + "...");

    // ========== TUGAS 3: LAKUKAN HTTP.GET() ==========
    int httpCode = http.GET();
    Serial.println("[API-Langkah3] HTTP Code: " + String(httpCode));

    if (httpCode > 0) {
        // ========== TUGAS 3: TANGKAP RESULT KE VARIABEL STRING ==========
        String dashboardPayload = http.getString();

        // ========== TUGAS 3: CETAK KE SERIAL MONITOR SECARA UTUH ==========
        Serial.println("[API-Langkah3] ===== RAW DASHBOARD DATA =====");
        Serial.println(dashboardPayload);
        Serial.println("[API-Langkah3] ==============================");

        // ========== TUGAS 4: PROSES & SIMPAN DATA KE SD CARD ==========
        processDashboardData(dashboardPayload);
        Serial.println("Data Dashboard berhasil disimpan ke SD Card");

        // Log ke system
        if (httpCode == 200) {
            logSystemActivity("API_LANGKAH3", "Dashboard data berhasil diterima");
        } else {
            logSystemActivity("API_LANGKAH3_ERROR", "HTTP: " + String(httpCode));
        }
    } else {
        Serial.println("[API-Langkah3] Request failed: " + String(http.errorToString(httpCode).c_str()));
        logSystemActivity("API_LANGKAH3_ERROR", http.errorToString(httpCode));
    }

    http.end();

    // ========== TUGAS 3: FUNGSI BERAKHIR ==========
    // Biarkan fungsi berakhir agar state machine bisa melanjutkan transisi ke Menu Dosen
    Serial.println("[API-Langkah3] Selesai - state machine dapat melanjutkan ke Menu Dosen");
}

// ========== TUGAS 2: HELPER FUNCTION ANTI-DUPLIKASI ==========
// Cek apakah sebuah ID sudah ada di dalam CSV tertentu
// filename: nama file CSV (contoh: "/dosen.csv")
// searchKey: nilai yang dicari (contoh: NIP, NIM, kode_mk)
// columnIndex: index kolom yang akan dicek (0 = kolom pertama, 1 = kolom kedua, dst)
bool APIManager::isRecordExists(const char* filename, String searchKey, int columnIndex) {
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        return false;  // File tidak ada, anggap tidak ada duplikat
    }

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() < 2) continue;
        if (line.startsWith("nip") || line.startsWith("kode") || line.startsWith("nim") ||
            line.startsWith("id,") || line.startsWith("id,kode")) continue;  // Skip header

        int colCurrent = 0;
        int startIdx = 0;

        // Parse kolom per baris
        for (int i = 0; i <= line.length(); i++) {
            if (i == line.length() || line.charAt(i) == ',') {
                if (colCurrent == columnIndex) {
                    String colValue = line.substring(startIdx, i);
                    colValue.trim();
                    if (colValue == searchKey) {
                        file.close();
                        return true;  // Ditemukan duplikat
                    }
                    break;
                }
                colCurrent++;
                startIdx = i + 1;
            }
        }
    }

    file.close();
    return false;  // Tidak ditemukan duplikat
}

// ========== TUGAS: HELPER FUNCTION ANTI-DUPLIKASI DUA KOLOM ==========
// Cek apakah kombinasi dua nilai (kolom1 + kolom2) sudah ada di CSV
bool APIManager::isRecordExists(const char* filename, String searchKey1, int columnIndex1, String searchKey2, int columnIndex2) {
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        return false;  // File tidak ada, anggap tidak ada duplikat
    }

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() < 2) continue;
        if (line.startsWith("nip") || line.startsWith("kode") || line.startsWith("nim") ||
            line.startsWith("id,") || line.startsWith("id,kode")) continue;  // Skip header

        // Parse semua kolom ke array
        std::vector<String> cols;
        int startIdx = 0;
        for (int i = 0; i <= line.length(); i++) {
            if (i == line.length() || line.charAt(i) == ',') {
                String colValue = line.substring(startIdx, i);
                colValue.trim();
                cols.push_back(colValue);
                startIdx = i + 1;
            }
        }

        // Cek apakah kedua kolom匹配
        if (columnIndex1 < (int)cols.size() && columnIndex2 < (int)cols.size()) {
            if (cols[columnIndex1] == searchKey1 && cols[columnIndex2] == searchKey2) {
                file.close();
                return true;  // Ditemukan duplikat
            }
        }
    }

    file.close();
    return false;  // Tidak ditemukan duplikat
}

// ========== TUGAS 1: HELPER FUNCTION CEK DUPLIKAT PERTEMUAN (STRICT 5-KOLOM) ==========
// ========== TUGAS 1: CEK DATA SUDAH ADA - FORMAT BARU 6 KOLOM ==========
// Cek apakah kombinasi kode_kelas + kelas + pertemuan_ke sudah ada di pertemuan.csv
// Format baru: id,server_jadwal_id,kode_kelas,kelas,pertemuan_ke,tanggal
bool APIManager::isPertemuanExists(String kode, String kls, String pert) {
    File file = SD.open("/pertemuan.csv", FILE_READ);
    if (!file) {
        Serial.println("[DEBUG-ISEXISTS] File pertemuan.csv tidak ada, anggap tidak ada duplikat");
        return false;  // File tidak ada, anggap tidak ada duplikat
    }

    int lineCount = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        lineCount++;
        line.trim();
        if (line.length() < 5) continue;
        // Skip header
        if (line.startsWith("id,") || line.startsWith("id,kode") || line.startsWith("id,server")) continue;

        // ========== TUGAS 1: PARSE 6 KOLOM ==========
        // Format: id,server_jadwal_id,kode_kelas,kelas,pertemuan_ke,tanggal
        int p0 = line.indexOf(',');           // setelah id
        int p1 = line.indexOf(',', p0 + 1);   // setelah server_jadwal_id
        int p2 = line.indexOf(',', p1 + 1);   // setelah kode_kelas
        int p3 = line.indexOf(',', p2 + 1);   // setelah kelas
        int p4 = line.indexOf(',', p3 + 1);   // setelah pertemuan_ke

        if (p0 > 0 && p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0) {
            String existingKode = line.substring(p2 + 1, p3);  // kode_kelas ada di kolom ke-3
            String existingKelas = line.substring(p3 + 1, p4);  // kelas ada di kolom ke-4
            String existingPert = line.substring(p4 + 1);      // pertemuan_ke ada di kolom ke-5

            existingKode.trim();
            existingKode.replace("\r", "");
            existingKelas.trim();
            existingKelas.replace("\r", "");
            existingPert.trim();
            existingPert.replace("\r", "");

            Serial.printf("[DEBUG-ISEXISTS] Line %d: kode=%s, kelas=%s, pert=%s\n", lineCount, existingKode.c_str(), existingKelas.c_str(), existingPert.c_str());

            // ========== TUGAS 3: STRICT COMPARISON SETELAH TRIM ==========
            if (existingKode == kode && existingKelas == kls && existingPert == pert) {
                file.close();
                Serial.println("[DEBUG-ISEXISTS] Ditemukan duplikat!");
                return true;  // Ditemukan duplikat
            }
        }
    }

    file.close();
    Serial.printf("[DEBUG-ISEXISTS] Total baris dibaca: %d, tidak ditemukan duplikat\n", lineCount);
    return false;  // Tidak ditemukan duplikat
}

// ========== CEK DUPLIKAT BERDASARKAN server_jadwal_id (KOLOM KE-2) ==========
// Format: id,server_jadwal_id,kode_kelas,kelas,pertemuan_ke,tanggal
// Bandingkan nilai DI ANTARA koma pertama & koma kedua dengan jadwal_id dari JSON.
bool APIManager::isJadwalExists(int serverJadwalId) {
    File file = SD.open("/pertemuan.csv", FILE_READ);
    if (!file) {
        Serial.println(F("[ISEXISTS] pertemuan.csv belum ada -> tidak duplikat"));
        return false;
    }

    bool exists = false;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() < 3) continue;
        if (line.startsWith("id,")) continue;  // skip header

        int p0 = line.indexOf(',');           // koma pertama (setelah id)
        if (p0 < 0) continue;
        int p1 = line.indexOf(',', p0 + 1);   // koma kedua (setelah server_jadwal_id)
        if (p1 < 0) continue;

        // Ekstrak nilai di antara koma pertama dan koma kedua = server_jadwal_id
        String existingId = line.substring(p0 + 1, p1);
        existingId.trim();
        existingId.replace("\r", "");

        if (existingId.length() > 0 && existingId.toInt() == serverJadwalId) {
            Serial.printf_P(PSTR("[ISEXISTS] Duplikat: server_jadwal_id=%d sudah ada\n"), serverJadwalId);
            exists = true;
            break;  // batalkan append
        }
    }

    file.close();
    return exists;
}

// ========== TUGAS 1: HELPER FUNCTION AUTO-INCREMENT ==========
// Buka file CSV, cari ID tertinggi di kolom pertama, return maxId + 1
int APIManager::getNextAutoId(const char* filename) {
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        return 1;  // File tidak ada, mulai dari 1
    }

    int maxId = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() < 2) continue;

        // Skip header - cek apakah baris pertama adalah "id,..." atau "nip,..." dll
        if (line.startsWith("id,") || line.startsWith("nip,") || line.startsWith("kode,") ||
            line.startsWith("nim,") || line.startsWith("id,kode") || line.startsWith("id,nip")) {
            continue;
        }

        // Ambil kolom pertama (ID)
        int commaIdx = line.indexOf(',');
        if (commaIdx > 0) {
            String idStr = line.substring(0, commaIdx);
            idStr.trim();
            int idVal = idStr.toInt();
            if (idVal > maxId) {
                maxId = idVal;
            }
        }
    }

    file.close();
    return maxId + 1;  // Jika file kosong/hanya header, return 1
}

// ========== TUGAS 3: PROSES & DISTRIBUSI DATA KE CSV ==========
// Mendistribusikan JSON Dashboard ke 7 tabel CSV di SD Card
void APIManager::processDashboardData(String jsonPayload) {
    Serial.println("[PROCESS_DASHBOARD] ============================");
    Serial.println("[PROCESS_DASHBOARD] Memproses JSON Dashboard...");

    // ========== TUGAS 1: ALOKASI MEMORI & PARSING JSON ==========
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonPayload);

    if (error) {
        Serial.println("[PROCESS_DASHBOARD] JSON Parse Error: " + String(error.c_str()));
        return;
    }

    Serial.println("[PROCESS_DASHBOARD] JSON berhasil di-parse!");

    // Get timestamp untuk created_at
    String timestamp = getFormattedTime();
    if (timestamp == "WAKTU_BELUM_SINKRON") {
        timestamp = "API_SYNC";
    }

    // ========== TUGAS 2: INISIALISASI ID DI LUAR LOOP ==========
    // Ambil ID berikutnya untuk setiap tabel relasi SEBELUM looping
    // TUGAS 5: Ganti fingerprint.csv ke fingerprint_users.csv
    int nextDosenKelasId = getNextAutoId("/dosen_kelas.csv");
    int nextKelasMhsId = getNextAutoId("/kelas_mahasiswa.csv");
    int nextFingerprintUsersId = getNextAutoId("/fingerprint_users.csv");
    // ========== TUGAS 2: INISIALISASI AUTO-INCREMENT PERTEMUAN ==========
    int nextPertemuanId = getNextAutoId("/pertemuan.csv");
    Serial.println("[SYNC] Next IDs - dosen_kelas: " + String(nextDosenKelasId) +
                  ", kelas_mahasiswa: " + String(nextKelasMhsId) +
                  ", fingerprint_users: " + String(nextFingerprintUsersId) +
                  ", pertemuan: " + String(nextPertemuanId));

    // ========== TUGAS 3A: DATA DOSEN (dosen.csv) ==========
    // Format: nip,nama_lengkap,created_at
    Serial.println("[PROCESS_DASHBOARD] Proses dosen.csv...");
    Serial.println("[SYNC] Mulai proses dosen...");
    lcd.printLine(1, F("Dosen..."));

    if (doc["lecturer"].is<JsonObject>()) {
        JsonObject lecturer = doc["lecturer"].as<JsonObject>();

        // ========== TUGAS 1: PARSING AMAN UNTUK INTEGER/LONG KE STRING ==========
        // WAJIB .as<String>() agar 18 digit NIP tidak overflow menjadi 0
        String nip = "";
        if (!lecturer["nip"].isNull()) {
            nip = lecturer["nip"].as<String>();
        }
        String nama = "";
        if (!lecturer["nama"].isNull()) {
            nama = lecturer["nama"].as<String>();
        }

        Serial.println("[SYNC] lecturer JSON: nip=" + nip + ", nama=" + nama);

        // ========== TUGAS 2: PROTEKSI VARIABEL KOSONG ==========
        if (nip.length() == 0 || nip == "0" || nip == "null") {
            Serial.println("[SYNC] Skip - NIP tidak valid: " + nip);
        } else {
            bool exists = isRecordExists("/dosen.csv", nip, 0);
            Serial.println("[SYNC] Cek dosen.csv, NIP=" + nip + ", exists=" + String(exists));

            if (!exists) {
                File file = SD.open("/dosen.csv", FILE_APPEND);
                if (file) {
                    file.println(String(nip) + "," + nama + "," + timestamp);
                    file.close();
                    Serial.println("[SYNC] BERHASIL append dosen.csv: NIP " + nip);
                } else {
                    Serial.println("[SYNC] GAGAL membuka dosen.csv untuk append");
                }
            } else {
                Serial.println("[SYNC] Skip dosen.csv - NIP sudah ada: " + nip);
            }
        }
    } else {
        Serial.println("[SYNC] GAGAL - doc['lecturer'] bukan JsonObject atau tidak ada");
    }

    // ========== TUGAS 3B: DATA KELAS & RELASI DOSEN (kelas.csv & dosen_kelas.csv) ==========
    // Format kelas.csv: kode_mk,kelas,nama_mk,sks
    // Format dosen_kelas.csv: id,nip,kode_kelas,kelas
    Serial.println("[PROCESS_DASHBOARD] Proses kelas.csv & dosen_kelas.csv...");
    Serial.println("[SYNC] Mulai proses kelas...");
    lcd.printLine(1, "Kelas...");

    // ========== TUGAS 3: PERBAIKI LOGIKA DOSEN_KELAS ==========
    // TUGAS 1: Ambil NIP dari doc["lecturer"]["nip"] menggunakan .as<String>() agar 18 digit tidak overflow
    String lecturerNIP = "";
    if (!doc["lecturer"]["nip"].isNull()) {
        lecturerNIP = doc["lecturer"]["nip"].as<String>();
    }
    Serial.println("[SYNC] Lecturer NIP: " + lecturerNIP);

    if (doc["kelas"].is<JsonArray>()) {
        JsonArray kelasArray = doc["kelas"].as<JsonArray>();
        Serial.println("[SYNC] Jumlah kelas: " + String(kelasArray.size()));

        // Kumpulan relasi dosen-kelas dari dashboard (untuk mirror dosen_kelas.csv).
        // Skema baru: kelas membawa kelas_id; jadwal/pertemuan sudah dihapus dari API.
        std::vector<String> dkKode, dkKelas, dkKelasId;

        int kelasIdx = 0;
        for (JsonObject kelasObj : kelasArray) {
            kelasIdx++;
            String kode_mk = kelasObj["kode_mk"] | "";
            String nama_mk = kelasObj["nama_mk"] | "";
            // ========== EKSTRAK VARIABEL KELAS + kelas_id ==========
            String kelas = "";
            if (!kelasObj["kelas"].isNull()) kelas = kelasObj["kelas"].as<String>();
            String kelasId = "";
            if (!kelasObj["kelas_id"].isNull()) kelasId = kelasObj["kelas_id"].as<String>();
            kode_mk.trim(); kelas.trim(); kelasId.trim();

            Serial.println("[SYNC] Kelas[" + String(kelasIdx) + "]: kelas_id=" + kelasId +
                           ", kode_mk=" + kode_mk + ", nama=" + nama_mk + ", kelas=" + kelas);

            // ke kelas.csv (master matkul). Skema lama: kode_mk,kelas,nama_mk,sks.
            // API baru tidak mengirim sks -> tulis "NULL" agar kolom tetap konsisten.
            if (kode_mk.length() > 0 && kelas.length() > 0) {
                bool existsKelas = isRecordExists("/kelas.csv", kode_mk, 0, kelas, 1);
                if (!existsKelas) {
                    File file = SD.open("/kelas.csv", FILE_APPEND);
                    if (file) {
                        file.printf("%s,%s,%s,NULL\n", kode_mk.c_str(), kelas.c_str(), nama_mk.c_str());
                        file.close();
                        Serial.println("[SYNC]   BERHASIL append kelas.csv: " + kode_mk + " Kelas " + kelas);
                    }
                } else {
                    Serial.println("[SYNC]   Skip kelas.csv - sudah ada: " + kode_mk + " kelas " + kelas);
                }
            }

            // Kumpulkan untuk mirror dosen_kelas.csv (termasuk kelas_id)
            if (kode_mk.length() > 0 && kelas.length() > 0 && lecturerNIP.length() > 0) {
                dkKode.push_back(kode_mk);
                dkKelas.push_back(kelas);
                dkKelasId.push_back(kelasId);
            }
        }

        // ===== MIRROR dosen_kelas.csv UNTUK lecturerNIP =====
        // Tulis ulang relasi milik NIP ini dari nol (dosen lain dipertahankan), agar
        // kelas_id SELALU ter-update walau relasi (kode+kelas) sudah pernah ada.
        // Skema baru: id,nip,kode_kelas,kelas,kelas_id
        if (lecturerNIP.length() > 0) {
            std::vector<String> keep;   // baris milik dosen LAIN (verbatim)
            int maxId = 0;
            File rf = SD.open("/dosen_kelas.csv", FILE_READ);
            if (rf) {
                if (rf.available()) rf.readStringUntil('\n');   // skip header
                while (rf.available()) {
                    String line = rf.readStringUntil('\n');
                    line.trim(); line.replace("\r", "");
                    if (line.length() < 3) continue;
                    int q1 = line.indexOf(',');
                    int q2 = line.indexOf(',', q1 + 1);
                    if (q1 < 0 || q2 < 0) continue;
                    String rowNip = line.substring(q1 + 1, q2); rowNip.trim();
                    int rowId = line.substring(0, q1).toInt();
                    if (rowId > maxId) maxId = rowId;
                    if (rowNip != lecturerNIP) keep.push_back(line);   // dosen lain -> simpan
                }
                rf.close();
            }

            SD.remove("/dosen_kelas.csv");
            File wf = SD.open("/dosen_kelas.csv", FILE_WRITE);
            if (wf) {
                wf.println("id,nip,kode_kelas,kelas,kelas_id");
                for (auto& l : keep) wf.println(l);   // dosen lain (id lama dipertahankan)
                int id = maxId + 1;
                for (size_t i = 0; i < dkKode.size(); i++) {
                    wf.printf("%d,%s,%s,%s,%s\n", id++, lecturerNIP.c_str(),
                        dkKode[i].c_str(), dkKelas[i].c_str(), dkKelasId[i].c_str());
                    Serial.println("[SYNC]   dosen_kelas: " + dkKode[i] + "-" + dkKelas[i] +
                                   " kelas_id=" + dkKelasId[i]);
                }
                wf.close();
                Serial.printf("[SYNC]   dosen_kelas.csv di-mirror utk NIP %s: %d relasi\n",
                    lecturerNIP.c_str(), (int)dkKode.size());
            }
        }
    } else {
        Serial.println("[SYNC] GAGAL - doc['kelas'] bukan JsonArray atau tidak ada");
    }

    // ========== TUGAS 3C: DATA PERTEMUAN (pertemuan.csv) ==========
    // Format: id,server_jadwal_id,kode_kelas,kelas,pertemuan_ke,tanggal
    Serial.println("[PROCESS_DASHBOARD] Proses pertemuan.csv...");
    Serial.println("[SYNC] Mulai proses pertemuan...");
    lcd.printLine(1, "Pertemuan...");

    if (doc["jadwal"].is<JsonArray>()) {
        JsonArray jadwalArray = doc["jadwal"].as<JsonArray>();
        Serial.println("[SYNC] Jumlah jadwal/pertemuan: " + String(jadwalArray.size()));

        int jadwalIdx = 0;
        for (JsonObject jadwal : jadwalArray) {
            jadwalIdx++;
            // ========== TUGAS: EKSTRAK VARIABEL DARI JSON ==========
            String kode_mk = jadwal["kode_mk"].as<String>();
            String pertemuan = String(jadwal["pertemuan"].as<int>());
            String tanggal = jadwal["tanggal"].as<String>();
            String kelas = String(jadwal["kelas"].as<int>());
            // ========== TUGAS 1: AMBIL jadwal_id DARI SERVER ==========
            int serverJadwalId = jadwal["jadwal_id"].as<int>();

            // ========== TUGAS 1: DEBUG JSON VALUES ==========
            Serial.printf("[SYNC] JSON[%d]: kode_mk=%s, kelas=%s, pert=%s, tanggal=%s, server_jadwal_id=%d\n",
                jadwalIdx, kode_mk.c_str(), kelas.c_str(), pertemuan.c_str(), tanggal.c_str(), serverJadwalId);

            // ========== TUGAS 2: PROTEKSI VARIABEL KOSONG ==========
            if (kode_mk.length() == 0 || kelas.length() == 0 || pertemuan.length() == 0) {
                Serial.println("[SYNC]   Skip - data tidak lengkap");
                continue;
            }

            // ========== CEK DUPLIKAT BERDASARKAN server_jadwal_id (kolom ke-2) ==========
            // Bandingkan server_jadwal_id di CSV dengan jadwal_id dari JSON
            bool exists = isJadwalExists(serverJadwalId);

            Serial.println("[SYNC]   Cek pertemuan.csv, kode_mk=" + kode_mk + ", kelas=" + kelas + ", pert=" + pertemuan + ", exists=" + String(exists));

            if (!exists) {
                // ========== TUGAS 1: CEK HEADER - BUAT JIKA BELUM ADA ==========
                File testFile = SD.open("/pertemuan.csv", FILE_READ);
                if (!testFile) {
                    testFile = SD.open("/pertemuan.csv", FILE_WRITE);
                    if (testFile) {
                        // ========== TUGAS 1: HEADER BARU DENGAN server_jadwal_id ==========
                        testFile.println("id,server_jadwal_id,kode_kelas,kelas,pertemuan_ke,tanggal");
                        testFile.close();
                        Serial.println("[SYNC]   Created pertemuan.csv with header (including server_jadwal_id)");
                    }
                } else {
                    testFile.close();
                }

                File file = SD.open("/pertemuan.csv", FILE_APPEND);
                if (file) {
                    // ========== TUGAS 1: SIMPAN server_jadwal_id DARI SERVER ==========
                    // Format: id,server_jadwal_id,kode_kelas,kelas,pertemuan_ke,tanggal
                    // Contoh: 1,5,EL2001,1,1,2026-05-07
                    file.printf("%d,%d,%s,%s,%s,%s\n", nextPertemuanId, serverJadwalId, kode_mk.c_str(), kelas.c_str(), pertemuan.c_str(), tanggal.c_str());
                    file.close();
                    Serial.printf("[SYNC]   BERHASIL append pertemuan.csv: ID %d, server_jadwal_id %d\n", nextPertemuanId, serverJadwalId);
                    nextPertemuanId++;  // Increment untuk ID berikutnya
                }
            } else {
                Serial.println("[SYNC]   Skip pertemuan.csv - sudah ada");
            }
        }
    } else {
        Serial.println("[SYNC] GAGAL - doc['jadwal'] bukan JsonArray atau tidak ada");
    }

    // ========== DPK: DATA MAHASISWA DIPINDAH KE fetchDataMahasiswa() ==========
    // Dashboard TIDAK lagi memproses students_by_class (sudah dihapus dari endpoint).
    // mahasiswa.csv & kelas_mahasiswa.csv kini diisi oleh fetchDataMahasiswa(kode_mk, kelas)
    // saat kelas dipilih (endpoint /api/device/dpk). Dashboard hanya simpan:
    // dosen.csv, kelas.csv, dosen_kelas.csv, pertemuan.csv.

    // (mahasiswa.csv & kelas_mahasiswa.csv kini ditangani oleh fetchDataMahasiswa via /api/device/dpk)

    Serial.println("[PROCESS_DASHBOARD] ============================");
    Serial.println("[PROCESS_DASHBOARD] Data Dashboard berhasil disimpan ke SD Card!");
    Serial.println("[PROCESS_DASHBOARD] ============================");

    // ========== ENROLL RATIO CACHE: dihitung di fetchDataMahasiswa() setelah DPK ==========
    // (dashboard tidak lagi mengisi kelas_mahasiswa.csv, jadi cache dihitung saat kelas dipilih)

    // ========== TUGAS 3: UPDATE STATUS SETELAH LANGKAH 3 ==========
    // Tandai bahwa seluruh data dari server berhasil ditarik
    syncStatusDashboard = true;   // FASE 2 SUKSES -> ikon centang di Menu Dosen/Admin
}

// ========== DPK: TARIK DATA MAHASISWA PER KELAS ==========
// GET /api/device/dpk?kode_mk=...&kelas=... lalu upsert mahasiswa.csv & kelas_mahasiswa.csv.
bool APIManager::fetchDataMahasiswa(String kode_mk, int kelas) {
    syncStatusFeature = false;   // Anggap GAGAL dulu; sukses di-set true di akhir.
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[DPK] WiFi tidak terhubung"));
        return false;
    }
    kode_mk.trim();

    Serial.println(F("[DPK] ============================"));
    Serial.printf_P(PSTR("[DPK] Tarik mahasiswa kelas %s-%d\n"), kode_mk.c_str(), kelas);

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    String url = String(API_BASE_URL) + "/api/device/dpk?kode_mk=" + kode_mk + "&kelas=" + String(kelas);
    Serial.printf_P(PSTR("[DPK] URL: %s\n"), url.c_str());

    http.begin(secureClient, url);
    http.addHeader("Authorization", "Bearer " + globalJwtToken);
    http.addHeader("x-device-code", DEVICE_CODE);
    http.addHeader("x-device-secret", DEVICE_SECRET);

    int httpCode = http.GET();
    Serial.printf_P(PSTR("[DPK] HTTP Code: %d\n"), httpCode);

    if (httpCode != 200) {
        Serial.printf_P(PSTR("[DPK] HTTP error: %d\n"), httpCode);
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();
    Serial.printf_P(PSTR("[DPK] Response length: %d\n"), response.length());

    // ========== TAMPILKAN RAW RESPONSE /dpk DI SERIAL MONITOR ==========
    Serial.println(F("[DPK] ===== RAW JSON RESPONSE ====="));
    Serial.println(response);
    Serial.println(F("[DPK] ============================="));

    JsonDocument doc;  // v7: heap-elastik
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf_P(PSTR("[DPK] JSON Parse Error: %s\n"), error.c_str());
        return false;
    }

    // Cari array mahasiswa (robust terhadap variasi struktur)
    JsonArray mhsArray;
    if (doc["mahasiswa"].is<JsonArray>()) {
        mhsArray = doc["mahasiswa"].as<JsonArray>();
    } else if (doc["data"]["mahasiswa"].is<JsonArray>()) {
        mhsArray = doc["data"]["mahasiswa"].as<JsonArray>();
    } else if (doc["data"].is<JsonArray>()) {
        mhsArray = doc["data"].as<JsonArray>();
    } else {
        Serial.println(F("[DPK] ERROR: array 'mahasiswa' tidak ditemukan di JSON"));
        return false;
    }

    String kelasStr = String(kelas);
    String timestamp = getFormattedTime();
    if (timestamp == "WAKTU_BELUM_SINKRON") timestamp = "API_SYNC";

    Serial.printf_P(PSTR("[DPK] Ditemukan %d mahasiswa\n"), (int)mhsArray.size());

    // ---- Kumpulkan baris DPK ke memori (1 kelas, aman untuk RAM) ----
    struct DpkRow { String nim; String nama; String sitIn; String statusFp; };
    std::vector<DpkRow> rows;
    for (JsonObject m : mhsArray) {
        // nim bisa number ATAU string -> tangani keduanya
        String nim;
        JsonVariant vNim = m["nim"];
        if (vNim.is<const char*>()) nim = vNim.as<String>();
        else nim = String(vNim.as<long>());
        nim.trim();
        if (nim.length() == 0 || nim == "0") continue;

        String nama = m["nama"].as<String>();
        nama.trim(); nama.replace(",", " ");  // jaga integritas CSV

        // ========== MAPPING type -> sit_in ==========
        // type == "tetap" -> sit_in = false (0); selain itu (sit-in) -> true (1)
        String type = m["type"].as<String>();
        type.trim(); type.toLowerCase();
        String sitIn = (type == "tetap") ? "false" : "true";

        // status_fp -> kolom status_fingerprint
        bool fp = false;
        if (!m["status_fp"].isNull()) fp = m["status_fp"].as<bool>();
        String statusFp = fp ? "true" : "false";

        DpkRow r; r.nim = nim; r.nama = nama; r.sitIn = sitIn; r.statusFp = statusFp;
        rows.push_back(r);
        Serial.printf_P(PSTR("[DPK]   nim=%s type=%s sit_in=%s status_fp=%s\n"),
            nim.c_str(), type.c_str(), sitIn.c_str(), statusFp.c_str());
    }

    if (rows.empty()) {
        Serial.println(F("[DPK] Tidak ada mahasiswa valid"));
        calculateEnrollRatioCache();
        return true;
    }

    // ========== UPSERT mahasiswa.csv (nim,nama,created_at) - SATU PASS ==========
    {
        File oldFile = SD.open("/mahasiswa.csv", FILE_READ);
        File tempFile = SD.open("/temp_mahasiswa.csv", FILE_WRITE);
        if (tempFile) {
            tempFile.println("nim,nama,created_at");
            std::vector<bool> written(rows.size(), false);
            if (oldFile) {
                if (oldFile.available()) oldFile.readStringUntil('\n');  // skip header
                while (oldFile.available()) {
                    String line = oldFile.readStringUntil('\n');
                    line.trim();
                    if (line.length() < 3) continue;
                    if (line.startsWith("nim,")) continue;
                    int p1 = line.indexOf(',');
                    int p2 = line.indexOf(',', p1 + 1);
                    if (p1 <= 0) continue;
                    String csvNim = line.substring(0, p1); csvNim.trim();
                    String csvNama = (p2 > 0) ? line.substring(p1 + 1, p2) : line.substring(p1 + 1);
                    String csvCreated = (p2 > 0) ? line.substring(p2 + 1) : "";
                    csvNama.trim(); csvNama.replace("\r", "");

                    int idx = -1;
                    for (size_t i = 0; i < rows.size(); i++) if (rows[i].nim == csvNim) { idx = (int)i; break; }
                    if (idx >= 0) {
                        // pertahankan nama lama bila ada, isi dari API bila kosong
                        String nama = (csvNama == "" || csvNama == "-") ? rows[idx].nama : csvNama;
                        tempFile.printf("%s,%s,%s\n", csvNim.c_str(), nama.c_str(), csvCreated.c_str());
                        written[idx] = true;
                    } else {
                        tempFile.println(line);  // mahasiswa kelas lain -> pertahankan
                    }
                }
                oldFile.close();
            }
            for (size_t i = 0; i < rows.size(); i++) {
                if (!written[i]) tempFile.printf("%s,%s,%s\n", rows[i].nim.c_str(), rows[i].nama.c_str(), timestamp.c_str());
            }
            tempFile.close();
            SD.remove("/mahasiswa.csv");
            SD.rename("/temp_mahasiswa.csv", "/mahasiswa.csv");
            Serial.println(F("[DPK] mahasiswa.csv di-upsert"));
        } else {
            if (oldFile) oldFile.close();
            Serial.println(F("[DPK] ERROR: gagal buka temp_mahasiswa.csv"));
        }
    }

    // ========== UPSERT kelas_mahasiswa.csv - SATU PASS untuk kelas ini ==========
    {
        int nextId = getNextAutoId("/kelas_mahasiswa.csv");
        File oldKM = SD.open("/kelas_mahasiswa.csv", FILE_READ);
        File tempKM = SD.open("/temp_km.csv", FILE_WRITE);
        if (tempKM) {
            tempKM.println("id,kode_kelas,kelas,nim,sit_in,status_fingerprint");
            std::vector<bool> done(rows.size(), false);
            if (oldKM) {
                if (oldKM.available()) oldKM.readStringUntil('\n');  // skip header
                while (oldKM.available()) {
                    String line = oldKM.readStringUntil('\n');
                    line.trim();
                    if (line.length() == 0) continue;
                    if (line.startsWith("id,")) continue;
                    String cId = getCsvColumn(line, 0);
                    String cKode = getCsvColumn(line, 1);
                    String cKelas = getCsvColumn(line, 2);
                    String cNim = getCsvColumn(line, 3);
                    cKode.trim(); cKode.replace("\r", "");
                    cKelas.trim(); cKelas.replace("\r", "");
                    cNim.trim(); cNim.replace("\r", "");

                    int idx = -1;
                    if (cKode == kode_mk && cKelas == kelasStr) {
                        for (size_t i = 0; i < rows.size(); i++) if (rows[i].nim == cNim) { idx = (int)i; break; }
                    }
                    if (idx >= 0) {
                        tempKM.printf("%s,%s,%s,%s,%s,%s\n", cId.c_str(), kode_mk.c_str(), kelasStr.c_str(),
                            cNim.c_str(), rows[idx].sitIn.c_str(), rows[idx].statusFp.c_str());
                        done[idx] = true;
                    } else {
                        tempKM.println(line);  // kelas lain / nim lain -> pertahankan
                    }
                }
                oldKM.close();
            }
            for (size_t i = 0; i < rows.size(); i++) {
                if (!done[i]) {
                    tempKM.printf("%d,%s,%s,%s,%s,%s\n", nextId++, kode_mk.c_str(), kelasStr.c_str(),
                        rows[i].nim.c_str(), rows[i].sitIn.c_str(), rows[i].statusFp.c_str());
                }
            }
            tempKM.close();
            SD.remove("/kelas_mahasiswa.csv");
            SD.rename("/temp_km.csv", "/kelas_mahasiswa.csv");
            Serial.println(F("[DPK] kelas_mahasiswa.csv di-upsert"));
        } else {
            if (oldKM) oldKM.close();
            Serial.println(F("[DPK] ERROR: gagal buka temp_km.csv"));
        }
    }

    // ========== WAJIB: update cache rasio agar UI LCD ter-update ==========
    calculateEnrollRatioCache();

    Serial.println(F("[DPK] Selesai - data mahasiswa kelas tersimpan"));
    syncStatusFeature = true;   // FASE 3 SUKSES -> ikon centang di state fitur
    return true;
}

// ========== PRE-CALCULATE ENROLL RATIO CACHE ==========
// Menghitung rasio enrolled/total per kelas dari kelas_mahasiswa.csv dan simpan ke cache
void APIManager::calculateEnrollRatioCache() {
    Serial.println(F("[RATIO_CACHE] Calculating enrollment ratio cache..."));

    File inFile = SD.open("/kelas_mahasiswa.csv", FILE_READ);
    if (!inFile) {
        Serial.println(F("[RATIO_CACHE] Cannot open kelas_mahasiswa.csv"));
        return;
    }

    // Struktur cache: kode_kelas|kelas|enrolled|total
    // Format per baris: EL1200|6|3|43
    const int MAX_CLASSES = 20;
    struct ClassRatio {
        char kode[12];
        char kelas[4];
        int enrolled;
        int total;
    };
    static ClassRatio ratios[MAX_CLASSES];
    int ratioCount = 0;

    // Reset
    for (int i = 0; i < MAX_CLASSES; i++) {
        ratios[i].kode[0] = '\0';
        ratios[i].kelas[0] = '\0';
        ratios[i].enrolled = 0;
        ratios[i].total = 0;
    }

    // Abaikan header
    if (inFile.available()) {
        inFile.readStringUntil('\n');
    }

    // Single-pass: hitung enrolled dan total
    while (inFile.available() && ratioCount < MAX_CLASSES) {
        String line = inFile.readStringUntil('\n');
        line.trim();
        if (line.length() < 10) continue;

        // ========== DEBUG: Tampilkan baris mentah ==========
        Serial.printf_P(PSTR("[DEBUG-PARSE] Raw Line: %s\n"), line.c_str());

        // ========== EKSTRAKSI ROBUST (dari belakang) ==========
        // Format: id,kode_kelas,kelas,nim,sit_in,status_fingerprint
        // Ambil dari belakang untuk status_fp (kolom terakhir)
        int lastComma = line.lastIndexOf(',');
        String statusFp = (lastComma > 0) ? line.substring(lastComma + 1) : "";

        // Untuk kode_kelas dan kelas, tetap dari depan
        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);

        if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

        String kodeKelas = line.substring(p1 + 1, p2);
        String kelas = line.substring(p2 + 1, p3);

        // ========== AGGRESSIVE TRIM (Hapus hidden characters) ==========
        kodeKelas.trim();
        kodeKelas.replace("\r", "");
        kodeKelas.replace("\n", "");

        kelas.trim();
        kelas.replace("\r", "");
        kelas.replace("\n", "");

        statusFp.trim();
        statusFp.replace("\r", "");
        statusFp.replace("\n", "");
        statusFp.replace(" ", "");

        // ========== DEBUG: Tampilkan setelah ekstraksi ==========
        Serial.printf_P(PSTR("[DEBUG-PARSE] Kode: %s | Kls: %s | FP Extract: '%s'\n"),
            kodeKelas.c_str(), kelas.c_str(), statusFp.c_str());

        // Cari atau buat entri
        int idx = -1;
        for (int i = 0; i < ratioCount; i++) {
            if (strcmp(ratios[i].kode, kodeKelas.c_str()) == 0 &&
                strcmp(ratios[i].kelas, kelas.c_str()) == 0) {
                idx = i;
                break;
            }
        }

        if (idx < 0 && ratioCount < MAX_CLASSES) {
            idx = ratioCount++;
            strncpy(ratios[idx].kode, kodeKelas.c_str(), sizeof(ratios[idx].kode) - 1);
            strncpy(ratios[idx].kelas, kelas.c_str(), sizeof(ratios[idx].kelas) - 1);
        }

        if (idx >= 0) {
            ratios[idx].total++;
            if (statusFp.equalsIgnoreCase("true") || statusFp == "1") {
                ratios[idx].enrolled++;
            }
        }
    }
    inFile.close();

    // Tulis ke cache file
    File cacheFile = SD.open("/ratio_cache.txt", FILE_WRITE);
    if (cacheFile) {
        for (int i = 0; i < ratioCount; i++) {
            cacheFile.printf("%s|%s|%d|%d\n",
                ratios[i].kode, ratios[i].kelas, ratios[i].enrolled, ratios[i].total);
            Serial.printf_P(PSTR("[RATIO_CACHE] %s|%s|%d|%d\n"),
                ratios[i].kode, ratios[i].kelas, ratios[i].enrolled, ratios[i].total);
        }
        cacheFile.close();
        Serial.println(F("[RATIO_CACHE] Cache written successfully"));
    } else {
        Serial.println(F("[RATIO_CACHE] Failed to write cache"));
    }
}

// ========== LOAD ENROLL RATIO FROM CACHE ==========
// Membaca rasio enrolled dari cache (O(1) lookup)
bool APIManager::getEnrollRatio(const char* kodeKelas, const char* kelas, int& enrolled, int& total) {
    File cacheFile = SD.open("/ratio_cache.txt", FILE_READ);
    if (!cacheFile) return false;

    while (cacheFile.available()) {
        String line = cacheFile.readStringUntil('\n');
        line.trim();
        if (line.length() < 5) continue;

        int p1 = line.indexOf('|');
        int p2 = line.indexOf('|', p1 + 1);
        int p3 = line.indexOf('|', p2 + 1);

        if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

        String kode = line.substring(0, p1);
        String kls = line.substring(p1 + 1, p2);
        String enr = line.substring(p2 + 1, p3);
        String tot = line.substring(p3 + 1);

        kode.trim();
        kls.trim();

        if (kode == kodeKelas && kls == kelas) {
            enrolled = enr.toInt();
            total = tot.toInt();
            cacheFile.close();
            return true;
        }
    }

    cacheFile.close();
    return false;
}

// ========== LANGKAH 2: SEND LOGIN ACKNOWLEDGE ==========
// Revisi: menggunakan nip dan pin sesuai request backend terbaru
void APIManager::sendLoginAcknowledge(String nip, String pin, bool fetchDashboard) {
    Serial.println(F("[API-Langkah2] ============================"));
    Serial.println(F("[API-Langkah2] Sending login acknowledge..."));
    Serial.printf_P(PSTR("[API-Langkah2] NIP: %s\n"), nip.c_str());
    Serial.printf_P(PSTR("[API-Langkah2] PIN: %s\n"), pin.c_str());

    // TUGAS 3: PASTIKAN LCD UPDATE - Cek WiFi terlebih dahulu sebelum LCD clear
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[API-Langkah2] WiFi not connected - skip API call"));
        // Tetap tampilkan pesan tapi tanpa API
        lcd.clear();
        lcd.printLine(0, MSG_API_LOGIN);
        lcd.printLine(1, F("WiFi Tidak Ada"));
        lcd.printLine(2, F("Mode Offline"));
        delay(1500);
        return;
    }

    // TUGAS 1: TAMPILAN LCD LOADING (setelah cek WiFi)
    // TUGAS 3: PASTIKAN LCD UPDATE - Tampilkan dulu sebelum HTTP request
    lcd.clear();
    lcd.printLine(0, MSG_API_LOGIN);
    lcd.printLine(1, MSG_API_LOADING);
    lcd.printLine(2, MSG_API_WAIT);
    Serial.println(F("[API-Langkah2] LCD updated - Showing 'Menghubungi API...'"));

    // Beri waktu sejenak agar LCD terlihat (500ms)
    delay(500);

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    String url = String(API_BASE_URL) + "/api/device/login";

    Serial.println("[API-Langkah2] URL: " + url);

    http.begin(secureClient, url);

    // Add headers
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("x-device-code", DEVICE_CODE);
    http.addHeader("x-device-secret", DEVICE_SECRET);

    // Create JSON body dengan nip dan pin sesuai revisi backend
    JsonDocument doc;
    doc["nip"] = nip;
    doc["pin"] = pin;

    String requestBody;
    serializeJson(doc, requestBody);

    Serial.println("[API-Langkah2] Request body: " + requestBody);

    int httpCode = http.POST(requestBody);
    Serial.println("[API-Langkah2] HTTP Code: " + String(httpCode));

    if (httpCode > 0) {
        String payload = http.getString();

        // TUGAS 3: OUTPUT KE SERIAL MONITOR - tetap tampilkan response payload
        Serial.println("[API-Langkah2] Response Payload:");
        Serial.println(payload);
        Serial.println("[API-Langkah2] ==============================");

        // ========== TUGAS 1: EKSTRAK TOKEN DARI LANGKAH 2 ==========
        String token = "";
        JsonDocument docAuth;
        DeserializationError error = deserializeJson(docAuth, payload);

        if (!error && docAuth["token"].is<const char*>()) {
            token = docAuth["token"].as<const char*>();
            globalJwtToken = token;  // TUGAS 2: Simpan ke global variable untuk Push Queue
            Serial.println("[API-Langkah2] Token berhasil didapatkan!: " + token);

            // ========== BYPASS DASHBOARD UNTUK ADMIN ==========
            // Admin tidak perlu data dashboard (dosen/kelas/jadwal); lewati Langkah 3.
            if (fetchDashboard) {
                Serial.println("[API-Langkah2] Memanggil fetchDashboardData...");
                fetchDashboardData(token);  // Langkah 3 (hanya untuk Dosen)
            } else {
                Serial.println("[API-Langkah2] Admin login - SKIP fetchDashboardData");
            }
        } else {
            Serial.println("[API-Langkah2] Gagal mengekstrak token dari response.");
            if (error) {
                Serial.println("[API-Langkah2] JSON Error: " + String(error.c_str()));
            }
        }
        // ============================================================

        // Log ke system
        if (httpCode == 200) {
            logSystemActivity("LOGIN_ACK", "Berhasil kirim acknowledge untuk NIP: " + nip);
        } else {
            logSystemActivity("LOGIN_ACK_ERROR", "HTTP: " + String(httpCode));
        }
    } else {
        Serial.printf("[API-Langkah2] Request failed: %s\n", http.errorToString(httpCode).c_str());
        logSystemActivity("LOGIN_ACK_ERROR", http.errorToString(httpCode));
    }

    http.end();
}

// ========== TUGAS 1: PUSH QUEUE HELPER FUNCTIONS ==========
// Helper untuk membaca kolom CSV (indeks 0-based)
String APIManager::getCsvColumn(String line, int index) {
    int currentIndex = 0;
    int start = 0;
    for (int i = 0; i <= index; i++) {
        int comma = line.indexOf(',', start);
        if (i == index) {
            String result;
            if (comma > 0) {
                result = line.substring(start, comma);
            } else {
                result = line.substring(start);
            }
            result.trim();
            return result;
        }
        start = comma + 1;
        currentIndex++;
    }
    return "";
}

// ========== INDEX-BASED FINGERPRINT SEARCH (OPTIMIZED) ==========
// Format fp_index_mahasiswa.csv: SlotID,NIM
// Format fingerprint_mahasiswa.csv: SlotID,Hex_Template

String findFingerprintByNimIndex(String nim) {
    String resultHex = "";
    String foundNim = "";

    // ========== STEP 1: CARI SLOT ID DI INDEX FILE ==========
    // Format: SlotID,NIM
    File idxFile = SD.open("/fp_index_mahasiswa.csv", FILE_READ);
    if (!idxFile) {
        Serial.println(F("[INDEX_SEARCH] File fp_index_mahasiswa.csv tidak ditemukan"));
        // Fallback ke cara lama
        return findFingerprintByNimOld(nim);
    }

    if (idxFile.available()) idxFile.readStringUntil('\n'); // Skip header

    int targetSlot = -1;
    while (idxFile.available()) {
        String idxLine = idxFile.readStringUntil('\n');
        idxLine.trim();
        if (idxLine.length() == 0) continue;

        // Format: SlotID,NIM
        int p1 = idxLine.indexOf(',');
        if (p1 <= 0) continue;

        String slotStr = idxLine.substring(0, p1);
        String idxNim = idxLine.substring(p1 + 1);
        idxNim.trim();

        Serial.printf_P(PSTR("[INDEX_SEARCH] Cek idx: Slot=%s, NIM=%s\n"), slotStr.c_str(), idxNim.c_str());

        if (idxNim == nim) {
            targetSlot = slotStr.toInt();
            foundNim = nim;
            Serial.printf_P(PSTR("[INDEX_SEARCH] Ditemukan SlotID: %d\n"), targetSlot);
            break;
        }
    }
    idxFile.close();

    if (targetSlot <= 0) {
        Serial.printf_P(PSTR("[INDEX_SEARCH] NIM tidak ditemukan di index: %s\n"), nim.c_str());
        return "";
    }

    // ========== STEP 2: CARI HEX DI FINGERPRINT FILE BERDASARKAN SLOT ID ==========
    // Format: id,user_id,role,data_jari,created_at (5 kolom)
    File fpFile = SD.open("/fingerprint_mahasiswa.csv", FILE_READ);
    if (!fpFile) {
        Serial.println(F("[INDEX_SEARCH] File fingerprint_mahasiswa.csv tidak ditemukan"));
        return "";
    }

    if (fpFile.available()) fpFile.readStringUntil('\n'); // Skip header

    while (fpFile.available()) {
        String fpLine = fpFile.readStringUntil('\n');
        fpLine.trim();
        fpLine.replace("\r", "");
        if (fpLine.length() < 10) continue;

        // ========== EKSTRAKSI SLOT ID (angka sebelum koma pertama) ==========
        int firstComma = fpLine.indexOf(',');
        if (firstComma <= 0) continue;

        String slotStr = fpLine.substring(0, firstComma);
        slotStr.trim();
        int csvSlot = slotStr.toInt();

        Serial.printf_P(PSTR("[INDEX_SEARCH] Cek line: '%s' -> Slot=%d (target=%d)\n"), fpLine.substring(0, 30).c_str(), csvSlot, targetSlot);

        if (csvSlot == targetSlot) {
            // ========== EKSTRAKSI HEX (kolom ke-4 = index 3) ==========
            // Format: id,user_id,role,data_jari,created_at
            //Kolom:  0 ,    1     ,  2  ,     3      ,    4
            resultHex = apiManager.getCsvColumn(fpLine, 3);
            resultHex.trim();

            Serial.printf_P(PSTR("[INDEX_SEARCH] Ditemukan! Hex: %d bytes\n"), resultHex.length());
            break;
        }
    }
    fpFile.close();

    return resultHex;
}

// ========== FALLBACK: CARA LAMA (LINEAR SEARCH) ==========
String findFingerprintByNimOld(String nim) {
    String hexData = "";
    String nimFound = "";

    File fpFile = SD.open("/fingerprint_mahasiswa.csv", FILE_READ);
    if (!fpFile) {
        Serial.println(F("[OLD_SEARCH] File fingerprint_mahasiswa.csv tidak ditemukan"));
        return "";
    }

    if (fpFile.available()) fpFile.readStringUntil('\n'); // Skip header
    while (fpFile.available()) {
        String fpLine = fpFile.readStringUntil('\n');
        fpLine.trim();
        if (fpLine.length() < 10) continue;

        // Format: id,user_id,role,data_jari,created_at
        String csvNim = apiManager.getCsvColumn(fpLine, 1);
        if (csvNim == nim) {
            hexData = apiManager.getCsvColumn(fpLine, 3);  // data_jari
            nimFound = csvNim;
            break;
        }
    }
    fpFile.close();

    return hexData;
}

// TUGAS 1: Add entry to push queue with creator_id
void addToPushQueue(String actionType, String referenceId, String creatorId) {
    // Get next auto-increment queue_id
    int nextQueueId = apiManager.getNextAutoId("/push_queue.csv");

    // Get timestamp
    String timestamp = apiManager.isTimeSynced() ? getFormattedTime() : "REG";

    // Append to file: queue_id,action_type,data,creator_id,created_at
    File file = SD.open("/push_queue.csv", FILE_APPEND);
    if (file) {
        file.printf("%d,%s,%s,%s,%s\n", nextQueueId, actionType.c_str(), referenceId.c_str(), creatorId.c_str(), timestamp.c_str());
        file.close();
        Serial.printf("[PUSH_QUEUE] Added: ID=%d, Action=%s, Ref=%s\n", nextQueueId, actionType.c_str(), referenceId.c_str());
        logSystemActivity("QUEUE_IN", "Masuk antrian: " + actionType + " (" + referenceId + ")");
    } else {
        Serial.println("[PUSH_QUEUE] ERROR - Gagal membuka file untuk append!");
    }
}

// TUGAS 1: Remove entry from push queue
void removeFromPushQueue(String targetQueueId) {
    // Buka file asli (READ) dan file sementara (WRITE)
    File readFile = SD.open("/push_queue.csv", FILE_READ);
    if (!readFile) {
        Serial.println("[PUSH_QUEUE] ERROR - Gagal membuka file untuk baca!");
        return;
    }

    File tempFile = SD.open("/temp_queue.csv", FILE_WRITE);
    if (!tempFile) {
        Serial.println("[PUSH_QUEUE] ERROR - Gagal membuka file temporary!");
        readFile.close();
        return;
    }

    // Skip header
    if (readFile.available()) {
        String header = readFile.readStringUntil('\n');
        tempFile.println(header);
    }

    // Baca baris per baris, tulis ke temp jika queue_id tidak sama
    while (readFile.available()) {
        String line = readFile.readStringUntil('\n');
        line.trim();
        if (line.length() < 3) continue;  // Skip empty lines

        String targetQueueId = apiManager.getCsvColumn(line, 0);

        // Tulis ke temp jika queue_id TIDAK SAMA dengan target
        if (targetQueueId != targetQueueId) {
            tempFile.println(line);
        } else {
            Serial.printf("[PUSH_QUEUE] Menghapus queue ID: %s\n", targetQueueId.c_str());
        }
    }

    readFile.close();
    tempFile.close();

    // Hapus file asli dan rename temp ke asli
    SD.remove("/push_queue.csv");
    SD.rename("/temp_queue.csv", "/push_queue.csv");
    Serial.println("[PUSH_QUEUE] File antrian diperbarui");
}

// ========== TUGAS 1: SIMPAN KE PENDING PUSH QUEUE ==========
// Simpan fingerprint mahasiswa ke file pending_push.csv untuk batch push later
// Format: id,nim,hex_data,status,created_at
void addToPendingPushQueue(String nim, String hexData) {
    // Get next auto-increment id
    int nextId = apiManager.getNextAutoId("/pending_push.csv");

    // Get timestamp
    String timestamp = apiManager.isTimeSynced() ? getFormattedTime() : "REG";

    // ========== TUGAS 1: PERBAIKAN INISIALISASI FILE ==========
    // Coba append dulu, jika gagal coba buat baru
    File file = SD.open("/pending_push.csv", FILE_APPEND);
    if (!file) {
        file = SD.open("/pending_push.csv", FILE_WRITE);
    }

    if (file) {
        // Buat header jika file kosong atau ukuran 0
        if (file.size() == 0) {
            file.println("id,nim,hex_data,status,created_at");
        }
        file.printf("%d,%s,%s,pending,%s\n", nextId, nim.c_str(), hexData.c_str(), timestamp.c_str());
        file.close();
        Serial.printf("[PENDING_PUSH] Added NIM: %s to queue, ID: %d\n", nim.c_str(), nextId);
        logSystemActivity("PENDING_PUSH", "NIM: " + nim + " queued");
    } else {
        Serial.println(F("[ERROR] Gagal membuka pending_push.csv"));
        logSystemActivity("PENDING_PUSH_ERROR", "Failed to open file");
    }
}

// ========== TUGAS 4: QUEUE MAHASISWA DUAL FINGERPRINT (2 JARI) ==========
// Simpan mahasiswa 2 jari ke queue untuk batch push via /api/device/sync
// Format CSV: mahasiswa,NIM,fp_1_mhsw,fp_2_mhsw
// Untuk differentiate dari format lama (id,nim,hex,status,timestamp)
bool addMahasiswaToQueue(String nim, String fp1, String fp2) {
    // Gunakan file terpisah: pending_mhsw_queue.csv untuk mahasiswa 2 jari
    const char* filename = "/pending_mhsw_queue.csv";

    File file = SD.open(filename, FILE_APPEND);
    if (!file) {
        file = SD.open(filename, FILE_WRITE);
    }

    if (file) {
        // Buat header jika file kosong atau ukuran 0
        if (file.size() == 0) {
            file.println("type,nim,fp_1_mhsw,fp_2_mhsw");
        }
        // Format: mahasiswa,NIM,fp_1_mhsw,fp_2_mhsw
        file.printf("mahasiswa,%s,%s,%s\n", nim.c_str(), fp1.c_str(), fp2.c_str());
        file.close();
        Serial.printf("[QUEUE_MHSW] Added: NIM=%s, FP1=%d bytes, FP2=%d bytes\n",
            nim.c_str(), fp1.length(), fp2.length());
        logSystemActivity("QUEUE_MHSW", "NIM: " + nim + " queued (2 FP)");
        return true;
    } else {
        Serial.println(F("[ERROR] Gagal membuka pending_mhsw_queue.csv"));
        logSystemActivity("QUEUE_MHSW_ERROR", "Failed to open file");
        return false;
    }
}

// ========== PROSES QUEUE MAHASISWA 2 JARI -> POST /api/device/enroll-mahasiswa ==========
// Format JSON: {"students": [{"nim": ..., "nama_mhsw": "", "fp_1_mhsw": ..., "fp_2_mhsw": ...}]}
// ========== SIDECAR FP_2 MAHASISWA ==========
#define FP2_SIDECAR_FILE "/fp2_mahasiswa.csv"

void clearFp2Sidecar() {
    if (SD.exists(FP2_SIDECAR_FILE)) {
        SD.remove(FP2_SIDECAR_FILE);
        Serial.println(F("[FP2] Sidecar fp_2 dibersihkan"));
    }
}

void appendFp2Sidecar(const String& nim, const String& fp2) {
    if (nim.length() == 0 || fp2.length() < 100) return;  // skip data tidak valid
    File f = SD.open(FP2_SIDECAR_FILE, FILE_APPEND);
    if (!f) {
        Serial.println(F("[FP2] ERROR: gagal buka sidecar utk append"));
        return;
    }
    // Format: nim,fp_2  (1 baris per mahasiswa)
    f.print(nim);
    f.print(',');
    f.println(fp2);
    f.close();
}

String readFp2Sidecar(const String& nim) {
    File f = SD.open(FP2_SIDECAR_FILE, FILE_READ);
    if (!f) return String("");
    String result = "";
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() < 3) continue;
        int p1 = line.indexOf(',');
        if (p1 <= 0) continue;
        String csvNim = line.substring(0, p1);
        csvNim.trim();
        if (csvNim == nim) {
            result = line.substring(p1 + 1);
            result.trim();
            result.replace("\r", "");
            break;
        }
    }
    f.close();
    return result;
}

// ========== UPSERT fp_2 ke /fp2_mahasiswa.csv (format: NIM,fp_2_hex) ==========
// Tambah baris baru bila NIM belum ada; timpa bila sudah ada. Memakai temp file
// (streaming) karena tiap baris ~3KB hex -> tidak aman ditampung di String RAM.
void saveFp2ToCSV(const String& nim, const String& hex) {
    if (nim.length() == 0 || hex.length() < 100) {
        Serial.println(F("[FP2] saveFp2ToCSV: data tidak valid, skip"));
        return;
    }

    // File belum ada -> buat baru langsung (kasus paling umum saat pull).
    if (!SD.exists(FP2_SIDECAR_FILE)) {
        File f = SD.open(FP2_SIDECAR_FILE, FILE_WRITE);
        if (f) {
            f.print(nim); f.print(','); f.println(hex);
            f.close();
            Serial.printf_P(PSTR("[FP2] Simpan baru: %s\n"), nim.c_str());
        }
        return;
    }

    // File ada -> upsert via temp file (memory-safe, tanpa String raksasa).
    const char* TMP = "/temp_fp2.csv";
    File in  = SD.open(FP2_SIDECAR_FILE, FILE_READ);
    File out = SD.open(TMP, FILE_WRITE);
    if (!in || !out) {
        if (in)  in.close();
        if (out) out.close();
        Serial.println(F("[FP2] ERROR: gagal buka file utk upsert"));
        return;
    }

    bool replaced = false;
    while (in.available()) {
        String line = in.readStringUntil('\n');
        line.trim(); line.replace("\r", "");
        if (line.length() < 3) continue;
        int c = line.indexOf(',');
        if (c <= 0) continue;
        String csvNim = line.substring(0, c);
        csvNim.trim();
        if (csvNim == nim) {
            out.print(nim); out.print(','); out.println(hex);   // TIMPA baris lama
            replaced = true;
        } else {
            out.println(line);                                  // pertahankan baris lain
        }
    }
    in.close();
    if (!replaced) { out.print(nim); out.print(','); out.println(hex); }  // TAMBAH baru
    out.close();

    SD.remove(FP2_SIDECAR_FILE);
    SD.rename(TMP, FP2_SIDECAR_FILE);
    Serial.printf_P(PSTR("[FP2] Upsert %s (%s)\n"), nim.c_str(), replaced ? "timpa" : "tambah");
}

void processMahasiswaEnrollmentQueue() {
    Serial.println(F("[QUEUE_MHSW] ========== START BATCH PUSH (2 JARI) =========="));

    const char* filename = "/pending_mhsw_queue.csv";
    const char* tempfile = "/temp_mhsw_queue.csv";

    File checkFile = SD.open(filename, FILE_READ);
    if (!checkFile) {
        Serial.println(F("[QUEUE_MHSW] File antrian tidak ada"));
        return;
    }
    if (!checkFile.available() || checkFile.size() == 0) {
        Serial.println(F("[QUEUE_MHSW] File antrian kosong"));
        checkFile.close();
        return;
    }
    checkFile.close();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[QUEUE_MHSW] WiFi tidak terhubung - tunda push"));
        return;
    }

    // ========== LOOP MULTI-BATCH: proses 3 data per iterasi sampai antrian habis ==========
    // SAFE-DELETE: tiap iterasi membaca file, ambil maks PUSH_BATCH_SIZE baris untuk
    // di-push, dan STREAMING sisa baris (#4 dst) ke temp file. Hanya jika POST sukses
    // temp di-rename jadi file utama. Jadi data baru yg belum masuk payload TIDAK akan
    // hilang. Sisa baris ditulis ke temp file (bukan String RAM) karena tiap baris ~6KB
    // hex -> remainder besar di RAM bisa fragmentasi heap / LoadProhibited.
    int totalPushed = 0;
    int loopGuard = 0;
    const int MAX_LOOPS = 100;  // proteksi runaway

    while (loopGuard++ < MAX_LOOPS) {
        String batchNim[PUSH_BATCH_SIZE];
        String batchFp1[PUSH_BATCH_SIZE];
        String batchFp2[PUSH_BATCH_SIZE];
        int batchCount = 0;
        int remainderCount = 0;   // jumlah baris valid yg ditunda ke iterasi berikut

        File file = SD.open(filename, FILE_READ);
        if (!file) { Serial.println(F("[QUEUE_MHSW] Gagal buka file")); break; }

        // Header: baca baris pertama, lalu tulis ke temp (jaga struktur)
        String header = "";
        if (file.available()) header = file.readStringUntil('\n');
        header.trim(); header.replace("\r", "");
        if (header.length() == 0) header = "type,nim,fp_1_mhsw,fp_2_mhsw";

        File temp = SD.open(tempfile, FILE_WRITE);
        if (!temp) { Serial.println(F("[QUEUE_MHSW] Gagal buka temp")); file.close(); break; }
        temp.println(header);

        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            line.replace("\r", "");
            if (line.length() == 0) continue;
            if (!line.startsWith("mahasiswa,")) continue;  // drop baris non-mahasiswa

            if (batchCount < PUSH_BATCH_SIZE) {
                // ----- Masuk window batch: parse utk payload -----
                int p1 = line.indexOf(',');          // setelah "mahasiswa"
                int p2 = line.indexOf(',', p1 + 1);   // setelah NIM
                int p3 = line.indexOf(',', p2 + 1);   // setelah fp_1
                if (p1 > 0 && p2 > p1 && p3 > p2) {
                    String nim = line.substring(p1 + 1, p2);
                    String fp1 = line.substring(p2 + 1, p3);
                    String fp2 = line.substring(p3 + 1);
                    nim.trim(); fp1.trim(); fp2.trim();
                    if (nim.length() >= 5 && fp1.length() > 100 && fp2.length() > 100) {
                        batchNim[batchCount] = nim;
                        batchFp1[batchCount] = trimHex(fp1);
                        batchFp2[batchCount] = trimHex(fp2);
                        batchCount++;
                        Serial.printf_P(PSTR("[QUEUE_MHSW] Batch[%d]: NIM=%s\n"), batchCount, nim.c_str());
                    } else {
                        Serial.println(F("[QUEUE_MHSW] Skip baris korup (panjang tidak valid)"));
                        // baris korup -> di-drop (tidak masuk batch maupun remainder)
                    }
                } else {
                    Serial.println(F("[QUEUE_MHSW] Skip baris korup (koma tidak valid)"));
                }
            } else {
                // ----- Di luar window batch: STREAMING ke temp utk iterasi berikut -----
                temp.println(line);
                remainderCount++;
            }
        }
        file.close();
        temp.close();

        if (batchCount == 0) {
            // Tidak ada data valid lagi -> selesai. temp (header saja) buang.
            SD.remove(tempfile);
            Serial.println(F("[QUEUE_MHSW] Tidak ada data valid tersisa"));
            break;
        }

        // ========== BUILD JSON PAYLOAD utk batch ini ==========
        JsonDocument doc;
        JsonArray students = doc["students"].to<JsonArray>();
        for (int i = 0; i < batchCount; i++) {
            JsonObject mhs = students.add<JsonObject>();
            mhs["nim"]       = batchNim[i];
            mhs["nama_mhsw"] = "";  // server akan lengkapi jika NIM sudah terdaftar
            mhs["fp_1_mhsw"] = batchFp1[i];
            mhs["fp_2_mhsw"] = batchFp2[i];
        }
        String payload;
        serializeJson(doc, payload);
        doc.clear();
        Serial.printf_P(PSTR("[QUEUE_MHSW] Push batch: %d data, %d bytes (sisa antrian: %d)\n"),
            batchCount, payload.length(), remainderCount);

        // ========== HTTP POST ==========
        WiFiClientSecure secureClient;
        secureClient.setInsecure();
        HTTPClient http;
        String endpoint = String(API_BASE_URL) + "/api/device/enroll-mahasiswa";
        http.begin(secureClient, endpoint);
        http.setTimeout(15000);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-device-code", DEVICE_CODE);
        http.addHeader("x-device-secret", DEVICE_SECRET);
        if (globalJwtToken.length() > 0) {
            http.addHeader("Authorization", "Bearer " + globalJwtToken);
        }


        Serial.println("Payload: " + payload);
        int httpCode = http.POST(payload);
        String resp = http.getString();
        Serial.println("[QUEUE_MHSW] Server Response: " + resp);
        http.end();

        if (httpCode == 200 || httpCode == 201) {
            // SUKSES: commit -> file utama = sisa baris (temp). Batch yg sukses lenyap,
            // baris #4+ AMAN karena sudah ada di temp.
            SD.remove(filename);
            SD.rename(tempfile, filename);
            totalPushed += batchCount;
            Serial.printf_P(PSTR("[QUEUE_MHSW] BERHASIL (HTTP %d) - commit, sisa %d\n"),
                httpCode, remainderCount);

            if (remainderCount == 0) {
                logSystemActivity("QUEUE_MHSW_SUCCESS", "Total " + String(totalPushed));
                break;  // antrian habis
            }
            // masih ada sisa -> loop lagi (iterasi berikut baca file yg sudah dipangkas)
        } else {
            // GAGAL: JANGAN commit. Buang temp, biarkan file utama UTUH (semua data aman).
            SD.remove(tempfile);
            Serial.printf_P(PSTR("[QUEUE_MHSW] GAGAL (HTTP %d) - antrian utuh, stop\n"), httpCode);
            logSystemActivity("QUEUE_MHSW_FAILED", "HTTP " + String(httpCode));
            break;  // stop; coba lagi nanti
        }
    }

    Serial.printf_P(PSTR("[QUEUE_MHSW] ===== SELESAI: total %d data ter-push =====\n"), totalPushed);
}

// ========== HELPER: cari nama mahasiswa dari mahasiswa.csv berdasarkan NIM ==========
// Untuk kunci "nama_mhsw" pada payload /api/device/enroll-mahasiswa
static String findNamaByNim(const String& nim) {
    File f = SD.open("/mahasiswa.csv", FILE_READ);
    if (!f) return "Mahasiswa Baru";
    if (f.available()) f.readStringUntil('\n');  // skip header
    String result = "";
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        String csvNim = apiManager.getCsvColumn(line, 0);
        if (csvNim == nim) {
            result = apiManager.getCsvColumn(line, 1);
            result.trim(); result.replace("\r", "");
            break;
        }
    }
    f.close();
    return result.length() > 0 ? result : "Mahasiswa Baru";
}

// ========== TUGAS 3: PROSES BATCH PUSH DARI PENDING QUEUE ==========
// Baca pending_push.csv, kirim ke server per-baris, hapus yang berhasil saja
bool processPendingPushQueue() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[PENDING_PUSH] WiFi not connected");
        return false;
    }

    File file = SD.open("/pending_push.csv", FILE_READ);
    if (!file) {
        Serial.println("[PENDING_PUSH] File tidak ada atau kosong");
        return false;
    }

    // Skip header
    if (file.available()) file.readStringUntil('\n');

    // Struktur untuk menyimpan data pending
    struct PendingData {
        String id;
        String nim;
        String hexData;
        bool sent;  // Flag untuk menandai apakah berhasil dikirim
    };
    std::vector<PendingData> allData;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() < 10) continue;

        // Parse: id(0), nim(1), hex_data(2), status(3), created_at(4)
        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);

        if (p1 > 0 && p2 > 0 && p3 > 0) {
            PendingData data;
            data.id = line.substring(0, p1);
            data.nim = line.substring(p1 + 1, p2);
            data.hexData = line.substring(p2 + 1, p3);
            data.hexData.trim();
            data.sent = false;
            allData.push_back(data);
        }
    }
    file.close();

    if (allData.size() == 0) {
        Serial.println("[PENDING_PUSH] Tidak ada data untuk di-push");
        return false;
    }

    Serial.printf("[PENDING_PUSH] Found %d items to push\n", allData.size());

    // ========== TUGAS 3: KIRIM PER-BARIS (ONE BY ONE) ==========
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    String url = "https://presensi-elektronik-ta-2526-016.vercel.app/api/device/enroll-mahasiswa";
    int successCount = 0;

    for (size_t i = 0; i < allData.size(); i++) {
        HTTPClient http;
        http.begin(secureClient, url);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-device-code", DEVICE_CODE);
        http.addHeader("x-device-secret", DEVICE_SECRET);

        if (globalJwtToken.length() > 0) {
            http.addHeader("Authorization", "Bearer " + globalJwtToken);
        }

        // Build payload per-item (skema baru /enroll-mahasiswa: nim, nama_mhsw, template_data)
        String cleanHex = trimHex(allData[i].hexData);
        String namaMhs = findNamaByNim(allData[i].nim);
        String payload = "{\"mahasiswa\":[{\"nim\":\"" + allData[i].nim +
                         "\",\"nama_mhsw\":\"" + namaMhs +
                         "\",\"template_data\":\"" + cleanHex + "\"}]}";

        Serial.printf("[PENDING_PUSH] Sending item %d/%d: NIM=%s\n", i+1, allData.size(), allData[i].nim.c_str());

        int httpCode = http.POST(payload);

        if (httpCode == 200 || httpCode == 201) {
            Serial.printf("[PENDING_PUSH] BERHASIL push NIM: %s (HTTP %d)\n", allData[i].nim.c_str(), httpCode);
            allData[i].sent = true;
            successCount++;
        } else {
            String response = http.getString();
            Serial.printf("[PENDING_PUSH] GAGAL push NIM: %s (HTTP %d)\n", allData[i].nim.c_str(), httpCode);
            allData[i].sent = false;
        }

        http.end();

        // Small delay antara request untuk menghindari rate limiting
        delay(100);
    }

    // ========== TUGAS 3: TULIS ULANG FILE DENGAN DATA YANG GAGAL SAJA ==========
    if (successCount > 0) {
        Serial.printf("[PENDING_PUSH] %d/%d berhasil, menulis ulang file...\n", successCount, allData.size());

        File writeFile = SD.open("/pending_push.csv", FILE_WRITE);
        if (writeFile) {
            writeFile.println("id,nim,hex_data,status,created_at");

            for (size_t i = 0; i < allData.size(); i++) {
                if (!allData[i].sent) {
                    // Tulis kembali data yang gagal
                    writeFile.printf("%s,%s,%s,pending,\n", allData[i].id.c_str(), allData[i].nim.c_str(), allData[i].hexData.c_str());
                }
            }
            writeFile.close();

            if (successCount == allData.size()) {
                // Semua berhasil - hapus file
                SD.remove("/pending_push.csv");
                Serial.println("[PENDING_PUSH] Semua data berhasil dikirim, queue cleared");
            }
        }
    }

    if (successCount > 0) {
        logSystemActivity("BATCH_PUSH", "Sukses: " + String(successCount) + "/" + String(allData.size()));
        return true;
    } else {
        logSystemActivity("BATCH_PUSH_ERROR", "0/" + String(allData.size()));
        return false;
    }
}

// ========== TUGAS 2: PUSH SINGLE FINGERPRINT (for Registration) ==========
// Kirim fingerprint tunggal ke server (bukan batch queue)
// Menggunakan Raw Hex String langsung tanpa Base64 (konsisten dengan PUSH_FP_MANUAL)
bool pushSingleFingerprint(String nim, String hexData) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[PUSH_REG] WiFi not connected");
        return false;
    }

    if (nim.length() == 0 || hexData.length() == 0) {
        Serial.println("[PUSH_REG] Error: NIM atau Hex kosong");
        return false;
    }

    Serial.println("[PUSH_REG] ============================");
    Serial.printf("[PUSH_REG] Sending fingerprint for NIM: %s\n", nim.c_str());

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    // ========== TUGAS 1: USE SAME ENDPOINT AS PUSH_FP_MANUAL ==========
    // Gunakan endpoint yang sama dengan processPushQueue
    String url = "https://presensi-elektronik-ta-2526-016.vercel.app/api/device/enroll-mahasiswa";

    http.begin(secureClient, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-device-code", DEVICE_CODE);
    http.addHeader("x-device-secret", DEVICE_SECRET);

    if (globalJwtToken.length() > 0) {
        http.addHeader("Authorization", "Bearer " + globalJwtToken);
    }

    // ========== TUGAS 2: KONSISTENSI DATA (skema baru /enroll-mahasiswa) ==========
    // Kirim Raw Hex String langsung tanpa Base64 encoding (sama dengan PUSH_FP_MANUAL).
    // Tambahkan nama_mhsw dari mahasiswa.csv agar konsisten dengan batch enroll.
    String namaMhs = findNamaByNim(nim);
    String payload = "{\"mahasiswa\":[{\"nim\":\"" + nim +
                     "\",\"nama_mhsw\":\"" + namaMhs +
                     "\",\"template_data\":\"" + hexData + "\"}]}";

    // ========== TUGAS 3: DEBUGGING PAYLOAD ==========
    Serial.printf_P(PSTR("[DEBUG-PUSH-PAYLOAD] NIM: %s, Payload Size: %d\n"), nim.c_str(), payload.length());
    Serial.println(payload);

    int httpCode = http.POST(payload);

    bool success = false;

    // ========== TUGAS 4: PENANGANAN 404 & ERROR ==========
    if (httpCode == 200 || httpCode == 201) {
        Serial.println(F("[PUSH_REG] Sukses push ke server"));
        success = true;
    } else if (httpCode == 404) {
        Serial.println(F("[PUSH_REG] ERROR 404 - Endpoint tidak ditemukan"));
        String response = http.getString();
        Serial.printf("[PUSH_REG] Response: %s\n", response.c_str());
    } else {
        String response = http.getString();
        Serial.printf_P(PSTR("[PUSH_REG] Gagal, HTTP Code: %d\n"), httpCode);
        Serial.printf("[PUSH_REG] Response: %s\n", response.c_str());
    }

    http.end();
    return success;
}

bool pushPresensiToAPI(String kelasId, String* listHadir, String* listHadirTime, int count) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[PUSH_PRESENSI] WiFi not connected");
        return false;
    }

    if (kelasId.length() == 0 || count <= 0) {
        Serial.println("[PUSH_PRESENSI] Error: kelas_id atau count tidak valid");
        return false;
    }

    Serial.println("[PUSH_PRESENSI] ============================");
    Serial.printf("[PUSH_PRESENSI] Auto-Match Jadwal -> kelas_id %s, count: %d\n", kelasId.c_str(), count);

    // Konversi timestamp lokal "YYYY-MM-DD HH:MM:SS" -> ISO-8601 "YYYY-MM-DDTHH:MM:SSZ".
    // getTimestamp() memakai waktu WIB; backend mengharapkan format ISO. Bila timestamp
    // bukan format waktu valid (mis. placeholder "API_DISCONNECTED"), kirim apa adanya.
    auto toIso8601 = [](String ts) -> String {
        ts.trim();
        if (ts.length() == 19 && ts.charAt(10) == ' ') {  // "YYYY-MM-DD HH:MM:SS"
            ts.setCharAt(10, 'T');
            ts += "Z";
        }
        return ts;
    };

    // ========== BUILD JSON PAYLOAD UNTUK /kehadiran ==========
    // { "kelas_id": "<id>", "scans": [ {"nim","timestamp"}, ... ] }
    JsonDocument doc;
    doc["kelas_id"] = kelasId;

    JsonArray scans = doc["scans"].to<JsonArray>();
    int scanCount = 0;
    for (int i = 0; i < count; i++) {
        if (listHadir[i].length() == 0) continue;
        JsonObject scan = scans.add<JsonObject>();
        scan["nim"]       = listHadir[i];
        scan["timestamp"] = toIso8601(listHadirTime[i]);
        scanCount++;
    }

    if (scanCount == 0) {
        Serial.println(F("[PUSH_PRESENSI] Tidak ada scan valid untuk dikirim"));
        return false;
    }

    String payload;
    serializeJson(doc, payload);

    Serial.printf("[PUSH_PRESENSI] Payload size: %d (%d scans)\n", payload.length(), scanCount);
    Serial.println(payload);

    // ========== HTTP POST ==========
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    String url = String(API_BASE_URL) + "/api/device/kehadiran";

    http.begin(secureClient, url);
    http.setTimeout(15000);  // Beri waktu 15 detik untuk server Vercel memproses
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-device-code", DEVICE_CODE);
    http.addHeader("x-device-secret", DEVICE_SECRET);

    if (globalJwtToken.length() > 0) {
        http.addHeader("Authorization", "Bearer " + globalJwtToken);
    }

    int httpCode = http.POST(payload);

    bool success = false;

    if (httpCode == 200 || httpCode == 201) {
        Serial.println(F("[PUSH_PRESENSI] BERHASIL - Presensi terkirim ke server"));
        success = true;
    } else {
        String response = http.getString();
        Serial.printf_P(PSTR("[PUSH_PRESENSI] Gagal, HTTP Code: %d\n"), httpCode);
        Serial.printf("[PUSH_PRESENSI] Response: %s\n", response.c_str());
    }

    http.end();
    return success;
}

// ========== BATCH PUSH DENGAN DYNAMIC JSON DOCUMENT ==========
// khusus untuk ENROLL_FP - TIDAK ADA FILTER BY USER
// Menggunakan batching dan DynamicJsonDocument untuk hemat RAM
void processPushQueue(String currentActiveUser) {
    (void)currentActiveUser;  // Tidak digunakan untuk ENROLL_FP

    Serial.println(F("[PUSH_QUEUE] ========== START BATCH PUSH =========="));
    Serial.printf_P(PSTR("[PUSH_QUEUE] Batch size: %d\n"), PUSH_BATCH_SIZE);

    // ========== CEK FILE QUEUE MAHASISWA DUAL FINGERPRINT ==========
    // Format: mahasiswa,NIM,fp_1_mhsw,fp_2_mhsw
    File checkMhswFile = SD.open("/pending_mhsw_queue.csv", FILE_READ);
    bool hasMhswQueue = checkMhswFile && checkMhswFile.available() && checkMhswFile.size() > 0;
    if (checkMhswFile) checkMhswFile.close();

    // ========== PROSES QUEUE MAHASISWA 2 JARI KE /api/device/sync ==========
    if (hasMhswQueue) {
        Serial.println(F("[PUSH_QUEUE] Mencari queue mahasiswa 2 jari..."));
        processMahasiswaEnrollmentQueue();
    }

    // ========== CEK FILE QUEUE LAMA (FINGERPRINT Tunggal) ==========
    File checkFile = SD.open("/pending_push.csv", FILE_READ);
    if (!checkFile) {
        Serial.println(F("[PUSH_QUEUE] File antrian tidak ada, batalkan push"));
        return;
    }
    if (!checkFile.available() || checkFile.size() == 0) {
        Serial.println(F("[PUSH_QUEUE] File antrian kosong, batalkan push"));
        checkFile.close();
        return;
    }
    checkFile.close();

    // ========== LOOP BATCH - ULANGI HINGGA ANTRIAN KOSONG ==========
    int totalProcessed = 0;
    bool queueNotEmpty = true;

    while (queueNotEmpty) {
        // ========== DYNAMIC JSON DOCUMENT (HEMAT RAM) ==========
        const size_t JSON_CAPACITY = 8192;
        JsonDocument doc;
        JsonArray mahasiswa = doc["mahasiswa"].to<JsonArray>();

        // Array penampung untuk batch
        String batchQueueIds[PUSH_BATCH_SIZE];
        String batchNim[PUSH_BATCH_SIZE];
        String batchHex[PUSH_BATCH_SIZE];
        String batchNama[PUSH_BATCH_SIZE];
        int batchCount = 0;

        // ========== BACA PENDING_PUSH.CSV - LANGSUNG TANPA FILTER USER ==========
        File file = SD.open("/pending_push.csv", FILE_READ);
        if (!file) {
            Serial.println(F("[PUSH_QUEUE] Antrian kosong atau file tidak ada"));
            queueNotEmpty = false;
            break;
        }

        if (file.available()) file.readStringUntil('\n'); // Skip header

        while (file.available() && batchCount < PUSH_BATCH_SIZE) {
            String line = file.readStringUntil('\n');
            line.trim();
            line.replace("\r", "");
            if (line.length() == 0) continue;

            // ========== DEBUG: Lihat isi asli baris ==========
            Serial.printf_P(PSTR("[DEBUG-QUEUE] Baris dibaca: '%s'\n"), line.c_str());

            // ========== LOGIKA PEMARSEAN KOLOM NIM (kolom ke-2) ==========
            // Format: id, nim, hex_template, status, timestamp
            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            String nimTarget = "";

            if (p1 >= 0 && p2 > p1) {
                // Ambil nilai di antara koma 1 dan koma 2 (kolom NIM)
                nimTarget = line.substring(p1 + 1, p2);
            } else if (p1 >= 0) {
                // Fallback jika format hanya id,nim
                nimTarget = line.substring(p1 + 1);
            } else {
                // Fallback jika format murni hanya nim
                nimTarget = line;
            }

            nimTarget.trim();
            nimTarget.replace("\r", "");
            nimTarget.replace("\n", "");

            Serial.printf_P(PSTR("[DEBUG-QUEUE] NIM target: '%s', panjang: %d\n"), nimTarget.c_str(), nimTarget.length());

            // Jika NIM tidak valid (terlalu pendek), skip
            if (nimTarget.length() < 5) {
                Serial.println(F("[DEBUG-QUEUE] Skip: NIM terlalu pendek"));
                continue;
            }

            // ========== CARI TEMPLATE DENGAN INDEX-BASED SEARCH ==========
            String hexData = findFingerprintByNimIndex(nimTarget);
            String nimFound = nimTarget;

            if (hexData.length() > 100) {
                batchQueueIds[batchCount] = nimTarget;
                batchNim[batchCount] = nimFound;
                batchHex[batchCount] = hexData;
                batchNama[batchCount] = "Mahasiswa Baru";
                batchCount++;
                Serial.printf_P(PSTR("[PUSH_QUEUE] Batch[%d]: NIM: %s, Hex: %d bytes\n"), batchCount, nimFound.c_str(), hexData.length());
            } else {
                Serial.printf_P(PSTR("[PUSH_QUEUE] Skip: Template tidak ditemukan untuk NIM: %s\n"), nimTarget.c_str());
            }
        }
        file.close();

        if (batchCount == 0) {
            Serial.println(F("[PUSH_QUEUE] Tidak ada data yang valid untuk di-push"));
            queueNotEmpty = false;
            break;
        }

        // ========== CARI NAMA MAHASISWA ==========
        File mhsFile = SD.open("/mahasiswa.csv", FILE_READ);
        if (mhsFile) {
            if (mhsFile.available()) mhsFile.readStringUntil('\n');
            while (mhsFile.available()) {
                String line = mhsFile.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) continue;
                String csvNim = apiManager.getCsvColumn(line, 0);
                String csvNama = apiManager.getCsvColumn(line, 1);
                for (int i = 0; i < batchCount; i++) {
                    if (batchNim[i] == csvNim) {
                        batchNama[i] = csvNama;
                        break;
                    }
                }
            }
            mhsFile.close();
        }

        // ========== BUILD JSON PAYLOAD ==========
        for (int i = 0; i < batchCount; i++) {
            JsonObject mhs = mahasiswa.add<JsonObject>();
            mhs["nim"] = batchNim[i];
            mhs["nama_mhsw"] = batchNama[i];
            String cleanHex = trimHex(batchHex[i]);
            mhs["template_data"] = cleanHex;
        }

        // ========== SERIALIZE KE STRING ==========
        String payload;
        serializeJson(doc, payload);

        Serial.printf_P(PSTR("[PUSH_QUEUE] Batch ke-%d: %d data, %d bytes\n"),
            totalProcessed / PUSH_BATCH_SIZE + 1, batchCount, payload.length());

        // ========== HTTP POST ==========
        String endpoint = "https://presensi-elektronik-ta-2526-016.vercel.app/api/device/enroll-mahasiswa";

        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            http.setReuse(true);
            http.setTimeout(10000);
            http.begin(endpoint);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("x-device-code", DEVICE_CODE);
            http.addHeader("x-device-secret", DEVICE_SECRET);
            if (globalJwtToken.length() > 0) {
                http.addHeader("Authorization", "Bearer " + globalJwtToken);
            }

            int httpCode = http.POST(payload);

            if (httpCode == 200 || httpCode == 201) {
                Serial.printf_P(PSTR("[PUSH_QUEUE] BERHASIL (HTTP %d)\n"), httpCode);
                logSystemActivity("PUSH_SUCCESS", "Batch " + String(batchCount) + " - HTTP " + String(httpCode));

                // Hapus antrian yang berhasil - BUKA FILE LAGI
                // Format: id, nim, hex_template, status, timestamp
                File delFile = SD.open("/pending_push.csv", FILE_READ);
                if (delFile) {
                    String allLines = "";
                    if (delFile.available()) allLines = delFile.readStringUntil('\n');  // Header
                    allLines += "\n";
                    while (delFile.available()) {
                        String l = delFile.readStringUntil('\n');
                        l.trim();
                        l.replace("\r", "");
                        if (l.length() == 0) continue;

                        // Ambil NIM dari kolom ke-2 (index 1)
                        String n = apiManager.getCsvColumn(l, 1);
                        bool shouldDelete = false;
                        for (int i = 0; i < batchCount; i++) {
                            if (n == batchNim[i]) {
                                shouldDelete = true;
                                Serial.printf_P(PSTR("[DEBUG-QUEUE] Hapus NIM: %s\n"), n.c_str());
                                break;
                            }
                        }
                        if (!shouldDelete) {
                            allLines += l + "\n";
                        }
                    }
                    delFile.close();
                    SD.remove("/pending_push.csv");
                    File wFile = SD.open("/pending_push.csv", FILE_WRITE);
                    if (wFile) {
                        wFile.print(allLines);
                        wFile.close();
                        Serial.println(F("[DEBUG-QUEUE] File pending_push.csv telah diupdate"));
                    }
                }

                totalProcessed += batchCount;
            } else {
                String response = http.getString();
                Serial.printf_P(PSTR("[PUSH_QUEUE] GAGAL (HTTP %d): %s\n"), httpCode, response.c_str());
                logSystemActivity("PUSH_FAILED", "HTTP " + String(httpCode));
                queueNotEmpty = false;
            }
            http.end();
        } else {
            Serial.println(F("[PUSH_QUEUE] WiFi Terputus"));
            queueNotEmpty = false;
        }

        // ========== CLEAR DYNAMIC JSON DOCUMENT (BEBASKAN RAM) ==========
        doc.clear();

        // ========== CEK APAKAH ANTRIAN MASIH ADA ==========
        file = SD.open("/pending_push.csv", FILE_READ);
        if (!file || !file.available()) {
            queueNotEmpty = false;
        } else {
            file.close();
        }
    }  // ========== END WHILE LOOP ==========

    Serial.printf_P(PSTR("[PUSH_QUEUE] ========== DONE: %d total processed ==========\n"), totalProcessed);
}