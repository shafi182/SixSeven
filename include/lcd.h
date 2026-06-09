#pragma once
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>

class LCDManager {
private:
    LiquidCrystal_I2C* lcd;

    String lastLines[4];   // 🔥 buffer isi LCD

public:
    LCDManager();
    void init();
    void printLine(int row, String text);
    void print(String text);  // Print teks di posisi cursor saat ini
    void clear();
    void setCursor(int col, int row);
    void write(uint8_t charIndex);
    void printWiFiStatus();  // Tampilkan ikon WiFi (Antena + Ceklis/Silang)
    void printSyncStatus(bool isSynced);  // Tampilkan ikon Sync (Refresh + Ceklis/Silang)
};

// Global instance
extern LCDManager lcd;