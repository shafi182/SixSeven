#include "fingerprint.h"
#include "config.h"
#include "user_manager.h"
#include "api_manager.h"   // readFp2Sidecar() untuk Adaptive Loading fp_2
#include <SD.h>

#include <Arduino.h>
#include <vector>
#include <algorithm>

// ========== EXTERNAL FINGERPINT MAP ( dari api_manager.cpp ) ==========
extern String fingerMap[201];

// ========== DEBUG: PRINT SELURUH FINGERPINT MAP ==========
void printFingerMap() {
    Serial.println(F("========================================"));
    Serial.println(F("========== FINGERMAP DEBUG VIEW =========="));
    Serial.println(F("========================================"));

    int count = 0;
    for (int i = 1; i <= 200; i++) {
        if (fingerMap[i] && fingerMap[i].length() > 0) {
            Serial.printf_P(PSTR("Slot[%03d] = %s\n"), i, fingerMap[i].c_str());
            count++;
        }
    }

    Serial.println(F("========================================"));
    Serial.printf_P(PSTR("Total fingerprint terregister: %d\n"), count);
    Serial.println(F("========================================"));
}

const char* fpGetErrorString(uint8_t err) {
    switch (err) {
        case FP_OK: return "OK";
        case FP_NOFINGER: return "No finger";
        case FP_ENROLLMISMATCH: return "Enroll mismatch";
        case FP_BADLOCATION: return "Bad location";
        case FP_FLASHERR: return "Flash error";
        case FP_INVALIDIMAGE: return "Invalid image";
        case FP_FEATUREFAIL: return "Feature fail";
        case FP_IMAGEFAIL: return "Image fail";
        case FP_IMAGEMESS: return "Image mess";
        case FP_PACKETRECEIVEERR: return "Packet error";
        default: return "Unknown error";
    }
}

FingerprintManager::FingerprintManager() {
    // TUGAS 1: Use HardwareSerial(2) with RX 32, TX 33
    serial = new HardwareSerial(2);
    finger = new Adafruit_Fingerprint_ESP32(serial);
    enrollState = ENROLL_IDLE;
    enrollId = 0;
}

void FingerprintManager::init() {
    // ========== NAIK GIGI ke 115200 bps (datasheet R503: max N=12) ==========
    // Sensor menyimpan baud rate terakhir di flash internal -> setelah upgrade
    // pertama, boot berikutnya sensor langsung start di 115200. Jadi kita coba
    // 115200 dulu (boot ke-2 dst), fallback ke 57600 + upgrade (boot pertama).
    bool connected = false;

    // --- Percobaan A: langsung 115200 (kasus boot ke-2 dst, sensor sudah di-upgrade) ---
    serial->begin(115200, SERIAL_8N1, FP_RX, FP_TX);
    delay(300);
    if (finger->verify_password() == FP_OK) {
        Serial.println(F("[INIT] Sensor R503 sudah pada 115200 bps (skip upgrade)"));
        connected = true;
    } else {
        // --- Percobaan B: 57600 default + kirim SetSysPara untuk naik ke 115200 ---
        serial->end();
        delay(100);
        serial->begin(57600, SERIAL_8N1, FP_RX, FP_TX);
        delay(300);

        if (finger->begin(57600, FP_RX, FP_TX)) {
            Serial.println(F("[INIT] Sensor terkoneksi di 57600 - upgrading ke 115200..."));

            // ========== Kirim raw SetSysPara: BaudRate = 12 (12 * 9600 = 115200) ==========
            // Packet R503 (per datasheet):
            //   header(2) addr(4) pid(1)=0x01 length(2)=0x0005
            //   instr(1)=0x0E param(1)=0x04 value(1)=0x0C  checksum(2)=0x0028
            // checksum = pid + length_hi + length_lo + instr + param + value
            //          = 0x01 + 0x00 + 0x05 + 0x0E + 0x04 + 0x0C = 0x28
            static const uint8_t SET_BAUD_115200[] = {
                0xEF, 0x01,                          // header
                0xFF, 0xFF, 0xFF, 0xFF,              // address (default)
                0x01,                                // PID = command
                0x00, 0x05,                          // length = 5
                0x0E, 0x04, 0x0C,                    // SetSysPara, param=4 (Baud), value=12
                0x00, 0x28                           // checksum
            };
            serial->write(SET_BAUD_115200, sizeof(SET_BAUD_115200));
            serial->flush();

            // ACK 12-byte masih datang di 57600. Kuras buffer sebelum ganti baud.
            unsigned long t0 = millis();
            while (millis() - t0 < 250) {
                while (serial->available()) serial->read();
                if (millis() - t0 > 100) break;
                delay(5);
            }

            // Reinit UART ESP32 di 115200 + beri sensor waktu menyesuaikan.
            serial->end();
            delay(150);
            serial->begin(115200, SERIAL_8N1, FP_RX, FP_TX);
            delay(300);  // window penyesuaian sensor

            // Verifikasi komunikasi pada baud baru
            if (finger->verify_password() == FP_OK) {
                Serial.println(F("[INIT] Upgrade BERHASIL - komunikasi di 115200 bps"));
                connected = true;
            } else {
                Serial.println(F("[INIT] Upgrade GAGAL - fallback ke 57600 bps"));
                serial->end();
                delay(100);
                serial->begin(57600, SERIAL_8N1, FP_RX, FP_TX);
                delay(300);
                connected = (finger->verify_password() == FP_OK);
            }
        }
    }

    if (connected) {
        Serial.println("[INIT] Fingerprint sensor found!");

        // Read system parameters
        uint8_t p = finger->read_sysparam();
        if (p == FP_OK) {
            // Cek dan ubah Data Package Size menjadi 256 byte jika belum (param 6, value 3)
            if (finger->data_packet_size != 3) {
                Serial.println("[INIT] Meningkatkan Data Package Size ke 256 byte...");
                if (finger->set_sysparam(6, 3) == FP_OK) {
                    Serial.println("[INIT] Data Package Size berhasil di-set ke 256 byte!");
                    delay(50);
                    finger->read_sysparam(); // Baca ulang agar nilai tersinkron
                } else {
                    Serial.println("[INIT] Gagal mengatur Data Package Size.");
                }
            }

            Serial.println("[INIT] ========== SENSOR PARAMETERS ==========");
            Serial.println("[INIT] Library Size: " + String(finger->library_size));
            Serial.println("[INIT] Security Level: " + String(finger->security_level));
            Serial.println("[INIT] Template Count: " + String(finger->template_count));
            Serial.println("[INIT] Baud Param (N): " + String(finger->baudrate_param) + " -> " + String((int)finger->baudrate_param * 9600) + " bps");
            Serial.println("[INIT] Pkt Size Param: " + String(finger->data_packet_size) + " (3 = 256B)");
        }
    } else {
        Serial.println("[INIT] Fingerprint sensor NOT FOUND!");
    }
}

bool FingerprintManager::isConnected() {
    return (finger->verify_password() == FP_OK);
}

// ========== TUGAS 1: GET LOWEST AVAILABLE FINGERPRINT ID (IMPROVED) ==========
// Cek slot yang benar-benar kosong di sensor (bukan hanya di CSV)
int FingerprintManager::getLowestAvailableFingerprintId() {
    // ========== TUGAS 1: Prioritaskan slot 3-200 untuk Mahasiswa ==========
    // Slot 1-2 reserved untuk Dosen
    // Pertama, cek sensor langsung untuk slot yang benar-benar kosong

    for (int i = 3; i <= finger->library_size; i++) {
        // Coba load model dari sensor
        uint8_t p = finger->load_model(i, 1);
        if (p == FP_NOTFOUND) {
            // Slot kosong di sensor
            Serial.printf("[GET_AVAIL_ID] Found empty slot in sensor: %d\n", i);
            return i;
        }
        // Slot sudah terisi atau error, lanjut ke slot berikutnya
    }

    // ========== TUGAS 3: Jika semua slot penuh, cek CSV untuk cari slot yang bisa di-overwrite ==========
    Serial.println("[GET_AVAIL_ID] All slots in sensor may be full, checking CSV...");

    // Read from fingerprint_mahasiswa.csv untuk cari slot yang bisa overwrite
    File file = SD.open("/fingerprint_mahasiswa.csv", FILE_READ);
    if (file) {
        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() < 3) continue;
            if (line.startsWith("id,") || line.startsWith("id,user")) continue;

            int p1 = line.indexOf(',');
            if (p1 > 0) {
                String idStr = line.substring(0, p1);
                idStr.trim();
                int id = idStr.toInt();
                // Slot 3+ yang sudah ada di CSV bisa di-overwrite
                if (id >= 3 && id <= finger->library_size) {
                    Serial.printf("[GET_AVAIL_ID] Can overwrite slot %d (mahasiswa exists in CSV)\n", id);
                    file.close();
                    return id;
                }
            }
        }
        file.close();
    }

    Serial.println("[GET_AVAIL_ID] No available ID found!");
    return 0;
}

// ========== TUGAS 1: LOAD TEMPLATES FROM CSV AT BOOT ==========
// Load templates from BOTH fingerprint_users.csv (dosen/admin) AND fingerprint_mahasiswa.csv (mahasiswa)
void FingerprintManager::loadTemplatesFromCSV(FingerprintDataManager* dataManager) {
    Serial.println("[INIT] ========== LOAD TEMPLATES FROM CSV ==========");

    // Initialize fingerMap array
    for (int i = 0; i <= 200; i++) {
        fingerMap[i] = "";
    }

    // ========== TUGAS 4: INIT - Kosongkan sensor dengan hati-hati ==========
    // Jangan kosongkan semua, karena akan kehilangan template dosen
    // Tapi bersihkan dulu untuk memastikan keadaan bersih

    int loadedCount = 0;

    // ========== TUGAS 1: LOAD fingerprint_users.csv (Dosen/Admin) DULU ==========
    // Load dosen dulu karena slot 1-2 reserved untuk mereka
    Serial.println("[INIT] ===== Loading Dosen fingerprints (slot 1-2) =====");
    File file = SD.open("/fingerprint_users.csv", FILE_READ);
    if (!file) {
        Serial.println("[INIT] Cannot open fingerprint_users.csv - creating new file");
        file = SD.open("/fingerprint_users.csv", FILE_WRITE);
        if (file) {
            file.println("id,user_id,role,data_jari,created_at");
            file.close();
            Serial.println("[INIT] Created fingerprint_users.csv with header");
        }
    } else {
        loadedCount += loadTemplatesFromFile(file, "fingerprint_users.csv");
        file.close();
    }

    // ========== TUGAS 4: JEDA ANTAR LOAD ==========
    // Beri waktu sensor untuk menyelesaikan write ke flash
    Serial.println("[INIT] Jeda 500ms sebelum load mahasiswa...");
    delay(500);

    // ========== TUGAS 3: LOAD fingerprint_mahasiswa.csv (Mahasiswa) ==========
    Serial.println("[INIT] ===== Loading Mahasiswa fingerprints (slot 3+) =====");
    File mhsFile = SD.open("/fingerprint_mahasiswa.csv", FILE_READ);
    if (!mhsFile) {
        Serial.println("[INIT] Cannot open fingerprint_mahasiswa.csv - creating new file");
        mhsFile = SD.open("/fingerprint_mahasiswa.csv", FILE_WRITE);
        if (mhsFile) {
            mhsFile.println("id,user_id,role,data_jari,created_at");
            mhsFile.close();
            Serial.println("[INIT] Created fingerprint_mahasiswa.csv with header");
        }
    } else {
        loadedCount += loadTemplatesFromFile(mhsFile, "fingerprint_mahasiswa.csv");
        mhsFile.close();
    }

    // ========== TUGAS 4: VERIFIKASI SETELAH INIT ==========
    delay(100);
    finger->count_templates();
    Serial.printf("[INIT] Total templates loaded: %d\n", loadedCount);
    Serial.printf("[INIT] Sensor template count after load: %d\n", finger->template_count);
}

// ========== TUGAS 1: HELPER FUNCTION TO LOAD TEMPLATES FROM FILE ==========
int FingerprintManager::loadTemplatesFromFile(File& file, const char* fileName) {
    int loadedCount = 0;
    uint8_t result;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() < 10) continue;
        if (line.startsWith("id,") || line.startsWith("id,user")) continue;

        // Format: id,user_id,role,data_jari,created_at
        int p1 = line.indexOf(',');  // after id
        int p2 = line.indexOf(',', p1 + 1);  // after user_id
        int p3 = line.indexOf(',', p2 + 1);  // after role
        int p4 = line.indexOf(',', p3 + 1);  // after data_jari

        if (p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0) {
            String idStr = line.substring(0, p1);
            String userId = line.substring(p1 + 1, p2);
            String role = line.substring(p2 + 1, p3);
            String hexData = line.substring(p3 + 1, p4);

            idStr.trim();
            userId.trim();
            role.trim();
            hexData.trim();

            uint16_t slot = idStr.toInt();
            if (slot == 0 || slot > 200) continue;

            // ========== TUGAS 2: VALIDASI DATA JARI ==========
            // Cek panjang hex - jika < 1000, data kemungkinan korup
            if (hexData.length() < 1000) {
                Serial.printf("[INIT] Data sidik jari korup (len=%d), skip slot %d\n", hexData.length(), slot);
                continue;
            }

            // ========== TUGAS 3: MANAJEMEN SLOT ==========
            // Slot 1 & 2 reserved untuk Dosen - jangan tulis jika ini data mahasiswa
            if (role == "mahasiswa" && (slot == 1 || slot == 2)) {
                Serial.printf("[INIT] Skip slot %d - reserved untuk Dosen\n", slot);
                continue;
            }

            // Populate RAM cache
            fingerMap[slot] = userId;
            Serial.printf("[INIT] fingerMap[%d] = %s\n", slot, userId.c_str());

            // Convert hex string to bytes and inject to sensor
            uint8_t raw[FP_TEMPLATE_MAX];
            size_t rawLen = Adafruit_Fingerprint_ESP32::hexStringToBytes(hexData.c_str(), hexData.length(), raw, FP_TEMPLATE_MAX);

            if (rawLen > 0) {
                // ========== TUGAS 1: HAPUS SEBELUM TULIS ==========
                // Hapus template lama di slot tersebut sebelum menulis yang baru
                result = finger->delete_model(slot);
                if (result != FP_OK && result != FP_BADLOCATION && result != FP_NOTFOUND) {
                    Serial.printf("[INIT] Warning: delete_model at slot %d returned: %d\n", slot, result);
                }

                // ========== TUGAS 2: DELAY ANTAR PERINTAH ==========
                delay(100);

                result = finger->send_fpdata(raw, rawLen, FP_BUFFER_CHAR, 1);
                if (result == FP_OK) {
                    // ========== TUGAS 2: DELAY SEBELUM STORE ==========
                    delay(100);

                    result = finger->store_model(slot, 1);
                    if (result == FP_OK) {
                        Serial.printf("[INIT] Loaded FP ID %d (user: %s, role: %s) from %s\n", slot, userId.c_str(), role.c_str(), fileName);
                        loadedCount++;

                        // ========== TUGAS 4: OPTIMASI WAKTU ==========
                        // Beri waktu sensor untuk menulis ke flash
                        delay(50);
                    } else {
                        Serial.printf("[INIT] Gagal store model di slot %d: %d\n", slot, result);
                    }
                } else {
                    Serial.printf("[INIT] Failed to send fpdata to slot %d: %d\n", slot, result);
                }
            } else {
                Serial.printf("[INIT] Failed to parse hex data for slot %d\n", slot);
            }
        }
    }

    return loadedCount;
}

// ========== TUGAS 1: INJECT SINGLE FINGERPRINT TO SENSOR (IMPROVED) ==========
// Inject a single fingerprint (from PULL) to the R503 sensor
// Returns: true if success, false if failed
bool FingerprintManager::injectSingleFingerprint(String nim, String hexData, int slot) {
    // ========== TUGAS 2: VALIDASI DATA JARI ==========
    if (hexData.length() < 1000) {
        Serial.printf("[INJECT_FP] Error: hexData terlalu pendek/korup untuk NIM %s (len=%d)\n", nim.c_str(), hexData.length());
        return false;
    }

    // Validasi format hex - harus genap (setiap byte 2 karakter)
    if (hexData.length() % 2 != 0) {
        Serial.printf("[INJECT_FP] Error: Format hex tidak valid untuk NIM %s (panjang ganjil)\n", nim.c_str());
        return false;
    }

    // ========== VALIDASI SLOT ==========
    // Slot adalah Source of Truth dari CSV - inject ke slot yang指定
    if (slot <= 0 || slot > finger->library_size) {
        Serial.printf("[INJECT_FP] ERROR: Slot %d invalid (max: %d)\n", slot, finger->library_size);
        return false;
    }

    // Convert hex string to bytes
    uint8_t raw[FP_TEMPLATE_MAX];
    size_t rawLen = Adafruit_Fingerprint_ESP32::hexStringToBytes(hexData.c_str(), hexData.length(), raw, FP_TEMPLATE_MAX);

    if (rawLen == 0) {
        Serial.printf("[INJECT_FP] Error: Gagal parse hex untuk NIM %s\n", nim.c_str());
        return false;
    }

    Serial.printf("[INJECT_FP] Parsed %d bytes for NIM %s, slot %d\n", rawLen, nim.c_str(), slot);

    // ========== TUGAS 3: RETRY MECHANISM ==========
    // Coba inject sampai 2 kali jika gagal
    const int MAX_RETRIES = 2;
    uint8_t result;

    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        if (retry > 0) {
            Serial.printf("[INJECT_FP] Retry %d/%d untuk NIM %s di slot %d...\n", retry, MAX_RETRIES - 1, nim.c_str(), slot);
            delay(100);  // Jeda sebelum retry
        }

        // ========== TUGAS 1: HAPUS SEBELUM TULIS ==========
        // Hapus template lama di slot tersebut sebelum menulis yang baru
        result = finger->delete_model(slot);
        if (result != FP_OK && result != FP_BADLOCATION && result != FP_NOTFOUND) {
            Serial.printf("[INJECT_FP] Warning: delete_model at slot %d returned: %d\n", slot, result);
            // Lanjutkan meskipun delete gagal (slot mungkin kosong)
        }

        // ========== TUGAS 2: DELAY ANTAR PERINTAH ==========
        delay(100);

        // Send to sensor buffer
        result = finger->send_fpdata(raw, rawLen, FP_BUFFER_CHAR, 1);
        if (result != FP_OK) {
            Serial.printf("[INJECT_FP] Retry %d: Gagal send_fpdata ke slot %d: %d\n", retry, slot, result);
            continue;  // Coba lagi
        }

        // ========== TUGAS 2: DELAY SEBELUM STORE ==========
        delay(100);

        // ========== DOUBLE GUARD: Hapus ulang sebelum store untuk mencegah Error 24 ==========
        finger->delete_model(slot);
        delay(50);

        // Store in sensor
        result = finger->store_model(slot, 1);
        if (result == FP_OK) {
            // ========== TUGAS 4: OPTIMASI WAKTU ==========
            // Beri waktu sensor untuk menulis ke flash
            delay(50);

            // Update fingerMap
            fingerMap[slot] = nim;
            Serial.printf_P(PSTR("[INJECT_FP] Berhasil inject NIM %s ke Slot %d\n"), nim.c_str(), slot);
            return true;
        }

        Serial.printf("[INJECT_FP] Retry %d: Gagal store_model di slot %d: %d\n", retry, slot, result);
    }

    // ========== TUGAS 3: JIKA GAGAL SETELAH RETRY ==========
    // Simpan ke failed_inject.csv
    Serial.printf("[INJECT_FP] GAGAL inject setelah %d percobaan untuk NIM %s\n", MAX_RETRIES, nim.c_str());
    saveFailedInject(nim, slot);

    return false;
}

// ========== TUGAS 3: SAVE FAILED INJECT TO CSV ==========
void FingerprintManager::saveFailedInject(String nim, int slot) {
    File f = SD.open("/failed_inject.csv", FILE_APPEND);
    if (f) {
        String timestamp = getFormattedTime();
        if (timestamp == "WAKTU_BELUM_SINKRON") {
            timestamp = String(millis());
        }
        f.printf("%s,%d,%s\n", nim.c_str(), slot, timestamp.c_str());
        f.close();
        Serial.printf("[FAILED_INJECT] Saved NIM %s to failed_inject.csv\n", nim.c_str());
    } else {
        Serial.println("[FAILED_INJECT] ERROR: Cannot open failed_inject.csv");
    }
}

// ========== FIND EMPTY SLOT ==========
int FingerprintManager::findEmptySlot() {
    // TUGAS 2: Use new library - count templates and find lowest available
    finger->count_templates();
    int used = finger->template_count;
    int capacity = finger->library_size;

    Serial.printf("[FIND_SLOT] Capacity: %d, Used: %d\n", capacity, used);

    if (used >= capacity) {
        Serial.println("[FIND_SLOT] Sensor full!");
        return 0;
    }

    // Use getLowestAvailableFingerprintId() for proper gap detection
    return getLowestAvailableFingerprintId();
}

// ========== GET TEMPLATE COUNT ==========
int FingerprintManager::getTemplateCount() {
    finger->count_templates();
    return finger->template_count;
}

// ========== SCAN (Simple) ==========
int FingerprintManager::scan() {
    // TUGAS 4: Use new library for scanning
    uint8_t p = finger->get_image();
    if (p != FP_OK) {
        return 0;  // No finger or error
    }

    p = finger->image_2_tz(1);
    if (p != FP_OK) {
        return 0;
    }

    p = finger->finger_search();
    if (p == FP_OK) {
        return finger->finger_id;  // Return slot ID
    }

    return 0;  // Not found
}

// ========== ENROLL (Simple) ==========
// TUGAS 3: Use new library for enrollment
int FingerprintManager::enroll(int slotId) {
    if (slotId <= 0) {
        Serial.println("[ENROLL] Invalid slot ID");
        return 0;
    }

    Serial.printf("[ENROLL] Starting enrollment at slot %d\n", slotId);

    // TUGAS 3: Delete existing template at slot first
    uint8_t p = finger->delete_model(slotId);
    if (p != FP_OK && p != FP_BADLOCATION) {
        Serial.println("[ENROLL] Warning: delete_model returned: " + String(fpGetErrorString(p)));
    } else {
        Serial.println("[ENROLL] Cleared slot " + String(slotId) + " for new enrollment");
    }

    // Wait for first finger
    Serial.println("Place finger...");
    unsigned long startTime = millis();

    while (finger->get_image() != FP_OK) {
        if (millis() - startTime > 10000) {
            Serial.println("[ENROLL] Timeout - no finger");
            return 0;
        }
        delay(100);
    }

    Serial.println(">> Image 1 taken");
    p = finger->image_2_tz(1);
    if (p != FP_OK) {
        Serial.println("[ENROLL] Failed to convert image 1");
        return 0;
    }

    delay(1000);

    // Wait for second finger
    Serial.println("Remove finger, place again...");
    startTime = millis();

    while (finger->get_image() != FP_OK) {
        if (millis() - startTime > 10000) {
            Serial.println("[ENROLL] Timeout - no finger");
            return 0;
        }
        delay(100);
    }

    Serial.println(">> Image 2 taken");
    p = finger->image_2_tz(2);
    if (p != FP_OK) {
        Serial.println("[ENROLL] Failed to convert image 2");
        return 0;
    }

    // TUGAS 3: Create model
    p = finger->create_model();
    if (p != FP_OK) {
        Serial.println("[ENROLL] Failed to create model: " + String(fpGetErrorString(p)));
        return 0;
    }

    // TUGAS 3: Store model to specific slot
    p = finger->store_model(slotId, 1);
    if (p != FP_OK) {
        Serial.println("[ENROLL] Failed to store model: " + String(fpGetErrorString(p)));
        return 0;
    }

    Serial.printf("[ENROLL] SUCCESS - stored at slot %d\n", slotId);
    return slotId;
}

// ========== DELETE TEMPLATE ==========
bool FingerprintManager::deleteTemplate(int slotId) {
    uint8_t p = finger->delete_model(slotId);
    Serial.printf("[DELETE] Slot %d: %s\n", slotId, fpGetErrorString(p));
    return (p == FP_OK);
}

// ========== VERIFY SLOT ==========
bool FingerprintManager::verifySlot(int slotId) {
    uint8_t p = finger->load_model(slotId, 1);
    if (p != FP_OK) {
        return false;
    }
    return true;
}

// ========== FIND EMPTY SLOT FOR MAHASISWA (Slot 3-200) ==========
// Langsung polling sensor R503 untuk cari slot kosong
// Mengabaikan ID lama dari CSV - slot yang benar-benar kosong di sensor
int FingerprintManager::findEmptySlotForMahasiswa() {
    Serial.println(F("[FIND_SLOT_MHS] ========== POLLING SENSOR R503 =========="));

    // Langsung cek sensor mulai dari slot 3 (slot 1-2 reserved untuk Dosen)
    for (int slot = 3; slot <= 200; slot++) {
        // Load template ke buffer karakter (hanya untuk cek eksistensi)
        uint8_t p = finger->load_model(slot, 1);
        if (p != FP_OK) {
            // Jika gagal load (FP_NOTFOUND), berarti slot ini KOSONG dan siap digunakan
            // EXTRA CHECK: Juga pastikan fingerMap[slot] kosong di RAM
            if (fingerMap[slot].length() == 0) {
                Serial.printf_P(PSTR("[FIND_SLOT_MHS] Found empty slot: %d (sensor & RAM OK)\n"), slot);
                return slot;
            } else {
                Serial.printf_P(PSTR("[FIND_SLOT_MHS] Slot %d: sensor kosong tapi RAM terisi\n"), slot);
            }
        }
    }

    Serial.println(F("[FIND_SLOT_MHS] WARNING: Semua slot 3-200 penuh!"));
    return -1; // Penuh
}

// ========== SCAN AND AUTHENTICATE ==========
String FingerprintManager::scanAndAuthenticate(FingerprintDataManager* dataManager) {
    (void)dataManager;  // Unused parameter - now using fingerMap RAM cache

    // Scan fingerprint
    int slotId = scan();

    if (slotId == 0) {
        return "";  // No match or error
    }

    Serial.printf("[AUTH] Found fingerprint at slot %d\n", slotId);

    // TUGAS 4: O(1) RAM CACHE lookup - gunakan fingerMap
    String userId = fingerMap[slotId];
    userId.trim();
    if (userId == "") {
        Serial.println("[AUTH] Slot not found in RAM cache (fingerMap)");
        return "";
    }

    Serial.printf("[AUTH] SUCCESS - User: %s\n", userId.c_str());
    return userId;
}

// ========== HARD RESET ==========
// Returns true if successful (sensor empty or error that doesn't prevent operation)
bool FingerprintManager::hardResetDB() {
    Serial.println(F("[RESET] ========== HARD RESET =========="));

    uint8_t result = finger->empty_library();
    bool success = (result == FP_OK);
    Serial.printf_P(PSTR("[RESET] Result: %s (code: %d)\n"), fpGetErrorString(result), result);

    finger->count_templates();
    Serial.printf_P(PSTR("[RESET] Final count: %d\n"), finger->template_count);

    return success;
}

// ========== RESET SCAN ==========
void FingerprintManager::resetScan() {
    Serial.println("[SCAN] Reset scan state");
    // Reset scan state - the checkFingerprint already handles this internally with static state
}

// ========== SYNC FROM CSV ==========
// FLUSH -> VALIDATE -> INJECT -> RAM SYNC
// Returns: true if successful, false if flush failed
bool FingerprintManager::syncFromCSV(UserManager* userManager) {
    Serial.println(F("=========================================="));
    Serial.println(F("[SYNC] syncFromCSV called"));

    // ========== STEP 1: FORCE FLUSH (WAJIB) ==========
    Serial.println(F("[SYNC] ======================================="));
    Serial.println(F("[SYNC] STEP 1: FORCE FLUSH SENSOR..."));
    Serial.println(F("[SYNC] Mengeksekusi empty_library()..."));

    bool flushSuccess = false;
    int flushRetry = 0;
    const int MAX_FLUSH_RETRY = 3;

    while (flushRetry < MAX_FLUSH_RETRY && !flushSuccess) {
        // Eksekusi empty_library()
        uint8_t flushResult = finger->empty_library();

        if (flushResult == FP_OK) {
            Serial.println(F("[SYNC] empty_library OK"));
        } else {
            Serial.printf_P(PSTR("[SYNC] empty_library error: %d\n"), flushResult);
        }

        // Validasi: wajib cek template_count
        delay(50);
        finger->count_templates();
        int templateCount = finger->template_count;

        if (templateCount == 0) {
            flushSuccess = true;
            Serial.println(F("[SYNC] Flush validated: sensor KOSONG"));
        } else {
            flushRetry++;
            Serial.printf_P(PSTR("[SYNC] Flush retry %d/%d: template_count=%d\n"),
                          flushRetry, MAX_FLUSH_RETRY, templateCount);

            if (flushRetry < MAX_FLUSH_RETRY) {
                // Force delete per-slot sebagai backup
                for (int d = 1; d <= 200; d++) {
                    finger->delete_model(d);
                }
                delay(100);
            }
        }
    }

    // Verifikasi akhir sebelum proceed
    finger->count_templates();
    int finalCount = finger->template_count;
    Serial.printf_P(PSTR("[SYNC] Pre-inject: %d templates\n"), finalCount);

    // JIKA FLUSH GAGAL -> ABORT
    if (!flushSuccess && finalCount > 0) {
        Serial.println(F("[SYNC] ERROR: Flush failed - ABORT sync!"));
        Serial.println(F("=========================================="));
        return false;
    }

    Serial.println(F("[SYNC] Flush OK - proceeding to inject..."));

    // ========== STEP 2: INJECT FROM CSV ==========
    Serial.println(F("[SYNC] ======================================="));
    Serial.println(F("[SYNC] STEP 2: INJECT FROM CSV..."));

    int totalInjected = 0;

    // --- fingerprint_users.csv (Dosen/Admin) ---
    File f = SD.open("/fingerprint_users.csv", FILE_READ);
    if (f) {
        Serial.println(F("[SYNC] Reading fingerprint_users.csv..."));
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() < 10) continue;
            if (line.startsWith(F("id,")) || line.startsWith(F("id,user"))) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);
            int p4 = line.indexOf(',', p3 + 1);
            if (p1 <= 0 || p2 <= 0 || p3 <= 0 || p4 <= 0) continue;

            int slot = line.substring(0, p1).toInt();
            String userId = line.substring(p1 + 1, p2);
            String hexData = line.substring(p3 + 1, p4);

            userId.trim();
            hexData.trim();

            if (hexData.length() < 1000) continue;
            if (slot > 0 && slot <= 200) {
                if (injectSingleFingerprint(userId, hexData, slot)) {
                    totalInjected++;
                }
            }
        }
        f.close();
    }

    // ========== STEP 3: SYNC RAM ==========
    Serial.println(F("[SYNC] ======================================="));
    Serial.println(F("[SYNC] STEP 3: SYNC RAM (fingerMap)..."));

    // Final verification
    finger->count_templates();
    int sensorCount = finger->template_count;
    Serial.printf_P(PSTR("[SYNC] ===== DONE: %d injected, %d sensor =====\n"),
                  totalInjected, sensorCount);
    Serial.println(F("=========================================="));

    return true;
}

// ========== NON-BLOCKING ENROLLMENT ==========
void FingerprintManager::startEnrollment(int slotId) {
    enrollId = slotId;
    enrollState = ENROLL_PLACE_FINGER;
    enrollStartTime = millis();

    Serial.println("========== ENROLLMENT START ==========");
    Serial.println("Slot: " + String(slotId));
    Serial.println("Step 1: Place finger");
}

EnrollState FingerprintManager::getEnrollState() {
    return enrollState;
}

void FingerprintManager::resetEnrollment() {
    enrollState = ENROLL_IDLE;
    enrollId = 0;
}

// ========== TUGAS 3: PROCESS ENROLLMENT (Non-blocking) ==========
bool FingerprintManager::processEnrollment() {
    if (enrollState == ENROLL_IDLE || enrollState == ENROLL_SUCCESS || enrollState == ENROLL_FAILED) {
        return false;
    }

    uint8_t result;

    if (enrollState == ENROLL_PLACE_FINGER) {
        // TUGAS 3: Use new library API
        result = finger->get_image();

        if (result == FP_OK) {
            Serial.println(">> Image 1 taken");

            result = finger->image_2_tz(1);
            Serial.println(">> image_2_tz(1): " + String(fpGetErrorString(result)));

            if (result == FP_OK) {
                enrollState = ENROLL_PLACE_AGAIN;
                enrollStartTime = millis();
                Serial.println("Step 2: Remove finger, then place again");
                return true;
            } else {
                Serial.println("ERROR: Failed to convert image");
                enrollState = ENROLL_FAILED;
                return false;
            }
        } else if (result != FP_NOFINGER) {
            Serial.println("ERROR: get_image: " + String(fpGetErrorString(result)));
            enrollState = ENROLL_FAILED;
            return false;
        }

        if (millis() - enrollStartTime > 15000) {
            Serial.println("ERROR: Timeout finger 1");
            enrollState = ENROLL_FAILED;
            return false;
        }
    }
    else if (enrollState == ENROLL_PLACE_AGAIN) {
        result = finger->get_image();

        if (result == FP_OK) {
            Serial.println(">> Image 2 taken");

            result = finger->image_2_tz(2);
            Serial.println(">> image_2_tz(2): " + String(fpGetErrorString(result)));

            if (result == FP_OK) {
                Serial.println(">> Creating model...");
                result = finger->create_model();
                Serial.println(">> create_model: " + String(fpGetErrorString(result)));

                if (result == FP_OK) {
                    Serial.println(">> Storing to slot " + String(enrollId) + "...");
                    result = finger->store_model(enrollId, 1);
                    Serial.println(">> store_model: " + String(fpGetErrorString(result)));

                    if (result == FP_OK) {
                        Serial.println("========== ENROLLMENT SUCCESS ==========");
                        enrollState = ENROLL_SUCCESS;
                        return true;
                    }
                }
            }

            Serial.println("ERROR: Enrollment failed");
            enrollState = ENROLL_FAILED;
            return false;
        } else if (result != FP_NOFINGER) {
            Serial.println("ERROR: get_image: " + String(fpGetErrorString(result)));
            enrollState = ENROLL_FAILED;
            return false;
        }

        if (millis() - enrollStartTime > 15000) {
            Serial.println("ERROR: Timeout finger 2");
            enrollState = ENROLL_FAILED;
            return false;
        }
    }

    return true;
}

// ========== LED CONTROL ==========
// TUGAS 4: Use new library set_led() for R503 LED control
void FingerprintManager::setLEDStandby() {
    finger->set_led(FP_LED_BLUE, FP_LED_ON, 0x80, 0);
}

void FingerprintManager::setLEDScanning() {
    finger->set_led(FP_LED_BLUE, FP_LED_BREATHE, 0x80, 0);
}

void FingerprintManager::setLEDSuccess() {
    finger->set_led(FP_LED_GREEN, FP_LED_ON, 0x80, 0);
    delay(2000);
}

void FingerprintManager::setLEDError() {
    finger->set_led(FP_LED_RED, FP_LED_FLASH, 0x80, 3);
}

void FingerprintManager::setLEDOff() {
    finger->set_led(FP_LED_BLUE, FP_LED_OFF, 0, 0);
}

// ========== TUGAS 3: EXTRACT HEX TEMPLATE FROM SENSOR ==========
String FingerprintManager::extractTemplateAsHex(int slotId) {
    // Load model from sensor
    uint8_t p = finger->load_model(slotId, 1);
    if (p != FP_OK) {
        Serial.printf("[EXTRACT_HEX] Failed to load model at slot %d: %d\n", slotId, p);
        return "";
    }

    // Extract template to buffer
    uint8_t buf[FP_TEMPLATE_MAX];
    size_t outLen = 0;
    p = finger->get_fpdata(buf, sizeof(buf), &outLen, FP_BUFFER_CHAR, 1);
    if (p != FP_OK) {
        Serial.printf("[EXTRACT_HEX] Failed to get fpdata: %d\n", p);
        return "";
    }

    // Convert to hex string
    String hexData = Adafruit_Fingerprint_ESP32::bytesToHexString(buf, outLen);
    Serial.printf("[EXTRACT_HEX] Extracted %d bytes -> hex length: %d\n", outLen, hexData.length());

    return hexData;
}

// ========== CHECK FINGERPRINT (Non-blocking) ==========
// TUGAS 4: Use new library for matching
// Returns: >0 if matched (slot ID), 0 if no finger/scanning, -1 if error, -2 if not found
int FingerprintManager::checkFingerprint() {
    static enum {
        FP_SCAN_IDLE,
        FP_SCAN_WAITING,
        FP_SCAN_CONVERTING,
        FP_SCAN_SEARCHING,
        FP_SCAN_DONE
    } state = FP_SCAN_IDLE;

    static int scanResult = 0;
    uint8_t p;

    switch (state) {
        case FP_SCAN_IDLE:
            // NO DELAY - concurrent non-blocking dengan keypad
            // Rate limiting harus dilakukan oleh caller dengan millis()
            p = finger->get_image();
            if (p == FP_OK) {
                state = FP_SCAN_CONVERTING;
            } else if (p != FP_NOFINGER) {
                return -1;  // Error
            }
            return 0;

        case FP_SCAN_CONVERTING:
            p = finger->image_2_tz(1);
            if (p == FP_OK) {
                state = FP_SCAN_SEARCHING;
            } else {
                state = FP_SCAN_IDLE;
                return -1;
            }
            return 0;

        case FP_SCAN_SEARCHING:
            // TUGAS 4: Use finger_search() for matching
            p = finger->finger_search();
            if (p == FP_OK) {
                Serial.printf("[DEBUG-LOGIN] SENSOR MATCH! Jari dikenali di Slot: %d dengan Akurasi: %d\n", finger->finger_id, finger->confidence);
                scanResult = finger->finger_id;
                state = FP_SCAN_DONE;
            } else if (p == FP_NOTFOUND) {
                Serial.println("[DEBUG-LOGIN] SENSOR REJECT: Jari tidak dikenali oleh hardware sensor R503!");
                state = FP_SCAN_DONE;
                return -2;
            } else {
                Serial.printf("[DEBUG-LOGIN] SENSOR ERROR: %d\n", p);
                state = FP_SCAN_IDLE;
                return -1;
            }
            return 0;

        case FP_SCAN_DONE: {
            int result = scanResult;
            state = FP_SCAN_IDLE;
            scanResult = 0;
            return result;
        }

        default: {
            state = FP_SCAN_IDLE;
            return 0;
        }
    }
    return 0;
}

// ========== PRESENSI: FLUSH & INJECT FOR SPECIFIC CLASS ==========
// Flush sensor + inject Dosen + inject specific class mahasiswa
// This is called before entering PRESENSI_SCANNING state
bool FingerprintManager::flushAndInjectPresensiUsers(String kodeKelas, String kelas, String dosenNip) {
    Serial.println(F("=========================================="));
    Serial.println(F("[PRESENSI_INJECT] flushAndInjectPresensiUsers called"));
    Serial.printf_P(PSTR("[PRESENSI_INJECT] Kelas: %s-%s | Dosen: %s\n"),
        kodeKelas.c_str(), kelas.c_str(), dosenNip.c_str());

    kodeKelas.trim();
    kelas.trim();
    dosenNip.trim();

    // ========== STEP A: FLUSH SENSOR (MUTLAK) ==========
    Serial.println(F("[PRESENSI_INJECT] ======================================="));
    Serial.println(F("[PRESENSI_INJECT] STEP A: FLUSH SENSOR..."));

    bool flushSuccess = false;
    int flushRetry = 0;
    const int MAX_FLUSH_RETRY = 3;

    while (flushRetry < MAX_FLUSH_RETRY && !flushSuccess) {
        uint8_t flushResult = finger->empty_library();
        if (flushResult == FP_OK) {
            Serial.println(F("[PRESENSI_INJECT] empty_library OK"));
        } else {
            Serial.printf_P(PSTR("[PRESENSI_INJECT] empty_library error: %d\n"), flushResult);
        }

        delay(50);
        finger->count_templates();
        int templateCount = finger->template_count;

        if (templateCount == 0) {
            flushSuccess = true;
            Serial.println(F("[PRESENSI_INJECT] Flush validated: sensor KOSONG"));
        } else {
            flushRetry++;
            Serial.printf_P(PSTR("[PRESENSI_INJECT] Flush retry %d/%d: template_count=%d\n"),
                flushRetry, MAX_FLUSH_RETRY, templateCount);

            if (flushRetry < MAX_FLUSH_RETRY) {
                // Force delete per-slot sebagai backup
                for (int d = 1; d <= 200; d++) {
                    finger->delete_model(d);
                }
                delay(100);
            }
        }
    }

    finger->count_templates();
    int finalCount = finger->template_count;
    Serial.printf_P(PSTR("[PRESENSI_INJECT] Pre-inject: %d templates\n"), finalCount);

    if (!flushSuccess && finalCount > 0) {
        Serial.println(F("[PRESENSI_INJECT] ERROR: Flush failed - ABORT!"));
        Serial.println(F("=========================================="));
        return false;
    }

    Serial.println(F("[PRESENSI_INJECT] Flush OK - proceeding to inject..."));

    // ========== CLEAR FINGERMAP RAM ==========
    for (int i = 0; i <= 200; i++) {
        fingerMap[i] = "";
    }

    // ========== STEP B: INJECT DOSEN YANG LOGIN (Slots 1-2) ==========
    // PENTING: inject sidik jari milik DOSEN YANG SEDANG LOGIN (match user_id == dosenNip),
    // BUKAN sekadar baris ber-id<=2. AUTH_START membandingkan fingerMap[slot] terhadap NIP
    // dosen login; jika FP dosen tsb tidak ter-inject ke slot 1-2, otorisasi FP start/stop
    // gagal total (hanya PIN yang jalan). Maks 2 jari -> slot 1 lalu slot 2.
    Serial.println(F("[PRESENSI_INJECT] ======================================="));
    Serial.printf_P(PSTR("[PRESENSI_INJECT] STEP B: INJECT DOSEN LOGIN %s (Slots 1-2)...\n"), dosenNip.c_str());

    int dosenInjected = 0;
    int dosenSlot = 1;   // dosen login menempati slot 1-2
    File f = SD.open("/fingerprint_users.csv", FILE_READ);
    if (f) {
        if (f.available()) f.readStringUntil('\n'); // Skip header

        while (f.available() && dosenSlot <= 2) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() < 10) continue;
            if (line.startsWith(F("id,")) || line.startsWith(F("id,user"))) continue;

            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);
            int p4 = line.indexOf(',', p3 + 1);
            if (p1 <= 0 || p2 <= 0 || p3 <= 0 || p4 <= 0) continue;

            String userId = line.substring(p1 + 1, p2);
            String hexData = line.substring(p3 + 1, p4);
            userId.trim();
            hexData.trim();

            // Hanya FP milik dosen yang sedang login
            if (userId != dosenNip) continue;
            if (hexData.length() < 1000) continue;

            if (injectSingleFingerprint(userId, hexData, dosenSlot)) {
                dosenInjected++;
                Serial.printf_P(PSTR("[PRESENSI_INJECT] Injected Dosen: %s ke slot %d\n"), userId.c_str(), dosenSlot);
                dosenSlot++;   // jari berikutnya -> slot 2
            }
        }
        f.close();
    } else {
        Serial.println(F("[PRESENSI_INJECT] ERROR: fingerprint_users.csv tidak ditemukan"));
    }

    Serial.printf_P(PSTR("[PRESENSI_INJECT] Dosen injected: %d (NIP %s)\n"), dosenInjected, dosenNip.c_str());

    // ========== STEP C: INJECT MAHASISWA KELAS TERPILIH ==========
    Serial.println(F("[PRESENSI_INJECT] ======================================="));
    Serial.println(F("[PRESENSI_INJECT] STEP C: INJECT MAHASISWA KELAS TERPILIH..."));

    int mhsInjected = 0;

    // Step C1: Kumpulkan NIM dari kelas_mahasiswa.csv untuk kelas ini
    std::vector<String> classNims;
    File kmFile = SD.open("/kelas_mahasiswa.csv", FILE_READ);
    if (kmFile) {
        if (kmFile.available()) kmFile.readStringUntil('\n'); // Skip header

        while (kmFile.available()) {
            String line = kmFile.readStringUntil('\n');
            line.trim();
            if (line.length() < 5) continue;
            if (line.startsWith("id,") || line.startsWith("id,kode")) continue;

            // Format: id,kode_kelas,kelas,nim,sit_in,status_fingerprint
            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);

            if (p1 > 0 && p2 > 0 && p3 > 0) {
                String csvKode = line.substring(p1 + 1, p2);
                String csvKelas = line.substring(p2 + 1, p3);
                String csvNim = line.substring(p3 + 1, line.indexOf(',', p3 + 1));

                csvKode.trim(); csvKode.replace("\r", "");
                csvKelas.trim(); csvKelas.replace("\r", "");
                csvNim.trim(); csvNim.replace("\r", "");

                if (csvKode == kodeKelas && csvKelas == kelas) {
                    classNims.push_back(csvNim);
                }
            }
        }
        kmFile.close();
    } else {
        Serial.println(F("[PRESENSI_INJECT] ERROR: kelas_mahasiswa.csv tidak ditemukan"));
    }

    Serial.printf_P(PSTR("[PRESENSI_INJECT] Ditemukan %d NIM untuk kelas %s-%s\n"),
        classNims.size(), kodeKelas.c_str(), kelas.c_str());

    // ========== BAGIAN 1: ADAPTIVE LOADING ==========
    // Kapasitas sensor R503 = 200 slot. Slot 1-2 = dosen, sisa 198 utk mahasiswa.
    //   - Kelas <= 99 mhs : inject fp_1 + fp_2 (2 slot/mhs) -> 99*2 = 198 slot (pas).
    //   - Kelas  > 99 mhs : inject fp_1 SAJA (1 slot/mhs) agar tidak Overcapacity (0x0B).
    //                       fp_2 tetap tersedia via sidecar utk Presensi Cadangan (on-demand).
    bool injectDual = (classNims.size() <= 99);
    // Flag global: kelas > 99 -> mode adaptif (fp_2 tdk diinjeksi) -> menu Jari Cadangan aktif.
    isAdaptiveMode = (classNims.size() > 99);
    Serial.printf_P(PSTR("[PRESENSI_INJECT] Adaptive: %d mhs -> mode %s\n"),
        (int)classNims.size(), injectDual ? "DUAL (fp_1+fp_2)" : "SINGLE (fp_1 saja)");

    // Step C2: SATU kali baca fingerprint_mahasiswa.csv, inject SEMUA mahasiswa kelas ini.
    // PENTING: loop berjalan sampai file habis - JANGAN break/return setelah 1 mahasiswa.
    int currentSlot = 3;  // Mulai dari slot 3 untuk mahasiswa (1-2 milik dosen)
    File fpFile = SD.open("/fingerprint_mahasiswa.csv", FILE_READ);
    if (fpFile) {
        if (fpFile.available()) fpFile.readStringUntil('\n'); // Skip header

        while (fpFile.available() && currentSlot <= 200) {
            String line = fpFile.readStringUntil('\n');
            line.trim();
            if (line.length() < 10) continue;
            if (line.startsWith("id,") || line.startsWith("id,user")) continue;

            // Format: id,user_id,role,data_jari,created_at
            int p1 = line.indexOf(',');
            int p2 = line.indexOf(',', p1 + 1);
            int p3 = line.indexOf(',', p2 + 1);
            int p4 = line.indexOf(',', p3 + 1);
            if (p1 <= 0 || p2 <= 0 || p3 <= 0 || p4 <= 0) continue;

            String csvNim = line.substring(p1 + 1, p2);
            csvNim.trim(); csvNim.replace("\r", "");

            // Apakah NIM ini termasuk mahasiswa kelas terpilih?
            bool inClass = false;
            for (size_t i = 0; i < classNims.size(); i++) {
                if (classNims[i] == csvNim) { inClass = true; break; }  // break hanya untuk pencarian kecil ini
            }
            if (!inClass) continue;  // bukan kelas ini -> lanjut, JANGAN berhenti

            String hexData = line.substring(p3 + 1, p4);  // fp_1 dari fingerprint_mahasiswa.csv
            hexData.trim();

            // ----- Inject fp_1 ke currentSlot (abaikan id/slot dari CSV) -----
            if (hexData.length() >= 1000) {
                if (injectSingleFingerprint(csvNim, hexData, currentSlot)) {
                    mhsInjected++;
                    fingerMap[currentSlot] = csvNim;  // Update fingerMap: slot -> NIM
                    Serial.printf_P(PSTR("[PRESENSI_INJECT] fp_1 mhs: %s ke slot %d\n"), csvNim.c_str(), currentSlot);
                    currentSlot++;  // Naik HANYA jika sukses
                } else {
                    Serial.printf_P(PSTR("[PRESENSI_INJECT] Gagal inject fp_1: %s\n"), csvNim.c_str());
                    continue;  // gagal fp_1 -> skip fp_2 utk NIM ini
                }
            } else {
                continue;  // hex fp_1 tidak valid
            }

            // ----- Inject fp_2 HANYA bila mode DUAL & slot masih ada -----
            if (injectDual && currentSlot <= 200) {
                String fp2Hex = readFp2Sidecar(csvNim);   // ambil dari /fp2_mahasiswa.csv
                if (fp2Hex.length() >= 1000) {
                    if (injectSingleFingerprint(csvNim, fp2Hex, currentSlot)) {
                        mhsInjected++;
                        fingerMap[currentSlot] = csvNim;  // slot fp_2 -> NIM yang sama
                        Serial.printf_P(PSTR("[PRESENSI_INJECT] fp_2 mhs: %s ke slot %d\n"), csvNim.c_str(), currentSlot);
                        currentSlot++;
                    }
                }
                fp2Hex = "";  // bebaskan heap
            }
            // TIDAK ada break -> teruskan sampai seluruh mahasiswa terinjeksi
        }
        fpFile.close();
    } else {
        Serial.println(F("[PRESENSI_INJECT] ERROR: fingerprint_mahasiswa.csv tidak ditemukan"));
    }

    Serial.printf_P(PSTR("[PRESENSI_INJECT] Mahasiswa injected: %d\n"), mhsInjected);

    // ========== FINAL VERIFICATION ==========
    finger->count_templates();
    int totalSensor = finger->template_count;
    Serial.printf_P(PSTR("[PRESENSI_INJECT] ===== DONE: %d Dosen, %d Mhs, %d total =====\n"),
        dosenInjected, mhsInjected, totalSensor);
    Serial.println(F("=========================================="));

    return true;
}