#include "user_manager.h"
#include <SD.h>
#include "mbedtls/md.h"

extern SDCardManager sd;

// FIXED [B]: Helper function to write CSV header (local to this file)
void ensureFingerprintCSVHeader(File& f) {
    f.println("id,user_id,role,data_jari,created_at");
}

// ================= INIT =================
bool UserManager::init() {
    // Initialize users.csv (authentication) - FORMAT BARU dengan fingerprint_id
    File f = sd.open("/users.csv", FILE_READ);
    if (!f) {
        f = sd.open("/users.csv", FILE_WRITE);
        if (f) {
            f.println("id,nip,pin,role,fingerprint_id,updated_at");
            f.close();
        }
    } else {
        f.close();
    }

    // Initialize dosen.csv (profil)
    f = sd.open("/dosen.csv", FILE_READ);
    if (!f) {
        f = sd.open("/dosen.csv", FILE_WRITE);
        if (f) {
            f.println("nip,nama_lengkap,created_at");
            f.close();
        }
    } else {
        f.close();
    }

    return true;
}

// ================= GET NEXT USER ID =================
int getNextUserID() {
    File f = sd.open("/users.csv", FILE_READ);
    int lastID = 0;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        int comma = line.indexOf(',');
        if (comma > 0) {
            int id = line.substring(0, comma).toInt();
            if (id > lastID) lastID = id;
        }
    }
    f.close();
    return lastID + 1;
}

// ================= ADD USER (to users.csv) =================
bool UserManager::addUser(String nip, String pin, String role) {
    if (nip == "123") return false;

    File f = sd.open("/users.csv", FILE_APPEND);
    if (!f) return false;

    int id = getNextUserID();
    f.print(id); f.print(",");
    f.print(nip); f.print(",");
    f.print(pin); f.print(",");
    f.println(role);
    f.close();

    return true;
}

// ================= CHECK USER =================
bool UserManager::checkUser(String nip, String pin) {
    File f = sd.open("/users.csv", FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);

        if (p1 < 0 || p2 < 0 || p3 < 0) continue;

        String fileNip = line.substring(p1 + 1, p2);
        String filePin = line.substring(p2 + 1, p3);

        if (fileNip == nip) {
            // Check if filePin is a sha256 pinv1 hash
            if (filePin.startsWith("pinv1$sha256$")) {
                int firstDollar = filePin.indexOf('$');
                int secondDollar = filePin.indexOf('$', firstDollar + 1);
                int thirdDollar = filePin.indexOf('$', secondDollar + 1);

                if (firstDollar > 0 && secondDollar > 0 && thirdDollar > 0) {
                    String salt = filePin.substring(secondDollar + 1, thirdDollar);
                    String dbHash = filePin.substring(thirdDollar + 1);

                    String payload = salt + ":" + nip + ":" + pin;
                    
                    // SHA-256 Calculation
                    byte shaResult[32];
                    mbedtls_md_context_t ctx;
                    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

                    mbedtls_md_init(&ctx);
                    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
                    mbedtls_md_starts(&ctx);
                    mbedtls_md_update(&ctx, (const unsigned char*)payload.c_str(), payload.length());
                    mbedtls_md_finish(&ctx, shaResult);
                    mbedtls_md_free(&ctx);

                    String computedHash = "";
                    for (int i = 0; i < 32; i++) {
                        char buf[3];
                        sprintf(buf, "%02x", shaResult[i]);
                        computedHash += buf;
                    }

                    if (computedHash == dbHash) {
                        f.close();
                        return true;
                    }
                }
            } else if (filePin.startsWith("$2a$") || filePin.startsWith("$2b$") || filePin.startsWith("$2y$")) {
                // Ignore bcrypt
            } else {
                // Fallback to plain text comparison
                if (filePin == pin) {
                    f.close();
                    return true;
                }
            }
        }
    }
    f.close();
    return false;
}

// ================= CHANGE PASSWORD =================
bool UserManager::changePassword(String nip, String newPin) {
    File f = sd.open("/users.csv", FILE_READ);
    if (!f) return false;

    String temp = "";
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            temp += line + "\n";
            continue;
        }

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);

        if (p1 < 0 || p2 < 0 || p3 < 0) {
            temp += line + "\n";
            continue;
        }

        String id = line.substring(0, p1);
        String fileNip = line.substring(p1 + 1, p2);
        String filePin = line.substring(p2 + 1, p3);
        String role = line.substring(p3 + 1);

        if (fileNip == nip) {
            filePin = newPin;
        }
        temp += id + "," + fileNip + "," + filePin + "," + role + "\n";
    }
    f.close();

    SD.remove("/users.csv");
    File fw = sd.open("/users.csv", FILE_WRITE);
    fw.print(temp);
    fw.close();

    return true;
}

// ================= DELETE USER =================
bool UserManager::deleteUser(String nip) {
    File f = sd.open("/users.csv", FILE_READ);
    if (!f) return false;

    String temp = "";
    bool deleted = false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p1 = line.indexOf(',');
        if (p1 < 0) continue;

        String fileNip = line.substring(p1 + 1, line.indexOf(',', p1 + 1));
        if (fileNip == nip) {
            deleted = true;
            continue;
        }
        temp += line + "\n";
    }
    f.close();

    if (deleted) {
        SD.remove("/users.csv");
        File fw = sd.open("/users.csv", FILE_WRITE);
        fw.print(temp);
        fw.close();
    }

    return deleted;
}

// ================= IS ADMIN =================
bool UserManager::isAdmin(String nip) {
    File f = sd.open("/users.csv", FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        // Format: id,nip,pin,role,fingerprint_id
        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);
        int p4 = line.indexOf(',', p3 + 1);   // batas akhir role (sebelum fingerprint_id)
        if (p1 < 0 || p2 < 0 || p3 < 0) continue;

        String fileNip = line.substring(p1 + 1, p2);
        fileNip.trim();
        // Ekstraksi role HARUS dibatasi p4; kalau tidak, kita malah ambil "admin,0"
        // -> string compare ke "admin" selalu false (bug routing admin -> dosen).
        String role = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
        role.trim();
        role.replace("\r", "");

        if (fileNip == nip) {
            f.close();
            role.toLowerCase();           // case-insensitive: "Admin"/"ADMIN" tetap lewat
            return (role == "admin");
        }
    }
    f.close();
    return false;
}

// ================= IS DOSEN =================
bool UserManager::isDosen(String nip) {
    File f = sd.open("/users.csv", FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);
        int p4 = line.indexOf(',', p3 + 1);   // batas akhir role
        if (p1 < 0 || p2 < 0 || p3 < 0) continue;

        String fileNip = line.substring(p1 + 1, p2);
        fileNip.trim();
        String role = (p4 > p3) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);
        role.trim();
        role.replace("\r", "");

        if (fileNip == nip) {
            f.close();
            role.toLowerCase();
            return (role == "dosen");
        }
    }
    f.close();
    return false;
}

// ================= GET ALL DOSEN =================
std::vector<String> UserManager::getAllDosen() {
    std::vector<String> list;

    File f = sd.open("/users.csv", FILE_READ);
    if (!f) return list;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);

        if (p1 < 0 || p2 < 0 || p3 < 0) continue;

        String fileNip = line.substring(p1 + 1, p2);
        String role = line.substring(p3 + 1);
        role.trim();

        if (role == "dosen") {
            list.push_back(fileNip);
        }
    }
    f.close();
    return list;
}

// ================= ADD DOSEN (to dosen.csv) =================
bool UserManager::addDosen(String nip, String nama) {
    if (dosenExists(nip)) return false;

    File f = sd.open("/dosen.csv", FILE_APPEND);
    if (!f) return false;

    uint32_t ts = millis();
    f.print(nip); f.print(",");
    f.print(nama); f.print(",");
    f.println(ts);
    f.close();

    return true;
}

// ================= DELETE DOSEN =================
bool UserManager::deleteDosen(String nip) {
    File f = sd.open("/dosen.csv", FILE_READ);
    if (!f) return false;

    String temp = "";
    bool deleted = false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int comma = line.indexOf(',');
        if (comma <= 0) continue;

        String fileNip = line.substring(0, comma);
        if (fileNip == nip) {
            deleted = true;
            continue;
        }
        temp += line + "\n";
    }
    f.close();

    if (deleted) {
        SD.remove("/dosen.csv");
        File fw = sd.open("/dosen.csv", FILE_WRITE);
        fw.print(temp);
        fw.close();
    }

    return deleted;
}

// ================= GET DOSEN NAMA =================
String UserManager::getDosenNama(String nip) {
    File f = sd.open("/dosen.csv", FILE_READ);
    if (!f) return "";

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p1 = line.indexOf(',');
        if (p1 <= 0) continue;

        String fileNip = line.substring(0, p1);
        if (fileNip == nip) {
            int p2 = line.indexOf(',', p1 + 1);
            String nama = line.substring(p1 + 1, p2);
            f.close();
            return nama;
        }
    }
    f.close();
    return "";
}

// ================= DOSEN EXISTS =================
bool UserManager::dosenExists(String nip) {
    File f = sd.open("/dosen.csv", FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int comma = line.indexOf(',');
        if (comma <= 0) continue;

        String fileNip = line.substring(0, comma);
        if (fileNip == nip) {
            f.close();
            return true;
        }
    }
    f.close();
    return false;
}

// ================= DELETE FINGERPRINT =================
// TUGAS 3: Delete from appropriate file based on role
bool UserManager::deleteFingerprint(String user_id, String role) {
    user_id.trim();
    role.trim();

    Serial.println("[DELETE_FP] user_id: '" + user_id + "' role: '" + role + "'");

    // TUGAS 3: Tentukan file target berdasarkan role
    String targetFile = (role == "mahasiswa") ? "/fingerprint_mahasiswa.csv" : "/fingerprint_users.csv";

    // Jika file target belum ada, tidak ada yang perlu dihapus
    File testFile = sd.open(targetFile.c_str(), FILE_READ);
    if (!testFile) {
        Serial.println("[DELETE_FP] File tidak ada: " + String(targetFile.c_str()));
        return false;
    }
    testFile.close();

    File f = sd.open(targetFile.c_str(), FILE_READ);
    if (!f) {
        Serial.println("[DELETE_FP] Cannot open: " + String(targetFile.c_str()));
        return false;
    }

    String temp = "";
    bool deleted = false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        Serial.println("[DELETE_FP] Line: " + line);

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);
        if (p1 < 0 || p2 < 0 || p3 < 0) continue;

        // CSV: id,user_id,role,data_jari,created_at
        String fileUserId = line.substring(p1 + 1, p2);
        String fileRole = line.substring(p2 + 1, p3);

        // Trim untuk hapus whitespace
        fileUserId.trim();
        fileRole.trim();

        Serial.println("[DELETE_FP] fileUserId: '" + fileUserId + "' fileRole: '" + fileRole + "'");

        if (fileUserId == user_id && fileRole == role) {
            Serial.println("[DELETE_FP] Match! Menghapus...");
            deleted = true;
            continue;
        }
        temp += line + "\n";
    }
    f.close();

    if (deleted) {
        SD.remove(targetFile.c_str());
        File fw = sd.open(targetFile.c_str(), FILE_WRITE);
        ensureFingerprintCSVHeader(fw);  // Write header before data
        fw.print(temp);
        fw.close();
        Serial.println("[DELETE_FP] Berhasil dihapus dari " + targetFile);
        return true;
    }
    Serial.println("[DELETE_FP] Gagal - data tidak ditemukan");
    return false;
}

// ================= SAVE OR UPDATE FINGERPRINT =================
// TUGAS 3: Save to appropriate file based on role
bool UserManager::saveOrUpdateFingerprint(String user_id, String role, String data_jari) {
    user_id.trim();
    role.trim();

    Serial.println("[SAVE_FP] OVERWRITE CHECK:");
    Serial.println("user_id: " + user_id);
    Serial.println("role: " + role);
    Serial.println("data_jari: " + data_jari);

    // TUGAS 3: Tentukan file target berdasarkan role
    String targetFile = (role == "mahasiswa") ? "/fingerprint_mahasiswa.csv" : "/fingerprint_users.csv";

    struct FPData {
        int id;
        String user_id;
        String role;
        String data_jari;
        String created_at;
    };

    std::vector<FPData> fpList;
    int maxId = 0;
    bool found = false;
    int existingId = 0;
    String timestamp = "2026-04-10";

    // TUGAS 3: Read from target file
    File f = sd.open(targetFile.c_str(), FILE_READ);
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);
            int p4 = line.indexOf(',', p3 + 1);
            if (p1 < 0 || p2 < 0 || p3 < 0) continue;

            FPData data;
            data.id = line.substring(0, p1).toInt();
            data.user_id = line.substring(p1 + 1, p2);
            data.role = line.substring(p2 + 1, p3);
            data.data_jari = line.substring(p3 + 1, p4);
            data.created_at = line.substring(p4 + 1);

            data.user_id.trim();
            data.role.trim();
            data.id = data.id;

            if (data.id > maxId) maxId = data.id;

            // Cek match user_id + role
            if (data.user_id == user_id && data.role == role) {
                Serial.println("[SAVE_FP] OVERWRITE fingerprint lama! ID: " + String(data.id));
                existingId = data.id;
                found = true;
                continue; // Skip old entry
            }

            fpList.push_back(data);
        }
        f.close();
    }

    // Add or update data
    FPData newData;
    newData.id = found ? existingId : maxId + 1;
    newData.user_id = user_id;
    newData.role = role;
    newData.data_jari = data_jari;
    newData.created_at = timestamp;
    fpList.push_back(newData);

    // Sort by ID
    std::sort(fpList.begin(), fpList.end(), [](FPData a, FPData b) {
        return a.id < b.id;
    });

    // TUGAS 3: Write to target file
    SD.remove(targetFile.c_str());
    File fw = sd.open(targetFile.c_str(), FILE_WRITE);
    ensureFingerprintCSVHeader(fw);  // Write header before data

    for (size_t i = 0; i < fpList.size(); i++) {
        String line = String(fpList[i].id) + "," + fpList[i].user_id + "," +
                      fpList[i].role + "," + fpList[i].data_jari + "," +
                      fpList[i].created_at;
        fw.println(line);
    }
    fw.close();

    Serial.println("[SAVE_FP] Berhasil disimpan ke " + targetFile + "! Total: " + String(fpList.size()));
    return true;
}