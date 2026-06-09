#include "input_handler.h"
#include "config.h"
#include <WiFi.h>

InputHandler::InputHandler(KeypadManager* k, LCDManager* l, int maxLen) {
    keypad = k;
    lcd = l;
    maxLength = maxLen;
    buffer = "";
    maskInput = true;
    label = "Masukan NIP:";
    lastInputTime = millis();
}

void InputHandler::setMode(String text, bool mask) {
    label = text;
    maskInput = mask;
}

void InputHandler::reset(bool showCancel) {
    buffer = "";
    lcd->clear();
    lcd->printLine(0, label);
    // Tampilkan empty line dengan cursor
    lcd->printLine(1, "_");
    // TUGAS 1 & 2: Selalu tampilkan opsi bottom line dengan padding spasi
    // Jika showCancel = true: tampilkan "D.Batal"
    // Jika showCancel = false (login): tampilkan "A.Setup WiFi"
    if (showCancel) {
        lcd->setCursor(0, 3);
        lcd->print("D.Batal             ");  // 20 karakter padding
    }
    //  else {
    //     lcd->setCursor(0, 3);
    //     lcd->print("A.Setup WiFi        ");  // 20 karakter padding
    // }

    // ========== TUGAS 1 & 2: TAMBAHKAN FULL IKON WIFI (ANTENA + STATUS) ==========
    // Tampilkan indikator WiFi di Baris 0, Kolom 18-19 (LCD 20x4)
    // Gunakan 2 custom char seperti di menu lain:
    // - Kolom 18: write(0) = icon_antenna
    // - Kolom 19: write(1) = icon_check (terhubung) ATAU write(2) = icon_cross (putus)
    lcd->setCursor(18, 0);
    lcd->write(0);  // Antena di kolom 18

    lcd->setCursor(19, 0);
    if (WiFi.status() == WL_CONNECTED) {
        lcd->write(1);  // Ceklis di kolom 19
    } else {
        lcd->write(2);  // Silang di kolom 19
    }
    // ======================================================================

    // ========== TUGAS: TAMBAHKAN INDIKATOR SYNC STATUS ==========
    // Kolom 16: Icon Sync (Panah Atas-Bawah)
    // Kolom 17: Ceklis (sudah sync) atau Silang (belum/gagal sync)
    lcd->setCursor(16, 0);
    lcd->write(3);  // Icon Sync

    lcd->setCursor(17, 0);
    if (getCurrentSyncStatus()) {
        lcd->write(1);  // Ceklis (flag konteks: Users/Dashboard/Feature)
    } else {
        lcd->write(2);  // Silang
    }
    // =================================================================

    lastInputTime = millis();
}

bool InputHandler::update() {
    // Debounce: ignore key press if too soon after last press
    static unsigned long lastKeyTime = 0;
    const unsigned long DEBOUNCE_MS = 150;

    char key = keypad->getKey();

    // Debounce: hanya proses jika sudah lewat waktu debounce
    if (key && millis() - lastKeyTime < DEBOUNCE_MS) {
        return false;
    }

    if (key) {
        lastKeyTime = millis();
        // Debug: print key press
        Serial.print("[KEYPAD] Pressed: ");
        Serial.println(key);
    }

    // TUGAS FIX: Cek timeout SEBELUM memproses key - agar input pertama tidak tertelan
    if (millis() - lastInputTime > 10000) {
        // Reset buffer tapi JANGAN return dulu - proses key dulu!
        buffer = "";
        // Jangan call lcd.clear() di sini karena akan menelan input pertama
        // Biarkan update display terjadi di akhir fungsi
        lastInputTime = millis();
        // Lanjutkan ke proses key di bawah - JANGAN return false di sini!
    }

    if (!key) return false;

    // TUGAS: Selalu update lastInputTime, baru proses key
    lastInputTime = millis();

    // TUGAS FIX: Jika buffer kosong (baru direset oleh timeout), redraw layar SEBELUM add key
    if (buffer.length() == 0 && key >= '0' && key <= '9') {
        // Redraw hanya baris 1, bukan seluruh layar
        lcd->printLine(0, label);
        lcd->printLine(1, "_");
        lcd->setCursor(0, 3);
        lcd->print("A.Setup WiFi        ");

        // ========== TUGAS 1 & 2: UPDATE FULL IKON WIFI SETELAH REDRAW ==========
        lcd->setCursor(18, 0);
        lcd->write(0);  // Antena di kolom 18

        lcd->setCursor(19, 0);
        if (WiFi.status() == WL_CONNECTED) {
            lcd->write(1);  // Ceklis di kolom 19
        } else {
            lcd->write(2);  // Silang di kolom 19
        }
        // ================================================================
    }

    if (key >= '0' && key <= '9') {
        if (buffer.length() < maxLength) {
            buffer += key;
        }
    }
    else if (key == '*') {
        if (buffer.length() > 0) {
            buffer.remove(buffer.length() - 1);
        }
    }
    else if (key == '#') {
        return true;
    }
    else if (key == 'A') {
        // TUGAS 1 & 2: Tombol 'A' TIDAK diproses oleh input handler
        // Biarkan state_machine yang menangani pintasan ke WiFi
        return false;
    }
    else if (key == 'D') {
        // Cancel - return special indicator
        buffer = "CANCEL";
        return true;
    }

    String display = "";

    if (maskInput) {
        // Untuk PIN: tampilkan *
        for (int i = 0; i < buffer.length(); i++) {
            display += '*';
        }
    } else {
        // Untuk NIP: tampilkan angka asli
        display = buffer;
    }

    display += "_";

    lcd->printLine(1, display);

    return false;
}

// TUGAS 1 & 3: Overload - gunakan key yang sudah diambil di state_machine
bool InputHandler::update(char preFetchedKey) {
    char key = preFetchedKey;

    // Debounce check - tapi gunakan waktu yang sudah tercatat
    if (!key) return false;

    // Debug: print key press
    Serial.print("[KEYPAD] Pressed: ");
    Serial.println(key);

    // TUGAS FIX: Cek timeout SEBELUM memproses key - agar input pertama tidak tertelan
    if (millis() - lastInputTime > 10000) {
        // Reset buffer tapi JANGAN return dulu - proses key dulu!
        buffer = "";
        // Jangan call lcd.clear() di sini karena akan menelan input pertama
        // Biarkan update display terjadi di akhir fungsi
        lastInputTime = millis();
        // Lanjutkan ke proses key di bawah - JANGAN return false di sini!
    }

    if (!key) return false;

    // TUGAS: Selalu update lastInputTime, baru proses key
    lastInputTime = millis();

    // TUGAS FIX: Jika buffer kosong (baru direset oleh timeout), redraw layar SEBELUM add key
    if (buffer.length() == 0 && key >= '0' && key <= '9') {
        // Redraw hanya baris 1, bukan seluruh layar
        lcd->printLine(0, label);
        lcd->printLine(1, "_");
        lcd->setCursor(0, 3);
        lcd->print("A.Setup WiFi        ");

        // ========== TUGAS 1 & 2: UPDATE FULL IKON WIFI SETELAH REDRAW ==========
        lcd->setCursor(18, 0);
        lcd->write(0);  // Antena di kolom 18

        lcd->setCursor(19, 0);
        if (WiFi.status() == WL_CONNECTED) {
            lcd->write(1);  // Ceklis di kolom 19
        } else {
            lcd->write(2);  // Silang di kolom 19
        }
        // ================================================================
    }

    if (key >= '0' && key <= '9') {
        if (buffer.length() < maxLength) {
            buffer += key;
        }
    }
    else if (key == '*') {
        if (buffer.length() > 0) {
            buffer.remove(buffer.length() - 1);
        }
    }
    else if (key == '#') {
        return true;
    }
    else if (key == 'A') {
        // TUGAS 1 & 2: Tombol 'A' TIDAK diproses oleh input handler
        return false;
    }
    else if (key == 'D') {
        // Cancel - return special indicator
        buffer = "CANCEL";
        return true;
    }

    String display = "";

    if (maskInput) {
        // Untuk PIN: tampilkan *
        for (int i = 0; i < buffer.length(); i++) {
            display += '*';
        }
    } else {
        // Untuk NIP: tampilkan angka asli
        display = buffer;
    }

    display += "_";

    lcd->printLine(1, display);

    return false;
}

String InputHandler::getValue() {
    return buffer;
}