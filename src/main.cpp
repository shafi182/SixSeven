#include <Arduino.h>

#include "config.h"
#include "lcd.h"
#include "keypad_manager.h"
#include "fingerprint.h"
#include "sdcard.h"
#include "ch376_manager.h"
#include "wifi_manager.h"
#include "api_manager.h"
#include "export_manager.h"

#include "input_handler.h"
#include "auth_manager.h"
#include "state_machine.h"
#include "user_manager.h"
#include "mahasiswa_manager.h"
#include "fingerprint_data_manager.h"
#include "presensi_manager.h"
#include "kelas_manager.h"

LCDManager lcd;
KeypadManager keypad;
FingerprintManager fingerprintManager;
SDCardManager sd;
CH376Manager ch376;
WiFiManager wifiManager;
ExportManager exportManager;

UserManager userManager;
MahasiswaManager mahasiswaManager;
FingerprintDataManager fingerprintDataManager;
PresensiManager presensiManager;
KelasManager kelasManager;

InputHandler input(&keypad, &lcd, 20);
AuthManager auth;

StateMachine sm(&lcd, &keypad, &input, &auth);

bool apiSynced = false;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(100);

    // ========== KONFIGURASI PIN SPI (BEFORE SD INIT) ==========
    // Gunakan makro dari config.h (SD_CS, USB_CS, USB_INT)
    pinMode(SD_CS, OUTPUT);
    pinMode(USB_CS, OUTPUT);
    pinMode(USB_INT, INPUT_PULLUP);
    // Pastikan CS high (active low)
    digitalWrite(SD_CS, HIGH);    // Disable SD Card (active LOW)
    digitalWrite(USB_CS, HIGH); // Disable CH376 (active LOW)
    // ==========================================================

    // Init LCD
    lcd.init();
    lcd.printLine(0, "System Starting...");
    lcd.printLine(1, "Mencari Jaringan...");

    // Init keypad
    keypad.init();

    // Init SD card
    if (!sd.init()) {
        lcd.clear();
        lcd.printLine(0, "SD Card Error!");
        logSystemActivity("SD_INIT", "GAGAL - SD Card tidak dapat dibaca");
        while(!sd.init()){
            // menunggu sampai SD Card diganti/diperbaiki
        }
        lcd.clear();
        lcd.printLine(0, "SD Card Ready!");
        
    } else {
        lcd.printLine(2, "SD Card Ready");
        logSystemActivity("SD_INIT", "Berhasil - SD Card ready");
    }

    // Init managers (requires SD card)
    fingerprintDataManager.init();
    presensiManager.init();
    kelasManager.init();

    // Init fingerprint manager
    fingerprintManager.init();

    // ========== LOCAL-FIRST FINGERPRINT SYNC (WAJIB - SEBELUM WiFi) ==========
    // Flush dan inject fingerprint_users.csv agar login offline berfungsi
    // Ini wajib dijalankan tanpa mempedulikan status WiFi/API
    Serial.println(F("[BOOT] Menjalankan local-first fingerprint sync..."));

    bool bootSync = fingerprintManager.syncFromCSV(&userManager);
    if (bootSync) {
        Serial.println(F("[BOOT] Local-first sync BERHASIL"));
    } else {
        Serial.println(F("[BOOT] Local-first sync GAGAL"));
    }

    // ========== NETWORK & API LOGIN (DENGAN BYPASS) ==========
    bool wifiConnected = false;
    int wifiRetryCount = 0;
    const int MAX_WIFI_RETRY = 2;
    const unsigned long WIFI_TIMEOUT_MS = 12000;  // 12 detik timeout
    unsigned long wifiStartTime = millis();

    while (!wifiConnected && wifiRetryCount < MAX_WIFI_RETRY) {
        wifiConnected = wifiManager.smartAutoConnect();

        if (!wifiConnected) {
            lcd.clear();
            lcd.printLine(0, "Wifi Connecting...");
            wifiRetryCount++;
            unsigned long elapsed = millis() - wifiStartTime;

            // Cek apakah sudah exceed timeout total
            if (elapsed >= WIFI_TIMEOUT_MS || wifiRetryCount >= MAX_WIFI_RETRY) {
                Serial.println(F("[WiFi] Timeout/bypass - masuk mode offline"));
                break;
            }

            Serial.printf_P(PSTR("[WiFi] Retry %d/%d dalam 3 detik...\n"), wifiRetryCount, MAX_WIFI_RETRY);
            delay(3000);
        }
    }

    if (!wifiConnected) {
        Serial.println(F("[WiFi] Tidak ada WiFi - mode offline"));
        // Fingerprint sudah di-sync di awal setup() via local-first sync
    } else {
        Serial.println(F("[WiFi] Terhubung ke jaringan"));
    }

    // --- Login ke API (/api/device/login) ---
    // Ambil token JWT (finger_id tidak dipakai di server, beri 0 sebagai placeholder)
    String token = apiManager.apiLogin(0);
    if (token.length() > 0) {
        // ========== STEP 1: FLUSH & VALIDATION (WAJIB) ==========
        // CSV adalah Source of Truth - flush sensor dulu, baru inject
        Serial.println(F("[SYNC] ===== MASTER SYNC START ====="));
        Serial.println(F("[SYNC] Step 1: Flush & validate sensor..."));

        bool flushSuccess = false;
        int flushRetryCount = 0;
        const int MAX_FLUSH_RETRY = 3;

        // Flush wajib: loop sampai sensor kosong atau max retry
        while (flushRetryCount < MAX_FLUSH_RETRY && !flushSuccess) {
            // Eksekusi empty_library()
            uint8_t flushResult = fingerprintManager.getFinger()->empty_library();

            if (flushResult == FP_OK) {
                Serial.println(F("[SYNC] empty_library OK"));
            } else {
                Serial.printf_P(PSTR("[SYNC] empty_library error: %d\n"), flushResult);
            }

            // Validasi: wajib cek template_count setelah flush
            delay(50);
            fingerprintManager.getFinger()->count_templates();
            int templateCount = fingerprintManager.getFinger()->template_count;

            if (templateCount == 0) {
                flushSuccess = true;
                Serial.println(F("[SYNC] Flush validated: sensor KOSONG"));
            } else {
                flushRetryCount++;
                Serial.printf_P(PSTR("[SYNC] Flush retry %d/%d: template_count=%d\n"),
                              flushRetryCount, MAX_FLUSH_RETRY, templateCount);

                if (flushRetryCount < MAX_FLUSH_RETRY) {
                    // Force delete per-slot sebagai backup
                    for (int delSlot = 1; delSlot <= 200; delSlot++) {
                        fingerprintManager.getFinger()->delete_model(delSlot);
                    }
                    delay(100);
                }
            }
        }

        // Verifikasi akhir sebelum proceed
        fingerprintManager.getFinger()->count_templates();
        int finalCount = fingerprintManager.getFinger()->template_count;
        Serial.printf_P(PSTR("[SYNC] Pre-inject check: %d templates\n"), finalCount);

        // JIKA FLUSH GAGAL setelah 3x retry, TIDAK lanjut injeksi
        if (!flushSuccess && finalCount > 0) {
            Serial.println(F("[SYNC] ERROR: Flush failed - ABORT injection!"));
            lcd.printLine(2, "Sensor Error!");
            logSystemActivity("SYNC_ERROR", "Flush gagal");
        } else {
            Serial.println(F("[SYNC] Flush OK - proceeding to injection..."));

            // ========== STEP 2: INJEKSI DARI CSV ==========
            Serial.println(F("[SYNC] Step 2: Inject dari CSV..."));

            int totalInjected = 0;

            // --- Load fingerprint_users.csv (Dosen/Admin: Slot 1-2) ---
            File adminFile = sd.open("/fingerprint_users.csv", FILE_READ);
            if (adminFile) {
                Serial.println(F("[SYNC] Reading fingerprint_users.csv..."));
                while (adminFile.available()) {
                    String line = adminFile.readStringUntil('\n');
                    line.trim();
                    if (line.length() < 10) continue;
                    if (line.startsWith(F("id,")) || line.startsWith(F("id,user"))) continue;

                    int p1 = line.indexOf(',');
                    int p2 = line.indexOf(',', p1 + 1);
                    int p3 = line.indexOf(',', p2 + 1);
                    int p4 = line.indexOf(',', p3 + 1);
                    if (p1 <= 0 || p2 <= 0 || p3 <= 0 || p4 <= 0) continue;

                    int slot = line.substring(0, p1).toInt();
                    String userId = line.substring(p1 + 1, p2);
                    String hexData = line.substring(p3 + 1, p4);

                    userId.trim();
                    hexData.trim();

                    if (hexData.length() < 1000) continue;
                    if (slot > 0 && slot <= 200) {
                        if (fingerprintManager.injectSingleFingerprint(userId, hexData, slot)) {
                            totalInjected++;
                        }
                    }
                }
                adminFile.close();
            }

            // --- Load fingerprint_mahasiswa.csv (Mahasiswa: Slot 3+) ---
            File studentFile = sd.open("/fingerprint_mahasiswa.csv", FILE_READ);
            if (studentFile) {
                Serial.println(F("[SYNC] Reading fingerprint_mahasiswa.csv..."));
                while (studentFile.available()) {
                    String line = studentFile.readStringUntil('\n');
                    line.trim();
                    if (line.length() < 10) continue;
                    if (line.startsWith(F("id,")) || line.startsWith(F("id,user"))) continue;

                    int p1 = line.indexOf(',');
                    int p2 = line.indexOf(',', p1 + 1);
                    int p3 = line.indexOf(',', p2 + 1);
                    int p4 = line.indexOf(',', p3 + 1);
                    if (p1 <= 0 || p2 <= 0 || p3 <= 0 || p4 <= 0) continue;

                    int slot = line.substring(0, p1).toInt();
                    String userId = line.substring(p1 + 1, p2);
                    String hexData = line.substring(p3 + 1, p4);

                    userId.trim();
                    hexData.trim();

                    if (hexData.length() < 1000) continue;
                    if (slot > 0 && slot <= 200) {
                        if (fingerprintManager.injectSingleFingerprint(userId, hexData, slot)) {
                            totalInjected++;
                        }
                    }
                }
                studentFile.close();
            }

            // ========== STEP 3: SYNC RAM ==========
            Serial.println(F("[SYNC] Step 3: Populate fingerMap..."));
            fingerprintDataManager.reloadData();

            // Verifikasi akhir
            fingerprintManager.getFinger()->count_templates();
            int sensorCount = fingerprintManager.getFinger()->template_count;
            Serial.printf_P(PSTR("[SYNC] ===== DONE: %d injected, %d sensor\n"),
                          totalInjected, sensorCount);

            logSystemActivity("API_LOGIN", "Berhasil - device login sukses");
        }
    } else {
        lcd.printLine(2, "Login Gagal!");
        logSystemActivity("API_LOGIN", "Gagal - device login gagal");
    }

    // Init state machine and display login screen
    sm.init();

    Serial.println("=== System Ready ===");
    logSystemActivity("BOOT", "System Ready");
}

void loop() {
    yield();

    static unsigned long lastTimeSyncAttempt = 0;
    const unsigned long TIME_SYNC_COOLDOWN = 10000;
    static bool credentialsSaved = false;
    static bool wasConnected = false;

    // ========== CEK DISKONEKSI (Reset flag sebelum proses lain) ==========
    if (WiFi.status() != WL_CONNECTED && wasConnected) {
        wasConnected = false;
        credentialsSaved = false;  // Reset agar bisa save ulang saat reconnect
    }
    if (WiFi.status() == WL_CONNECTED) {
        wasConnected = true;
    }

    if (WiFi.status() == WL_CONNECTED && !apiManager.isTimeSynced()) {
        if (millis() - lastTimeSyncAttempt > TIME_SYNC_COOLDOWN || lastTimeSyncAttempt == 0) {
            lastTimeSyncAttempt = millis();

            // ========== AUTO-SAVE CREDENTIALS (Pertama kali terhubung) ==========
            if (!credentialsSaved && WiFi.SSID().length() > 0) {
                Serial.println(F("[WiFi] Auto-save kredensial baru..."));
                // saveCredentials()会自动 cek duplikasi
                if (wifiManager.saveCredentials(WiFi.SSID(), wifiManager.getTargetPassword(), wifiManager.getTargetUsername())) {
                    Serial.println(F("[WiFi] Kredensial berhasil disimpan ke wifi.csv"));
                }
                credentialsSaved = true;
            }

            Serial.println("[SYSTEM] Attempting to sync time from API...");
            if (apiManager.syncTimeFromAPI()) {
                logSystemActivity("TIME_SYNC", "Waktu berhasil disinkronisasi");
            } else {
                Serial.println("[TIME] Sync failed, will retry after cooldown...");
            }
        }
    }

    if (WiFi.status() != WL_CONNECTED) {
        lastTimeSyncAttempt = 0;
    }

    static bool apiSyncInProgress = false;
    if (WiFi.status() == WL_CONNECTED && apiManager.isTimeSynced() && !apiSynced && !apiSyncInProgress) {
        apiSyncInProgress = true;
        Serial.println("[SYSTEM] WiFi Connected + SNTP Synced! Memulai Sinkronisasi User...");
        apiManager.syncUsersFromAPI();
        apiSynced = true;

        Serial.println("[SYSTEM] Mengembalikan state ke Login...");
        sm.init();  // Reset state machine ke STATE_LOGIN
    }

    if (WiFi.status() != WL_CONNECTED) {
        apiSynced = false;
        apiSyncInProgress = false;
        // isDataSynced reset handled elsewhere
    }

    sm.update();
}