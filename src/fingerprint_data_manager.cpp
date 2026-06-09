#include "fingerprint_data_manager.h"
#include "fingerprint.h"
#include "config.h"
#include "api_manager.h"
#include <SD.h>

extern SDCardManager sd;
extern APIManager apiManager;

// Helper: buat folder templates jika belum ada
void ensureTemplatesFolder() {
    if (!SD.exists("/templates")) {
        SD.mkdir("/templates");
    }
}

// FIXED [B]: Helper function to write CSV header - called when creating/truncating file
void ensureCSVHeader(File& f) {
    f.println("id,user_id,role,data_jari,created_at");
}

// ================= INIT =================
bool FingerprintDataManager::init() {
    ensureTemplatesFolder();

    // Initialize fingerprint_users.csv (dosen/admin)
    File f = sd.open("/fingerprint_users.csv", "r");
    if (!f) {
        f = sd.open("/fingerprint_users.csv", "w");
        if (f) {
            f.println("id,user_id,role,data_jari,created_at");
            f.close();
        }
    } else {
        f.close();
    }

    // Initialize fingerprint_mahasiswa.csv
    File m = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (!m) {
        m = sd.open("/fingerprint_mahasiswa.csv", "w");
        if (m) {
            m.println("id,user_id,role,data_jari,created_at");
            m.close();
        }
    } else {
        m.close();
    }

    return true;
}

// ================= GET TEMPLATE FROM FILE =================
bool FingerprintDataManager::loadTemplateFromFile(int id, uint8_t* buffer, size_t* bufferSize) {
    if (!buffer || !bufferSize) return false;

    String filename = "/templates/" + String(id) + ".bin";
    File f = sd.open(filename.c_str(), "r");
    if (!f) {
        Serial.println("Template file not found: " + filename);
        return false;
    }

    *bufferSize = f.readBytes((char*)buffer, MAX_TEMPLATE_SIZE);
    f.close();

    if (*bufferSize == 0) {
        Serial.println("Empty template file");
        return false;
    }

    Serial.println("Loaded " + String(*bufferSize) + " bytes from " + filename);
    return true;
}

// ================= ADD FINGERPRINT (ORIGINAL FORMAT) =================
bool FingerprintDataManager::addFingerprint(int id, String userId, String role, int slotId, String timestamp) {
    bool isMahasiswa = (role == "mahasiswa");
    const char* targetFile = isMahasiswa ? "/fingerprint_mahasiswa.csv" : "/fingerprint_users.csv";

    File checkFile = sd.open(targetFile, "r");
    if (checkFile) {
        while (checkFile.available()) {
            String line = checkFile.readStringUntil('\n');
            line.trim();
            if (line.length() < 5) continue;
            if (line.startsWith("id,") || line.startsWith("id,user")) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 > 0 && p2 > 0) {
                String csvUserId = line.substring(p1 + 1, p2);
                csvUserId.trim();
                csvUserId.replace("\r", "");
                if (csvUserId == userId) {
                    checkFile.close();
                    Serial.println("[ADD_FP] SKIP - user_id sudah ada: " + userId);
                    return false;
                }
            }
        }
        checkFile.close();
    }

    File f = sd.open(targetFile, "a");
    if (!f) {
        Serial.printf("Failed to open %s for append\n", targetFile);
        return false;
    }

    f.printf("%d,%s,%s,%d,%s\n", id, userId.c_str(), role.c_str(), slotId, timestamp.c_str());
    f.close();

    Serial.printf("Added fingerprint: ID=%d, UserId=%s, Role=%s, SlotId=%d to %s\n",
        id, userId.c_str(), role.c_str(), slotId, targetFile);
    return true;
}

bool FingerprintDataManager::addFingerprint(int id, String userId, String role) {
    return addFingerprint(id, userId, role, id, String(millis()));
}

// ================= ADD FINGERPRINT WITH HEX TEMPLATE =================
int FingerprintDataManager::addFingerprintWithHex(int id, String userId, String role, String hexTemplate, String timestamp) {
    String targetFile = (role == "mahasiswa") ? "/fingerprint_mahasiswa.csv" : "/fingerprint_users.csv";
    String currentTimestamp = timestamp;
    if (currentTimestamp == "API_SYNC" || currentTimestamp == "API_DISCONNECTED" || currentTimestamp.length() == 0) {
        currentTimestamp = apiManager.isTimeSynced() ? getFormattedTime() : "API_DISCONNECTED";
    }

    // Pastikan targetFile ada
    File testFile = sd.open(targetFile.c_str(), "r");
    if (!testFile) {
        testFile = sd.open(targetFile.c_str(), "w");
        if (testFile) {
            testFile.println("id,user_id,role,data_jari,created_at");
            testFile.close();
            Serial.printf("[ADD_FP_HEX] Created %s with header\n", targetFile.c_str());
        }
        // File baru, langsung append
        File f = sd.open(targetFile.c_str(), "a");
        if (f) {
            f.println(String(id) + "," + userId + "," + role + "," + hexTemplate + "," + currentTimestamp);
            f.close();
            Serial.printf("[ADD_FP_HEX] Added to %s: ID=%d, UserId=%s\n", targetFile.c_str(), id, userId.c_str());
            if (role == "mahasiswa") updateFingerprintIndex(id, userId);
            return 1; // 1 = Appended (New)
        }
        return 0; // 0 = Failed
    }
    testFile.close();

    // ========== FASE 1: SCAN CEPAT — cari NIM tanpa membaca hex (hemat RAM) ==========
    File scanFile = sd.open(targetFile.c_str(), "r");
    if (!scanFile) return 0; // 0 = Failed

    bool nimFound = false;
    String existingTimestamp = "";
    
    while (scanFile.available()) {
        String line = scanFile.readStringUntil('\n');
        if (line.length() < 5) continue;
        if (line.startsWith("id,")) continue;

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        if (p1 <= 0 || p2 <= 0) continue;

        String csvUserId = line.substring(p1 + 1, p2);
        csvUserId.trim();
        csvUserId.replace("\r", "");

        if (csvUserId == userId) {
            nimFound = true;
            // Ekstrak timestamp (kolom ke-5)
            int colRoleEnd = line.indexOf(',', p2 + 1);
            int colHexEnd  = (colRoleEnd > 0) ? line.indexOf(',', colRoleEnd + 1) : -1;
            if (colRoleEnd > 0 && colHexEnd > 0) {
                existingTimestamp = line.substring(colHexEnd + 1);
                existingTimestamp.trim();
                existingTimestamp.replace("\r", "");
            }
            break;  // Tidak perlu baca sisa file
        }
    }
    scanFile.close();

    // ========== FASE 2: DELTA CHECK — return langsung jika up-to-date ==========
    if (nimFound) {
        if (currentTimestamp != "API_DISCONNECTED" && currentTimestamp != "API_SYNC" && existingTimestamp.length() > 0) {
            if (currentTimestamp <= existingTimestamp) {
                Serial.printf_P(PSTR("[ADD_FP_HEX] Data lokal up-to-date untuk NIM: %s, skip overwrite!\n"), userId.c_str());
                return 3;  // 3 = Skipped (Up-to-date)
            }
        }
    }

    // ========== FASE 3: TULIS — hanya dijalankan jika ada perubahan nyata ==========
    if (!nimFound) {
        // NIM baru: cukup append (O(1), sangat cepat)
        File f = sd.open(targetFile.c_str(), "a");
        if (f) {
            f.println(String(id) + "," + userId + "," + role + "," + hexTemplate + "," + currentTimestamp);
            f.close();
            Serial.printf("[ADD_FP_HEX] Added to %s: ID=%d, UserId=%s\n", targetFile.c_str(), id, userId.c_str());
            if (role == "mahasiswa") updateFingerprintIndex(id, userId);
            return 1; // 1 = Appended (New)
        }
        return 0; // 0 = Failed
    }

    // NIM ada tapi timestamp lebih baru: streaming overwrite
    File readFile = sd.open(targetFile.c_str(), "r");
    if (!readFile) return 0; // 0 = Failed

    File tempFile = sd.open("/temp_fp.csv", "w");
    if (!tempFile) { readFile.close(); return 0; } // 0 = Failed

    while (readFile.available()) {
        String line = readFile.readStringUntil('\n');
        line.replace("\r", "");
        line.replace("\n", "");
        if (line.length() < 5) continue;

        if (line.startsWith("id,") || line.startsWith("id,user")) {
            tempFile.println(line);
            continue;
        }

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        if (p1 > 0 && p2 > 0) {
            String csvId = line.substring(0, p1);
            String csvUserId = line.substring(p1 + 1, p2);
            csvUserId.trim();
            csvUserId.replace("\r", "");

            if (csvUserId == userId) {
                // Overwrite dengan ID lama (mencegah slot leak)
                String csvRole = line.substring(p2 + 1);
                int p3 = csvRole.indexOf(',');
                if (p3 > 0) csvRole = csvRole.substring(0, p3);
                tempFile.println(csvId + "," + userId + "," + csvRole + "," + hexTemplate + "," + currentTimestamp);
                Serial.printf("[ADD_FP_HEX] Replaced data for: %s (Slot lama: %s)\n", userId.c_str(), csvId.c_str());
            } else {
                tempFile.println(line);
            }
        } else {
            tempFile.println(line);
        }
    }

    readFile.close();
    tempFile.close();

    SD.remove(targetFile.c_str());
    SD.rename("/temp_fp.csv", targetFile.c_str());

    return 2; // 2 = Overwritten
}

// ========== UPDATE FINGERPRINT INDEX FILE ==========
void FingerprintDataManager::updateFingerprintIndex(int slotId, String nim) {
    const char* indexFile = "/fp_index_mahasiswa.csv";
    String tempContent = "";
    bool foundAndReplaced = false;

    // Read existing index file
    File idxFile = sd.open(indexFile, "r");
    if (idxFile) {
        if (idxFile.available()) idxFile.readStringUntil('\n'); // Skip header
        while (idxFile.available()) {
            String line = idxFile.readStringUntil('\n');
            line.trim();
            line.replace("\r", "");
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;

            String existingSlot = line.substring(0, p1);
            String existingNim = line.substring(p1 + 1);
            existingNim.trim();

            if (existingNim == nim) {
                // Replace this line with new slot
                tempContent += String(slotId) + "," + nim + "\n";
                foundAndReplaced = true;
                Serial.printf("[INDEX_UPDATE] Replace: NIM=%s, Slot baru=%d\n", nim.c_str(), slotId);
            } else {
                tempContent += line + "\n";
            }
        }
        idxFile.close();
    }

    // If not found, append new entry
    if (!foundAndReplaced) {
        tempContent += String(slotId) + "," + nim + "\n";
        Serial.printf("[INDEX_UPDATE] Add new: NIM=%s, Slot=%d\n", nim.c_str(), slotId);
    }

    // Write back to index file
    File wFile = sd.open(indexFile, "w");
    if (wFile) {
        wFile.print("SlotID,NIM\n");
        wFile.print(tempContent);
        wFile.close();
        Serial.printf("[INDEX_UPDATE] File %s updated\n", indexFile);
    } else {
        Serial.printf("[INDEX_UPDATE] ERROR: Cannot write %s\n", indexFile);
    }
}

// ================= UPDATE FINGERPRINT SLOT =================
bool FingerprintDataManager::updateFingerprintSlot(String userId, String role, int slotId) {
    const char* targetFile = (role == "mahasiswa") ? "/fingerprint_mahasiswa.csv" : "/fingerprint_users.csv";
    const char* tempFileName = (role == "mahasiswa") ? "/temp_mhs.csv" : "/temp_users.csv";

    File f = sd.open(targetFile, "r");
    if (!f) {
        Serial.printf("[UPDATE_FP] ERROR: Cannot open %s for read\n", targetFile);
        return false;
    }

    bool idExists[256] = {false};
    String tempData = "";
    bool userFound = false;
    int existingId = 0;
    int newId = 0;

    String headerLine = "id,user_id,role,data_jari,created_at";
    String timestamp = getTimestamp();

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("id,user_id") || line.length() < 5) continue;

        int p1 = line.indexOf(',');
        if (p1 <= 0) continue;

        int fileId = line.substring(0, p1).toInt();
        if (fileId > 0 && fileId < 256) idExists[fileId] = true;

        int p2 = line.indexOf(',', p1 + 1);
        if (p2 <= 0) continue;

        String fileUserId = line.substring(p1 + 1, p2);
        if (fileUserId.length() == 0) continue;

        if (fileUserId == userId) {
            userFound = true;
            existingId = fileId;
            tempData += String(existingId) + "," + userId + "," + role + "," + String(slotId) + "," + timestamp + "\n";
            Serial.println("[UPDATE_FP] Updated existing user: " + userId + " with slot " + String(slotId));
        } else {
            tempData += line + "\n";
        }
    }
    f.close();

    if (!userFound) {
        newId = -1;
        for (int i = 1; i <= 255; i++) {
            if (!idExists[i]) { newId = i; break; }
        }
        if (newId <= 0) {
            Serial.println("[UPDATE_FP] ERROR: No available ID (sensor full)");
            return false;
        }
        tempData += String(newId) + "," + userId + "," + role + "," + String(slotId) + "," + timestamp + "\n";
    }

    File tempFile = sd.open(tempFileName, "w");
    if (!tempFile) return false;

    tempFile.println(headerLine);
    tempFile.print(tempData);
    tempFile.close();

    SD.remove(targetFile);
    if (!SD.rename(tempFileName, targetFile)) {
        File fw = sd.open(targetFile, "w");
        if (fw) { fw.println(headerLine); fw.print(tempData); fw.close(); }
        else return false;
    }

    int finalSlotId = userFound ? existingId : newId;
    if (finalSlotId > 0 && finalSlotId <= 200) {
        fingerMap[finalSlotId] = userId;
    }

    reloadData();
    return true;
}

// ================= UPDATE FINGERPRINT SLOT WITH HEX =================
bool FingerprintDataManager::updateFingerprintSlotWithHex(String userId, String role, int slotId, String hexTemplate) {
    String targetFile = (role == "mahasiswa") ? "/fingerprint_mahasiswa.csv" : "/fingerprint_users.csv";

    File testFile = sd.open(targetFile.c_str(), "r");
    if (!testFile) {
        testFile = sd.open(targetFile.c_str(), "w");
        if (testFile) { testFile.println("id,user_id,role,data_jari,created_at"); testFile.close(); }
    } else {
        testFile.close();
    }

    File f = sd.open(targetFile.c_str(), "r");
    if (!f) return false;

    bool idExists[256] = {false};
    String tempData = "";
    bool userFound = false;
    int existingId = 0;
    int newId = 0;

    String headerLine = "id,user_id,role,data_jari,created_at";
    String timestamp = getTimestamp();

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("id,user_id") || line.length() < 5) continue;

        int p1 = line.indexOf(',');
        if (p1 <= 0) continue;

        int fileId = line.substring(0, p1).toInt();
        if (fileId > 0 && fileId < 256) idExists[fileId] = true;

        int p2 = line.indexOf(',', p1 + 1);
        if (p2 <= 0) continue;

        String fileUserId = line.substring(p1 + 1, p2);
        if (fileUserId.length() == 0) continue;

        if (fileUserId == userId) {
            userFound = true;
            existingId = fileId;
            tempData += String(existingId) + "," + userId + "," + role + "," + hexTemplate + "," + timestamp + "\n";
        } else {
            tempData += line + "\n";
        }
    }
    f.close();

    if (!userFound) {
        newId = -1;
        for (int i = 1; i <= 255; i++) {
            if (!idExists[i]) { newId = i; break; }
        }
        if (newId <= 0) return false;
        tempData += String(newId) + "," + userId + "," + role + "," + hexTemplate + "," + timestamp + "\n";
    }

    String tempFileName = (role == "mahasiswa") ? "/temp_mhs.csv" : "/temp_users.csv";
    File tempFile = sd.open(tempFileName.c_str(), "w");
    if (!tempFile) return false;

    tempFile.println(headerLine);
    tempFile.print(tempData);
    tempFile.close();

    SD.remove(targetFile.c_str());
    if (!SD.rename(tempFileName.c_str(), targetFile.c_str())) {
        File fw = sd.open(targetFile.c_str(), "w");
        if (fw) { fw.println(headerLine); fw.print(tempData); fw.close(); }
        else return false;
    }

    int finalSlotId = userFound ? existingId : newId;
    if (finalSlotId > 0 && finalSlotId <= 200) fingerMap[finalSlotId] = userId;

    reloadData();
    return true;
}

// ================= GET TARGET ID FOR ENROLLMENT =================
int FingerprintDataManager::getTargetIdForEnrollment(String userId) {
    bool idExists[256] = {false};
    int existingCsvId = 0;
    bool userFound = false;
    bool dataJariIsEmpty = false;
    bool isMahasiswa = false;  // Cek apakah user dari fingerprint_mahasiswa.csv

    // ========== BACA fingerprint_users.csv (Dosen/Admin) ==========
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.startsWith("id,user_id") || line.length() < 5) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;

            int fileId = line.substring(0, p1).toInt();
            if (fileId > 0 && fileId < 256) idExists[fileId] = true;

            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;
            String csvUserId = line.substring(p1 + 1, p2);
            csvUserId.trim();

            if (csvUserId == userId) {
                userFound = true;
                existingCsvId = fileId;
                isMahasiswa = false;  // Bukan mahasiswa

                int p3 = line.indexOf(',', p2 + 1);
                if (p3 <= 0) continue;
                int p4 = line.indexOf(',', p3 + 1);

                String csvDataJari = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
                csvDataJari.trim();

                dataJariIsEmpty = (csvDataJari.length() == 0);
            }
        }
        f.close();
    }

    // ========== BACA fingerprint_mahasiswa.csv (Mahasiswa) ==========
    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.startsWith("id,user_id") || line.length() < 5) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;

            int fileId = line.substring(0, p1).toInt();
            if (fileId > 0 && fileId < 256) idExists[fileId] = true;

            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;
            String csvUserId = line.substring(p1 + 1, p2);
            csvUserId.trim();

            if (csvUserId == userId) {
                userFound = true;
                existingCsvId = fileId;
                isMahasiswa = true;  // Adalah mahasiswa

                int p3 = line.indexOf(',', p2 + 1);
                if (p3 <= 0) continue;
                int p4 = line.indexOf(',', p3 + 1);

                String csvDataJari = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
                csvDataJari.trim();

                dataJariIsEmpty = (csvDataJari.length() == 0);
            }
        }
        f.close();
    }

    // Jika sudah ada dan data_jari tidak kosong, return ID yang ada
    if (userFound && !dataJariIsEmpty) return existingCsvId;

    // ========== CARI SLOT KOSONG ==========
    // Dosen/Admin: mulai dari slot 1
    // Mahasiswa: mulai dari slot 3 (slot 1-2 reserved untuk Dosen/Admin)
    int startSlot = isMahasiswa ? 3 : 1;

    for (int i = startSlot; i <= 255; i++) {
        if (!idExists[i]) {
            Serial.printf_P(PSTR("[ENROLL] Menargetkan Slot: %d untuk %s (isMahasiswa=%d)\n"),
                i, userId.c_str(), isMahasiswa ? 1 : 0);
            return i;
        }
    }

    Serial.println(F("[ENROLL] ERROR: Semua slot (3-255) terisi untuk mahasiswa!"));
    return -1;  // Slot penuh
}

// ================= TIMESTAMP HELPER =================
String FingerprintDataManager::getTimestamp() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char buffer[50];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return String(buffer);
    }
    return "API_DISCONNECTED";
}

// ================= GET FINGERPRINT ID BY USER ID =================
int FingerprintDataManager::getFingerprintIdByUserId(String userId) {
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                int p3 = line.indexOf(',', p2 + 1);
                int p4 = line.indexOf(',', p3 + 1);
                String dataJari = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
                dataJari.trim();
                f.close();
                return dataJari.toInt();
            }
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                int p3 = line.indexOf(',', p2 + 1);
                int p4 = line.indexOf(',', p3 + 1);
                String dataJari = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
                dataJari.trim();
                f.close();
                return dataJari.toInt();
            }
        }
        f.close();
    }
    return 0;
}

// ================= GET CSV ID BY USER ID =================
int FingerprintDataManager::getCsvIdByUserId(String userId) {
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                int csvId = line.substring(0, p1).toInt();
                f.close();
                return csvId;
            }
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                int csvId = line.substring(0, p1).toInt();
                f.close();
                return csvId;
            }
        }
        f.close();
    }
    return 0;
}

// ================= CHECK IF USER EXISTS IN CSV =================
bool FingerprintDataManager::hasUserId(String userId) {
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) { f.close(); return true; }
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) { f.close(); return true; }
        }
        f.close();
    }
    return false;
}

// ================= DELETE FINGERPRINT =================
bool FingerprintDataManager::deleteFingerprint(int id) {
    String filename = "/templates/" + String(id) + ".bin";
    if (SD.exists(filename.c_str())) {
        SD.remove(filename.c_str());
    }

    bool deleted = false;

    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        String temp = "id,user_id,role,data_jari,created_at\n";
        bool fileDeleted = false;

        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.startsWith("id,user_id") || line.length() < 5) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;
            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId.length() == 0) continue;

            int fileId = line.substring(0, p1).toInt();
            if (fileId == id) { deleted = true; fileDeleted = true; continue; }
            temp += line + "\n";
        }
        f.close();

        if (fileDeleted) {
            SD.remove("/fingerprint_users.csv");
            File fw = sd.open("/fingerprint_users.csv", "w");
            fw.print(temp);
            fw.close();
        }
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        String temp = "id,user_id,role,data_jari,created_at\n";
        bool fileDeleted = false;

        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.startsWith("id,user_id") || line.length() < 5) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;
            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId.length() == 0) continue;

            int fileId = line.substring(0, p1).toInt();
            if (fileId == id) { deleted = true; fileDeleted = true; continue; }
            temp += line + "\n";
        }
        f.close();

        if (fileDeleted) {
            SD.remove("/fingerprint_mahasiswa.csv");
            File fw = sd.open("/fingerprint_mahasiswa.csv", "w");
            fw.print(temp);
            fw.close();
        }
    }

    return deleted;
}

// ================= DELETE FINGERPRINT BY USER ID =================
bool FingerprintDataManager::deleteFingerprintByUserId(String userId) {
    bool deleted = false;
    int deletedId = 0;

    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        String temp = "id,user_id,role,data_jari,created_at\n";
        bool fileDeleted = false;

        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.startsWith("id,user_id") || line.length() < 5) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId.length() == 0) continue;

            int fileId = line.substring(0, p1).toInt();
            if (fileUserId == userId) { deleted = true; fileDeleted = true; deletedId = fileId; continue; }
            temp += line + "\n";
        }
        f.close();

        if (fileDeleted) {
            SD.remove("/fingerprint_users.csv");
            File fw = sd.open("/fingerprint_users.csv", "w");
            fw.print(temp);
            fw.close();

            String filename = "/templates/" + String(deletedId) + ".bin";
            if (SD.exists(filename.c_str())) SD.remove(filename.c_str());
            if (deletedId > 0 && deletedId <= 200) fingerMap[deletedId] = "";
        }
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        String temp = "id,user_id,role,data_jari,created_at\n";
        bool fileDeleted = false;

        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.startsWith("id,user_id") || line.length() < 5) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId.length() == 0) continue;

            int fileId = line.substring(0, p1).toInt();
            if (fileUserId == userId) { deleted = true; fileDeleted = true; deletedId = fileId; continue; }
            temp += line + "\n";
        }
        f.close();

        if (fileDeleted) {
            SD.remove("/fingerprint_mahasiswa.csv");
            File fw = sd.open("/fingerprint_mahasiswa.csv", "w");
            fw.print(temp);
            fw.close();

            String filename = "/templates/" + String(deletedId) + ".bin";
            if (SD.exists(filename.c_str())) SD.remove(filename.c_str());
            if (deletedId > 0 && deletedId <= 200) fingerMap[deletedId] = "";
        }
    }

    return deleted;
}

// ================= GET USER ID =================
String FingerprintDataManager::getUserId(int id) {
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);
            int p4 = line.indexOf(',', p3 + 1);
            if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

            String dataJari = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
            dataJari.trim();

            if (String(id) == dataJari) {
                String userId = line.substring(p1 + 1, p2);
                f.close();
                return userId;
            }
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);
            int p4 = line.indexOf(',', p3 + 1);
            if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

            String dataJari = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
            dataJari.trim();

            if (String(id) == dataJari) {
                String userId = line.substring(p1 + 1, p2);
                f.close();
                return userId;
            }
        }
        f.close();
    }

    return "";
}

// ================= GET FINGERPRINT ID =================
int FingerprintDataManager::getFingerprintId(String userId) {
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 <= 0 || p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                int id = line.substring(0, p1).toInt();
                f.close();
                return id;
            }
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 <= 0 || p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                int id = line.substring(0, p1).toInt();
                f.close();
                return id;
            }
        }
        f.close();
    }

    return 0;
}

// ================= GET ROLE =================
String FingerprintDataManager::getRole(int id) {
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);
            int p4 = line.indexOf(',', p3 + 1);
            if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

            String dataJari = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
            dataJari.trim();

            if (String(id) == dataJari) {
                String role = line.substring(p2 + 1, p3);
                role.trim();
                f.close();
                return role;
            }
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);
            int p4 = line.indexOf(',', p3 + 1);
            if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

            String dataJari = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
            dataJari.trim();

            if (String(id) == dataJari) {
                String role = line.substring(p2 + 1, p3);
                role.trim();
                f.close();
                return role;
            }
        }
        f.close();
    }

    return "";
}

// ================= GET ROLE BY USER ID =================
String FingerprintDataManager::getRoleByUserId(String userId) {
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 <= 0 || p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                String role = line.substring(p2 + 1);
                role.trim();
                f.close();
                return role;
            }
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 <= 0 || p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                String role = line.substring(p2 + 1);
                role.trim();
                f.close();
                return role;
            }
        }
        f.close();
    }

    return "";
}

// ================= GET NEXT ID =================
int FingerprintDataManager::getNextID() {
    int lastID = 0;

    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;

            int id = line.substring(0, p1).toInt();
            if (id > lastID) lastID = id;
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;

            int id = line.substring(0, p1).toInt();
            if (id > lastID) lastID = id;
        }
        f.close();
    }

    return lastID + 1;
}

// ================= GET ALL USER ID =================
std::vector<String> FingerprintDataManager::getAllUserId() {
    std::vector<String> list;

    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 <= 0 || p2 <= 0) continue;

            String userId = line.substring(p1 + 1, p2);
            list.push_back(userId);
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 <= 0 || p2 <= 0) continue;

            String userId = line.substring(p1 + 1, p2);
            list.push_back(userId);
        }
        f.close();
    }

    return list;
}

// ================= SAVE TEMPLATE TO FILE =================
bool FingerprintDataManager::saveTemplateToFile(int id, const uint8_t* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) return false;

    String filename = "/templates/" + String(id) + ".bin";
    File f = sd.open(filename.c_str(), "w");
    if (!f) return false;

    size_t written = f.write(buffer, bufferSize);
    f.close();
    return written == bufferSize;
}

// ================= GET TEMPLATE COUNT =================
int FingerprintDataManager::getTemplateCount() {
    int count = 0;
    File dir = sd.open("/templates", "r");
    if (!dir) return 0;

    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        count++;
        entry.close();
    }
    dir.close();
    return count;
}

// ================= HAS FINGERPRINT =================
bool FingerprintDataManager::hasFingerprint(String userId) {
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);
            int p4 = line.indexOf(',', p3 + 1);
            if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                String dataJari = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
                dataJari.trim();
                f.close();
                return dataJari.length() > 0;
            }
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);
            int p4 = line.indexOf(',', p3 + 1);
            if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                String dataJari = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
                dataJari.trim();
                f.close();
                return dataJari.length() > 0;
            }
        }
        f.close();
    }

    return false;
}

// ================= GET FINGERPRINT INFO =================
bool FingerprintDataManager::getFingerprintInfo(String userId, int* id, String* role) {
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 <= 0 || p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                if (id) *id = line.substring(0, p1).toInt();
                if (role) { *role = line.substring(p2 + 1); role->trim(); }
                f.close();
                return true;
            }
        }
        f.close();
    }

    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 <= 0 || p2 <= 0) continue;

            String fileUserId = line.substring(p1 + 1, p2);
            if (fileUserId == userId) {
                if (id) *id = line.substring(0, p1).toInt();
                if (role) { *role = line.substring(p2 + 1); role->trim(); }
                f.close();
                return true;
            }
        }
        f.close();
    }

    return false;
}

// ================= GET USER ID BY SLOT =================
String FingerprintDataManager::getUserIdBySlot(int slotId) {
    if (slotId > 0 && slotId <= 200) {
        String cachedUserId = fingerMap[slotId];
        cachedUserId.trim();
        if (cachedUserId.length() > 0) {
            return cachedUserId;
        }
    }

    bool found = false;
    String resultUserId = "";

    File f = SD.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;
            if (line.startsWith("id,user_id")) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;

            String firstField = line.substring(0, p1);
            int idFromCsv = firstField.toInt();

            int p2 = line.indexOf(',', p1 + 1);
            if (p2 <= 0) continue;
            String csvUserId = line.substring(p1 + 1, p2);
            csvUserId.trim();

            if (String(idFromCsv) == String(slotId)) {
                found = true;
                resultUserId = csvUserId;
                fingerMap[slotId] = csvUserId;
                break;
            }
        }
        f.close();
    }

    if (!found) {
        f = SD.open("/fingerprint_mahasiswa.csv", "r");
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) continue;
                if (line.startsWith("id,user_id")) continue;

                int p1 = line.indexOf(',');
                if (p1 <= 0) continue;

                String firstField = line.substring(0, p1);
                int idFromCsv = firstField.toInt();

                int p2 = line.indexOf(',', p1 + 1);
                if (p2 <= 0) continue;
                String csvUserId = line.substring(p1 + 1, p2);
                csvUserId.trim();

                if (String(idFromCsv) == String(slotId)) {
                    found = true;
                    resultUserId = csvUserId;
                    fingerMap[slotId] = csvUserId;
                    break;
                }
            }
            f.close();
        }
    }

    return resultUserId;
}

// ================= GET ROLE BY SLOT =================
String FingerprintDataManager::getRoleBySlot(int slotId) {
    File f = SD.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;

            String firstField = line.substring(0, p1);
            int firstVal = firstField.toInt();

            if (firstVal > 0) {
                int p2 = line.indexOf(',', p1 + 1);
                if (p2 <= 0) continue;

                if (String(firstVal) == String(slotId)) {
                    String role = line.substring(p2 + 1);
                    role.trim();
                    f.close();
                    return role;
                }
            }
        }
        f.close();
    }

    f = SD.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            if (p1 <= 0) continue;

            String firstField = line.substring(0, p1);
            int firstVal = firstField.toInt();

            if (firstVal > 0) {
                int p2 = line.indexOf(',', p1 + 1);
                if (p2 <= 0) continue;

                if (String(firstVal) == String(slotId)) {
                    String role = line.substring(p2 + 1);
                    role.trim();
                    f.close();
                    return role;
                }
            }
        }
        f.close();
    }

    return "";
}

// ========== RELOAD FINGERMAP FROM CSV (FLASH-OPTIMIZED) ==========
// Repopulate fingerMap array from both fingerprint_users.csv and fingerprint_mahasiswa.csv
// Uses F() macro for all static strings to save RAM
void FingerprintDataManager::reloadData() {
    // Reset fingerMap array first
    for (int i = 0; i <= 200; i++) {
        fingerMap[i] = "";
    }

    int totalLoaded = 0;

    // ========== LOAD fingerprint_users.csv (Dosen/Admin) ==========
    File f = sd.open("/fingerprint_users.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() < 5) continue;
            if (line.startsWith(F("id,")) || line.startsWith(F("id,user"))) continue;

            // Format: id,user_id,role,data_jari,created_at
            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 <= 0 || p2 <= 0) continue;

            String idStr = line.substring(0, p1);
            String userId = line.substring(p1 + 1, p2);
            int slot = idStr.toInt();

            userId.trim();
            if (slot > 0 && slot <= 200 && userId.length() > 0) {
                fingerMap[slot] = userId;
                totalLoaded++;
            }
        }
        f.close();
    }

    // ========== LOAD fingerprint_mahasiswa.csv (Mahasiswa) ==========
    f = sd.open("/fingerprint_mahasiswa.csv", "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() < 5) continue;
            if (line.startsWith(F("id,")) || line.startsWith(F("id,user"))) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            if (p1 <= 0 || p2 <= 0) continue;

            String idStr = line.substring(0, p1);
            String userId = line.substring(p1 + 1, p2);
            int slot = idStr.toInt();

            userId.trim();
            if (slot > 0 && slot <= 200 && userId.length() > 0) {
                fingerMap[slot] = userId;
                totalLoaded++;
            }
        }
        f.close();
    }

    Serial.printf_P(PSTR("[RELOAD] Loaded %d fingerprint mappings to RAM\n"), totalLoaded);
}

// ================= FACTORY RESET =================
bool FingerprintDataManager::formatAllFingerprintData(FingerprintManager* fpManager) {
    Serial.println("[FORMAT] ========== FACTORY RESET ==========");

    if (fpManager) {
        fpManager->hardResetDB();
        Serial.println("[FORMAT] Sensor R503 dikosongkan");
    }

    SD.remove("/fingerprint_users.csv");
    File fw = sd.open("/fingerprint_users.csv", "w");
    if (fw) { fw.println("id,user_id,role,data_jari,created_at"); fw.close(); }

    SD.remove("/fingerprint_mahasiswa.csv");
    fw = sd.open("/fingerprint_mahasiswa.csv", "w");
    if (fw) { fw.println("id,user_id,role,data_jari,created_at"); fw.close(); }
    else return false;

    Serial.println("[FORMAT] ========== FACTORY RESET SELESAI ==========");
    return true;
}