#include "sdcard.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>

bool SDCardManager::init() {
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    bool sdInit = SD.begin(SD_CS);

    // ========== TUGAS 1: AUTO-DELETE FILE LEGACY ==========
    // Hapus file fingerprint.csv yang sudah tidak dipakai agar tidak salah baca
    if (SD.exists("/fingerprint.csv")) {
        SD.remove("/fingerprint.csv");
        Serial.println(F("[SYSTEM] File legacy fingerprint.csv berhasil dihapus."));
    }

    return sdInit;
}

File SDCardManager::open(const char* path, const char* mode) {
    return SD.open(path, mode);
}

// ========== GET FORMATTED TIME ==========
String getFormattedTime() {
    time_t now;
    time(&now);

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Check if time is valid (after year 2020)
    if (timeinfo.tm_year < 120) {  // 2020 - 1900
        return "WAKTU_BELUM_SINKRON";
    }

    char buffer[24];
    strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &timeinfo);

    return String(buffer);
}

// ========== LOG SYSTEM ACTIVITY ==========
void logSystemActivity(String event, String detail) {
    // Get formatted time
    String timestamp = getFormattedTime();

    // Format: [Timestamp],[Event],[Detail]
    String logEntry = timestamp + "," + event + "," + detail;

    Serial.println("[LOG] " + logEntry);

    // Open file in append mode
    File logFile = SD.open("/system_log.csv", FILE_APPEND);

    if (!logFile) {
        Serial.println("[LOG] Failed to open system_log.csv");
        return;
    }

    // Write log entry
    logFile.println(logEntry);
    logFile.close();
}