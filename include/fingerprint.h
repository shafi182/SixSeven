#pragma once
#include "Adafruit_Fingerprint_ESP32.h"
#include "fingerprint_data_manager.h"
#include "user_manager.h"

enum EnrollState {
    ENROLL_IDLE,
    ENROLL_PLACE_FINGER,
    ENROLL_PLACE_AGAIN,
    ENROLL_SUCCESS,
    ENROLL_FAILED
};

const char* fpGetErrorString(uint8_t err);

// ========== DEBUG: PRINT FINGERPINT MAP ==========
void printFingerMap();

class FingerprintManager {
private:
    HardwareSerial* serial;
    Adafruit_Fingerprint_ESP32* finger;

    // Enrollment state
    EnrollState enrollState;
    int enrollId;
    unsigned long enrollStartTime;

public:
    FingerprintManager();
    void init();
    void loadSensorMap();
    void saveSensorMap();

    // ========== TUGAS 1: LOAD TEMPLATE FROM CSV AT BOOT ==========
    void loadTemplatesFromCSV(FingerprintDataManager* dataManager);
    int loadTemplatesFromFile(File& file, const char* fileName);
    bool injectSingleFingerprint(String nim, String hexData, int slot);

    // ========== TUGAS 3: SAVE FAILED INJECT ==========
    void saveFailedInject(String nim, int slot);

    // ========== TUGAS 2: GET LOWEST AVAILABLE FINGERPRINT ID ==========
    int getLowestAvailableFingerprintId();

    // ========== SIMPLE FINGERPRINT OPERATIONS ==========
    // Scan fingerprint and return sensor slot ID (0 if not found)
    int scan();

    // Simple enroll - returns slot ID used, or 0 if failed
    int enroll(int slotId);

    // Delete template from sensor
    bool deleteTemplate(int slotId);

    // Get template count from sensor
    int getTemplateCount();

    // Find empty slot in sensor
    int findEmptySlot();

    // Non-blocking enrollment
    EnrollState getEnrollState();
    void startEnrollment(int slotId);
    bool processEnrollment();
    void resetEnrollment();

    // ========== AUTHENTICATION ==========
    // Non-blocking check - returns slot ID (0 if no finger, -1 if error, -2 if not found)
    int checkFingerprint();

    // Check if slot_id exists in sensor
    bool verifySlot(int slotId);

    // ========== FIND EMPTY SLOT FOR MAHASISWA (Slot 3-200) ==========
    // Langsung polling sensor R503 untuk cari slot kosong
    int findEmptySlotForMahasiswa();

    // Scan and authenticate - returns userId string
    String scanAndAuthenticate(FingerprintDataManager* dataManager);

    // Reset scan state
    void resetScan();

    // Sync from CSV - returns true if successful, false if flush failed
    bool syncFromCSV(UserManager* userManager);

    // ========== LED CONTROL ==========
    void setLEDStandby();
    void setLEDScanning();
    void setLEDSuccess();
    void setLEDError();
    void setLEDOff();

    // ========== DATABASE MANAGEMENT ==========
    // Clear all templates from sensor - returns true if successful
    bool hardResetDB();

    // Check if sensor is connected
    bool isConnected();

    // ========== ACCESS TO FINGER OBJECT FOR HEX OPERATIONS ==========
    Adafruit_Fingerprint_ESP32* getFinger() { return finger; }

    // ========== TUGAS 3: EXTRACT HEX TEMPLATE FROM SENSOR ==========
    // After enrollment, extract the template as hex string for CSV storage
    String extractTemplateAsHex(int slotId);

    // ========== PRESENSI: FLUSH & INJECT FOR SPECIFIC CLASS ==========
    // Flush sensor + inject sidik jari DOSEN YANG LOGIN (slot 1-2) + mahasiswa kelas ini.
    // dosenNip = NIP dosen yang sedang login; FP-nya di-inject ke slot 1-2 agar otorisasi
    // FP (start/stop presensi) cocok. Returns true if successful.
    bool flushAndInjectPresensiUsers(String kodeKelas, String kelas, String dosenNip);
};