#pragma once
#include <Arduino.h>
#include <vector>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <sys/time.h>

// API Configuration
#define API_BASE_URL "https://presensi-elektronik-ta-2526-016.vercel.app"

// Device Headers (TUGAS 1) - Menggunakan x-device-code dan x-device-secret
#define DEVICE_CODE "DEV-001"
#define DEVICE_SECRET "key-secret-001"

// Forward declaration
class UserManager;

class APIManager {
private:
    bool isFetching;
    bool timeSynced;

    // ========== PERSISTENT TLS SESSION untuk BATCH_PULL (Keep-Alive) ==========
    // Dipakai oleh fetchBatchStudentFingerprints() agar 9 request batch berbagi
    // satu TCP+TLS handshake. Lazy-init di pemanggilan pertama, ditutup oleh
    // endBatchPullSession() setelah seluruh batch selesai.
    WiFiClientSecure _batchPullClient;
    HTTPClient       _batchPullHttp;
    bool             _batchPullActive = false;

public:
    // Tutup sesi persistent TLS di akhir alur PULL_FP (panggil dari state machine
    // setelah pullIndex >= targetCount). WAJIB dipanggil untuk membebaskan socket.
    void endBatchPullSession();

    APIManager();

    // ========== SYNC USERS FROM API (TUGAS 1, 2, 3) ==========
    // Sync users from API to users.csv - called once after WiFi + SNTP sync
    void syncUsersFromAPI();

    // ========== TUGAS 1: FETCH USERS & DELTA SYNC ==========
    // Fetch users from API and perform delta sync to users.csv
    bool fetchUsersAndSync();

    // Check if currently fetching
    bool isProcessing();

    // ========== TUGAS 2: API LOGIN ==========
    // Login to API after local login success, returns JWT token
    String apiLogin(int finger_id);

    // ========== TUGAS 2: FETCH STUDENT FINGERPRINT ==========
    // Pull hex fingerprint template untuk mahasiswa tertentu dari server
    String fetchStudentFingerprint(String nim, String token);

    // ========== BATCH PULL: Fetch multiple fingerprints in one API call ==========
    // PENTING: 'doc' dimiliki oleh pemanggil agar JsonArray yang dikembalikan tetap
    // valid selama loop pemrosesan (mencegah dangling reference / LoadProhibited).
    JsonArray fetchBatchStudentFingerprints(String nimList, String token, JsonDocument& doc);

    // ========== LANGKAH 2: SEND LOGIN ACKNOWLEDGE ==========
    // Send login acknowledge to API after successful local login
    // Revisi: menggunakan nip dan pin sesuai request backend terbaru
    // fetchDashboard=false untuk Admin (bypass tarik dashboard), true untuk Dosen
    void sendLoginAcknowledge(String nip, String pin, bool fetchDashboard = true);

    // ========== TUGAS 3: API DASHBOARD & DELTA SYNC ==========
    // Fetch dashboard data with token and sync to 3 CSV files
    void fetchDashboardData(String token);

    // ========== PROCESS DASHBOARD DATA ==========
    // Process JSON Dashboard dan distribute ke 7 CSV files
    void processDashboardData(String jsonPayload);

    // ========== DPK: TARIK DATA MAHASISWA PER KELAS ==========
    // GET /api/device/dpk?kode_mk=...&kelas=... lalu upsert mahasiswa.csv & kelas_mahasiswa.csv.
    // Mapping: type=="tetap" -> sit_in=false; selain itu -> sit_in=true. status_fp -> status_fingerprint.
    // Setelah simpan, panggil calculateEnrollRatioCache(). Return true jika sukses.
    bool fetchDataMahasiswa(String kode_mk, int kelas);

    // ========== ENROLL RATIO CACHE ==========
    // Pre-calculate dan simpan rasio enrolled ke cache
    void calculateEnrollRatioCache();
    // Baca rasio dari cache (O(1))
    bool getEnrollRatio(const char* kodeKelas, const char* kelas, int& enrolled, int& total);

    // ========== HELPER: ANTI-DUPLIKASI ==========
    // Cek apakah ID sudah ada di CSV (satu kolom)
    bool isRecordExists(const char* filename, String searchKey, int columnIndex);
    // Cek apakah kombinasi dua nilai sudah ada di CSV (dua kolom)
    bool isRecordExists(const char* filename, String searchKey1, int columnIndex1, String searchKey2, int columnIndex2);
    // Cek apakah kombinasi kode_kelas + kelas + pertemuan_ke sudah ada di pertemuan.csv
    bool isPertemuanExists(String kode, String kls, String pert);
    // Cek duplikat berdasarkan server_jadwal_id (kolom ke-2) terhadap jadwal_id dari JSON
    bool isJadwalExists(int serverJadwalId);

    // ========== HELPER: AUTO-INCREMENT ==========
    // Cari ID tertinggi di CSV, return maxId + 1
    int getNextAutoId(const char* filename);

    // ========== TIME SYNC ==========
    // Sync time via NTP (ESP32 built-in)
    bool syncTimeFromAPI();

    // Check if time has been synced
    bool isTimeSynced();

    // ========== TUGAS 1: PUSH QUEUE HELPER ==========
    String getCsvColumn(String line, int index);
};

// ========== TUGAS 1: KONVERSI HEX KE BASE64 ==========
String convertHexToBase64(String hexString);
String convertBase64ToHex(String base64String);
size_t hexToBytes(String hex, uint8_t *out);
String bytesToHex(uint8_t *data, size_t len);
String trimHex(String hex);

// ========== TUGAS 1: PUSH QUEUE FUNCTIONS ==========
void addToPushQueue(String actionType, String referenceId, String creatorId);
void removeFromPushQueue(String targetQueueId);

// ========== TUGAS 3: PROCESS PUSH QUEUE (Contextual) ==========
void processPushQueue(String currentActiveUser);

// ========== INDEX-BASED FINGERPRINT SEARCH ==========
String findFingerprintByNimIndex(String nim);
String findFingerprintByNimOld(String nim);

// ========== TUGAS 2: PUSH SINGLE FINGERPRINT (for Registration) ==========
bool pushSingleFingerprint(String nim, String hexData);

// ========== TUGAS 1: PUSH PRESENSI TO SERVER ==========
bool pushPresensiToAPI(int jadwalId, String* listHadir, String* listHadirTime, int count, String kodeMk, int kelas);

// ========== TUGAS 1: BATCH PUSH - SIMPAN KE PENDING QUEUE ==========
void addToPendingPushQueue(String nim, String hexData);
bool processPendingPushQueue();

// ========== TUGAS 4: QUEUE MAHASISWA DUAL FINGERPRINT (2 JARI) ==========
// Simpan mahasiswa 2 jari ke queue untuk batch push via /api/device/enroll-mahasiswa
// Format CSV: mahasiswa,NIM,fp_1_mhsw,fp_2_mhsw
bool addMahasiswaToQueue(String nim, String fp1, String fp2);
// Proses queue mahasiswa 2 jari -> POST /api/device/enroll-mahasiswa
void processMahasiswaEnrollmentQueue();

// ========== SIDECAR FP_2 MAHASISWA (untuk Adaptive Loading & Presensi Cadangan) ==========
// File terpisah /fp2_mahasiswa.csv (format: nim,fp_2) menyimpan jari kedua mahasiswa.
// Dipakai saat kelas <=99 (inject fp_2 juga) dan saat presensi darurat (jari rusak).
// Tidak mengubah fingerprint_mahasiswa.csv (fp_1) yang sudah berjalan.
void   clearFp2Sidecar();                              // hapus file (panggil di awal PULL_FP)
void   appendFp2Sidecar(const String& nim, const String& fp2);  // tambah 1 baris fp_2
String readFp2Sidecar(const String& nim);              // ambil hex fp_2 by NIM ("" jika tdk ada)
void   saveFp2ToCSV(const String& nim, const String& hex);      // UPSERT fp_2 (tambah/timpa)

extern APIManager apiManager;
extern String globalJwtToken;  // TUGAS 2: JWT token untuk Push Queue
extern bool isSyncUserFailed;  // true bila sinkronisasi users gagal (untuk Retry di Login)