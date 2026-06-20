#pragma once
#include <Arduino.h>
#include <vector>
#include "sdcard.h"

// Forward declaration
class FingerprintManager;

#define MAX_TEMPLATE_SIZE 1024

class FingerprintDataManager {
public:
    bool init();

    // ================= CSV OPERATIONS =================
    // TUGAS 1: Add fingerprint with format: id,user_id,role,data_jari,created_at
    bool addFingerprint(int id, String userId, String role, int slotId, String timestamp);

    // Legacy: Add without slotId (auto-generate)
    bool addFingerprint(int id, String userId, String role);

    // TUGAS 3: Add fingerprint with hex template data (for new enrollment)
    int addFingerprintWithHex(int id, String userId, String role, String hexTemplate, String timestamp);

    // Update fingerprint index file (fp_index_mahasiswa.csv)
    void updateFingerprintIndex(int slotId, String nim);

    // Delete fingerprint by ID
    bool deleteFingerprint(int id);

    // Delete fingerprint by user_id (NIM/NIP)
    bool deleteFingerprintByUserId(String userId);

    // Get user_id (NIM/NIP) from fingerprint ID
    String getUserId(int id);

    // Get fingerprint ID from user_id
    int getFingerprintId(String userId);

    // Get role from fingerprint ID
    String getRole(int id);

    // Get role from user_id
    String getRoleByUserId(String userId);

    // Get next available fingerprint ID
    int getNextID();

    // Get all user_ids
    std::vector<String> getAllUserId();

    // ================= TEMPLATE OPERATIONS =================
    // Load template from file to buffer
    bool loadTemplateFromFile(int id, uint8_t* buffer, size_t* bufferSize);

    // Save template from buffer to file
    bool saveTemplateToFile(int id, const uint8_t* buffer, size_t bufferSize);

    // Get template count
    int getTemplateCount();

    // ================= HELPER =================
    // Check if user_id has fingerprint
    bool hasFingerprint(String userId);

    // Get fingerprint info by user_id
    bool getFingerprintInfo(String userId, int* id, String* role);

    // ========== SLOT-BASED LOOKUP ==========
    // Get user_id by slot_id (for simple fingerprint auth)
    String getUserIdBySlot(int slotId);

    // Get role by slot_id
    String getRoleBySlot(int slotId);

    // ========== UPDATE OPERATIONS ==========
    // Update fingerprint slot (untuk enroll/update) - cari user_id lalu update data_jari
    bool updateFingerprintSlot(String userId, String role, int slotId);

    // TUGAS 3: Update fingerprint slot with hex template data
    bool updateFingerprintSlotWithHex(String userId, String role, int slotId, String hexTemplate);

    // Get fingerprint ID (slot) by user_id - cari di kolom data_jari
    int getFingerprintIdByUserId(String userId);

    // Get CSV ID (kolom pertama) by user_id - ini digunakan untuk slot sensor
    int getCsvIdByUserId(String userId);

    // Check if user_id exists in CSV
    bool hasUserId(String userId);

    // ========== TUGAS 1: DETERMINE TARGET ID BEFORE ENROLL ==========
    // Cek apakah user sudah ada di CSV, jika ya return ID-nya
    // Jika belum, cari lowest available ID (1-255)
    int getTargetIdForEnrollment(String userId);

    // ========== TUGAS 3: RELOAD/REFRESH CACHE ==========
    // Reload data from CSV (for consistency after enrollment)
    void reloadData();

    // ========== TUGAS 1: FACTORY RESET ==========
    // Format all fingerprint data - clears sensor + CSV
    bool formatAllFingerprintData(FingerprintManager* fpManager);

    // ========== HELPER: TIMESTAMP ==========
    // Get timestamp for CSV - returns NTP time or "API_DISCONNECTED"
    String getTimestamp();

    // Get local timestamp for a specific user ID
    String getLocalFpTimestamp(String userId, String role = "mahasiswa");

    // ========== PUBLIC: SYNC POINTER FOR STATE MACHINE ==========
    // Pointer to syncFromCSV function in FingerprintManager
    void (*syncCallback)();
};