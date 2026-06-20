#include "sdcard.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>

bool SDCardManager::init() {
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    bool sdInit = SD.begin(SD_CS);

    // ========== TUGAS 1: AUTO-DELETE FILE LEGACY ==========
    // Hapus file fingerprint.csv yang sudah tidak dipakai agar tidak salah baca
    if (SD.exists("/fingerprint.csv")) {
        SD.remove("/fingerprint.csv");
        Serial.println(F("[SYSTEM] File legacy fingerprint.csv berhasil dihapus."));
    }

    return sdInit;
}

File SDCardManager::open(const char* path, const char* mode) {
    return SD.open(path, mode);
}

// ========== AUTO-PROVISIONING: HELPER ==========
// Buat file dengan header HANYA jika belum ada. Jika header == "" file dibuat
// kosong (untuk file headerless seperti wifi.csv / fp2_mahasiswa.csv).
static void ensureCSVFile(const char* path, const char* header) {
    if (SD.exists(path)) return;  // sudah ada -> jangan disentuh (idempoten)

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[PROVISION] GAGAL membuat %s\n", path);
        return;
    }
    if (header && header[0] != '\0') {
        f.println(header);
    }
    f.close();
    Serial.printf("[PROVISION] File dibuat: %s\n", path);
}

// ========== AUTO-PROVISIONING: SD CARD PLUG-AND-PLAY ==========
// Header DISESUAIKAN dengan parser yang sudah ada di codebase (BUKAN diubah),
// agar file hasil provisioning langsung bisa dibaca modul lain.
void initSDCardFiles() {
    Serial.println(F("[PROVISION] Memeriksa file dasar SD Card..."));

    // --- Autentikasi & profil ---
    // users.csv: NIP di indeks 1, role di indeks 3. Kolom updated_at (indeks 5)
    // ditambahkan untuk Delta Sync (bandingkan timestamp server vs lokal).
    ensureCSVFile("/users.csv",                 "id,nip,pin,role,fingerprint_id,updated_at");
    ensureCSVFile("/dosen.csv",                 "nip,nama_lengkap,created_at");
    ensureCSVFile("/mahasiswa.csv",             "nim,nama_lengkap,created_at");

    // --- Kelas & relasi ---
    ensureCSVFile("/kelas.csv",                 "kode_kelas,nama_kelas");
    ensureCSVFile("/dosen_kelas.csv",           "id,nip,kode_kelas,kelas,kelas_id");
    ensureCSVFile("/kelas_mahasiswa.csv",       "id,kode_kelas,kelas,nim,sit_in,status_fingerprint");
    ensureCSVFile("/pertemuan.csv",             "id,server_jadwal_id,kode_kelas,kelas,pertemuan_ke,tanggal");

    // --- Fingerprint (1 baris per slot; kolom data_jari = hex template) ---
    // CATATAN: sistem TIDAK memakai kolom fp_1/fp_2; tiap sidik jari = 1 baris.
    ensureCSVFile("/fingerprint_users.csv",     "id,user_id,role,data_jari,created_at");
    ensureCSVFile("/fingerprint_mahasiswa.csv", "id,user_id,role,data_jari,created_at");

    // --- Auto-save sesi (resume presensi/registrasi setelah alat mati) ---
    // session.csv: 1 baris checkpoint (mode,nip,pin,role,kode_mk,kelas,kelas_id,status).
    // session_scans.csv: kehadiran real-time (nim,timestamp) agar tidak hilang saat mati.
    ensureCSVFile("/session.csv",               "mode,nip,pin,role,kode_mk,kelas,kelas_id,status");
    ensureCSVFile("/session_scans.csv",         "nim,timestamp");
    // session_fingermap.csv: snapshot slot->NIM/NIP saat sesi presensi mulai, agar
    // resume tidak perlu inject ulang ke R503 (template di flash sudah persisten).
    ensureCSVFile("/session_fingermap.csv",     "slot,user_id");

    // --- Presensi & antrian push ---
    ensureCSVFile("/presensi.csv",              "id,nim,kode_kelas,pertemuan_id,waktu,status");
    // pending_push.csv: dibuat kosong; header ditulis otomatis saat append pertama
    // (lihat addToPendingPushQueue: header dibuat saat file.size()==0).
    ensureCSVFile("/pending_push.csv",          "");

    // --- File HEADERLESS (reader memproses SEMUA baris, tidak skip header) ---
    // fp2_mahasiswa.csv: sidecar "nim,fp_2_hex". Header akan dibaca sbg NIM palsu -> kosongkan.
    ensureCSVFile("/fp2_mahasiswa.csv",         "");

    // wifi.csv: loadAllCredentials() memparse SETIAP baris sbg kredensial
    // (TIDAK skip header). Format: ssid,password,username.
    // Isi 1 baris AP default agar smartAutoConnect tidak menemukan file kosong.
    // GANTI sesuai AP kampus. AP terbuka (tanpa username) = "Nama,,".
    ensureCSVFile("/wifi.csv",                  "ITB-Hotspot,,");

    Serial.println(F("[PROVISION] Pemeriksaan file SD Card selesai."));
}

// ========== GET FORMATTED TIME ==========
String getFormattedTime() {
    time_t now;
    time(&now);

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Check if time is valid (after year 2020)
    if (timeinfo.tm_year < 120) {  // 2020 - 1900
        return "WAKTU_BELUM_SINKRON";
    }

    char buffer[24];
    strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &timeinfo);

    return String(buffer);
}

// ========== LOG SYSTEM ACTIVITY ==========
void logSystemActivity(String event, String detail) {
    // Get formatted time
    String timestamp = getFormattedTime();

    // Format: [Timestamp],[Event],[Detail]
    String logEntry = timestamp + "," + event + "," + detail;

    Serial.println("[LOG] " + logEntry);

    // Open file in append mode
    File logFile = SD.open("/system_log.csv", FILE_APPEND);

    if (!logFile) {
        Serial.println("[LOG] Failed to open system_log.csv");
        return;
    }

    // Write log entry
    logFile.println(logEntry);
    logFile.close();
}