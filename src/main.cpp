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

    // ========== DIAGNOSTIK: ALASAN RESET TERAKHIR ==========
    // Membedakan brownout/power (ESP_RST_BROWNOUT / ESP_RST_POWERON) dari
    // crash software (ESP_RST_PANIC) atau watchdog (ESP_RST_TASK_WDT/INT_WDT).
    esp_reset_reason_t resetReason = esp_reset_reason();
    Serial.printf("[BOOT] Reset reason = %d ", (int)resetReason);
    switch (resetReason) {
        case ESP_RST_POWERON:  Serial.println("(POWERON - cold boot / power cycle)"); break;
        case ESP_RST_BROWNOUT: Serial.println("(BROWNOUT - tegangan turun! cek catu daya)"); break;
        case ESP_RST_PANIC:    Serial.println("(PANIC - crash software / exception)"); break;
        case ESP_RST_TASK_WDT: Serial.println("(TASK_WDT - watchdog task)"); break;
        case ESP_RST_INT_WDT:  Serial.println("(INT_WDT - watchdog interrupt)"); break;
        case ESP_RST_SW:       Serial.println("(SW - esp_restart)"); break;
        default:               Serial.println("(lainnya)"); break;
    }
    Serial.printf("[BOOT] Free heap awal = %u, blok terbesar = %u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

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

    // ========== AUTO-PROVISIONING (Plug-and-Play SD Card) ==========
    // Buat semua file CSV dasar + header bila SD Card baru/kosong. WAJIB sebelum
    // baca kredensial WiFi (smartAutoConnect) & local-first fingerprint sync.
    initSDCardFiles();
    logSystemActivity("SD_PROVISION", "File dasar SD Card diperiksa/dibuat");

    // Init managers (requires SD card)
    fingerprintDataManager.init();
    presensiManager.init();
    kelasManager.init();

    // Init fingerprint manager
    fingerprintManager.init();

    // ========== AUTO-SAVE RESUME: deteksi sesi presensi yang terputus ==========
    // Bila ada sesi PRESENSI ACTIVE, template di flash R503 MASIH UTUH (persisten saat
    // mati listrik). Maka boot TIDAK boleh flush+inject sensor - cukup pulihkan fingerMap
    // dari snapshot (dilakukan nanti di sm.init()). Ini menghapus inject ganda saat resume.
    bool resumePresensi = hasActivePresensiResume();
    if (resumePresensi) {
        Serial.println(F("[BOOT] Resume presensi terdeteksi -> LEWATI flush/inject sensor (data R503 dipertahankan)"));
    }

    // ========== LOCAL-FIRST FINGERPRINT SYNC (WAJIB - SEBELUM WiFi) ==========
    // Flush dan inject fingerprint_users.csv agar login offline berfungsi
    // Ini wajib dijalankan tanpa mempedulikan status WiFi/API
    if (!resumePresensi) {
        Serial.println(F("[BOOT] Menjalankan local-first fingerprint sync..."));
        bool bootSync = fingerprintManager.syncFromCSV(&userManager);
        if (bootSync) {
            Serial.println(F("[BOOT] Local-first sync BERHASIL"));
        } else {
            Serial.println(F("[BOOT] Local-first sync GAGAL"));
        }
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
    if (resumePresensi) {
        // Resume: jangan sentuh sensor (flush+inject akan menghapus data presensi yang
        // sedang berjalan). fingerMap dipulihkan via snapshot di sm.init().
        Serial.println(F("[BOOT] Resume presensi -> lewati master-sync inject sensor"));
    } else if (token.length() > 0) {
        Serial.println(F("[BOOT] API Login Sukses. Sensor siap digunakan."));
        logSystemActivity("API_LOGIN", "Berhasil - device login sukses");
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
        
        // Skip sync jika sistem sedang dalam sesi aktif (resume session)
        if (sm.getState() == STATE_LOGIN) {
            Serial.println("[SYSTEM] WiFi Connected + SNTP Synced! Memulai Sinkronisasi User...");
            apiManager.syncUsersFromAPI();
            apiSynced = true;

            // Refresh layar login
            Serial.println("[SYSTEM] Refresh layar Login pasca-sync...");
            sm.init();
        } else {
            Serial.println("[SYSTEM] WiFi Connected + SNTP Synced! Sesi aktif terdeteksi, lewati Sinkronisasi User.");
            apiSynced = true;
        }
    }

    if (WiFi.status() != WL_CONNECTED) {
        apiSynced = false;
        apiSyncInProgress = false;
        // isDataSynced reset handled elsewhere
    }

    sm.update();
}