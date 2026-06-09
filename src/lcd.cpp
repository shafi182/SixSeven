#include "lcd.h"
#include "config.h"
#include <Wire.h>
#include <Arduino.h>

// Char 0: Simbol Antena
byte icon_antenna[8] = {
    0b00000,
    0b01110,
    0b10001,
    0b00100,
    0b01010,
    0b00000,
    0b00100,
    0b00000
};

// Char 1: Tanda Ceklis (Konek)
byte icon_check[8] = {
    0b00000,
    0b00001,
    0b00011,
    0b10110,
    0b11100,
    0b01000,
    0b00000,
    0b00000
};

// Char 2: Tanda Silang (Putus)
byte icon_cross[8] = {
    0b00000,
    0b10001,
    0b01010,
    0b00100,
    0b01010,
    0b10001,
    0b00000,
    0b00000
};

// Char 3: Ikon Sync (Panah Atas-Bawah ⇅)
byte icon_sync[8] = {
    0b01000, //  *
    0b11100, // ***
    0b01000, //  * 
    0b01010, //  * *
    0b01010, //  * *
    0b00010, //    *
    0b00111, //   ***
    0b00010  //    *
};

LCDManager::LCDManager() {
    lcd = new LiquidCrystal_I2C(LCD_ADDR, LCD_COLS, LCD_ROWS);
}

void LCDManager::init() {
    Wire.begin(I2C_SDA, I2C_SCL);
    lcd->init();
    lcd->backlight();
    lcd->clear();

    // Create custom characters: Antena, Ceklis, Silang
    lcd->createChar(0, icon_antenna);  // Antena
    lcd->createChar(1, icon_check);    // Ceklis (konek)
    lcd->createChar(2, icon_cross);    // Silang (putus)
    lcd->createChar(3, icon_sync);      // Refresh/Sync

    // Reset buffer
    for (int i = 0; i < 4; i++) {
        lastLines[i] = "";
    }
}

void LCDManager::printWiFiStatus() {
    // Kolom 18: Antena, Kolom 19: Ceklis/Silang
    lcd->setCursor(18, 0);
    lcd->write(0);  // Antena di kolom 18

    lcd->setCursor(19, 0);
    if (WiFi.status() == WL_CONNECTED) {
        lcd->write(1);  // Ceklis di kolom 19
    } else {
        lcd->write(2);  // Silang di kolom 19
    }
}

// ========== TUGAS 4: PRINT SYNC STATUS ==========
// Kolom 16: Sync Icon, Kolom 17: Ceklis/Silang
void LCDManager::printSyncStatus(bool isSynced) {
    lcd->setCursor(16, 0);
    lcd->write(3);  // Icon Sync/Refresh di kolom 16

    lcd->setCursor(17, 0);
    if (isSynced) {
        lcd->write(1);  // Ceklis - sudah sync
    } else {
        lcd->write(2);  // Silang - belum/gagal sync
    }
}

// ========== PRINT DI POSISI CURSOR ==========
void LCDManager::print(String text) {
    lcd->print(text);
}

// 🔥 PRINT DENGAN CACHE + SERIAL DEBUG
void LCDManager::printLine(int row, String text) {

    // Print ke Serial
    Serial.println("[LCD] Row " + String(row) + ": " + text);

    // 🔥 kalau sama, skip
    if (lastLines[row] == text) return;

    // 🔥 update buffer
    lastLines[row] = text;

    lcd->setCursor(0, row);

    // clear 1 baris saja
    lcd->print("                    "); // 20 char

    lcd->setCursor(0, row);
    lcd->print(text);
}

// 🔥 CLEAR + RESET BUFFER
void LCDManager::clear() {
    Serial.println("[LCD] CLEAR");
    lcd->clear();

    for (int i = 0; i < 4; i++) {
        lastLines[i] = "";
    }
}

// 🔥 SET CURSOR
void LCDManager::setCursor(int col, int row) {
    lcd->setCursor(col, row);
}

// 🔥 WRITE CUSTOM CHAR
void LCDManager::write(uint8_t charIndex) {
    lcd->write(charIndex);
}