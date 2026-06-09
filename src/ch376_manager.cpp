#include "ch376_manager.h"
#include <SD.h>

// External references
extern CH376Manager ch376;

// Forward declaration
bool testCH376();

// ========== HELPER: SPI BUS CONTROL ==========
void enableCH376SPI() {
    // BUG FIX: Both CS should be HIGH (inactive) first
    digitalWrite(SD_CS_PIN, HIGH);   // Disable SD Card (active LOW)
    digitalWrite(CH376_CS_PIN, HIGH); // Disable CH376 first
    delayMicroseconds(10);

    // Then enable CH376
    digitalWrite(CH376_CS_PIN, LOW); // Enable CH376 (active LOW)
    delayMicroseconds(10);
}

void disableCH376SPI() {
    // Disable CH376
    digitalWrite(CH376_CS_PIN, HIGH); // Disable CH376 (active LOW)
    delayMicroseconds(10);

    // JANGAN mengaktifkan SD Card di sini!
    // Biarkan keduanya tetap HIGH (Idle state)
    // SD Card tetap non-aktif setelah operasi CH376
}

// ========== CONSTRUCTOR ==========
CH376Manager::CH376Manager() {
    initialized = false;
    usbConnected = false;
    lastStatus = 0;
}

// ========== INITIALIZE ==========
void CH376Manager::init() {
    Serial.println("[CH376] Initializing SPI mode...");

    // Initialize SPI
    SPI.begin(14, 12, 13, 4);  // SCK=14, MISO=12, MOSI=13, CS=4
    SPI.setFrequency(CH376_SPI_SPEED);
    SPI.setDataMode(SPI_MODE0);

    // Set CS pins
    pinMode(SD_CS_PIN, OUTPUT);
    pinMode(CH376_CS_PIN, OUTPUT);
    pinMode(CH376_INT_PIN, INPUT_PULLUP);

    // BUG FIX: Both CS HIGH for idle state (active LOW)
    digitalWrite(SD_CS_PIN, HIGH);    // SD Card disabled
    digitalWrite(CH376_CS_PIN, HIGH); // CH376 disabled

    // Test raw SPI connection to CH376
    if (testCH376()) {
        Serial.println("[CH376] Module responded!");
        initialized = true;
    } else {
        Serial.println("[CH376] ERROR: Module not responding! Check wiring.");
    }
}

// ========== TEST CH376 RAW SPI ==========
// BUG FIX: Proper raw SPI test for CH376
bool CH376Manager::testCH376() {
    // BUG FIX: Ensure SD Card is completely disabled
    digitalWrite(SD_CS_PIN, HIGH);   // Force SD Card inactive
    delayMicroseconds(50);

    // Start SPI transaction
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

    // Pull CH376 CS LOW (active)
    digitalWrite(CH376_CS_PIN, LOW);
    delayMicroseconds(20);

    // Send CHECK_EXIST command (0x06) with test data (0x57)
    SPI.transfer(0x06);
    SPI.transfer(0x57);  // Test data
    delay(20);

    // Read response - CH376 should return 0xA8 (bitwise NOT of 0x57)
    uint8_t response = SPI.transfer(0xFF);

    // Release CH376 CS
    digitalWrite(CH376_CS_PIN, HIGH);
    delayMicroseconds(20);

    // End SPI transaction
    SPI.endTransaction();

    // DILARANG KERAS MENGUBAH SD_CS_PIN MENJADI LOW DI SINI!
    // Biarkan keduanya tetap HIGH (Idle).
    // SD Card tetap non-aktif, CH376 tetap non-aktif

    Serial.printf("[CH376] Test Ping 0x57 -> Response: 0x%02X\n", response);

    // Correct response from CH376 is 0xA8 (inverted 0x57)
    return (response == 0xA8);
}

// ========== PING CH376 (wrapper for compatibility) ==========
bool CH376Manager::pingCH376() {
    return testCH376();
}

// ========== SEND COMMAND ==========
bool CH376Manager::sendCommand(uint8_t cmd) {
    enableCH376SPI();
    SPI.transfer(cmd);
    delay(10);
    return true;
}

// ========== WAIT RESPONSE ==========
bool CH376Manager::waitResponse(uint8_t timeout_ms) {
    unsigned long start = millis();
    while (millis() - start < timeout_ms) {
        if (digitalRead(CH376_INT_PIN) == LOW) {
            disableCH376SPI();
            return true;
        }
        delay(1);
    }
    disableCH376SPI();
    return false;
}

// ========== READ DATA ==========
uint8_t CH376Manager::readData() {
    return SPI.transfer(0xFF);
}

// ========== WRITE DATA ==========
bool CH376Manager::writeData(const uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        SPI.transfer(data[i]);
    }
    return true;
}

// ========== CHECK USB DEVICE CONNECTED ==========
bool CH376Manager::isUSBDeviceConnected() {
    if (!initialized) return false;

    Serial.println("[USB] Checking USB connection...");

    enableCH376SPI();

    // Send GET_STATUS command
    SPI.transfer(0x22);
    delay(20);

    uint8_t status = SPI.transfer(0xFF);

    disableCH376SPI();

    lastStatus = status;
    Serial.println("[USB] Status: 0x" + String(status, HEX));

    // Status 0x15 = USB device connected
    // Status 0x42 = USB ready
    usbConnected = (status == 0x15 || status == 0x42 || status == 0x14);

    return usbConnected;
}

// ========== CHECK USB READY ==========
bool CH376Manager::isUSBReady() {
    return isUSBDeviceConnected() && (lastStatus == 0x14 || lastStatus == 0x42);
}

// ========== MOUNT USB ==========
bool CH376Manager::mountUSB() {
    Serial.println("[USB] Mounting USB drive...");

    if (!isUSBDeviceConnected()) {
        Serial.println("[USB] No USB device detected");
        return false;
    }

    Serial.println("[USB] USB drive mounted successfully");
    return true;
}

// ========== UNMOUNT USB ==========
void CH376Manager::unmountUSB() {
    usbConnected = false;
    Serial.println("[USB] USB unmounted");
}

// ========== OPEN FILE FOR WRITE ==========
bool CH376Manager::openFileWrite(const char* filename) {
    Serial.println("[USB] Opening file: " + String(filename));

    enableCH376SPI();

    // Set filename
    SPI.transfer(0x2F);  // SET_FILE_NAME
    SPI.transfer('*');  // Wildcard

    for (int i = 0; filename[i] != '\0'; i++) {
        SPI.transfer(filename[i]);
    }
    SPI.transfer(0x00);  // End of filename
    delay(50);

    // Try to open file
    SPI.transfer(0x32);  // FILE_OPEN
    delay(50);

    uint8_t status = SPI.transfer(0xFF);

    if (status != 0x14 && status != 0x00) {
        // Create new file
        SPI.transfer(0x34);  // FILE_CREATE
        delay(100);

        SPI.transfer(0x32);  // FILE_OPEN again
        delay(50);
        status = SPI.transfer(0xFF);
    }

    disableCH376SPI();

    return (status == 0x14 || status == 0x00);
}

// ========== WRITE TO FILE ==========
bool CH376Manager::writeToFile(const uint8_t* data, uint16_t len) {
    enableCH376SPI();

    SPI.transfer(0x3D);  // BYTE_WRITE
    SPI.transfer(len & 0xFF);
    SPI.transfer((len >> 8) & 0xFF);
    delay(30);

    for (uint16_t i = 0; i < len; i++) {
        SPI.transfer(data[i]);
    }

    SPI.transfer(0x39);  // BYTE_WR_GO
    delay(100);

    disableCH376SPI();

    return true;
}

// ========== CLOSE FILE ==========
bool CH376Manager::closeFile() {
    enableCH376SPI();

    SPI.transfer(0x36);  // FILE_CLOSE
    SPI.transfer(0x00);
    delay(50);

    disableCH376SPI();
    return true;
}

// ========== CHECK USB DISCONNECTED ==========
bool CH376Manager::isUSBDisconnected() {
    if (!initialized) return true;

    // Check INT pin - if HIGH, USB is disconnected
    return (digitalRead(CH376_INT_PIN) == HIGH);
}

// ========== GET LAST STATUS ==========
uint8_t CH376Manager::getLastStatus() {
    return lastStatus;
}

// ========== COPY FILE FROM SD TO USB ==========
bool copyFileToUSB(const char* sourcePath, const char* destFilename) {
    Serial.println("[COPY] Starting: " + String(sourcePath) + " -> " + String(destFilename));

    // Open source file on SD (make sure CH376 is disabled first)
    disableCH376SPI();
    delay(10);

    File srcFile = SD.open(sourcePath, FILE_READ);
    if (!srcFile) {
        Serial.println("[COPY] Failed to open source file");
        return false;
    }

    size_t fileSize = srcFile.size();
    Serial.println("[COPY] File size: " + String(fileSize));

    // Create destination file on USB
    if (!ch376.openFileWrite(destFilename)) {
        Serial.println("[COPY] Failed to create dest file");
        srcFile.close();
        return false;
    }

    // Copy in chunks using buffer
    uint8_t buffer[512];
    size_t bytesRead;
    size_t totalWritten = 0;

    while (srcFile.available()) {
        // Disable CH376, enable SD briefly to read
        disableCH376SPI();

        bytesRead = srcFile.read(buffer, sizeof(buffer));

        // Re-enable CH376 to write
        if (!ch376.writeToFile(buffer, bytesRead)) {
            Serial.println("[COPY] Write failed at: " + String(totalWritten));
            srcFile.close();
            ch376.closeFile();
            return false;
        }

        totalWritten += bytesRead;
        delayMicroseconds(100);
    }

    srcFile.close();
    ch376.closeFile();

    Serial.println("[COPY] Success! Total bytes: " + String(totalWritten));
    return true;
}

// ========== EXPORT ALL DATA TO USB ==========
bool exportAllDataToUSB() {
    Serial.println("[EXPORT] Starting full export...");

    // Ensure SPI is properly configured
    disableCH376SPI();  // Make sure CH376 is disabled first
    delay(50);

    // List of files to export from SD
    const char* filesToExport[] = {
        "/users.csv",
        "/fingerprint.csv",
        "/mahasiswa.csv",
        "/presensi.csv",
        "/kelas.csv"
    };

    const char* destNames[] = {
        "users.csv",
        "fingerprint.csv",
        "mahasiswa.csv",
        "presensi.csv",
        "kelas.csv"
    };

    int fileCount = sizeof(filesToExport) / sizeof(filesToExport[0]);
    int successCount = 0;

    for (int i = 0; i < fileCount; i++) {
        if (SD.exists(filesToExport[i])) {
            if (copyFileToUSB(filesToExport[i], destNames[i])) {
                successCount++;
            }
            delay(100);
        } else {
            Serial.println("[EXPORT] Skipping (not found): " + String(filesToExport[i]));
        }
    }

    Serial.println("[EXPORT] Completed: " + String(successCount) + "/" + String(fileCount) + " files");

    // Final cleanup
    disableCH376SPI();

    return (successCount > 0);
}