/*
 * Offline Enrollment Example
 *
 * Contoh penggunaan fitur offline enrollment:
 * 1. Enroll fingerprint mahasiswa
 * 2. Simpan template ke SD card
 * 3. Verifikasi fingerprint dari SD card
 */

#include <Arduino.h>

#include "lcd.h"
#include "keypad_manager.h"
#include "fingerprint.h"
#include "sdcard.h"
#include "fingerprint_data_manager.h"
#include "mahasiswa_manager.h"

LCDManager lcd;
KeypadManager keypad;
FingerprintManager fingerprintManager;
SDCardManager sd;
FingerprintDataManager fingerprintDataManager;
MahasiswaManager mahasiswaManager;

// ================= UTILITY FUNCTIONS =================

void printLine(String line1, String line2, String line3 = "", String line4 = "") {
    lcd.printLine(0, line1.substring(0, min(20, line1.length())));
    lcd.printLine(1, line2.substring(0, min(20, line2.length())));
    if (line3.length() > 0) {
        lcd.printLine(2, line3.substring(0, min(20, line3.length())));
    }
    if (line4.length() > 0) {
        lcd.printLine(3, line4.substring(0, min(20, line4.length())));
    }
}

// ================= MENU FUNCTIONS =================

void showMainMenu() {
    printLine("1. Enroll New", "2. Verify", "3. Delete", "4. List All");
}

void showEnrollMenu() {
    printLine("Enter NIM:", "", "", "");
}

void showVerifyMenu() {
    printLine("Place finger...", "", "", "");
}

// ================= STATE VARIABLES =================

String inputNIM = "";
int inputState = 0;  // 0: main menu, 1: enroll NIM, 2: verify NIM

// ================= SETUP =================

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(100);

    Serial.println("=== Offline Enrollment System ===");

    // Inisialisasi LCD
    lcd.init();
    lcd.printLine(0, "System Starting...");
    delay(1000);

    // Inisialisasi keypad
    keypad.init();

    // Inisialisasi fingerprint
    fingerprintManager.init();
    Serial.println("Fingerprint sensor initialized");

    // Inisialisasi SD card
    if (!sd.init()) {
        lcd.printLine(2, "SD Card Error!");
        Serial.println("SD Card initialization failed!");
    } else {
        lcd.printLine(2, "SD Card Ready");
        Serial.println("SD Card initialized");

        // Inisialisasi data manager
        fingerprintDataManager.init();
        mahasiswaManager.init();
        Serial.println("Data managers initialized");
    }

    delay(1000);
    showMainMenu();
}

// ================= LOOP =================

void loop() {
    // Check keypad input
    if (keypad.available()) {
        char key = keypad.read();
        handleInput(key);
    }

    // Update LCD
    delay(50);
}

// ================= INPUT HANDLER =================

void handleInput(char key) {
    Serial.println("Key pressed: " + String(key));

    // Number keys - input NIM
    if (key >= '0' && key <= '9') {
        if (inputState == 1 || inputState == 2) {
            inputNIM += key;
            printLine(inputState == 1 ? "Enroll: " : "Verify:", inputNIM, "", "");
        } else if (inputState == 0) {
            handleMainMenuInput(key);
        }
        return;
    }

    // Backspace
    if (key == '#') {
        if (inputNIM.length() > 0) {
            inputNIM = inputNIM.substring(0, inputNIM.length() - 1);
            printLine(inputState == 1 ? "Enroll: " : "Verify:", inputNIM, "", "");
        } else {
            inputState = 0;
            showMainMenu();
        }
        return;
    }

    // Enter/Confirm
    if (key == '*') {
        if (inputState == 1) {
            enrollFingerprint();
        } else if (inputState == 2) {
            verifyFingerprint();
        }
        return;
    }

    // Star key - back to menu
    if (key == '*') {
        inputState = 0;
        showMainMenu();
    }
}

void handleMainMenuInput(char key) {
    if (key == '1') {
        // Enroll new
        inputState = 1;
        inputNIM = "";
        showEnrollMenu();
        Serial.println("Enrollment mode - enter NIM");
    } else if (key == '2') {
        // Verify
        inputState = 2;
        inputNIM = "";
        showVerifyMenu();
        Serial.println("Verification mode - enter NIM");
    } else if (key == '3') {
        // Delete - not implemented yet
        printLine("Delete feature", "not available", "", "");
        delay(1000);
        showMainMenu();
    } else if (key == '4') {
        // List all
        listAll();
        showMainMenu();
    }
}

// ================= ENROLLMENT FUNCTION =================

void enrollFingerprint() {
    if (inputNIM.length() == 0) {
        printLine("Error: Empty", "NIM", "", "");
        delay(1000);
        showEnrollMenu();
        return;
    }

    // Check if NIM already exists
    if (mahasiswaManager.exists(inputNIM)) {
        printLine("NIM already", "registered!", "", "");
        delay(1000);
        showMainMenu();
        return;
    }

    printLine("Place finger...", "on sensor", "", "");

    // Enroll dengan sensor
    // Dapatkan ID dari data manager
    int id = fingerprintDataManager.getNextID();
    if (id == 0) id = 1;
    Serial.println("Enrolling ID: " + String(id));

    // Gunakan fungsi enrollAndSave yang baru
    if (fingerprintManager.enrollAndSave(&fingerprintDataManager, inputNIM)) {
        // Simpan data mahasiswa
        mahasiswaManager.addMahasiswa(inputNIM, "Mahasiswa " + inputNIM);

        printLine("Enrollment", "SUCCESS!", "ID: " + String(id), "NIM: " + inputNIM);
        Serial.println("=== ENROLLMENT SUCCESS ===");
    } else {
        printLine("Enrollment", "FAILED", "", "");
        Serial.println("=== ENROLLMENT FAILED ===");
    }

    delay(2000);
    showMainMenu();
    inputNIM = "";
}

// ================= VERIFICATION FUNCTION =================

void verifyFingerprint() {
    if (inputNIM.length() == 0) {
        printLine("Error: Empty", "NIM", "", "");
        delay(1000);
        showVerifyMenu();
        return;
    }

    // Check if NIM exists
    if (!mahasiswaManager.exists(inputNIM)) {
        printLine("NIM not", "found!", "", "");
        delay(1000);
        showMainMenu();
        return;
    }

    printLine("Place finger...", "for verification", "", "");

    // Verify dari SD card
    bool result = fingerprintManager.verifyFromSD(&fingerprintDataManager, inputNIM);

    if (result) {
        printLine("ACCESS", "GRANTED", "NIM: " + inputNIM, "");
        Serial.println("ACCESS GRANTED for NIM: " + inputNIM);
    } else {
        printLine("ACCESS", "DENIED", "", "");
        Serial.println("ACCESS DENIED for NIM: " + inputNIM);
    }

    delay(2000);
    showMainMenu();
    inputNIM = "";
}

// ================= LIST ALL FUNCTION =================

void listAll() {
    printLine("Listing all", "registered...", "", "");

    std::vector<String> allNIM = fingerprintDataManager.getAllNIM();
    int count = allNIM.size();

    printLine("Total: " + String(count), "registered", "", "");

    Serial.println("=== ALL REGISTERED ===");
    for (int i = 0; i < count; i++) {
        String nim = allNIM[i];
        int id = fingerprintDataManager.getID(nim);
        String nama = mahasiswaManager.getNama(nim);
        Serial.println("ID: " + String(id) + ", NIM: " + nim + ", Nama: " + nama);
    }
}
