#include "state_machine.h"
#include "config.h"
#include "user_manager.h"
#include "mahasiswa_manager.h"
#include "fingerprint_data_manager.h"
#include "fingerprint.h"
#include "presensi_manager.h"
#include "kelas_manager.h"
#include "ch376_manager.h"
#include "wifi_manager.h"
#include "api_manager.h"
#include "sdcard.h"
#include "export_manager.h"
#include <SD.h>

// ========== TUGAS 3: KONSOLIDASI STRING (PROGMEM) ==========
// String yang sering digunakan (>3x) - disimpan di Flash (PROGMEM)
const char MSG_LOADING[] PROGMEM = "Loading...";
const char MSG_BATAL[] PROGMEM = "D:Batal";
const char MSG_BATAL_SHORT[] PROGMEM = "Batal";
const char MSG_SELESAI[] PROGMEM = "D.Selesai";
const char MSG_BERHASIL[] PROGMEM = "BERHASIL!";
const char MSG_GAGAL[] PROGMEM = "GAGAL!";
const char MSG_PIN_SALAH[] PROGMEM = "PIN Salah!";
const char MSG_TIDAK_VALID[] PROGMEM = "Tidak Valid!";
const char MSG_LOGIN_GAGAL[] PROGMEM = "Login Gagal!";
const char MSG_LOGIN_ADMIN[] PROGMEM = "Admin Login";
const char MSG_LOGIN_DOSEN[] PROGMEM = "Dosen Login";
const char MSG_MOHON_TUNGGU[] PROGMEM = "Mohon Tunggu...";
const char MSG_SINKRONISASI[] PROGMEM = "Sinkronisasi...";
const char MSG_YAKIN_HAPUS[] PROGMEM = "Yakin Hapus?";
const char MSG_SUKSES[] PROGMEM = "Berhasil!";
const char MSG_GAGAL_SIMPAN[] PROGMEM = "Gagal Simpan";
const char MSG_SENSOR_PENUH[] PROGMEM = "Sensor penuh";
const char MSG_BACA_GAGAL[] PROGMEM = "Baca gagal";
const char MSG_TEMPEL_JARI[] PROGMEM = "Tempel JARI";
const char MSG_ANGKAT_TEMPEL[] PROGMEM = "ANGKAT & TEMPEL";
const char MSG_REGISTRASI_JARI[] PROGMEM = "REGISTRASI JARI";
const char MSG_DAFTAR_FP[] PROGMEM = "DAFTAR FINGERPRINT";
const char MSG_MEMPROSES[] PROGMEM = "MEMPROSES...";
const char MSG_SELESAI_EX[] PROGMEM = "Ekspor Selesai!";
const char MSG_AKSES_DITOLAK[] PROGMEM = "Akses Ditolak!";
const char MSG_NIM_TIDAK_VALID[] PROGMEM = "NIM TIDAK VALID";
const char MSG_PILIH_KELAS[] PROGMEM = "PILIH KELAS";
const char MSG_LAINNYA[] PROGMEM = "LAINNYA";
const char MSG_MENU_ADMIN[] PROGMEM = "MENU ADMIN";
const char MSG_MENU_DOSEN[] PROGMEM = "MENU DOSEN";
const char MSG_TIDAK_TERDAFTAR[] PROGMEM = "Tidak Terdaftar!";

// Logging tags - sering digunakan
const char TAG_LOGIN[] PROGMEM = "[LOGIN]";
const char TAG_API[] PROGMEM = "[API]";
const char TAG_ENROLL[] PROGMEM = "[ENROLL]";
const char TAG_SYNC[] PROGMEM = "[SYNC]";
const char TAG_WIFI[] PROGMEM = "[WIFI]";
const char TAG_USB[] PROGMEM = "[USB]";
const char TAG_NAV[] PROGMEM = "[NAV]";

// TUGAS 4: PENYESUAIAN HEADER untuk WPA2-Enterprise
#if __has_include("esp_eap_client.h")
    #include "esp_eap_client.h"
#elif __has_include("esp_wpa2.h")
    #include "esp_wpa2.h"
#endif

extern UserManager userManager;
extern MahasiswaManager mahasiswaManager;
extern FingerprintDataManager fingerprintDataManager;
extern FingerprintManager fingerprintManager;
extern PresensiManager presensiManager;
extern KelasManager kelasManager;
extern CH376Manager ch376;
extern WiFiManager wifiManager;
extern APIManager apiManager;
extern ExportManager exportManager;
extern String globalJwtToken;  // TUGAS 3: Untuk pull fingerprint

// ========== HELPER: LOAD KELAS BY NIP ==========
// SUMBER TUNGGAL: dosen_kelas.csv, cocokkan NIP saja.
// PENTING: fungsi ini SENGAJA TIDAK menyentuh pertemuan.csv / jadwal sama sekali.
// Untuk Registrasi Mhs maupun Presensi, daftar kelas = SEMUA kelas yang diampu dosen
// (berdasarkan NIP), TANPA syarat punya jadwal/pertemuan. Filter pertemuan hanya
// relevan saat memilih PERTEMUAN (STATE_PRESENSI_PILIH_PERTEMUAN), bukan saat pilih kelas.
// Format dosen_kelas.csv: id,nip,kode_kelas,kelas
int loadKelasByNIP(String nip, String kodeList[], String kelasList[], int maxCount) {
    int count = 0;

    // Normalisasi NIP target sekali di awal (tahan spasi / CR sisa)
    nip.trim();
    nip.replace("\r", "");
    nip.replace("\n", "");

    File file = SD.open("/dosen_kelas.csv", FILE_READ);
    if (!file) {
        Serial.printf("[LOAD_KELAS] ERROR: Cannot open dosen_kelas.csv for NIP: %s\n", nip.c_str());
        return 0;
    }

    if (file.available()) file.readStringUntil('\n'); // Skip header

    int rowSeen = 0;
    while (file.available() && count < maxCount) {
        String line = file.readStringUntil('\n');
        line.trim();
        line.replace("\r", "");
        if (line.length() < 5) continue;
        if (line.startsWith("id,") || line.startsWith("id,nip")) continue;

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);
        int p4 = line.indexOf(',', p3 + 1);

        if (p1 > 0 && p2 > 0 && p3 > 0) {
            String csvNip = line.substring(p1 + 1, p2);
            csvNip.trim();
            csvNip.replace("\r", "");

            String kodeMk = "";
            if (p3 > p2) {
                kodeMk = line.substring(p2 + 1, p3);
                kodeMk.trim();
                kodeMk.replace("\r", "");
            }

            String kelas = "";
            if (p4 > 0 && p4 > p3) {
                kelas = line.substring(p3 + 1, p4);
            } else {
                kelas = line.substring(p3 + 1);
            }
            kelas.trim();
            kelas.replace("\r", "");
            kelas.replace("\n", "");
            kelas.replace(" ", "");

            rowSeen++;
            // DEBUG: cetak SETIAP baris + status match -> mudah lihat apakah EL1111
            // memang ada di dosen_kelas.csv atau tidak (akar masalah ada di sini).
            bool match = (csvNip == nip && kodeMk.length() > 0);
            Serial.printf_P(PSTR("[LOAD_KELAS] Row%d: nip='%s' kode='%s' kelas='%s' -> %s\n"),
                rowSeen, csvNip.c_str(), kodeMk.c_str(), kelas.c_str(),
                match ? "MATCH" : "skip");

            // HANYA cocokkan NIP - TANPA cek pertemuan.csv
            if (match) {
                kodeList[count] = kodeMk;
                kelasList[count] = kelas;
                count++;
            }
        }
    }
    file.close();

    Serial.printf("[LOAD_KELAS] Total kelas untuk NIP %s: %d (dari %d baris)\n",
        nip.c_str(), count, rowSeen);
    return count;
}

// ========== HELPER: HITUNG RATIO KELAS ==========
// Mengisi classEnrolled[] dan classTotal[] berdasarkan kelas yang dipilih
void calculateClassRatio(String kodeList[], String kelasList[], int count, int enrolled[], int total[]) {
    for (int i = 0; i < count; i++) {
        enrolled[i] = 0;
        total[i] = 0;

        String targetKode = kodeList[i];
        String targetKelas = kelasList[i];
        targetKode.trim();
        targetKelas.trim();

        // ========== CEK CACHE DULU ==========
        if (targetKode.length() > 0 && targetKelas.length() > 0) {
            File cacheFile = SD.open("/ratio_cache.txt", FILE_READ);
            if (cacheFile) {
                while (cacheFile.available()) {
                    String line = cacheFile.readStringUntil('\n');
                    line.trim();
                    if (line.length() < 5) continue;

                    int p1 = line.indexOf('|');
                    int p2 = line.indexOf('|', p1 + 1);
                    int p3 = line.indexOf('|', p2 + 1);

                    if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

                    String kode = line.substring(0, p1);
                    String kls = line.substring(p1 + 1, p2);
                    String enr = line.substring(p2 + 1, p3);
                    String tot = line.substring(p3 + 1);

                    kode.trim();
                    kls.trim();

                    if (kode == targetKode && kls == targetKelas) {
                        enrolled[i] = enr.toInt();
                        total[i] = tot.toInt();
                        Serial.printf_P(PSTR("[RATIO] Cache hit: %s|%s = %d/%d\n"),
                            kode.c_str(), kls.c_str(), enrolled[i], total[i]);
                        break;
                    }
                }
                cacheFile.close();
            }
        }
    }
}

String tempNIP;
String sessionPin;   // PIN dosen/admin yang sedang login (diisi saat login, dikosongkan saat logout)
String deleteTarget;

// ADAPTIVE LOADING flag: true bila kelas > 99 mhs (fp_2 tidak diinjeksi -> menu Jari Cadangan aktif)
bool isAdaptiveMode = false;

// ========== ENROLLMENT 2-JARI (Jempol Kanan + Kiri) ==========
// Hex jari pertama ditahan di sini lintas fase loop state machine; jari kedua
// di-ekstrak lalu di-payload bersama tempHexJari1 -> push satu request.
String tempHexJari1 = "";
// Slot R503 sementara - jangan bentrok dengan slot mahasiswa (3..198) atau dosen (1-2).
// Setelah extract+push, kedua slot ini di-delete; data resmi akan diinjeksi ulang
// saat PULL_FP/INJECT_SENSOR berikutnya menarik dari server.
#define ENROLL_TEMP_SLOT_1 198
#define ENROLL_TEMP_SLOT_2 199
String tempNIM;
String selectedKelas;

int viewIndex = 0;
bool needRender = true;
State wifiReturnState = STATE_MENU_ADMIN_PAGE4_2;  // Default kembali ke menu Admin

// ========== TUGAS 4 & 5: PRESENSI SESSION VARIABLES ==========
static String presensiKodeMk = "";
static String presensiKelas = "";
static String presensiPertemuan = "";
static String presensiTanggal = "";
int presensiJadwalId = 0;  // Non-static agar bisa diakses dari api_manager.cpp
static String listHadir[120];  // Max 120 students per session
static String listHadirTime[120];
static int listHadirCount = 0;
static int currentFPMatchSlot = 0;  // Slot ID fingerprint yang cocok
static bool isRetryPull = false;    // Menyimpan state apakah sedang retry pull FP
// ========== TUGAS 4: PULL FINGERPRINT STATUS ==========
static int pullSuccess = 0;
static int pullFailed = 0;

// ========== TUGAS 1: ENROLLMENT RATIO ==========
static int enrolledCount = 0;
static int totalCount = 0;

enum InputMode {
    INPUT_NIP,
    INPUT_PIN,
    INPUT_ADD_NIP,
    INPUT_ADD_PIN,
    INPUT_OLD_PIN,
    INPUT_NEW_PIN,

    // DELETE FLOW
    INPUT_DELETE_NIP,
    INPUT_DELETE_ADMIN_PIN,
    INPUT_CONFIRM_DELETE,

    // DELETE MAHASISWA FLOW
    INPUT_DELETE_MAHASISWA_NIM,
    INPUT_DELETE_MAHASISWA_ADMIN_PIN,
    INPUT_CONFIRM_DELETE_MAHASISWA,

    // REGISTRASI MAHASISWA
    INPUT_REG_NIM
};

InputMode mode;

// ================= CONSTRUCTOR =================
StateMachine::StateMachine(LCDManager* l, KeypadManager* k, InputHandler* i, AuthManager* a) {
    lcd = l;
    keypad = k;
    input = i;
    auth = a;

    // ========== TUGAS 6: INISIALISASI VARIABEL REGISTRASI ==========
    tempKodeMk = "";
    tempKelas = "";
    tempNIM = "";
    tempIsSitIn = false;
    tempFingerId = 0;
    regSessionCount = 0;
    regSessionSitInCount = 0;
    enrolling = false;
    lastLCDUpdate = 0;
}

// ================= LOGIN RESET =================
void StateMachine::goToLogin() {
    currentState = STATE_LOGIN;
    mode = INPUT_NIP;

    // Kosongkan PIN sesi agar tidak bocor ke sesi user berikutnya.
    sessionPin = "";

    input->setMode("Masukan NIP:", false);
    input->reset(false);  // Tidak tampilkan Batal di login awal
    // Catatan: input->reset(false) sudah menampilkan "A.Setup WiFi" di baris 3

    needRender = true;
}

// ================= GO TO MAIN MENU =================
void StateMachine::goToMainMenu() {
    if (currentRole == ROLE_ADMIN) {
        currentState = STATE_MENU_ADMIN;
    } else {
        currentState = STATE_MENU_DOSEN;
    }
    needRender = true;
}

// ================= INIT =================
void StateMachine::init() {
    goToLogin();
}

// ================= CONTEXT-AWARE SYNC STATUS HELPER =================
// Memilih flag sync berdasarkan state aktif. Implementasi di sini agar
// enum StateMachineState bisa dipakai langsung tanpa hardcode nilai.
bool getCurrentSyncStatus() {
    State s = (State)g_currentState;
    if (s == STATE_LOGIN) {
        return syncStatusUsers;
    }
    if (s == STATE_MENU_DOSEN || s == STATE_MENU_ADMIN) {
        return syncStatusDashboard;
    }
    // Fase 3: state Presensi, Pilih Kelas, Registrasi, dll
    return syncStatusFeature;
}

// ================= STATUS ICONS RIGHT-CORNER HELPER =================
// Cetak ikon WiFi + Sync di pojok kanan atas (cols 15-19) agar konsisten di setiap
// halaman utama (Login, Menu Dosen, Menu Admin). Judul WAJIB max 15 karakter
// (cols 0-14) + padding agar tidak menimpa ikon.
//
// Layout 5-kolom (cols 15-19):
//   col 15 = spasi separator
//   col 16 = Icon Sync       (custom char 3)
//   col 17 = Sync status     (custom char 1 = OK / 2 = gagal, context-aware)
//   col 18 = Icon Antena WiFi (custom char 0)
//   col 19 = WiFi status     (custom char 1 = terhubung / 2 = putus)
void drawStatusIcons() {
    lcd.setCursor(15, 0);
    lcd.print(" ");                                   // separator agar judul tidak menempel ikon
    lcd.setCursor(16, 0);
    lcd.write(3);                                     // Icon Sync
    lcd.setCursor(17, 0);
    lcd.write(getCurrentSyncStatus() ? 1 : 2);        // Sync status (3 fase)
    lcd.setCursor(18, 0);
    lcd.write(0);                                     // Icon Antena
    lcd.setCursor(19, 0);
    lcd.write(WiFi.status() == WL_CONNECTED ? 1 : 2); // WiFi status
}


// ================= PUSH SYNC OPERATOR DOSEN (2-JARI) =================
// POST /api/device/sync dengan {operators:[{nip, pin, fp_1_user, fp_2_user}], presensi:[]}.
// Mengembalikan true bila HTTP 200/201.
static bool pushSyncOpDualFingers(const String& nip, const String& pin,
                                  const String& fp1, const String& fp2) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[SYNC_OP] WiFi off"));
        return false;
    }
    if (pin.length() == 0) {
        Serial.println(F("[SYNC_OP] sessionPin kosong - skip push"));
        return false;
    }
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    HTTPClient http;
    String url = String(API_BASE_URL) + "/api/device/sync";
    http.begin(secureClient, url);
    http.setTimeout(15000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-device-code",   DEVICE_CODE);
    http.addHeader("x-device-secret", DEVICE_SECRET);
    if (globalJwtToken.length() > 0) {
        http.addHeader("Authorization", "Bearer " + globalJwtToken);
    }

    // Payload: {"operators":[{nip, pin, fp_1_user, fp_2_user}], "presensi":[]}
    JsonDocument doc;
    JsonArray operators = doc["operators"].to<JsonArray>();
    JsonObject op = operators.add<JsonObject>();
    op["nip"]       = nip;
    op["pin"]       = pin;
    op["fp_1_user"] = trimHex(fp1);
    op["fp_2_user"] = trimHex(fp2);
    doc["presensi"].to<JsonArray>();   // kosong - tahap ini sync user saja

    String payload;
    serializeJson(doc, payload);
    Serial.printf_P(PSTR("[SYNC_OP] Payload size: %d\n"), payload.length());

    int code = http.POST(payload);
    Serial.printf_P(PSTR("[SYNC_OP] HTTP Code: %d\n"), code);
    http.end();
    doc.clear();
    return (code == 200 || code == 201);
}

// ================= UPDATE =================
void StateMachine::update() {
    // Publikasikan state aktif agar getCurrentSyncStatus() (yang dipanggil
    // dari input_handler, lcd, dll) tahu konteks halaman saat ini.
    g_currentState = (int)currentState;

    switch (currentState) {

        // ================= LOGIN =================
        case STATE_LOGIN:
        {
            // Ikon WiFi dan Sync Status sudah ditangani oleh input->reset() di goToLogin()
            // Jadi tidak perlu menambahkannya di sini

            // ========== RETRY SYNC: hint di baris 2 (baris bebas) bila sinkron users gagal ==========
            if (needRender) {
                drawStatusIcons();
                if (isSyncUserFailed) {
                    lcd->printLine(2, F("A.Retry Sync"));
                    lcd->printLine(3, F("B.Setup Wifi"));
                } else {
                    lcd->printLine(3, F("A.Setup Wifi"));
                }
                needRender = false;
            }

            // TUGAS 1 & 3:ambil key SEKALI saja, lalu proses sesuai logika
            char key = keypad->getKey();

            // ========== RETRY SYNC: tombol B menarik ulang /api/device/users ==========
            if (key == 'A' && isSyncUserFailed && mode == INPUT_NIP) {
                Serial.println(F("[LOGIN] Retry sinkronisasi users..."));
                logSystemActivity("SYNC_RETRY", "Retry sync users dari Login");
                apiManager.fetchUsersAndSync();
                goToLogin();  // gambar ulang layar login (hint hilang bila sukses)
                break;
            }

            // TUGAS 1: CEGAT TOMBOL 'A' untuk WiFi Setup
            if ((key == 'A' && !isSyncUserFailed) || (key == 'B' && isSyncUserFailed)) {
                Serial.println(F("[NAV] Masuk ke Setup WiFi dari Login (Cegat di awal)"));
                logSystemActivity("MENU_NAV", "Masuk ke Setup WiFi dari Login");

                // Bersihkan buffer input
                input->reset(false);

                // Set wifi return state dan ubah state
                wifiReturnState = STATE_LOGIN;
                currentState = STATE_WIFI_BROWSE;  // TUGAS 1: Browse WiFi list first
                needRender = true;
                break;  // Keluar dari switch
            }

            // TUGAS 2 & 3: Jika BUKAN 'A', teruskan ke input handler dengan key yang sudah diambil
            if (input->update(key)) {

                if (mode == INPUT_NIP) {
                    tempNIP = input->getValue();
                    mode = INPUT_PIN;

                    input->setMode("Masukan PIN:", true);
                    // TUGAS 2: reset(true) karena sudah mulai ketik (buffer tidak kosong)
                    input->reset(true);  // Tampilkan "D.Batal"
                }
                else if (mode == INPUT_PIN) {

                    String pin = input->getValue();
                    currentRole = auth->login(tempNIP, pin);

                    // Simpan PIN sesi (dosen/admin) supaya push /api/device/sync
                    // tidak perlu I/O SD card untuk lookup PIN.
                    sessionPin = pin;

                    lcd->clear();

                    if (currentRole == ROLE_ADMIN) {
                        logSystemActivity("LOGIN", "Admin: " + tempNIP);

                        // ========== TUGAS 1: INJEKSI API LANGKAH 2 - NIP/PIN LOGIN ==========
                        // Revisi: Kirim nip dan pin ke API (bukan finger_id)
                        Serial.println(F("[API-Langkah2] Memanggil API untuk Admin NIP/PIN login..."));
                        Serial.printf_P(PSTR("[API-Langkah2] NIP: %s, PIN: %s\n"), tempNIP.c_str(), pin.c_str());
                        // BYPASS DASHBOARD: Admin tidak menarik data dashboard
                        apiManager.sendLoginAcknowledge(tempNIP, pin, false);
                        // ==========================================================================

                        // Tampilkan UI dan transisi
                        lcd->clear();
                        lcd->printLine(0, MSG_LOGIN_ADMIN);
                        delay(1000);
                        currentState = STATE_MENU_ADMIN;

                        // TUGAS 3: Proses push queue Milik Sendiri setelah login
                        processPushQueue(tempNIP);
                    }
                    else if (currentRole == ROLE_DOSEN) {
                        logSystemActivity("LOGIN", "Dosen: " + tempNIP);

                        // ========== TUGAS 1: INJEKSI API LANGKAH 2 - NIP/PIN LOGIN ==========
                        // Revisi: Kirim nip dan pin ke API (bukan finger_id)
                        Serial.println(F("[API-Langkah2] Memanggil API untuk Dosen NIP/PIN login..."));
                        Serial.printf_P(PSTR("[API-Langkah2] NIP: %s, PIN: %s\n"), tempNIP.c_str(), pin.c_str());
                        apiManager.sendLoginAcknowledge(tempNIP, pin);
                        // ==========================================================================

                        // Tampilkan UI dan transisi
                        lcd->clear();
                        lcd->printLine(0, MSG_LOGIN_DOSEN);

                        // TUGAS 3: Proses push queue Milik Sendiri setelah login
                        processPushQueue(tempNIP);
                        delay(1000);
                        currentState = STATE_MENU_DOSEN;
                    }
                    else {
                        lcd->printLine(0, MSG_LOGIN_GAGAL);
                        logSystemActivity("LOGIN_FAILED", "NIP: " + tempNIP + " - Password salah");
                        delay(1500);
                        goToLogin();
                    }

                    needRender = true;
                }
            }

            // ========== CONCURRENT FINGERPRINT POLLING (Non-blocking) ==========
            // KEAMANAN: Fingerprint scanning HANYA aktif di STATE_LOGIN
            // Sensor TIDAK boleh polling saat di menu Admin/Dosen untuk mencegah cross-login
            static unsigned long lastFingerPoll = 0;
            int fingerId = 0;

            // Hanya polling fingerprint saat currentState adalah STATE_LOGIN
            if (currentState == STATE_LOGIN) {
                if (millis() - lastFingerPoll > 250) {  // Cek setiap 250ms
                    lastFingerPoll = millis();
                    fingerId = fingerprintManager.checkFingerprint();
                }
            } else {
                // Reset polling state saat keluar dari STATE_LOGIN
                lastFingerPoll = 0;
            }

            // Proses hasil fingerprint scan tanpa memblokir
            if (fingerId > 0) {
                // ========== DEBUG: PRINT FINGERPINT MAP ==========
                printFingerMap();

                // ========== DESYNC PREVENTION ==========
                // Proteksi: Jika slot di luar batas atau fingerMap kosong, re-sync dari SD
                if (fingerId < 1 || fingerId > 200) {
                    Serial.printf_P(PSTR("[LOGIN_ERROR] Slot %d di luar batas (1-200), abaikan\n"), fingerId);
                    fingerprintManager.resetScan();
                    return;
                }

                // Cek fingerMap untuk slot ini
                String userId = fingerMap[fingerId];
                userId.trim();

                // Jika fingerMap kosong atau korup, trigger re-sync dari CSV
                if (userId.length() == 0) {
                    Serial.printf_P(PSTR("[LOGIN_DESYNC] fingerMap[%d] kosong, melakukan re-sync dari SD...\n"), fingerId);
                    fingerprintDataManager.reloadData();

                    // Coba lagi setelah re-sync
                    userId = fingerMap[fingerId];
                    userId.trim();

                    if (userId.length() == 0) {
                        // Masih kosong setelah re-sync -可能有 new enrollment dari enrollment flow
                        // Coba cari langsung di CSV
                        userId = fingerprintDataManager.getUserIdBySlot(fingerId);
                        userId.trim();
                        if (userId.length() > 0) {
                            Serial.printf_P(PSTR("[LOGIN_RECOVERY] Ditemukan di CSV: fingerMap[%d] = %s\n"), fingerId, userId.c_str());
                        } else {
                            Serial.printf_P(PSTR("[LOGIN_ERROR] Slot %d tidak ditemukan di sensor, abaikan\n"), fingerId);
                            fingerprintManager.resetScan();
                            return;
                        }
                    }
                }

                Serial.printf_P(PSTR("[LOGIN] fingerMap[%d] = \"%s\"\n"), fingerId, userId.c_str());

                // ========== VALIDASI USER PROFIL (POST-FINGERPRINT) ==========
                // Setelah mendapatkan userId (NIP) dari fingerMap, validasi ke users.csv untuk tentukan role
                if (userId.length() > 0) {
                    bool userFound = false;
                    String role = "";
                    String fpPin = "";

                    // 1. Buka users.csv (Format: id,nip,pin,role,fingerprint_id)
                    // NIP di indeks 1, Role di indeks 3
                    File file = SD.open("/users.csv", FILE_READ);
                    if (file) {
                        // Baca header (skip)
                        if (file.available()) file.readStringUntil('\n');

                        while (file.available()) {
                            String line = file.readStringUntil('\n');
                            line.trim();
                            if (line.length() == 0) continue;

                            int p1 = line.indexOf(',');
                            int p2 = line.indexOf(',', p1 + 1);
                            int p3 = line.indexOf(',', p2 + 1);
                            int p4 = line.indexOf(',', p3 + 1);
                            if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

                            // csvNip ada di indeks 1, csvRole ada di indeks 3
                            String csvNip = line.substring(p1 + 1, p2);
                            String csvRole = line.substring(p3 + 1, p4);
                            csvNip.trim();
                            csvRole.trim();

                            // ========== TUGAS 2: LOGGING AGRESIF ==========
                            Serial.printf_P(PSTR("[DEBUG-LOGIN-CSV] Cek CSV NIP: '%s' vs Target: '%s'\n"), csvNip.c_str(), userId.c_str());

                            if (csvNip == userId) {
                                userFound = true;
                                role = csvRole;

                                // Ambil PIN juga untuk API call
                                fpPin = line.substring(p2 + 1, p3);
                                fpPin.trim();

                                Serial.printf_P(PSTR("[LOGIN] User ditemukan! NIP: %s, Role: %s\n"), csvNip.c_str(), role.c_str());
                                break;
                            }
                        }
                        file.close();
                    } else {
                        Serial.println(F("[LOGIN_ERROR] Tidak bisa membuka /users.csv"));
                    }

                    // 2. Arahkan ke menu yang benar
                    if (userFound) {
                        tempNIP = userId;
                        lcd->clear();
                        lcd->printLine(0, "Login Fingerprint");
                        lcd->printLine(1, userId);
                        fingerprintManager.setLEDSuccess();
                        delay(1500);
                        fingerprintManager.setLEDOff();

                        // ========== INJEKSI API LANGKAH 2 - FINGERPRINT LOGIN ==========
                        if (fpPin.length() > 0) {
                            Serial.println(F("[API-Langkah2] Memanggil API untuk Fingerprint login..."));
                            Serial.printf_P(PSTR("[API-Langkah2] NIP: %s, PIN: %s\n"), userId.c_str(), fpPin.c_str());
                            // Simpan PIN sesi (dari users.csv) untuk push /api/device/sync nanti
                            sessionPin = fpPin;
                            // BYPASS DASHBOARD untuk Admin (role sudah diketahui di sini)
                            apiManager.sendLoginAcknowledge(userId, fpPin, role != "admin");
                        } else {
                            Serial.printf_P(PSTR("[API-Langkah2] Warning: PIN tidak ditemukan untuk NIP: %s\n"), userId.c_str());
                        }
                        // ====================================================================

                        Serial.printf_P(PSTR("[LOGIN_SUCCESS] NIP: %s | Role: %s\n"), userId.c_str(), role.c_str());

                        // Pindah State berdasarkan role
                        if (role == "admin") {
                            currentRole = ROLE_ADMIN;
                            lcd->clear();
                            lcd->printLine(0, "Admin Login");
                            delay(1000);
                            currentState = STATE_MENU_ADMIN;
                        } else {
                            currentRole = ROLE_DOSEN;
                            lcd->clear();
                            lcd->printLine(0, "Dosen Login");
                            delay(1000);
                            currentState = STATE_MENU_DOSEN;
                        }
                        needRender = true;
                    } else {
                        Serial.printf_P(PSTR("[LOGIN_FAILED] User %s ada di fingerMap tapi tidak ditemukan di users.csv\n"), userId.c_str());
                        lcd->clear();
                        lcd->printLine(0, MSG_TIDAK_TERDAFTAR);
                        lcd->printLine(1, F("Kembali Menu..."));
                        logSystemActivity("LOGIN_FAILED", "Fingerprint - User tidak ada di users.csv");
                        fingerprintManager.setLEDError();
                        delay(1500);
                        fingerprintManager.setLEDOff();
                        goToLogin();
                        needRender = true;
                    }
                } else {
                    lcd->clear();
                    lcd->printLine(0, MSG_TIDAK_TERDAFTAR);
                    lcd->printLine(1, F("Kembali Menu..."));
                    logSystemActivity("LOGIN_FAILED", "Fingerprint - Fingerprint tidak terdaftar di sistem");
                    fingerprintManager.setLEDError();
                    delay(1500);
                    fingerprintManager.setLEDOff();
                    goToLogin();
                    needRender = true;
                }
            } else if (fingerId == -2) {
                // Fingerprint not matched - show briefly then continue
                lcd->clear();
                lcd->printLine(0, F("Tidak Dikenali"));
                logSystemActivity("FP_SCAN", "Fingerprint tidak cocok/coba lagi");
                fingerprintManager.setLEDError();
                delay(1000);
                fingerprintManager.setLEDOff();
                needRender = true;
                goToLogin();
            }
            // fingerId == 0 (no finger) or -1 (error) - skip, don't block
            // Catatan: Tombol 'A' sudah di-cegat di awal fungsi SEBELUM input->update()
        }
        break;

        // ================= MENU ADMIN =================
        case STATE_MENU_ADMIN:
        {
            if (needRender) {
                lcd->clear();
                // Judul max 15 char (cols 0-14) supaya ikon di cols 15-19 tidak tertimpa.
                lcd->printLine(0, F("MENU ADMIN     "));
                drawStatusIcons();   // gambar WiFi + Sync di pojok kanan atas (konsisten)

                // Pull/Push FP dipindahkan ke menu utama Admin
                lcd->printLine(1, "A.Pull FP Mhs");
                lcd->printLine(2, "B.Push Antrian FP");
                lcd->printLine(3, "C.Menu Lanjut D.Logout");
                needRender = false;
            }

            // KEAMANAN: Fingerprint polling DIMATIKAN saat di dalam menu
            // Fingerprint hanya aktif di STATE_LOGIN untuk mencegah cross-login

            char key = keypad->getKey();

            if (key == 'A') {
                // ========== PULL FP MAHASISWA (dipindah dari Menu Dosen) ==========
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Pull Fingerprint");
                currentState = STATE_PULL_FP_MANUAL;
                needRender = true;
            }
            else if (key == 'B') {
                // ========== PUSH ANTRIAN FP / PUSH PENDING (dipindah dari Menu Dosen) ==========
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Push Antrian FP");
                currentState = STATE_PUSH_PENDING_AUTH;
                needRender = true;
            }
            else if (key == 'C') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Menu Lanjut (Akun Dosen)");
                currentState = STATE_MENU_ADMIN_AKUN;
                needRender = true;
            }
            else if (key == 'D') {
                logSystemActivity("LOGOUT", "Admin logout: " + tempNIP);
                goToLogin();
            }
        }
        break;

        // ================= MENU ADMIN - AKUN DOSEN =================
        // Tambah/Data Akun Dosen dipindah ke sini dari menu utama Admin
        case STATE_MENU_ADMIN_AKUN:
        {
            if (needRender) {
                lcd->clear();
                lcd->printLine(0, F("AKUN DOSEN"));
                lcd->printLine(1, F("A.Tambah Akun Dosen"));
                lcd->printLine(2, F("B.Data Akun Dosen"));
                lcd->printLine(3, F("C.Lanjut D.Kembali"));
                lcd->printWiFiStatus();
                needRender = false;
            }

            char key = keypad->getKey();

            if (key == 'A') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Tambah Akun Dosen");
                currentState = STATE_MENU_ADD_USER;
                mode = INPUT_ADD_NIP;

                input->setMode("NIP Dosen:", false);
                input->reset();
                needRender = true;
            }
            else if (key == 'B') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Data Dosen");
                currentState = STATE_VIEW_DOSEN;
                viewIndex = 0;
                needRender = true;
            }
            else if (key == 'C') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Menu Lainnya");
                currentState = STATE_MENU_ADMIN_MORE;
                needRender = true;
            }
            else if (key == 'D') {
                currentState = STATE_MENU_ADMIN;
                needRender = true;
            }
        }
        break;

        // ================= MENU ADMIN MORE =================
        case STATE_MENU_ADMIN_MORE:
        {
            if (needRender) {
                lcd->clear();
                lcd->printLine(0, MSG_LAINNYA);
                lcd->printLine(1, F("A.Hapus FP Dosen"));
                lcd->printLine(2, F("B.Hapus FP Mhs"));
                lcd->printLine(3, F("C.Lainnya D.Kembali"));
                lcd->printWiFiStatus();
                needRender = false;
            }

            char key = keypad->getKey();

            if (key == 'A') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Hapus FP Dosen");
                currentState = STATE_DELETE_DOSEN_LIST;
                needRender = true;
            }
            else if (key == 'B') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Hapus FP Mhs");
                currentState = STATE_DELETE_MHS_SELECT_KELAS;
                needRender = true;
            }
            else if (key == 'C') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Menu Lainnya (Lanjutan)");
                currentState = STATE_MENU_ADMIN_PAGE3;
                needRender = true;
            }
            else if (key == 'D') {
                currentState = STATE_MENU_ADMIN_AKUN;  // kembali ke Akun Dosen (rantai menu)
                needRender = true;
            }
        }
        break;

        // ================= DELETE DOSEN =================
        case STATE_DELETE_USER:
        {
            // STEP 1: INPUT NIP
            if (mode == INPUT_DELETE_NIP) {
                if (input->update()) {
                    deleteTarget = input->getValue();
                    
                    if(deleteTarget == "123"){
                        currentState = STATE_MENU_ADMIN;
                        lcd->printLine(0, "NIP Tidak Valid!");
                        delay(1500);
                        input->reset();
                        needRender = true;
                        break;
                    }
                    mode = INPUT_DELETE_ADMIN_PIN;
                    input->setMode("Masukan PIN Admin:", true);
                    input->reset();
                }
            }

            // STEP 2: INPUT PIN ADMIN
            else if (mode == INPUT_DELETE_ADMIN_PIN) {
                if (input->update()) {

                    String pin = input->getValue();

                    if (!auth->login("123", pin)) {
                        lcd->clear();
                        lcd->printLine(0, MSG_PIN_SALAH);
                        delay(1500);

                        currentState = STATE_MENU_ADMIN;
                        needRender = true;
                        break;
                    }

                    mode = INPUT_CONFIRM_DELETE;

                    lcd->clear();
                    lcd->printLine(0, MSG_YAKIN_HAPUS);
                    lcd->printLine(1, deleteTarget);
                    lcd->printLine(2, "A.Ya  B.Batal");
                }
            }

            // STEP 3: KONFIRMASI (FIX DI SINI)
            else if (mode == INPUT_CONFIRM_DELETE) {

                char key = keypad->getKey();

                if (key != NO_KEY) {

                    Serial.print("KEY: ");
                    Serial.println(key);

                    if (key == 'A') {
                        bool ok = userManager.deleteUser(deleteTarget);

                        lcd->clear();
                        lcd->printLine(0, ok ? "Berhasil Dihapus" : "User Tidak Ada");
                        delay(1500);

                        currentState = STATE_MENU_ADMIN;
                        needRender = true;
                    }
                    else if (key == 'B') {
                        currentState = STATE_MENU_ADMIN;
                        needRender = true;
                    }
                }
            }
        }
        break;

        // ================= VIEW DOSEN =================
        case STATE_VIEW_DOSEN:
        {
            if (needRender) {
                std::vector<String> list = userManager.getAllDosen();

                lcd->clear();
                lcd->printLine(0, "LIST DOSEN");

                for (int i = 0; i < 2; i++) {
                    int idx = viewIndex + i;

                    if (idx < list.size()) {
                        String nip = list[idx];
                        bool hasFp = fingerprintDataManager.hasFingerprint(nip);

                        lcd->printLine(i + 1, String(idx + 1) + ". " + nip);

                        // FP Status: Char 1 = Ceklis (terdaftar), Char 2 = Silang (belum)
                        // Geser ke kiri (column 16) agar tidak overflow
                        lcd->setCursor(16, i + 1);
                        lcd->print("FP ");
                        if (hasFp) {
                            lcd->write(1); // Ceklis
                        } else {
                            lcd->write(2); // Silang
                        }
                    } else {
                        lcd->printLine(i + 1, "");
                    }
                }

                lcd->printLine(3, "A.Next B.Back D.Exit");
                needRender = false;
            }

            char key = keypad->getKey();

            if (key == 'A') {
                std::vector<String> list = userManager.getAllDosen();
                if (viewIndex + 2 < list.size()) {
                    viewIndex += 2;
                    needRender = true;
                }
            }
            else if (key == 'B') {
                if (viewIndex - 2 >= 0) {
                    viewIndex -= 2;
                    needRender = true;
                }
            }
            else if (key == 'D') {
                currentState = STATE_MENU_ADMIN;
                needRender = true;
            }
        }
        break;

        // ================= VIEW MAHASISWA =================
        case STATE_VIEW_MAHASISWA:
        {
            if (needRender) {
                std::vector<MahasiswaData> list = mahasiswaManager.getAllMahasiswa();

                lcd->clear();
                lcd->printLine(0, "== LIST MAHASISWA ==");

                for (int i = 0; i < 2; i++) {
                    int idx = viewIndex + i;

                    if (idx < list.size()) {
                        String nim = list[idx].nim;
                        bool hasFp = fingerprintDataManager.hasFingerprint(nim);

                        // Print NIM first
                        String display = String(idx + 1) + ". " + nim;
                        lcd->printLine(i + 1, display);

                        // FP Status: Char 1 = Ceklis (terdaftar), Char 2 = Silang (belum)
                        // Geser ke kiri (column 16) agar tidak overflow
                        lcd->setCursor(16, i + 1);
                        lcd->print("FP ");
                        if (hasFp) {
                            lcd->write(1); // Ceklis
                        } else {
                            lcd->write(2); // Silang
                        }
                    } else {
                        lcd->printLine(i + 1, "");
                    }
                }

                lcd->printLine(3, "A.Next B.Back D.Exit");
                needRender = false;
            }

            char key = keypad->getKey();

            if (key == 'A') {
                std::vector<MahasiswaData> list = mahasiswaManager.getAllMahasiswa();
                if (viewIndex + 2 < list.size()) {
                    viewIndex += 2;
                    needRender = true;
                }
            }
            else if (key == 'B') {
                if (viewIndex - 2 >= 0) {
                    viewIndex -= 2;
                    needRender = true;
                }
            }
            else if (key == 'D') {
                if (currentRole == ROLE_ADMIN) {
                    currentState = STATE_MENU_ADMIN_PAGE3;
                } else {
                    currentState = STATE_MENU_DOSEN;
                }
                needRender = true;
            }
        }
        break;

        // ================= DELETE MAHASISWA =================
        case STATE_DELETE_MAHASISWA:
        {
            // STEP 1: INPUT NIM
            if (mode == INPUT_DELETE_MAHASISWA_NIM) {
                if (input->update()) {
                    deleteTarget = input->getValue();

                    mode = INPUT_DELETE_MAHASISWA_ADMIN_PIN;
                    input->setMode("Masukan PIN Admin:", true);
                    input->reset();
                }
            }

            // STEP 2: INPUT PIN ADMIN
            else if (mode == INPUT_DELETE_MAHASISWA_ADMIN_PIN) {
                if (input->update()) {
                    String pin = input->getValue();

                    if (!auth->login("123", pin)) {
                        lcd->clear();
                        lcd->printLine(0, MSG_PIN_SALAH);
                        delay(1500);

                        if (currentRole == ROLE_ADMIN) {
                            currentState = STATE_MENU_ADMIN_PAGE4;
                        } else {
                            currentState = STATE_MENU_DOSEN;
                        }
                        needRender = true;
                        break;
                    }

                    mode = INPUT_CONFIRM_DELETE_MAHASISWA;

                    lcd->clear();
                    lcd->printLine(0, MSG_YAKIN_HAPUS);
                    lcd->printLine(1, deleteTarget);
                    lcd->printLine(2, "A.Ya  B.Batal");
                }
            }

            // STEP 3: KONFIRMASI
            else if (mode == INPUT_CONFIRM_DELETE_MAHASISWA) {
                char key = keypad->getKey();

                if (key != NO_KEY) {
                    if (key == 'A') {
                        // Get fingerprint ID first
                        int fpId = fingerprintDataManager.getFingerprintId(deleteTarget);

                        // Delete from sensor if exists
                        if (fpId > 0) {
                            fingerprintManager.deleteTemplate(fpId);
                        }

                        // Delete from CSV (only fingerprint, not mahasiswa.csv)
                        bool delOk = fingerprintDataManager.deleteFingerprintByUserId(deleteTarget);

                        lcd->clear();
                        lcd->printLine(0, delOk ? "Berhasil Dihapus" : "Data Tidak Ada");
                        delay(1500);

                        // Log: Fingerprint dihapus
                        if (delOk) {
                            logSystemActivity("DELETE_FP", "Mahasiswa: " + deleteTarget + " Slot: " + fpId);
                        }

                        if (currentRole == ROLE_ADMIN) {
                            currentState = STATE_MENU_ADMIN_PAGE4;
                        } else {
                            currentState = STATE_MENU_DOSEN;
                        }
                        needRender = true;
                    }
                    else if (key == 'B') {
                        if (currentRole == ROLE_ADMIN) {
                            currentState = STATE_MENU_ADMIN_PAGE4;
                        } else {
                            currentState = STATE_MENU_DOSEN;
                        }
                        needRender = true;
                    }
                }
            }
        }
        break;

        // ================= ADD USER =================
        case STATE_MENU_ADD_USER:
        {
            if (input->update()) {
                String val = input->getValue();

                // Cek jika user membatalkan dengan tombol D
                if (val == "CANCEL") {
                    tempNIP = "";
                    input->setMode("NIP Dosen:", false);
                    currentState = STATE_MENU_ADMIN;
                    needRender = true;
                    break;
                }

                if (mode == INPUT_ADD_NIP) {
                    tempNIP = val;
                    mode = INPUT_ADD_PIN;

                    input->setMode("PIN Dosen:", true);
                    input->reset();
                }
                else if (mode == INPUT_ADD_PIN) {

                    String pin = val;

                    lcd->clear();

                    if (userManager.addUser(tempNIP, pin, "dosen")) {
                        lcd->printLine(0, "Berhasil!");
                        logSystemActivity("ADD_DOSEN", "NIP: " + tempNIP);
                    } else {
                        lcd->printLine(0, "Gagal!");
                        logSystemActivity("ADD_DOSEN", "GAGAL NIP: " + tempNIP);
                    }

                    delay(1500);
                    currentState = STATE_MENU_ADMIN;
                    needRender = true;
                }
            }
        }
        break;

        // ================= CHANGE PASSWORD =================
        case STATE_CHANGE_PASSWORD:
        {
            if (input->update()) {
                String val = input->getValue();

                // Cek jika user membatalkan dengan tombol D
                if (val == "CANCEL") {
                    input->setMode("PIN Lama:", true);
                    input->reset();
                    if (currentRole == ROLE_ADMIN) {
                        currentState = STATE_MENU_ADMIN;
                    } else {
                        currentState = STATE_MENU_DOSEN;
                    }
                    needRender = true;
                    break;
                }

                if (mode == INPUT_OLD_PIN) {

                    String oldPin = val;

                    if (!auth->login(tempNIP, oldPin)) {
                        lcd->clear();
                        lcd->printLine(0, MSG_PIN_SALAH);
                        delay(1500);
                        if (currentRole == ROLE_ADMIN) {
                            currentState = STATE_MENU_ADMIN;
                        } else {
                            currentState = STATE_MENU_DOSEN;
                        }
                        needRender = true;
                        break;
                    }

                    mode = INPUT_NEW_PIN;
                    input->setMode("PIN Baru:", true);
                    input->reset();
                }
                else if (mode == INPUT_NEW_PIN) {

                    String newPin = val;

                    userManager.changePassword(tempNIP, newPin);

                    lcd->clear();
                    lcd->printLine(0, "Password Berhasil");
                    lcd->printLine(1, "Diganti!");
                    delay(1500);

                    // Log: Password changed
                    logSystemActivity("CHANGE_PASSWORD", "User: " + tempNIP);

                    if (currentRole == ROLE_ADMIN) {
                        currentState = STATE_MENU_ADMIN;
                    } else {
                        currentState = STATE_MENU_DOSEN;
                    }
                    needRender = true;
                }
            }
        }
        break;

        // ================= DOSEN =================
        case STATE_MENU_DOSEN:
        {
            static int menuPage = 0;
            static bool needsRedraw = true;

            if (needRender) {
                menuPage = 0;
                needsRedraw = true;
                needRender = false;
            }

            // ========== CONTEXT-AWARE MENU: berdasarkan syncStatusDashboard ==========
            if (needsRedraw) {
                lcd->clear();
                if (menuPage == 0) {
                    // Judul max 15 char (cols 0-14) supaya ikon di cols 15-19 tidak tertimpa.
                    lcd->printLine(0, F("MENU DOSEN ===="));
                    drawStatusIcons();   // gambar WiFi + Sync di pojok kanan atas

                    if (syncStatusDashboard) {
                        // Sync SUKSES: alur normal
                        lcd->printLine(1, F("A.Presensi Kuliah "));
                        lcd->printLine(2, F("B.Registrasi Mhs  "));
                    } else {
                        // Sync GAGAL: tonjolkan Retry; Registrasi pindah ke Next
                        lcd->printLine(1, F("A.Retry Sync       "));
                        lcd->printLine(2, F("B.Presensi Kuliah "));
                    }
                    lcd->printLine(3, F("C.Next   D.Logout  "));
                } else if (menuPage == 1) {
                    // Judul max 15 char (cols 0-14) supaya ikon di cols 15-19 tidak tertimpa.
                    lcd->printLine(0, F("MENU DOSEN ===="));
                    drawStatusIcons();   // gambar WiFi + Sync di pojok kanan atas

                    if (syncStatusDashboard) {
                        // Sync SUKSES: layout Next normal
                        lcd->printLine(1, F("A.Daftar Fingerprint"));
                        lcd->printLine(2, F("B.Setup WiFi       "));
                    } else {
                        // Sync GAGAL: Registrasi tergeser ke sini; Setup WiFi tetap
                        // dapat diakses langsung agar user bisa benerin WiFi sebelum retry.
                        lcd->printLine(1, F("A.Registrasi Mhs   "));
                        lcd->printLine(2, F("B.Setup WiFi       "));
                    }
                    lcd->printLine(3, F("C.Next   D.Kembali  "));
                } else if (menuPage == 2) {
                    lcd->printLine(0, F("MENU DOSEN ===="));
                    drawStatusIcons();
                    lcd->printLine(1, F("A.Ekspor Data      "));
                    lcd->printLine(2, F("                    "));
                    lcd->printLine(3, F("C.Next   D.Kembali  "));
                }
                needsRedraw = false;
            }

            char key = keypad->getKey();

            // ========== TUGAS 1: LOGIKA PAGINASI ==========
            if (key == 'C') {
                // Next page
                menuPage = (menuPage + 1) % 3;
                needsRedraw = true;
                Serial.printf_P(PSTR("[MENU_DOSEN] Page berubah ke: %d\n"), menuPage);
            }
            else if (key == 'D') {
                if (menuPage == 0) {
                    // Logout dari page 1
                    logSystemActivity("LOGOUT", "Dosen logout: " + tempNIP);
                    goToLogin();
                } else {
                    // Kembali ke page 1
                    menuPage = 0;
                    needsRedraw = true;
                }
            }
            else if (key == 'A' || key == 'B') {
                // ========== CONTEXT-AWARE HANDLER: cabang sesuai syncStatusDashboard ==========
                if (menuPage == 0) {
                    if (key == 'A') {
                        if (!syncStatusDashboard) {
                            // ----- MODE GAGAL: A = Retry Sync (ulangi penarikan dashboard) -----
                            logSystemActivity("MENU_NAV", "Dosen: " + tempNIP + " Retry Sync Dashboard");
                            lcd->clear();
                            lcd->printLine(0, F("MENGULANG SYNC..."));
                            lcd->printLine(1, F("Mohon tunggu"));
                            apiManager.fetchDashboardData(globalJwtToken);
                            // syncStatusDashboard sudah di-update di dalam fetchDashboardData
                            // (false di awal, true di akhir processDashboardData bila sukses).
                            needsRedraw = true;  // gambar ulang -> layout otomatis menyesuaikan
                        } else {
                            // ----- MODE SUKSES: A = Presensi Kuliah -----
                            logSystemActivity("MENU_NAV", "Dosen: " + tempNIP + " membuka Presensi Kuliah");
                            tempKodeMk = ""; tempKelas = ""; tempNIM = "";
                            tempIsSitIn = false; tempFingerId = 0;
                            regSessionCount = 0; regSessionSitInCount = 0;
                            currentState = STATE_PRESENSI_PILIH_KELAS;
                            needRender = true;
                        }
                    } else if (key == 'B') {
                        if (!syncStatusDashboard) {
                            // ----- MODE GAGAL: B = Presensi Kuliah -----
                            logSystemActivity("MENU_NAV", "Dosen: " + tempNIP + " membuka Presensi Kuliah");
                            tempKodeMk = ""; tempKelas = ""; tempNIM = "";
                            tempIsSitIn = false; tempFingerId = 0;
                            regSessionCount = 0; regSessionSitInCount = 0;
                            currentState = STATE_PRESENSI_PILIH_KELAS;
                            needRender = true;
                        } else {
                            // ----- MODE SUKSES: B = Registrasi Mhs -----
                            logSystemActivity("MENU_NAV", "Dosen: " + tempNIP + " membuka Registrasi Mhs");
                            tempKodeMk = ""; tempKelas = ""; tempNIM = "";
                            tempIsSitIn = false; tempFingerId = 0;
                            regSessionCount = 0; regSessionSitInCount = 0;
                            currentState = STATE_REG_MHS_PILIH_KELAS;
                            needRender = true;
                        }
                    }
                } else if (menuPage == 1) {
                    // Page 2: A juga context-aware (Registrasi vs Daftar Fingerprint)
                    if (key == 'A') {
                        if (!syncStatusDashboard) {
                            // ----- MODE GAGAL: A = Registrasi Mhs (yang tergeser dari page 0) -----
                            logSystemActivity("MENU_NAV", "Dosen: " + tempNIP + " membuka Registrasi Mhs (page 2)");
                            tempKodeMk = ""; tempKelas = ""; tempNIM = "";
                            tempIsSitIn = false; tempFingerId = 0;
                            regSessionCount = 0; regSessionSitInCount = 0;
                            currentState = STATE_REG_MHS_PILIH_KELAS;
                            needRender = true;
                        } else {
                            // ----- MODE SUKSES: A = Daftar Fingerprint -----
                            logSystemActivity("MENU_NAV", "Dosen: " + tempNIP + " membuka Daftar Fingerprint");
                            currentState = STATE_ENROLL_DOSEN_FINGERPRINT;
                            needRender = true;
                        }
                    } else if (key == 'B') {
                        // B = Setup WiFi (langsung, tanpa sub-menu "Lainnya")
                        logSystemActivity("MENU_NAV", "Dosen: " + tempNIP + " membuka Setup WiFi");
                        wifiReturnState = STATE_MENU_DOSEN;     // selesai Setup WiFi -> kembali ke Menu Dosen
                        currentState   = STATE_WIFI_BROWSE;
                        needRender = true;
                    }
                } else if (menuPage == 2) {
                    if (key == 'A') {
                        logSystemActivity("MENU_NAV", "Dosen: " + tempNIP + " membuka Ekspor Data AP");
                        currentState = STATE_EKSPOR_AP;
                        needRender = true;
                    }
                }
            }
        }
        break;

        case STATE_EKSPOR_AP:
        {
            if (needRender) {
                // Pastikan WiFi config mode tidak sedang aktif
                wifiManager.stopConfigMode();

                // Ambil nama dosen dari dosen.csv
                String nama = userManager.getDosenNama(tempNIP);

                // Start Export AP mode
                exportManager.startExportMode(tempNIP, nama);

                lcd->clear();
                lcd->printLine(0, F("AP MODE AKTIF"));
                lcd->printLine(1, F("Konek WiFi: SixSeven"));
                lcd->printLine(2, F("Buka IP: 192.168.4.1"));
                lcd->printLine(3, F("D.Tutup AP"));
                needRender = false;
            }

            // Non-blocking: proses web server requests
            exportManager.process();

            char key = keypad->getKey();
            if (key == 'D') {
                exportManager.stopExportMode();
                logSystemActivity("EXPORT_AP", "Dosen " + tempNIP + " menutup AP");
                currentState = STATE_MENU_DOSEN;
                needRender = true;
            }
        }
        break;

        // STATE_MENU_DOSEN_MORE DIHAPUS - "Setup WiFi" kini langsung di Menu Dosen page 2 (B).

        // ================= ADMIN PAGE 3 =================
        case STATE_MENU_ADMIN_PAGE3:
        {
            if (needRender) {
                lcd->clear();
                lcd->printLine(0, MSG_LAINNYA);
                lcd->printLine(1, F("A.Registrasi Mhs"));
                lcd->printLine(2, F("B.Data Mahasiswa"));
                lcd->printLine(3, F("C.Lainnya D.Kembali"));
                lcd->printWiFiStatus();
                needRender = false;
            }

            char key = keypad->getKey();

            if (key == 'A') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Registrasi Mhs");
                currentState = STATE_REG_ADMIN_MHS_NIM;
                mode = INPUT_REG_NIM;

                input->setMode("NIM Mahasiswa:", false);
                input->reset();
                needRender = true;
            }
            else if (key == 'B') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Data Mahasiswa");
                currentState = STATE_SELECT_KELAS;
                viewIndex = 0;
                needRender = true;
            }
            else if (key == 'C') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Menu Lainnya (Lanjutan)");
                currentState = STATE_MENU_ADMIN_PAGE4;
                needRender = true;
            }
            else if (key == 'D') {
                currentState = STATE_MENU_ADMIN_MORE;
                needRender = true;
            }
        }
        break;

        // ================= ADMIN PAGE 4 =================
        case STATE_MENU_ADMIN_PAGE4:
        {
            if (needRender) {
                lcd->clear();
                lcd->printLine(0, MSG_LAINNYA);
                lcd->printLine(1, F("A.Daftar Fingerprint"));
                lcd->printLine(2, F("B.Ganti Password"));
                lcd->printLine(3, F("C.Lainnya D.Kembali"));
                lcd->printWiFiStatus();
                needRender = false;
            }

            char key = keypad->getKey();

            if (key == 'A') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Daftar Fingerprint");
                currentState = STATE_ENROLL_FINGERPRINT;
                needRender = true;
            }
            else if (key == 'B') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Ganti Password");
                currentState = STATE_CHANGE_PASSWORD;
                mode = INPUT_OLD_PIN;

                input->setMode("PIN Lama:", true);
                input->reset();
                needRender = true;
            }
            else if (key == 'C') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Menu Ekspor/WiFi");
                currentState = STATE_MENU_ADMIN_PAGE4_2;
                needRender = true;
            }
            else if (key == 'D') {
                currentState = STATE_MENU_ADMIN_PAGE3;
                needRender = true;
            }
        }
        break;

        // ================= ADMIN PAGE 4-2 =================
        case STATE_MENU_ADMIN_PAGE4_2:
        {
            if (needRender) {
                lcd->clear();
                lcd->printLine(0, MSG_LAINNYA);
                lcd->printLine(1, F("A.Ekspor Data"));
                lcd->printLine(2, F("B.Format Sensor"));
                lcd->printLine(3, F("D.Kembali"));
                lcd->printWiFiStatus();
                needRender = false;
            }

            char key = keypad->getKey();

            if (key == 'A') {
                logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " membuka Ekspor Data");
                currentState = STATE_ADMIN_WAIT_USB;
                needRender = true;
            }
            else if (key == 'B') {
                // Format Fingerprint Sensor & CSV
                lcd->clear();
                lcd->printLine(0, MSG_MEMPROSES);
                lcd->printLine(1, MSG_MOHON_TUNGGU);

                // Jalankan factory reset
                bool success = fingerprintDataManager.formatAllFingerprintData(&fingerprintManager);

                if (success) {
                    lcd->clear();
                    lcd->printLine(0, MSG_BERHASIL);
                    lcd->printLine(1, F("Sensor & CSV Kosong"));
                    logSystemActivity("FORMAT", "Admin memformat sensor dan database CSV");
                } else {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, F("Format Error"));
                    logSystemActivity("FORMAT_ERROR", "Admin gagal memformat sensor");
                }

                delay(2000);
                needRender = true;
            }
            else if (key == 'D') {
                currentState = STATE_MENU_ADMIN_PAGE4;
                needRender = true;
            }
        }
        break;

        // ================= REG MAHASISWA NIM (by Admin) =================
        case STATE_REG_ADMIN_MHS_NIM:
        {
            static String nimBuffer = "";

            if (needRender) {
                nimBuffer = "";
                lcd->clear();
                lcd->printLine(0, F("NIM Mahasiswa:"));
                lcd->printLine(1, "_");
                lcd->printLine(3, MSG_SELESAI);
                needRender = false;
            }

            char key = keypad->getKey();

            if (key >= '0' && key <= '9') {
                if (nimBuffer.length() < 10) {
                    nimBuffer += key;
                    lcd->printLine(1, nimBuffer + "_");
                }
            }
            else if (key == '*') {
                if (nimBuffer.length() > 0) {
                    nimBuffer.remove(nimBuffer.length() - 1);
                }
                lcd->printLine(1, nimBuffer + "_");
            }
            else if (key == '#') {
                if (nimBuffer.length() > 0) {
                    tempNIM = nimBuffer;

                    // Check if already registered
                    if (fingerprintDataManager.getFingerprintId(tempNIM) > 0) {
                        lcd->clear();
                        lcd->printLine(0, "Anda sudah");
                        lcd->printLine(1, "teregistrasi!");
                        delay(2000);
                        nimBuffer = "";
                        lcd->clear();
                        lcd->printLine(0, "NIM Mahasiswa:");
                        lcd->printLine(1, "_");
                        lcd->printLine(3, MSG_SELESAI);
                    } else {
                        // Don't save to mahasiswa.csv yet - wait for enrollment success
                        nimBuffer = "";
                        currentState = STATE_REG_ADMIN_MHS_FINGERPRINT;
                        needRender = true;
                    }
                }
            }
            else if (key == 'A') {
                nimBuffer = "";
                lcd->printLine(1, "_");
            }
            else if (key == 'D') {
                nimBuffer = "";
                // Back to previous menu
                if (currentRole == ROLE_ADMIN) {
                    currentState = STATE_MENU_ADMIN_PAGE3;
                } else {
                    currentState = STATE_MENU_DOSEN;
                }
                needRender = true;
            }
        }
        break;

        // ================= REG MAHASISWA FINGERPRINT (by Admin) =================
        case STATE_REG_ADMIN_MHS_FINGERPRINT:
        {
            static int fpId = 0;
            static unsigned long enrollStart = 0;

            if (needRender) {
                lcd->clear();
                lcd->printLine(0, "Scan Fingerprint");
                lcd->printLine(1, "Tempel jari pada");
                lcd->printLine(2, "sensor...");
                lcd->printLine(3, MSG_BATAL);
                needRender = false;
                enrolling = false;
                enrollStart = millis();
                lastLCDUpdate = millis();
            }

            char key = keypad->getKey();

            if (key == 'D') {
                // Cancel enrollment
                logSystemActivity("ENROLL_CANCEL", "Mahasiswa: " + tempNIM + " - User cancelled");
                fingerprintManager.resetEnrollment();
                enrolling = false;
                currentState = STATE_REG_ADMIN_MHS_NIM;
                mode = INPUT_REG_NIM;
                needRender = true;
                break;
            }

            // Start enrollment process
            if (!enrolling && millis() - enrollStart > 500) {
                enrolling = true;

                // ========== FIX: LANGSUNG POLLING SENSOR R503 UNTUK CARI SLOT KOSONG ==========
                // Abaikan ID lama dari CSV - cari slot yang benar-benar kosong di sensor
                int targetSlot = fingerprintManager.findEmptySlotForMahasiswa();
                if (targetSlot == -1) {
                    // Semua slot penuh
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "Memori Penuh");
                    delay(2000);
                    fingerprintManager.resetEnrollment();
                    enrolling = false;
                    currentState = STATE_REG_ADMIN_MHS_NIM;
                    mode = INPUT_REG_NIM;
                    needRender = true;
                    break;
                }

                fpId = targetSlot;
                Serial.printf_P(PSTR("[ENROLL] Target ID final (dari polling sensor): %d\n"), fpId);

                fingerprintManager.startEnrollment(fpId);
            }

            // Process enrollment (non-blocking)
            if (enrolling) {
                // Process enrollment
                fingerprintManager.processEnrollment();

                // If enrollment failed, update LCD immediately
                EnrollState state = fingerprintManager.getEnrollState();
                if (state == ENROLL_FAILED) {
                    // Enrollment failed - show error immediately
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "Packet Error / Coba");
                    lcd->printLine(2, "Lagi...");
                    lcd->printLine(3, "Tempel jari");

                    logSystemActivity("ENROLL_FP", "GAGAL - Mahasiswa: " + tempNIM + " - Packet error");

                    delay(1000);  // Brief delay before returning to input
                    fingerprintManager.resetEnrollment();
                    enrolling = false;
                    currentState = STATE_REG_ADMIN_MHS_NIM;
                    mode = INPUT_REG_NIM;
                    needRender = true;
                    break;
                }

                // Update LCD based on state every 500ms
                if (millis() - lastLCDUpdate > 500) {
                    lastLCDUpdate = millis();

                    lcd->clear();

                    if (state == ENROLL_PLACE_FINGER) {
                        lcd->printLine(0, MSG_TEMPEL_JARI);
                        lcd->printLine(1, " pada sensor...");
                        lcd->printLine(3, MSG_BATAL);
                    }
                    else if (state == ENROLL_PLACE_AGAIN) {
                        lcd->printLine(0, MSG_ANGKAT_TEMPEL);
                        lcd->printLine(1, " jari lagi...");
                        lcd->printLine(3, MSG_BATAL);
                    }
                    else if (state == ENROLL_SUCCESS) {
                        // TUGAS 3: Ekstrak hex template dari sensor sebelum save ke CSV
                        String hexTemplate = fingerprintManager.extractTemplateAsHex(fpId);

                        // Enrollment success - update CSV dengan robust algorithm
                        // updateFingerprintSlotWithHex sudah mencakup:
                        // - Lowest available ID
                        // - Update jika user exists, append jika baru
                        // - Timestamp (NTP atau "API_DISCONNECTED")
                        // - reloadData() dipanggil secara internal
                        bool saveSuccess = false;
                        if (hexTemplate.length() > 0) {
                            saveSuccess = fingerprintDataManager.updateFingerprintSlotWithHex(tempNIM, "mahasiswa", fpId, hexTemplate);
                        } else {
                            // Fallback: simpan slot ID jika gagal ekstrak hex
                            saveSuccess = fingerprintDataManager.updateFingerprintSlot(tempNIM, "mahasiswa", fpId);
                        }

                        if (!saveSuccess) {
                            // CSV update failed
                            lcd->printLine(0, MSG_GAGAL);
                            lcd->printLine(1, "Save CSV gagal");
                            delay(2000);

                            fingerprintManager.resetEnrollment();
                            enrolling = false;
                            currentState = STATE_REG_ADMIN_MHS_NIM;
                            mode = INPUT_REG_NIM;
                            needRender = true;
                        } else if (!mahasiswaManager.addMahasiswa(tempNIM, "")) {
                            // Rollback: delete fingerprint if mahasiswa save fails
                            fingerprintDataManager.deleteFingerprintByUserId(tempNIM);
                            lcd->printLine(0, MSG_GAGAL);
                            lcd->printLine(1, "Data sudah ada");
                            delay(2000);

                            fingerprintManager.resetEnrollment();
                            enrolling = false;
                            currentState = STATE_REG_ADMIN_MHS_NIM;
                            mode = INPUT_REG_NIM;
                            needRender = true;
                        } else {
                            // All saved successfully
                            // ========== FIX: UPDATE RAM LANGSUNG - JANGAN PANGGIL syncFromCSV() ==========
                            // syncFromCSV() akan flush sensor (empty_library) - HAPUSKAN!
                            // Langsung update fingerMap saja
                            if (fpId >= 3 && fpId <= 200) {
                                fingerMap[fpId] = tempNIM;
                                Serial.printf_P(PSTR("[ENROLL] RAM updated: fingerMap[%d] = %s\n"), fpId, tempNIM.c_str());
                            }

                            lcd->printLine(0, MSG_BERHASIL);
                            lcd->printLine(1, "NIM: " + tempNIM);
                            lcd->printLine(2, "FP ID: " + String(fpId));
                            delay(2000);

                            // Log: Mahasiswa fingerprint berhasil didaftarkan
                            logSystemActivity("ENROLL_FP", "Mahasiswa: " + tempNIM + " Slot: " + fpId);

                            fingerprintManager.resetEnrollment();
                            enrolling = false;
                            currentState = STATE_REG_ADMIN_MHS_NIM;
                            mode = INPUT_REG_NIM;
                            needRender = true;
                        }
                    }
                    else if (state == ENROLL_FAILED) {
                        // Enrollment failed - don't save anything
                        lcd->printLine(0, MSG_GAGAL);
                        lcd->printLine(1, "Coba Lagi...");
                        logSystemActivity("ENROLL_FP", "GAGAL - Mahasiswa: " + tempNIM + " - Fingerprint scan error");
                        delay(2000);

                        fingerprintManager.resetEnrollment();
                        enrolling = false;
                        needRender = true;
                    }
                }
            }
        }
        break;

        // ========== TUGAS 2: PILIH KELAS (STATE_REG_MHS_PILIH_KELAS) ==========
        case STATE_REG_MHS_PILIH_KELAS:
        {
            // ========== TUGAS 1: INISIALISASI SEKALI SAJA + PRE-CALCULATE RATIO ==========
            static bool isInitialized = false;
            static bool needsRedraw = true;
            static int currentPage = 0;
            static String tempClassSelect = "";
            static String classListKode[20];
            static String classListKelas[20];
            static int classCount = 0;
            static int classEnrolled[20];
            static int classTotal[20];
            static bool isInputMode = false;  // TUGAS 2: Mode View vs Input

            // Reset initialization saat pertama kali masuk (needRender dari state lain)
            if (needRender) {
                isInitialized = false;
                needsRedraw = true;
                currentPage = 0;
                tempClassSelect = "";
                classCount = 0;
                isInputMode = false;
                needRender = false;
            }

            // ========== BACA CSV + HITUNG RATIO HANYA SEKALI ==========
            if (!isInitialized) {
                lcd->clear();
                lcd->printLine(0, MSG_PILIH_KELAS);
                lcd->printLine(1, MSG_LOADING);

                classCount = 0;

                // ========== LOAD KELAS MENGGUNAKAN HELPER FUNCTION ==========
                classCount = loadKelasByNIP(tempNIP, classListKode, classListKelas, 20);

                if (classCount == 0) {
                    lcd->clear();
                    lcd->printLine(0, "TIDAK ADA KELAS");
                    lcd->printLine(1, "TERDAFTAR!");
                    delay(1500);
                    currentState = STATE_MENU_DOSEN;
                    needRender = true;
                    break;
                }

                // ========== TUGAS 1: PRE-CALCULATE RATIO KELAS (DENGAN CACHE) ==========
                // Coba dulu dari cache, fallback ke CSV jika tidak ada
                for (int i = 0; i < classCount; i++) {
                    classEnrolled[i] = 0;
                    classTotal[i] = 0;

                    String targetKode = classListKode[i];
                    String targetKelas = classListKelas[i];
                    targetKode.trim();
                    targetKelas.trim();

                    // ========== OPTIMASI: CEK CACHE DULU ==========
                    bool fromCache = false;
                    if (targetKode.length() > 0 && targetKelas.length() > 0) {
                        // Coba baca dari ratio_cache.txt
                        File cacheFile = SD.open("/ratio_cache.txt", FILE_READ);
                        if (cacheFile) {
                            while (cacheFile.available()) {
                                String line = cacheFile.readStringUntil('\n');
                                line.trim();
                                if (line.length() < 5) continue;

                                int p1 = line.indexOf('|');
                                int p2 = line.indexOf('|', p1 + 1);
                                int p3 = line.indexOf('|', p2 + 1);

                                if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

                                String kode = line.substring(0, p1);
                                String kls = line.substring(p1 + 1, p2);
                                String enr = line.substring(p2 + 1, p3);
                                String tot = line.substring(p3 + 1);

                                kode.trim();
                                kls.trim();

                                if (kode == targetKode && kls == targetKelas) {
                                    classEnrolled[i] = enr.toInt();
                                    classTotal[i] = tot.toInt();
                                    fromCache = true;
                                    Serial.printf_P(PSTR("[ENROLL_RATIO] Cache hit: %s|%s = %d/%d\n"),
                                        kode.c_str(), kls.c_str(), classEnrolled[i], classTotal[i]);
                                    break;
                                }
                            }
                            cacheFile.close();
                        }
                    }

                    // ========== FALLBACK: Hitung dari CSV jika cache miss ==========
                    if (!fromCache) {
                        // Hitung dari kelas_mahasiswa.csv -langsung dari status_fp column
                        File kmFile = SD.open("/kelas_mahasiswa.csv", FILE_READ);
                        if (kmFile) {
                            while (kmFile.available()) {
                                String line = kmFile.readStringUntil('\n');
                                line.trim();
                                if (line.length() < 5) continue;
                                if (line.startsWith("id,") || line.startsWith("id,kode")) continue;

                                // Format: id,kode_kelas,kelas,nim,sit_in,status_fingerprint
                                int p1 = line.indexOf(',');
                                int p2 = line.indexOf(',', p1 + 1);
                                int p3 = line.indexOf(',', p2 + 1);
                                int p4 = line.indexOf(',', p3 + 1);
                                int p5 = line.indexOf(',', p4 + 1);

                                if (p1 > 0 && p2 > 0 && p3 > 0) {
                                    String kodeKelas = line.substring(p1 + 1, p2);
                                    String kelas = line.substring(p2 + 1, p3);
                                    String statusFp = (p5 > 0) ? line.substring(p4 + 1, p5) : line.substring(p4 + 1);

                                    kodeKelas.trim(); kodeKelas.replace("\r", "");
                                    kelas.trim(); kelas.replace("\r", "");
                                    statusFp.trim(); statusFp.replace("\r", "");

                                    // Cocokkan kode_kelas dan kelas
                                    if (kodeKelas == targetKode && (targetKelas == "" || kelas == targetKelas)) {
                                        classTotal[i]++;

                                        // Langsung cek status_fp (true/1 = enrolled)
                                        if (statusFp == "true" || statusFp == "1") {
                                            classEnrolled[i]++;
                                        }
                                    }
                                }
                            }
                            kmFile.close();
                        }
                    }

                    Serial.printf_P(PSTR("[ENROLL_RATIO] Kelas[%d]: %s (%s) - Enrolled: %d/%d\n"),
                        i, targetKode.c_str(), targetKelas.c_str(), classEnrolled[i], classTotal[i]);
                }

                isInitialized = true;
                needsRedraw = true;
            }

            // ========== TUGAS 3: RENDER UI BERDASARKAN MODE ==========
            if (needsRedraw) {
                lcd->clear();

                int totalPages = (classCount + 1) / 2;
                if (totalPages == 0) totalPages = 1;

                if (isInputMode) {
                    // ========== MODE INPUT ==========
                    lcd->printLine(0, "PILIH KELAS ========");
                    lcd->printLine(1, "Pilih: " + tempClassSelect + "_");
                    lcd->printLine(3, "#.Enter D.Batal");
                } else {
                    // ========== MODE VIEW ==========
                    lcd->printLine(0, "PILIH KELAS ========");

                    int startIndex = currentPage * 2;

                    // Cetak Item 1 (Baris 1) dengan rasio
                    if (startIndex < classCount) {
                        lcd->setCursor(0, 1);
                        lcd->print(String(startIndex + 1) + "." + classListKode[startIndex]);
                        // lcd->setCursor(13, 1);
                        // lcd->print("(" + String(classEnrolled[startIndex]) + "/" + String(classTotal[startIndex]) + ")");
                    }

                    // Cetak Item 2 (Baris 2) dengan rasio
                    if (startIndex + 1 < classCount) {
                        lcd->setCursor(0, 2);
                        lcd->print(String(startIndex + 2) + "." + classListKode[startIndex + 1]);
                        // lcd->setCursor(13, 2);
                        // lcd->print("(" + String(classEnrolled[startIndex + 1]) + "/" + String(classTotal[startIndex + 1]) + ")");
                    }

                    // Cetak Navigasi (Baris 3)
                    if (totalPages > 1) {
                        if(currentPage > 0){
                            lcd->printLine(3, "C.Next D.Back");
                        }
                        else{
                            lcd->printLine(3, "C.Next D.Exit");                            
                        }
                    } else {
                        lcd->printLine(3, "D.Exit");
                    }
                }

                needsRedraw = false;
            }

            // ========== TUGAS 3: LOGIKA KEYPAD BERDASARKAN MODE ==========
            char key = keypad->getKey();

            if (key >= '0' && key <= '9') {
                // Angka selalu masuk ke mode input
                isInputMode = true;
                if (tempClassSelect.length() < 2) {
                    tempClassSelect += key;
                    needsRedraw = true;
                }
            } else if (key == '*') {
                if (isInputMode && tempClassSelect.length() > 0) {
                    tempClassSelect.remove(tempClassSelect.length() - 1);
                    if (tempClassSelect.length() == 0) {
                        isInputMode = false;  // Kembali ke view jika kosong
                    }
                    needsRedraw = true;
                }
            } else if (key == 'D') {
                if (isInputMode) {
                    // Jika di mode input, D berfungsi membatalkan input
                    tempClassSelect = "";
                    isInputMode = false;
                    needsRedraw = true;
                } else {
                    // Next Page hanya berlaku di Mode View
                    int totalPages = (classCount + 1) / 2;
                    if (currentPage > 0) {
                        currentPage = (currentPage - 1) % totalPages;
                        needsRedraw = true;
                    }
                    else{
                        // Jika di mode view, D berfungsi keluar fitur
                        isInitialized = false;
                        isInputMode = false;
                        currentPage = 0;
                        tempClassSelect = "";
                        currentState = STATE_MENU_DOSEN;
                        needRender = true;
                        break;
                    }
                }
            } else if (key == 'C' && !isInputMode) {
                // Next Page hanya berlaku di Mode View
                int totalPages = (classCount + 1) / 2;
                if (totalPages > 1) {
                    currentPage = (currentPage + 1) % totalPages;
                    needsRedraw = true;
                }
            } else if (key == '#' && isInputMode) {
                // Proses pemilihan kelas (Mode Input)
                int selectedIndex = tempClassSelect.toInt() - 1;
                if (selectedIndex >= 0 && selectedIndex < classCount) {
                    tempKodeMk = classListKode[selectedIndex];
                    tempKelas = classListKelas[selectedIndex];

                    tempKodeMk.trim();
                    tempKodeMk.replace("\r", "");
                    tempKodeMk.replace("\n", "");

                    tempKelas.trim();
                    tempKelas.replace("\r", "");
                    tempKelas.replace("\n", "");

                    // ========== TUGAS 1: SIMPAN RASIO KE VARIABEL GLOBAL ==========
                    // Agar bisa digunakan di CONFIRM_START tanpa recalculate
                    enrolledCount = classEnrolled[selectedIndex];
                    totalCount = classTotal[selectedIndex];

                    Serial.printf("[DEBUG-KELAS] Dipilih: Kode='%s', Kelas='%s' (Enrolled: %d/%d)\n",
                        tempKodeMk.c_str(), tempKelas.c_str(), enrolledCount, totalCount);

                    logSystemActivity("REG_MHS", "Pilih kelas: " + tempKodeMk + " (" + tempKelas + ")");

                    // Reset untuk下次 masuk
                    isInitialized = false;
                    isInputMode = false;
                    tempClassSelect = "";
                    currentPage = 0;

                    currentState = STATE_REG_MHS_CONFIRM_START;
                    needRender = true;
                    break;
                } else {
                    // Pilihan tidak valid
                    lcd->clear();
                    lcd->printLine(0, "Pilihan Tidak");
                    lcd->printLine(1, "Valid!");
                    delay(1000);
                    tempClassSelect = "";
                    isInputMode = false;
                    needsRedraw = true;
                }
            } else if (key == '#' && !isInputMode) {
                // Di mode view, # tidak melakukan apa-apa (harus pilih nomor dulu)
            }
            // Legacy support: navigasi angka langsung (1-9 tanpa input) - masuk ke mode view langsung
            else if (key >= '1' && key <= '9' && !isInputMode) {
                int selection = key - '0';
                if (selection > 0 && selection <= classCount) {
                    int idx = selection - 1;
                    tempKodeMk = classListKode[idx];
                    tempKelas = classListKelas[idx];

                    tempKodeMk.trim();
                    tempKodeMk.replace("\r", "");
                    tempKodeMk.replace("\n", "");

                    tempKelas.trim();
                    tempKelas.replace("\r", "");
                    tempKelas.replace("\n", "");

                    // ========== TUGAS 1: SIMPAN RASIO KE VARIABEL GLOBAL ==========
                    enrolledCount = classEnrolled[idx];
                    totalCount = classTotal[idx];

                    Serial.printf("[DEBUG-KELAS] Dipilih: Kode='%s', Kelas='%s' (Enrolled: %d/%d)\n",
                        tempKodeMk.c_str(), tempKelas.c_str(), enrolledCount, totalCount);

                    logSystemActivity("REG_MHS", "Pilih kelas: " + tempKodeMk + " (" + tempKelas + ")");

                    isInitialized = false;
                    isInputMode = false;
                    currentPage = 0;

                    currentState = STATE_REG_MHS_CONFIRM_START;
                    needRender = true;
                    break;
                }
            }
        }
        break;

        // ========== TUGAS 2: KONFIRMASI AWAL (STATE_REG_MHS_CONFIRM_START) ==========
        case STATE_REG_MHS_CONFIRM_START:
        {
            static String tempPIN = "";
            static bool authAttempted = false;

            if (needRender) {
                tempPIN = "";
                authAttempted = false;

                // ========== DPK: Tarik data mahasiswa kelas ini SEBELUM layar registrasi ==========
                lcd->clear();
                lcd->printLine(0, "Memuat Data...");
                lcd->printLine(1, tempKodeMk + "-" + tempKelas);
                if (apiManager.fetchDataMahasiswa(tempKodeMk, tempKelas.toInt())) {
                    // Refresh rasio dari cache yang baru dihitung agar tampilan akurat
                    int en = 0, tot = 0;
                    if (apiManager.getEnrollRatio(tempKodeMk.c_str(), tempKelas.c_str(), en, tot)) {
                        enrolledCount = en;
                        totalCount = tot;
                    }
                } else {
                    Serial.println(F("[REG_MHS] DPK gagal - lanjut dengan data lokal"));
                }

                // ========== TUGAS 1: ENROLLMENT RATIO SUDAH DI-PRE-CALCULATE DI PILIH_KELAS ==========
                // Nilai enrolledCount dan totalCount sudah diset saat pemilihan kelas
                Serial.printf("[ENROLL_RATIO] Kelas: %s (%s) - Enrolled: %d/%d\n",
                    tempKodeMk.c_str(), tempKelas.c_str(), enrolledCount, totalCount);

                // Tampilkan dengan rasio
                lcd->clear();
                lcd->printLine(0, tempKodeMk + " (" + String(enrolledCount) + "/" + String(totalCount) + ")");
                lcd->printLine(1, "PIN/Fingerprint:");
                lcd->printLine(2, "_");
                lcd->printLine(3, "#.Lanjut D.Batal");
                lcd->printWiFiStatus();
                needRender = false;
            }

            // ========== TUGAS 3: FIX FINGERPRINT AUTH & LED FEEDBACK ==========
            int detectedFpId = fingerprintManager.checkFingerprint();
            if (detectedFpId > 0) {
                // ========== DEBUG: PRINT FINGERPINT MAP ==========
                printFingerMap();

                // Gunakan fingerMap untuk O(1) lookup userId
                String matchedUserID = fingerMap[detectedFpId];
                matchedUserID.trim();

                Serial.printf("[DEBUG-FP-AUTH] Slot: %d, Matched UserID: '%s' vs Current User: '%s'\n",
                    detectedFpId, matchedUserID.c_str(), tempNIP.c_str());

                if (matchedUserID == tempNIP) {
                    // MATCH BERHASIL - Jari milik dosen ini
                    fingerprintManager.setLEDSuccess();
                    delay(500);
                    fingerprintManager.setLEDOff();

                    logSystemActivity("REG_MHS", "Mulai (FP) kelas: " + tempKodeMk);
                    currentState = STATE_REG_MHS_INPUT_NIM;
                    tempNIM = "";
                    tempPIN = "";
                    needRender = true;
                    break;
                } else {
                    // JARI BUKAN MILIK DOSEN INI - Akses Ditolak
                    fingerprintManager.setLEDError();
                    delay(500);
                    fingerprintManager.setLEDOff();

                    // Tampilkan pesan akses ditolak
                    lcd->clear();
                    lcd->printLine(0, MSG_AKSES_DITOLAK);
                    lcd->printLine(1, "Jari tidak cocok");
                    lcd->printLine(2, "dengan akun Anda");
                    delay(1500);
                    needRender = true;
                }
            }

            char key = keypad->getKey();

            // TUGAS 1: Only process numeric keys for PIN input
            if (key >= '0' && key <= '9') {
                if (tempPIN.length() < 6) {
                    tempPIN += key;
                    lcd->setCursor(0, 2);
                    String stars = "";
                    for (int i = 0; i < tempPIN.length(); i++) stars += "*";
                    lcd->print(stars + "_          ");
                }
            }
            // TUGAS 1: Backspace only with *
            else if (key == '*' && tempPIN.length() > 0) {
                tempPIN.remove(tempPIN.length() - 1);
                lcd->setCursor(0, 2);
                String stars = "";
                for (int i = 0; i < tempPIN.length(); i++) stars += "*";
                lcd->print(stars + "_          ");
            }
            // TUGAS 1: Submit with # only if PIN is entered
            else if (key == '#' && tempPIN.length() > 0) {
                // TUGAS 1: Aggressive PIN cleaning - buat string baru untuk keamanan
                String inputPin = String(tempPIN);
                inputPin.replace("\r", "");
                inputPin.replace("\n", "");
                inputPin.trim();

                Serial.printf("[DEBUG-PIN-CEK] Mencari user dengan NIP: '%s'\n", tempNIP.c_str());

                // Cek PIN dari users.csv (Format: id,nip,pin,role,fingerprint_id)
                bool pinValid = false;
                String realPin = "";
                File file = SD.open("/users.csv", FILE_READ);
                if (file) {
                    while (file.available()) {
                        String line = file.readStringUntil('\n');
                        line.trim();
                        if (line.length() < 5) continue;
                        if (line.startsWith("id,") || line.startsWith("id,nip")) continue;

                        // Format: id(0), nip(1), pin(2), role(3), fingerprint_id(4)
                        int p1 = line.indexOf(',');  // after id
                        int p2 = line.indexOf(',', p1 + 1);  // after nip
                        int p3 = line.indexOf(',', p2 + 1);  // after pin

                        if (p1 > 0 && p2 > 0) {
                            // idx 1 = nip
                            String csvNip = line.substring(p1 + 1, p2);
                            csvNip.trim();
                            csvNip.replace("\r", "");
                            csvNip.replace("\n", "");

                            Serial.printf("[DEBUG-PIN-CEK] CSV row - NIP: '%s'\n", csvNip.c_str());

                            // idx 2 = pin
                            String pin = line.substring(p2 + 1, p3);
                            pin.trim();
                            pin.replace("\r", "");
                            pin.replace("\n", "");

                            if (csvNip == tempNIP) {
                                realPin = pin;
                                Serial.printf("[DEBUG-PIN-CEK] KETEMU! Input: '%s' | Real: '%s'\n", inputPin.c_str(), realPin.c_str());

                                // TUGAS 1: Verifikasi PIN dengan panjang > 0
                                if (inputPin == realPin && realPin.length() > 0) {
                                    pinValid = true;
                                    Serial.printf("[DEBUG-PIN] MATCH! PIN benar\n");
                                }
                                break;
                            }
                        }
                    }
                    file.close();
                }

                if (!pinValid) {
                    Serial.printf("[DEBUG-PIN-CEK] GAGAL - Input: '%s' | Real: '%s' | Length: %d\n",
                        inputPin.c_str(), realPin.c_str(), realPin.length());
                }

                if (pinValid) {
                    logSystemActivity("REG_MHS", "Mulai (PIN) kelas: " + tempKodeMk);
                    currentState = STATE_REG_MHS_INPUT_NIM;
                    tempNIM = "";
                    tempPIN = "";
                    needRender = true;
                    break;
                } else {
                    // PIN salah
                    lcd->clear();
                    lcd->printLine(0, "PIN SALAH!");
                    lcd->printLine(1, "Coba lagi...");
                    delay(1500);
                    tempPIN = "";
                    needRender = true;
                    break;
                }
            }
            // TUGAS 1: Cancel with D - KEMBALI KE PILIH KELAS
            else if (key == 'D') {
                tempPIN = "";
                // ========== TUGAS 2: FORCE REFRESH RATIO ==========
                // needRender=true akan mengeset isInitialized=false di blok if(needRender)
                // sehingga rasio kelas akan di-recalculate dari data terbaru
                currentState = STATE_REG_MHS_PILIH_KELAS;
                needRender = true;
                break;
            }
        }
        break;

        // ========== TUGAS 3: INPUT NIM (STATE_REG_MHS_INPUT_NIM) ==========
        case STATE_REG_MHS_INPUT_NIM:
        {
            static String nimBuffer = "";

            if (needRender) {
                nimBuffer = "";
                lcd->clear();
                // ========== TUGAS 1: TAMPILKAN RASIO DI LCD ==========
                lcd->printLine(0, "NIM Mahasiswa:");
                lcd->printLine(1, "_");
                lcd->printLine(2, tempKodeMk + " (" + String(enrolledCount) + "/" + String(totalCount) + ")");
                lcd->printLine(3, "#.Enter D.Selesai");
                lcd->printWiFiStatus();
                needRender = false;
            }

            char key = keypad->getKey();

            // TUGAS 1: Input angka (max 15 karakter)
            if (key >= '0' && key <= '9') {
                if (nimBuffer.length() < 15) {
                    nimBuffer += key;
                    lcd->setCursor(0, 1);
                    // TUGAS 1: Padding spasi untuk避免 overflow
                    String paddedNIM = nimBuffer + "_";
                    while (paddedNIM.length() < 20) paddedNIM += " ";
                    lcd->print(paddedNIM);
                }
            }
            // TUGAS 3: Backspace (*)
            else if (key == '*' && nimBuffer.length() > 0) {
                nimBuffer.remove(nimBuffer.length() - 1);
                lcd->setCursor(0, 1);
                // TUGAS 1: Padding spasi untuk avoid overflow
                String paddedNIM = nimBuffer + "_";
                while (paddedNIM.length() < 20) paddedNIM += " ";
                lcd->print(paddedNIM);
            }
            // Submit (#)
            else if (key == '#' && nimBuffer.length() > 0) {
                // ========== TUGAS 4: VALIDASI NIM 8 DIGIT ==========
                if (nimBuffer.length() != 8) {
                    lcd->clear();
                    lcd->printLine(0, "NIM TIDAK VALID");
                    lcd->printLine(1, "Harus 8 Digit!");
                    delay(1500);
                    nimBuffer = "";
                    // Redraw layar input NIM
                    lcd->clear();
                    lcd->printLine(0, "NIM Mahasiswa:");
                    lcd->printLine(1, "_");
                    lcd->printLine(2, tempKodeMk + " (" + String(enrolledCount) + "/" + String(totalCount) + ")");
                    lcd->printLine(3,"#.Enter " + String(MSG_SELESAI));
                    lcd->printWiFiStatus();
                    break;
                }

                tempNIM = nimBuffer;
                nimBuffer = "";

                // ========== TUGAS 2 & 3: CEK NIM DI KELAS_MAHASISWA.CSV ==========
                // TUGAS 3: DEBUG LOGGING
                Serial.printf("[DEBUG-NIM] Target NIM: '%s' (Len:%d)\n", tempNIM.c_str(), tempNIM.length());
                Serial.printf("[DEBUG-NIM] Target KodeKelas: '%s', Kelas: '%s'\n", tempKodeMk.c_str(), tempKelas.c_str());

                // TUGAS 2: Aggressive trimming pada tempNIM
                String targetNIM = tempNIM;
                targetNIM.trim();
                targetNIM.replace("\r", "");
                targetNIM.replace("\n", "");
                targetNIM.replace(" ", "");

                String targetKodeMk = tempKodeMk;
                targetKodeMk.trim();
                targetKodeMk.replace("\r", "");

                String targetKelas = tempKelas;
                targetKelas.trim();
                targetKelas.replace("\r", "");

                Serial.printf("[DEBUG-NIM] After trim - NIM: '%s' (Len:%d), Kode: '%s', Kelas: '%s'\n",
                    targetNIM.c_str(), targetNIM.length(), targetKodeMk.c_str(), targetKelas.c_str());

                bool nimExists = false;
                File file = SD.open("/kelas_mahasiswa.csv", FILE_READ);
                if (file) {
                    while (file.available()) {
                        String line = file.readStringUntil('\n');
                        line.trim();
                        if (line.length() < 5) continue;
                        if (line.startsWith("id,") || line.startsWith("id,kode")) continue;

                        // TUGAS 2: Format: id,kode_kelas,kelas,nim (idx: 0,1,2,3)
                        int p1 = line.indexOf(',');  // id (idx 0)
                        int p2 = line.indexOf(',', p1 + 1);  // kode_kelas (idx 1)
                        int p3 = line.indexOf(',', p2 + 1);  // kelas (idx 2)
                        int p4 = line.indexOf(',', p3 + 1);  // nim (idx 3)

                        if (p1 > 0 && p2 > 0 && p3 > 0) {
                            String kodeMk = line.substring(p1 + 1, p2);  // idx 1
                            String kls = line.substring(p2 + 1, p3);     // idx 2
                            String nim = (p4 > 0) ? line.substring(p3 + 1, p4) : line.substring(p3 + 1);  // idx 3

                            // TUGAS 2: Aggressive trimming - hapus \r, \n, spasi
                            kodeMk.trim();
                            kodeMk.replace("\r", "");
                            kodeMk.replace("\n", "");

                            kls.trim();
                            kls.replace("\r", "");
                            kls.replace("\n", "");

                            nim.trim();
                            nim.replace("\r", "");
                            nim.replace("\n", "");
                            nim.replace(" ", "");

                            // TUGAS 2: DEBUG LOGGING - Tampilkan SEMUA variabel termasuk KELAS
                            Serial.printf("[DEBUG-NIM] CSV Row - Kode: '%s', Kelas: '%s', NIM: '%s'\n",
                                kodeMk.c_str(), kls.c_str(), nim.c_str());

                            Serial.printf("[DEBUG-NIM] Comparing - CSV(Kode:'%s',Kelas:'%s',NIM:'%s') vs Target(Kode:'%s',Kelas:'%s',NIM:'%s')\n",
                                kodeMk.c_str(), kls.c_str(), nim.c_str(),
                                targetKodeMk.c_str(), targetKelas.c_str(), targetNIM.c_str());

                            // TUGAS 1: Toleransi komparasi kelas - jika targetKelas kosong, cukup cocokkan Kode dan NIM
                            if (kodeMk == targetKodeMk && nim == targetNIM && (targetKelas == "" || kls == targetKelas)) {
                                Serial.printf("[DEBUG-NIM] MATCH FOUND! NIM: %s\n", nim.c_str());
                                nimExists = true;
                                break;
                            }
                        }
                    }
                    file.close();
                }

                if (nimExists) {
                    // NIM sudah terdaftar - lanjut ke enrollment
                    tempIsSitIn = false;
                    logSystemActivity("REG_MHS", "NIM terdaftar: " + tempNIM);
                    currentState = STATE_REG_MHS_ENROLL;
                    enrolling = false;
                    needRender = true;
                } else {
                    // NIM tidak terdaftar - tanya Sit-In
                    logSystemActivity("REG_MHS", "NIM tidak terdaftar: " + tempNIM + " - Tanya Sit-In");
                    currentState = STATE_REG_MHS_SIT_IN_CONFIRM;
                    needRender = true;
                }
                break;
            }
            // Selesai (D)
            else if (key == 'D') {
                currentState = STATE_REG_MHS_SUMMARY;
                needRender = true;
                break;
            }
        }
        break;

        // ========== TUGAS 3: KONFIRMASI SIT-IN (STATE_REG_MHS_SIT_IN_CONFIRM) ==========
        case STATE_REG_MHS_SIT_IN_CONFIRM:
        {
            if (needRender) {
                lcd->clear();
                lcd->printLine(0, "NIM TDK TERDAFTAR");
                lcd->setCursor(0, 1);
                lcd->print("NIM: " + tempNIM);
                lcd->printLine(2, "Mhs Sit In?");
                lcd->printLine(3, "A:Ya  B:Tidak");
                needRender = false;
            }

            char key = keypad->getKey();

            if (key == 'A') {
                // ========== TUGAS 2: TAMBAHKAN NIM SIT-IN KE KELAS_MAHASISWA.CSV ==========
                tempIsSitIn = true;

                // Generate ID baru
                int nextId = apiManager.getNextAutoId("/kelas_mahasiswa.csv");

                // TUGAS 2: Debug log untuk memastikan nilai terisi
                Serial.printf("[DEBUG-SITIN] kelas: Kode='%s', Kelas='%s', NIM='%s'\n",
                    tempKodeMk.c_str(), tempKelas.c_str(), tempNIM.c_str());

                // ========== TUGAS 1: UPSERT LOGIC (Update or Insert) ==========
                bool isUpdated = false;
                String targetKodeKelas = tempKodeMk;
                String targetKelas = tempKelas;

                File file = SD.open("/kelas_mahasiswa.csv", FILE_READ);
                File tempFile = SD.open("/temp_kelas.csv", FILE_WRITE);

                if (file && tempFile) {
                    // Copy header if exists
                    if (file.available()) {
                        String header = file.readStringUntil('\n');
                        header.trim();
                        tempFile.print(header + "\n");
                    }

                    // Process existing records
                    while (file.available()) {
                        String line = file.readStringUntil('\n');
                        line.trim();
                        if (line.length() == 0) continue;

                        // CSV format: id(0), kode_kelas(1), kelas(2), nim(3), sit_in(4)
                        String cId = apiManager.getCsvColumn(line, 0);
                        String cKode = apiManager.getCsvColumn(line, 1);
                        String cKelas = apiManager.getCsvColumn(line, 2);
                        String cNim = apiManager.getCsvColumn(line, 3);

                        // Jika Kode Matkul dan NIM sudah ada, UPDATE baris ini!
                        if (cKode == targetKodeKelas && cNim == tempNIM) {
                            tempFile.printf("%s,%s,%s,%s,true\n", cId.c_str(), targetKodeKelas.c_str(), targetKelas.c_str(), tempNIM.c_str());
                            isUpdated = true;
                            Serial.printf("[REG_SITIN] Updated existing record: NIM %s di kelas %s\n", tempNIM.c_str(), targetKodeKelas.c_str());
                        } else {
                            tempFile.print(line + "\n"); // Write existing line as-is
                        }
                    }

                    // Jika belum ada di CSV, INSERT sebagai baris baru
                    if (!isUpdated) {
                        tempFile.printf("%d,%s,%s,%s,true\n", nextId, targetKodeKelas.c_str(), targetKelas.c_str(), tempNIM.c_str());
                        Serial.printf("[REG_SITIN] Inserted new record: NIM %s ke kelas %s\n", tempNIM.c_str(), targetKodeKelas.c_str());
                    }

                    file.close();
                    tempFile.close();
                    SD.remove("/kelas_mahasiswa.csv");
                    SD.rename("/temp_kelas.csv", "/kelas_mahasiswa.csv");

                    logSystemActivity("REG_MHS_SITIN", "NIM: " + tempNIM + " - Upsert ke kelas " + targetKodeKelas + " (" + targetKelas + ")");

                    // ========== TAMBAHKAN KE PUSH QUEUE ==========
                    // Setelah mahasiswa Sit-In berhasil didaftarkan
                    // Format: kode_kelas|kelas|nim
                    String sitInRef = targetKodeKelas + "|" + targetKelas + "|" + tempNIM;
                    // TUGAS 1: Sertakan creator_id (current user NIP)
                    addToPushQueue("SIT_IN", sitInRef, tempNIP);
                } else {
                    Serial.println("[REG_SITIN] GAGAL membuka file untuk UPSERT!");
                }

                currentState = STATE_REG_MHS_ENROLL;
                enrolling = false;
                needRender = true;
                break;
            }
            else if (key == 'B') {
                currentState = STATE_REG_MHS_INPUT_NIM;
                tempNIM = "";
                needRender = true;
                break;
            }
        }
        break;

        // ========== TUGAS 4: ENROLLMENT FINGERPRINT (STATE_REG_MHS_ENROLL) ==========
        case STATE_REG_MHS_ENROLL:
        {
            // ========== ENROLLMENT 2-JARI (Jempol Kanan -> Jempol Kiri) ==========
            // Per-jari: phase 1=scan1, 2=wait_lift, 3=scan2, 4=process+extract.
            // currentFinger: 1 = Jempol Kanan (-> tempHexJari1), 2 = Jempol Kiri (-> hexJari2 lokal).
            // Setelah 2 jari OK -> rakit JSON + POST /api/device/enroll-mahasiswa,
            // lalu HAPUS kedua slot R503 sementara. Data resmi akan datang via PULL DPK.
            static int currentFinger = 1;
            static int enrollPhase = 0;
            static bool started = false;

            if (needRender) {
                currentFinger = 1;
                enrollPhase = 0;
                started = false;
                tempHexJari1 = "";
                lcd->clear();
                lcd->printLine(0, F("REGISTRASI 2 JARI"));
                lcd->printLine(1, "NIM: " + tempNIM);
                lcd->printLine(2, F("Tahap 1: KANAN"));
                lcd->printLine(3, F("#.Mulai  D.Batal"));
                needRender = false;
            }

            char key = keypad->getKey();

            if (key == '#' && !started) {
                started = true;
                enrollPhase = 1;
            }
            else if (key == 'D' && enrollPhase != 4) {
                // Batal -> bersihkan slot temp R503 (jaga-jaga)
                Adafruit_Fingerprint_ESP32* fg = fingerprintManager.getFinger();
                fg->delete_model(ENROLL_TEMP_SLOT_1);
                fg->delete_model(ENROLL_TEMP_SLOT_2);
                tempHexJari1 = "";
                currentFinger = 1;
                enrollPhase = 0;
                started = false;
                currentState = STATE_REG_MHS_INPUT_NIM;
                needRender = true;
                break;
            }

            if (!started || enrollPhase == 0) break;

            Adafruit_Fingerprint_ESP32* finger = fingerprintManager.getFinger();
            const __FlashStringHelper* sideLabel = (currentFinger == 1)
                ? F("JEMPOL KANAN") : F("JEMPOL KIRI");
            int targetSlot = (currentFinger == 1) ? ENROLL_TEMP_SLOT_1 : ENROLL_TEMP_SLOT_2;

            // --- FASE SCAN 1 ---
            if (enrollPhase == 1) {
                lcd->clear();
                lcd->printLine(0, sideLabel);
                lcd->printLine(1, F("Tempel jari (1)"));
                lcd->printLine(2, "NIM: " + tempNIM);
                while (finger->get_image() != FP_OK) {
                    delay(50);
                    if (keypad->getKey() == 'D') { started = false; enrollPhase = 0; break; }
                }
                if (!started) { currentState = STATE_REG_MHS_INPUT_NIM; needRender = true; break; }

                lcd->printLine(2, F("Membaca..."));
                if (finger->image_2_tz(1) != FP_OK) enrollPhase = 6;
                else { fingerprintManager.setLEDScanning(); enrollPhase = 2; }
            }

            // --- FASE TUNGGU ANGKAT JARI ---
            if (enrollPhase == 2) {
                lcd->clear();
                lcd->printLine(0, sideLabel);
                lcd->printLine(1, F("Angkat Jari!"));
                while (finger->get_image() != FP_NOFINGER) {
                    delay(50);
                    if (keypad->getKey() == 'D') { started = false; enrollPhase = 0; break; }
                }
                if (!started) { currentState = STATE_REG_MHS_INPUT_NIM; needRender = true; break; }
                delay(500);
                enrollPhase = 3;
            }

            // --- FASE SCAN 2 (jari yg SAMA, untuk averaging template) ---
            if (enrollPhase == 3) {
                lcd->clear();
                lcd->printLine(0, sideLabel);
                lcd->printLine(1, F("Tempel lagi (2)"));
                while (finger->get_image() != FP_OK) {
                    delay(50);
                    if (keypad->getKey() == 'D') { started = false; enrollPhase = 0; break; }
                }
                if (!started) { currentState = STATE_REG_MHS_INPUT_NIM; needRender = true; break; }

                lcd->printLine(2, F("Membaca..."));
                if (finger->image_2_tz(2) != FP_OK) enrollPhase = 6;
                else { fingerprintManager.setLEDScanning(); enrollPhase = 4; }
            }

            // --- FASE PROSES + EKSTRAK HEX (per jari) ---
            if (enrollPhase == 4) {
                lcd->clear();
                lcd->printLine(0, sideLabel);
                lcd->printLine(1, F("Memproses..."));

                if (finger->create_model() != FP_OK) {
                    Serial.println(F("[ENROLL_MHS] create_model GAGAL"));
                    enrollPhase = 6;
                }
                else if (finger->store_model(targetSlot) != FP_OK) {
                    Serial.printf_P(PSTR("[ENROLL_MHS] store_model GAGAL slot %d\n"), targetSlot);
                    enrollPhase = 6;
                }
                else {
                    String hex = fingerprintManager.extractTemplateAsHex(targetSlot);
                    Serial.printf_P(PSTR("[ENROLL_MHS] Jari %d hex len=%d\n"), currentFinger, hex.length());

                    if (hex.length() < 100) {
                        finger->delete_model(targetSlot);
                        enrollPhase = 6;
                    }
                    else if (currentFinger == 1) {
                        // ----- Jari 1 (KANAN) selesai -> simpan tempHexJari1, lanjut Jari 2 -----
                        tempHexJari1 = trimHex(hex);
                        fingerprintManager.setLEDSuccess();
                        lcd->clear();
                        lcd->printLine(0, F("JARI 1 OK!"));
                        lcd->printLine(1, F("Tahap 2: KIRI"));
                        lcd->printLine(2, F("Tempel Jempol Kiri"));
                        delay(1800);
                        fingerprintManager.setLEDOff();
                        currentFinger = 2;
                        enrollPhase = 1;     // ulang siklus untuk jari ke-2
                    }
                    else {
                        // ----- Jari 2 (KIRI) selesai -> SIMPAN KE QUEUE (pending_push.csv) -----
                        String hexJari2 = trimHex(hex);

                        lcd->clear();
                        lcd->printLine(0, F("Menyimpan ke Queue..."));
                        lcd->printLine(1, "NIM: " + tempNIM);
                        lcd->printLine(2, F("Akan di-push later"));

                        // Format: mahasiswa,NIM,fp_1_mhsw,fp_2_mhsw
                        bool ok = addMahasiswaToQueue(tempNIM, tempHexJari1, hexJari2);

                        // ----- Bersihkan 2 slot R503 sementara -----
                        // Data RESMI dosen/mhsw akan diinjeksi ulang lewat PULL_FP/INJECT_SENSOR
                        // pada sesi presensi berikutnya (single source of truth: server).
                        finger->delete_model(ENROLL_TEMP_SLOT_1);
                        finger->delete_model(ENROLL_TEMP_SLOT_2);

                        lcd->clear();
                        if (ok) {
                            fingerprintManager.setLEDSuccess();
                            lcd->printLine(0, F("ANTREAN DITAMBAH"));
                            lcd->printLine(1, "NIM: " + tempNIM);
                            lcd->printLine(2, F("Tunggu push batch"));
                            logSystemActivity("ENROLL_MHS_2FP_QUEUED", "OK: " + tempNIM);
                            regSessionCount++;
                            if (tempIsSitIn) regSessionSitInCount++;
                        } else {
                            fingerprintManager.setLEDError();
                            lcd->printLine(0, F("QUEUE GAGAL"));
                            lcd->printLine(1, F("Cek SD Card/Ulangi"));
                            lcd->printLine(2, "NIM: " + tempNIM);
                            logSystemActivity("ENROLL_MHS_2FP_Q_ERR", tempNIM);
                        }
                        delay(2500);
                        fingerprintManager.setLEDOff();

                        // Reset state -> kembali ke input NIM
                        tempHexJari1 = "";
                        currentFinger = 1;
                        enrollPhase = 0;
                        started = false;
                        currentState = STATE_REG_MHS_INPUT_NIM;
                        tempNIM = "";
                        needRender = true;
                    }
                }
            }

            // --- FASE GAGAL: tetap di state, ulangi dari awal (siklus jari yg sama) ---
            if (enrollPhase == 6) {
                fingerprintManager.setLEDError();
                lcd->clear();
                lcd->printLine(0, F("GAGAL"));
                lcd->printLine(1, sideLabel);
                lcd->printLine(2, F("Ulangi tempel"));
                delay(2000);
                fingerprintManager.setLEDOff();
                // Reset siklus jari yang sama (currentFinger tidak berubah)
                enrollPhase = 1;
            }
        }
        break;

        // ========== TUGAS 5: SUMMARY (STATE_REG_MHS_SUMMARY) ==========
        case STATE_REG_MHS_SUMMARY:
        {
            static String tempPIN = "";

            if (needRender) {
                tempPIN = "";
                lcd->clear();
                lcd->printLine(0, "Tot: " + String(regSessionCount) + " SitIn: " + String(regSessionSitInCount));
                lcd->printLine(1, "PIN/Fingerprint:");
                lcd->printLine(2, "_");
                lcd->printLine(3, "#.Enter D.Batal");
                lcd->printWiFiStatus();
                needRender = false;
            }

            // ========== TUGAS 3: FIX FINGERPRINT AUTH & LED FEEDBACK ==========
            int detectedFpId = fingerprintManager.checkFingerprint();
            if (detectedFpId > 0) {
                // ========== DEBUG: PRINT FINGERPINT MAP ==========
                printFingerMap();

                // Gunakan fingerMap untuk O(1) lookup userId
                String matchedUserID = fingerMap[detectedFpId];
                matchedUserID.trim();

                Serial.printf("[DEBUG-FP-AUTH-SUMMARY] Slot: %d, Matched UserID: '%s' vs Current User: '%s'\n",
                    detectedFpId, matchedUserID.c_str(), tempNIP.c_str());

                if (matchedUserID == tempNIP) {
                    // MATCH BERHASIL - Jari milik dosen ini
                    fingerprintManager.setLEDSuccess();
                    delay(500);
                    fingerprintManager.setLEDOff();

                    logSystemActivity("REG_MHS", "Selesai (FP). Total: " + String(regSessionCount) + ", SitIn: " + String(regSessionSitInCount));

                    // ========== TUGAS 4: PROSES PUSH QUEUE ==========
                    // Proses antrian push sebelum kembali ke menu
                    // TUGAS 3: Proses hanya antrian miliknya sendiri
                    processPushQueue(tempNIP);

                    lcd->clear();
                    lcd->printLine(0, "Registrasi Selesai!");
                    // lcd->printLine(1, "Siap di-push");
                    delay(2000);

                    // Reset variabel sesi
                    tempKodeMk = "";
                    tempKelas = "";
                    tempNIM = "";
                    tempIsSitIn = false;
                    tempFingerId = 0;
                    regSessionCount = 0;
                    regSessionSitInCount = 0;
                    tempPIN = "";

                    currentState = STATE_MENU_DOSEN;
                    needRender = true;
                    break;
                } else {
                    // JARI BUKAN MILIK DOSEN INI - Akses Ditolak
                    fingerprintManager.setLEDError();
                    delay(500);
                    fingerprintManager.setLEDOff();

                    // Tampilkan pesan akses ditolak
                    lcd->clear();
                    lcd->printLine(0, MSG_AKSES_DITOLAK);
                    lcd->printLine(1, "Jari tidak cocok");
                    lcd->printLine(2, "dengan akun Anda");
                    delay(1500);
                    needRender = true;
                }
            }

            char key = keypad->getKey();

            // TUGAS 1: Only process numeric keys for PIN input
            if (key >= '0' && key <= '9') {
                if (tempPIN.length() < 6) {
                    tempPIN += key;
                    lcd->setCursor(0, 2);
                    String stars = "";
                    for (int i = 0; i < tempPIN.length(); i++) stars += "*";
                    lcd->print(stars + "_          ");
                }
            }
            // TUGAS 1: Backspace only with *
            else if (key == '*' && tempPIN.length() > 0) {
                tempPIN.remove(tempPIN.length() - 1);
                lcd->setCursor(0, 2);
                String stars = "";
                for (int i = 0; i < tempPIN.length(); i++) stars += "*";
                lcd->print(stars + "_          ");
            }
            // TUGAS 1: Push with # only if PIN is entered
            else if (key == '#' && tempPIN.length() > 0) {
                // TUGAS 1: Aggressive PIN cleaning - buat string baru untuk keamanan
                String inputPin = String(tempPIN);
                inputPin.replace("\r", "");
                inputPin.replace("\n", "");
                inputPin.trim();

                Serial.printf("[DEBUG-PIN-CEK] Mencari user dengan NIP: '%s'\n", tempNIP.c_str());

                // Cek PIN dari users.csv (Format: id,nip,pin,role,fingerprint_id)
                bool pinValid = false;
                String realPin = "";
                File file = SD.open("/users.csv", FILE_READ);
                if (file) {
                    while (file.available()) {
                        String line = file.readStringUntil('\n');
                        line.trim();
                        if (line.length() < 5) continue;
                        if (line.startsWith("id,") || line.startsWith("id,nip")) continue;

                        // Format: id(0), nip(1), pin(2), role(3), fingerprint_id(4)
                        int p1 = line.indexOf(',');  // after id
                        int p2 = line.indexOf(',', p1 + 1);  // after nip
                        int p3 = line.indexOf(',', p2 + 1);  // after pin

                        if (p1 > 0 && p2 > 0) {
                            // idx 1 = nip
                            String csvNip = line.substring(p1 + 1, p2);
                            csvNip.trim();
                            csvNip.replace("\r", "");
                            csvNip.replace("\n", "");

                            // idx 2 = pin
                            String pin = line.substring(p2 + 1, p3);
                            pin.trim();
                            pin.replace("\r", "");
                            pin.replace("\n", "");

                            if (csvNip == tempNIP) {
                                realPin = pin;
                                Serial.printf("[DEBUG-PIN-CEK] KETEMU! Input: '%s' | Real: '%s'\n", inputPin.c_str(), realPin.c_str());

                                // TUGAS 1: Verifikasi PIN dengan panjang > 0
                                if (inputPin == realPin && realPin.length() > 0) {
                                    pinValid = true;
                                    Serial.printf("[DEBUG-PIN-SUMMARY] MATCH!\n");
                                }
                                break;
                            }
                        }
                    }
                    file.close();
                }

                if (pinValid) {
                    // ========== TUGAS 4 & 5: API PLACEHOLDER ==========
                    logSystemActivity("REG_MHS", "Selesai (PIN). Total: " + String(regSessionCount) + ", SitIn: " + String(regSessionSitInCount));
                    logSystemActivity("API", "Placeholder: Data registrasi siap di-push");

                    // ========== TUGAS 4: PROSES PUSH QUEUE ==========
                    // Proses antrian push sebelum kembali ke menu
                    // TUGAS 3: Proses hanya antrian miliknya sendiri
                    processPushQueue(tempNIP);

                    lcd->clear();
                    lcd->printLine(0, "Registrasi Selesai!");
                    // lcd->printLine(1, "Siap di-push");
                    delay(2000);

                    // Reset variabel sesi
                    tempKodeMk = "";
                    tempKelas = "";
                    tempNIM = "";
                    tempIsSitIn = false;
                    tempFingerId = 0;
                    regSessionCount = 0;
                    regSessionSitInCount = 0;
                    tempPIN = "";

                    currentState = STATE_MENU_DOSEN;
                    needRender = true;
                    break;
                }

                if (!pinValid) {
                    Serial.printf("[DEBUG-PIN-CEK] GAGAL - Input: '%s' | Real: '%s' | Length: %d\n",
                        inputPin.c_str(), realPin.c_str(), realPin.length());
                } else {
                    // PIN salah
                    lcd->clear();
                    lcd->printLine(0, "PIN SALAH!");
                    lcd->printLine(1, "Coba lagi...");
                    delay(1500);
                    tempPIN = "";
                    needRender = true;
                    break;
                }
            }
            else if (key == 'D') {
                tempPIN = "";
                currentState = STATE_REG_MHS_INPUT_NIM;
                needRender = true;
                break;
            }
        }
        break;

        // ================= SELECT KELAS =================
        case STATE_SELECT_KELAS:
        {
            static bool firstEntry = true;
            static String selectBuffer = "";
            static bool isSelecting = false;
            if (firstEntry) {
                viewIndex = 0;
                selectBuffer = "";
                isSelecting = false;
                firstEntry = false;
            }

            if (needRender) {
                std::vector<String> list;
                if (currentRole == ROLE_ADMIN) {
                    list = kelasManager.getAllKelas();
                } else {
                    list = kelasManager.getKelasByDosen(tempNIP);
                }

                lcd->clear();
                lcd->printLine(0, "=== PILIH KELAS ===");

                if (isSelecting) {
                    lcd->printLine(1, "Pilih: " + selectBuffer + "_");
                    // lcd->printLine(2, "");
                } else {
                    for (int i = 0; i < 2; i++) {
                        int idx = viewIndex + i;
                        if (idx < list.size()) {
                            lcd->printLine(i + 1, String(idx + 1) + ". " + list[idx]);
                        } else {
                            lcd->printLine(i + 1, "");
                        }
                    }
                }

                lcd->printLine(3, "A.Next B.Back D.Exit");
                needRender = false;
            }

            char key = keypad->getKey();

            if (key >= '0' && key <= '9') {
                isSelecting = true;
                if (selectBuffer.length() < 3) {
                    selectBuffer += key;
                    needRender = true;
                }
            }
            else if (key == '*') {
                if (selectBuffer.length() > 0) {
                    selectBuffer.remove(selectBuffer.length() - 1);
                    needRender = true;
                } else if (isSelecting) {
                    isSelecting = false;
                    needRender = true;
                }
            }
            else if (key == '#') {
                std::vector<String> list;
                if (currentRole == ROLE_ADMIN) {
                    list = kelasManager.getAllKelas();
                } else {
                    list = kelasManager.getKelasByDosen(tempNIP);
                }
                int idx = selectBuffer.toInt() - 1;
                if (idx >= 0 && idx < list.size()) {
                    selectedKelas = list[idx];
                    // Log saat Dosen memilih kelas
                    if (currentRole == ROLE_DOSEN) {
                        logSystemActivity("MENU_NAV", "Dosen " + tempNIP + " membuka kelas " + selectedKelas);
                    } else {
                        logSystemActivity("MENU_NAV", "Admin membuka kelas " + selectedKelas);
                    }
                    viewIndex = 0;
                    selectBuffer = "";
                    isSelecting = false;
                    firstEntry = true;
                    currentState = STATE_VIEW_MAHASISWA_BY_KELAS;
                    needRender = true;
                } else {
                    lcd->printLine(1, MSG_TIDAK_VALID);
                    delay(1000);
                    selectBuffer = "";
                    isSelecting = false;
                    needRender = true;
                }
            }
            else if (key == 'A') {
                std::vector<String> list;
                if (currentRole == ROLE_ADMIN) {
                    list = kelasManager.getAllKelas();
                } else {
                    list = kelasManager.getKelasByDosen(tempNIP);
                }
                if (viewIndex + 2 < list.size()) {
                    viewIndex += 2;
                    needRender = true;
                }
            }
            else if (key == 'B') {
                if (viewIndex - 2 >= 0) {
                    viewIndex -= 2;
                    needRender = true;
                }
            }
            else if (key == 'D') {
                if (isSelecting) {
                    isSelecting = false;
                    selectBuffer = "";
                    needRender = true;
                } else {
                    firstEntry = true;
                    goToMainMenu();
                }
            }
        }
        break;

        // ================= VIEW MAHASISWA BY KELAS =================
        case STATE_VIEW_MAHASISWA_BY_KELAS:
        {
            static bool firstEntry = true;
            if (firstEntry) {
                viewIndex = 0;
                firstEntry = false;
            }

            if (needRender) {
                std::vector<String> list = kelasManager.getMahasiswaByKelas(selectedKelas);

                lcd->clear();
                lcd->printLine(0, "=== MHS: " + selectedKelas + "  ===");

                for (int i = 0; i < 2; i++) {
                    int idx = viewIndex + i;
                    if (idx < list.size()) {
                        String nim = list[idx];
                        bool hasFp = fingerprintDataManager.hasFingerprint(nim);

                        // Print NIM first
                        lcd->printLine(i + 1, String(idx + 1) + ". " + nim);

                        // Print custom char at the end (column 18)
                        lcd->setCursor(18, i + 1);
                        if (hasFp) {
                            lcd->write(byte(0)); // ✔
                        } else {
                            lcd->write(byte(1)); // ✖
                        }
                    } else {
                        lcd->printLine(i + 1, "");
                    }
                }

                lcd->printLine(3, "A.Next B.Back D.Exit");
                needRender = false;
            }

            char key = keypad->getKey();

            if (key == 'A') {
                std::vector<String> list = kelasManager.getMahasiswaByKelas(selectedKelas);
                if (viewIndex + 2 < list.size()) {
                    viewIndex += 2;
                    needRender = true;
                }
            }
            else if (key == 'B') {
                if (viewIndex - 2 >= 0) {
                    viewIndex -= 2;
                    needRender = true;
                }
            }
            else if (key == 'D') {
                firstEntry = true;
                currentState = STATE_SELECT_KELAS;
                needRender = true;
            }
        }
        break;

        // ================= DELETE MHS SELECT KELAS =================
        case STATE_DELETE_MHS_SELECT_KELAS:
        {
            static bool firstEntry = true;
            static String selectBuffer = "";
            static bool isSelecting = false;
            if (firstEntry) {
                viewIndex = 0;
                selectBuffer = "";
                isSelecting = false;
                firstEntry = false;
            }

            if (needRender) {
                std::vector<String> list = kelasManager.getAllKelas();

                lcd->clear();
                lcd->printLine(0, "== HAPUS MHS ==");

                if (isSelecting) {
                    lcd->printLine(1, "Pilih: " + selectBuffer + "_");
                    lcd->printLine(2, "");
                } else {
                    for (int i = 0; i < 2; i++) {
                        int idx = viewIndex + i;
                        if (idx < list.size()) {
                            lcd->printLine(i + 1, String(idx + 1) + ". " + list[idx]);
                        } else {
                            lcd->printLine(i + 1, "");
                        }
                    }
                }

                lcd->printLine(3, "A.Next B.Back D.Exit");
                needRender = false;
            }

            char key = keypad->getKey();

            if (key >= '0' && key <= '9') {
                isSelecting = true;
                if (selectBuffer.length() < 3) {
                    selectBuffer += key;
                    needRender = true;
                }
            }
            else if (key == '*') {
                if (selectBuffer.length() > 0) {
                    selectBuffer.remove(selectBuffer.length() - 1);
                    needRender = true;
                } else if (isSelecting) {
                    isSelecting = false;
                    needRender = true;
                }
            }
            else if (key == '#') {
                std::vector<String> list = kelasManager.getAllKelas();
                int idx = selectBuffer.toInt() - 1;
                if (idx >= 0 && idx < list.size()) {
                    selectedKelas = list[idx];
                    logSystemActivity("MENU_NAV", "Admin: " + tempNIP + " memilih kelas " + selectedKelas + " untuk hapus Mhs");
                    viewIndex = 0;
                    selectBuffer = "";
                    isSelecting = false;
                    firstEntry = true;
                    currentState = STATE_DELETE_MHS_LIST;
                    needRender = true;
                } else {
                    lcd->printLine(1, MSG_TIDAK_VALID);
                    delay(1000);
                    selectBuffer = "";
                    isSelecting = false;
                    needRender = true;
                }
            }
            else if (key == 'A') {
                std::vector<String> list = kelasManager.getAllKelas();
                if (viewIndex + 2 < list.size()) {
                    viewIndex += 2;
                    needRender = true;
                }
            }
            else if (key == 'B') {
                if (viewIndex - 2 >= 0) {
                    viewIndex -= 2;
                    needRender = true;
                }
            }
            else if (key == 'D') {
                if (isSelecting) {
                    isSelecting = false;
                    selectBuffer = "";
                    needRender = true;
                } else {
                    firstEntry = true;
                    currentState = STATE_MENU_ADMIN_PAGE4;
                    needRender = true;
                }
            }
        }
        break;

        // ================= DELETE MHS LIST =================
        case STATE_DELETE_MHS_LIST:
        {
            static bool firstEntry = true;
            static String selectBuffer = "";
            static bool isSelecting = false;
            static std::vector<String> filteredList;  // Hanya MHS dengan FP
            if (firstEntry) {
                // Filter: hanya mahasiswa dengan fingerprint terdaftar
                filteredList.clear();
                std::vector<String> allMhs = kelasManager.getMahasiswaByKelas(selectedKelas);
                for (String nim : allMhs) {
                    if (fingerprintDataManager.hasFingerprint(nim)) {
                        filteredList.push_back(nim);
                    }
                }
                viewIndex = 0;
                selectBuffer = "";
                isSelecting = false;
                firstEntry = false;
            }

            if (needRender) {
                lcd->clear();
                lcd->printLine(0, "== MHS: " + selectedKelas + " ==");

                if (isSelecting) {
                    lcd->printLine(1, "Pilih: " + selectBuffer + "_");
                    lcd->printLine(2, "");
                } else {
                    // Tampilkan hanya yang ada di filteredList
                    for (int i = 0; i < 2; i++) {
                        int idx = viewIndex + i;
                        if (idx < filteredList.size()) {
                            lcd->printLine(i + 1, String(idx + 1) + ". " + filteredList[idx]);
                        } else {
                            lcd->printLine(i + 1, "");
                        }
                    }
                }

                lcd->printLine(3, "A.Next B.Back D.Exit");
                needRender = false;
            }

            char key = keypad->getKey();

            if (key >= '0' && key <= '9') {
                isSelecting = true;
                if (selectBuffer.length() < 3) {
                    selectBuffer += key;
                    needRender = true;
                }
            }
            else if (key == '*') {
                if (selectBuffer.length() > 0) {
                    selectBuffer.remove(selectBuffer.length() - 1);
                    needRender = true;
                } else if (isSelecting) {
                    isSelecting = false;
                    needRender = true;
                }
            }
            else if (key == '#') {
                // Gunakan filteredList yang sudah difilter
                int idx = selectBuffer.toInt() - 1;
                if (idx >= 0 && idx < filteredList.size()) {
                    deleteTarget = filteredList[idx];
                    selectBuffer = "";
                    isSelecting = false;
                    mode = INPUT_DELETE_ADMIN_PIN;
                    input->setMode("PIN Admin:", true);
                    input->reset();
                    firstEntry = true;
                    currentState = STATE_DELETE_MHS_CONFIRM_PIN;
                    needRender = true;
                } else {
                    lcd->printLine(1, MSG_TIDAK_VALID);
                    delay(1000);
                    selectBuffer = "";
                    isSelecting = false;
                    needRender = true;
                }
            }
            else if (key == 'A') {
                // Gunakan filteredList untuk pagination
                if (viewIndex + 2 < filteredList.size()) {
                    viewIndex += 2;
                    needRender = true;
                }
            }
            else if (key == 'B') {
                if (viewIndex - 2 >= 0) {
                    viewIndex -= 2;
                    needRender = true;
                }
            }
            else if (key == 'D') {
                if (isSelecting) {
                    isSelecting = false;
                    selectBuffer = "";
                    needRender = true;
                } else {
                    firstEntry = true;
                    currentState = STATE_DELETE_MHS_SELECT_KELAS;
                    needRender = true;
                }
            }
        }
        break;

        // ================= DELETE MHS CONFIRM PIN =================
        case STATE_DELETE_MHS_CONFIRM_PIN:
        {
            static bool firstEntry = true;
            if (firstEntry) {
                firstEntry = false;
            }

            if (needRender) {
                lcd->clear();
                lcd->printLine(0, "== VALIDASI ==");
                lcd->printLine(1, "PIN Admin:");
                lcd->printLine(3, "#.Konfirmasi D.Batal");
                needRender = false;
            }

            if (input->update()) {
                String pin = input->getValue();

                if (auth->login(tempNIP, pin) == ROLE_ADMIN) {
                    // PIN valid - delete fingerprint only
                    int fpId = fingerprintDataManager.getFingerprintId(deleteTarget);
                    if (fpId > 0) {
                        fingerprintManager.deleteTemplate(fpId);
                    }
                    fingerprintDataManager.deleteFingerprintByUserId(deleteTarget);

                    lcd->clear();
                    lcd->printLine(0, "Berhasil Dihapus!");
                    delay(1500);

                    // Log: Fingerprint Dosen dihapus
                    logSystemActivity("DELETE_FP", "Dosen: " + deleteTarget + " Slot: " + fpId);

                    firstEntry = true;
                    currentState = STATE_MENU_ADMIN_PAGE4;
                    needRender = true;
                } else {
                    // PIN salah
                    lcd->clear();
                    lcd->printLine(0, MSG_PIN_SALAH);
                    delay(1500);

                    firstEntry = true;
                    currentState = STATE_MENU_ADMIN_PAGE4;
                    needRender = true;
                }
            }

            char key = keypad->getKey();
            if (key == 'D') {
                firstEntry = true;
                currentState = STATE_DELETE_MHS_LIST;
                needRender = true;
            }
        }
        break;

        // ================= DELETE DOSEN LIST =================
        case STATE_DELETE_DOSEN_LIST:
        {
            static bool firstEntry = true;
            static String selectBuffer = "";
            static bool isSelecting = false;
            if (firstEntry) {
                viewIndex = 0;
                selectBuffer = "";
                isSelecting = false;
                firstEntry = false;
            }

            if (needRender) {
                std::vector<String> allDosen = userManager.getAllDosen();
                std::vector<String> list;

                // Filter: hanya dosen dengan fingerprint
                for (String nip : allDosen) {
                    if (fingerprintDataManager.hasFingerprint(nip)) {
                        list.push_back(nip);
                    }
                }

                lcd->clear();
                lcd->printLine(0, "== HAPUS FP DOSEN ==");

                if (isSelecting) {
                    lcd->printLine(1, "Pilih: " + selectBuffer + "_");
                    lcd->printLine(2, "");
                } else {
                    for (int i = 0; i < 2; i++) {
                        int idx = viewIndex + i;
                        if (idx < list.size()) {
                            lcd->printLine(i + 1, String(idx + 1) + ". " + list[idx]);
                        } else {
                            lcd->printLine(i + 1, "");
                        }
                    }
                }

                lcd->printLine(3, "A.Next B.Back D.Exit");
                needRender = false;
            }

            char key = keypad->getKey();

            if (key >= '0' && key <= '9') {
                isSelecting = true;
                if (selectBuffer.length() < 3) {
                    selectBuffer += key;
                    needRender = true;
                }
            }
            else if (key == '*') {
                if (selectBuffer.length() > 0) {
                    selectBuffer.remove(selectBuffer.length() - 1);
                    needRender = true;
                } else if (isSelecting) {
                    isSelecting = false;
                    needRender = true;
                }
            }
            else if (key == '#') {
                std::vector<String> allDosen = userManager.getAllDosen();
                std::vector<String> list;
                for (String nip : allDosen) {
                    if (fingerprintDataManager.hasFingerprint(nip)) {
                        list.push_back(nip);
                    }
                }
                int idx = selectBuffer.toInt() - 1;
                if (idx >= 0 && idx < list.size()) {
                    deleteTarget = list[idx];
                    selectBuffer = "";
                    isSelecting = false;
                    firstEntry = true;
                    currentState = STATE_DELETE_DOSEN_CONFIRM_PIN;
                    needRender = true;
                } else {
                    lcd->printLine(1, MSG_TIDAK_VALID);
                    delay(1000);
                    selectBuffer = "";
                    isSelecting = false;
                    needRender = true;
                }
            }
            else if (key == 'A') {
                std::vector<String> allDosen = userManager.getAllDosen();
                std::vector<String> list;
                for (String nip : allDosen) {
                    if (fingerprintDataManager.hasFingerprint(nip)) {
                        list.push_back(nip);
                    }
                }
                if (viewIndex + 2 < list.size()) {
                    viewIndex += 2;
                    needRender = true;
                }
            }
            else if (key == 'B') {
                if (viewIndex - 2 >= 0) {
                    viewIndex -= 2;
                    needRender = true;
                }
            }
            else if (key == 'D') {
                if (isSelecting) {
                    isSelecting = false;
                    selectBuffer = "";
                    needRender = true;
                } else {
                    firstEntry = true;
                    currentState = STATE_MENU_ADMIN_PAGE4;
                    needRender = true;
                }
            }
        }
        break;

        // ================= DELETE DOSEN CONFIRM PIN =================
        case STATE_DELETE_DOSEN_CONFIRM_PIN:
        {
            static bool firstEntry = true;
            static String pinBuffer = "";
            if (firstEntry) {
                pinBuffer = "";
                firstEntry = false;
            }

            if (needRender) {
                lcd->clear();
                lcd->printLine(0, "== HAPUS FP DOSEN ==");
                lcd->printLine(1, "NIP: " + deleteTarget);

                String masked = "";
                for (int i = 0; i < pinBuffer.length(); i++) {
                    masked += "*";
                }
                lcd->printLine(2, "PIN: " + masked);
                lcd->printLine(3, "A.OK  D.Batal");
                needRender = false;
            }

            char key = keypad->getKey();

            if (key >= '0' && key <= '9') {
                if (pinBuffer.length() < 6) {
                    pinBuffer += key;
                    needRender = true;
                }
            }
            else if (key == '*') {
                if (pinBuffer.length() > 0) {
                    pinBuffer.remove(pinBuffer.length() - 1);
                    needRender = true;
                }
            }
            else if (key == 'A') {
                if (auth->login("123", pinBuffer) == ROLE_ADMIN) {
                    bool ok = userManager.deleteFingerprint(deleteTarget, "dosen");

                    lcd->clear();
                    lcd->printLine(0, ok ? "Berhasil Dihapus!" : "Data Tidak Ada");
                    delay(1500);

                    pinBuffer = "";
                    firstEntry = true;
                    currentState = STATE_MENU_ADMIN_PAGE4;
                    needRender = true;
                } else {
                    lcd->clear();
                    lcd->printLine(0, MSG_PIN_SALAH);
                    delay(1500);

                    pinBuffer = "";
                    needRender = true;
                }
            }
            else if (key == 'D') {
                pinBuffer = "";
                firstEntry = true;
                currentState = STATE_DELETE_DOSEN_LIST;
                needRender = true;
            }
        }
        break;

        // ================= ENROLL FINGERPRINT (INTERAKTIF/BLOCKING) =================
        case STATE_ENROLL_FINGERPRINT:
        {
            static bool firstEntry = true;
            static bool enrolling = false;
            static int fingerId = -1;
            static int enrollPhase = 0;  // 0=idle, 1=scan1, 2=wait_lift, 3=scan2, 4=process, 5=success, 6=failed

            if (firstEntry) {
                enrolling = false;
                fingerId = -1;
                enrollPhase = 0;
                firstEntry = false;

                // TUGAS 1: TENTUKAN TARGET ID SEBELUM SENSOR MENYALA
                fingerId = fingerprintDataManager.getTargetIdForEnrollment(tempNIP);
                if (fingerId <= 0) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, MSG_SENSOR_PENUH);
                    delay(2000);
                    firstEntry = true;
                    currentState = STATE_MENU_ADMIN_PAGE4;
                    needRender = true;
                    break;
                }

                Serial.println("[ENROLL_ADMIN] Target ID (Sensor=CSV): " + String(fingerId));

                lcd->clear();
                lcd->printLine(0, "== DAFTAR FP ==");
                lcd->printLine(1, "Tekan # untuk mulai");
                lcd->printLine(3, "D.Cancel");
                fingerprintManager.setLEDStandby();
            }

            // CEK TOMBOL D SEBELUM PROCESS (bukan SESUDAH)
            char key = keypad->getKey();
            if (key == 'D') {
                logSystemActivity("ENROLL_CANCEL", "Admin: " + tempNIP + " - User cancelled");
                fingerprintManager.setLEDOff();
                enrolling = false;
                enrollPhase = 0;
                firstEntry = true;
                currentState = STATE_MENU_ADMIN_PAGE4;
                needRender = true;
                break;
            }

            // ========== TUGAS 2: IMPLEMENTASI UX SCANNING INTERAKTIF (BLOCKING WAIT) ==========
            if (key == '#' && !enrolling) {
                enrolling = true;
                enrollPhase = 1;  // Mulai fase scan 1
            }

            if (!enrolling || enrollPhase == 0) break;

            Adafruit_Fingerprint_ESP32* finger = fingerprintManager.getFinger();

            // --- FASE SCAN 1 ---
            if (enrollPhase == 1) {
                lcd->clear();
                lcd->printLine(0, MSG_DAFTAR_FP);
                lcd->printLine(1, "Tempelkan Jari...");

                // Tunggu sampai jari menempel
                while (finger->get_image() != FP_OK) {
                    delay(50);  // Mencegah Watchdog Timer (WDT) reset
                    char k = keypad->getKey();
                    if (k == 'D') {
                        enrolling = false;
                        enrollPhase = 0;
                        firstEntry = true;
                        currentState = STATE_MENU_ADMIN_PAGE4;
                        needRender = true;
                        break;
                    }
                }
                if (!enrolling) break;

                lcd->printLine(2, "Membaca...");
                uint8_t p = finger->image_2_tz(1);
                if (p != FP_OK) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, MSG_BACA_GAGAL);
                    delay(2000);
                    enrollPhase = 6;  // Failed
                } else {
                    fingerprintManager.setLEDScanning();
                    enrollPhase = 2;  // Lanjut ke fase tunggu angkat jari
                }
            }

            // --- FASE TUNGGU ANGKAT JARI ---
            if (enrollPhase == 2) {
                lcd->clear();
                lcd->printLine(0, MSG_DAFTAR_FP);
                lcd->printLine(1, "Angkat Jari Anda!");

                // Tunggu sampai sensor benar-benar tidak mendeteksi jari
                while (finger->get_image() != FP_NOFINGER) {
                    delay(50);
                    char k = keypad->getKey();
                    if (k == 'D') {
                        enrolling = false;
                        enrollPhase = 0;
                        firstEntry = true;
                        currentState = STATE_MENU_ADMIN_PAGE4;
                        needRender = true;
                        break;
                    }
                }
                if (!enrolling || enrollPhase != 2) break;

                delay(500);  // Jeda bernapas sedikit sebelum scan 2
                enrollPhase = 3;  // Lanjut ke fase scan 2
            }

            // --- FASE SCAN 2 ---
            if (enrollPhase == 3) {
                lcd->clear();
                lcd->printLine(0, MSG_DAFTAR_FP);
                lcd->printLine(1, "Tempelkan Lagi...");

                // Tunggu sampai jari menempel lagi
                while (finger->get_image() != FP_OK) {
                    delay(50);
                    char k = keypad->getKey();
                    if (k == 'D') {
                        enrolling = false;
                        enrollPhase = 0;
                        firstEntry = true;
                        currentState = STATE_MENU_ADMIN_PAGE4;
                        needRender = true;
                        break;
                    }
                }
                if (!enrolling) break;

                lcd->printLine(2, "Membaca...");
                uint8_t p = finger->image_2_tz(2);
                if (p != FP_OK) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, MSG_BACA_GAGAL);
                    delay(2000);
                    enrollPhase = 6;  // Failed
                } else {
                    fingerprintManager.setLEDScanning();
                    enrollPhase = 4;  // Lanjut ke fase proses & simpan
                }
            }

            // --- FASE PROSES & SIMPAN ---
            if (enrollPhase == 4) {
                lcd->clear();
                lcd->printLine(0, MSG_MEMPROSES);
                lcd->printLine(1, "Mohon Tunggu");

                // ========== FIX: URUTAN YANG BENAR ==========
                // 1. Create model terlebih dahulu
                uint8_t p = finger->create_model();
                if (p != FP_OK) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "Jari tidak cocok");
                    Serial.printf("[ENROLL_ADMIN] create_model GAGAL: %d\n", p);
                    delay(2000);
                    enrollPhase = 6;  // Failed
                } else {
                    // 2. Store ke flash sensor SEBELUM extract hex
                    uint8_t storeResult = finger->store_model(fingerId);
                    if (storeResult != FP_OK) {
                        lcd->clear();
                        lcd->printLine(0, MSG_GAGAL);
                        lcd->printLine(1, "Simpan ke sensor gagal");
                        Serial.printf("[ENROLL_ADMIN] store_model GAGAL: %d\n", storeResult);
                        delay(2000);
                        enrollPhase = 6;  // Failed
                    } else {
                        Serial.printf("[ENROLL_ADMIN] SUCCESS - stored at slot %d\n", fingerId);
                        enrollPhase = 5;  // Lanjut ke fase hasil
                    }
                }
            }

            // --- FASE HASIL ---
            if (enrollPhase == 5) {
                // ========== SIMPAN KE CSV ==========
                // 3. Extract hex template SETELAH store berhasil
                String hexTemplate = fingerprintManager.extractTemplateAsHex(fingerId);

                Serial.printf("[ENROLL_ADMIN] hexTemplate length: %d\n", hexTemplate.length());

                // Validasi: hexTemplate harus > 100 karakter
                bool saveSuccess = false;
                if (hexTemplate.length() > 100) {
                    saveSuccess = fingerprintDataManager.updateFingerprintSlotWithHex(tempNIP, "admin", fingerId, hexTemplate);
                } else {
                    // hexTemplate gagal - hapus template dari sensor
                    finger->delete_model(fingerId);
                    Serial.println("[ENROLL_ADMIN] GAGAL - hex template kosong atau teralu pendek");
                }

                if (saveSuccess) {
                    fingerprintManager.syncFromCSV(&userManager);

                    // Update fingerMap
                    fingerMap[fingerId] = tempNIP;

                    lcd->clear();
                    lcd->printLine(0, "Berhasil!");
                    lcd->printLine(1, "Slot: " + String(fingerId));
                    fingerprintManager.setLEDSuccess();
                    delay(2000);
                    fingerprintManager.setLEDOff();
                    logSystemActivity("ENROLL_FP", "Admin: " + tempNIP + " Slot: " + fingerId);
                } else {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "Save CSV/hex gagal");
                    fingerprintManager.setLEDError();
                    delay(2000);
                    fingerprintManager.setLEDOff();
                }
                enrolling = false;
                enrollPhase = 0;
                firstEntry = true;
                currentState = STATE_MENU_ADMIN_PAGE4;
                needRender = true;
            }
            else if (enrollPhase == 6) {
                // ========== GAGAL ==========
                fingerprintManager.setLEDError();
                delay(1000);
                fingerprintManager.setLEDOff();

                logSystemActivity("ENROLL_FP", "GAGAL - Admin: " + tempNIP + " - Fingerprint scan error");
                enrolling = false;
                enrollPhase = 0;
                firstEntry = true;
                currentState = STATE_MENU_ADMIN_PAGE4;
                needRender = true;
            }
        }
        break;

        // ================= ENROLL DOSEN FINGERPRINT (INTERAKTIF/BLOCKING) =================
        case STATE_ENROLL_DOSEN_FINGERPRINT:
        {
            // ========== ENROLLMENT 2-JARI DOSEN (Jempol Kanan + Jempol Kiri) ==========
            // Jari 1 (KANAN) -> disimpan permanen di slot fingerId (dipakai untuk login),
            //                  hex-nya ditampung di tempHexJari1.
            // Jari 2 (KIRI)  -> disimpan sementara di ENROLL_TEMP_SLOT_2, hex diekstrak
            //                  lalu slot di-delete. Hex dikirim bersama jari 1 ke /sync.
            // Server menerima fp_1_user + fp_2_user; local hanya menyimpan jari 1 sebagai
            // login utama (jari 2 bisa di-inject ulang via fetchUsersAndSync bila perlu).
            static bool firstEntry = true;
            static int  fingerId = -1;
            static int  currentFinger = 1;       // 1 = Jempol Kanan, 2 = Jempol Kiri
            static int  enrollPhase = 0;         // 0=idle, 1=scan1, 2=wait_lift, 3=scan2, 4=process
            static bool started = false;

            if (firstEntry) {
                firstEntry = false;
                fingerId = -1;
                currentFinger = 1;
                enrollPhase = 0;
                started = false;
                tempHexJari1 = "";

                fingerId = fingerprintDataManager.getTargetIdForEnrollment(tempNIP);
                if (fingerId <= 0) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, MSG_SENSOR_PENUH);
                    delay(2000);
                    firstEntry = true;
                    currentState = STATE_MENU_DOSEN;
                    needRender = true;
                    break;
                }

                Serial.println("[ENROLL DOSEN] Target slot Jari 1 = " + String(fingerId)
                    + " | Jari 2 temp slot = " + String(ENROLL_TEMP_SLOT_2));

                lcd->clear();
                lcd->printLine(0, F("DAFTAR 2 JARI"));
                lcd->printLine(1, "NIP: " + tempNIP);
                lcd->printLine(2, F("Tahap 1: KANAN"));
                lcd->printLine(3, F("#.Mulai  D.Batal"));
                fingerprintManager.setLEDStandby();
            }

            char key = keypad->getKey();
            if (key == 'D' && enrollPhase != 4) {
                logSystemActivity("ENROLL_CANCEL", "Dosen: " + tempNIP + " - User cancelled");
                Adafruit_Fingerprint_ESP32* fg = fingerprintManager.getFinger();
                fg->delete_model(ENROLL_TEMP_SLOT_2);  // jaga-jaga
                fingerprintManager.setLEDOff();
                tempHexJari1 = "";
                firstEntry = true;
                currentState = STATE_MENU_DOSEN;
                needRender = true;
                break;
            }

            if (key == '#' && !started) {
                started = true;
                enrollPhase = 1;
            }

            if (!started || enrollPhase == 0) break;

            Adafruit_Fingerprint_ESP32* finger = fingerprintManager.getFinger();
            const __FlashStringHelper* sideLabel = (currentFinger == 1)
                ? F("JEMPOL KANAN") : F("JEMPOL KIRI");
            // Jari 1 disimpan permanen di slot fingerId (utk login); jari 2 hanya scratch
            int targetSlot = (currentFinger == 1) ? fingerId : ENROLL_TEMP_SLOT_2;

            // --- FASE SCAN 1 (per jari) ---
            if (enrollPhase == 1) {
                lcd->clear();
                lcd->printLine(0, sideLabel);
                lcd->printLine(1, F("Tempel jari (1)"));
                lcd->printLine(2, "NIP: " + tempNIP);
                while (finger->get_image() != FP_OK) {
                    delay(50);
                    if (keypad->getKey() == 'D') { started = false; break; }
                }
                if (!started) { firstEntry = true; currentState = STATE_MENU_DOSEN; needRender = true; break; }

                lcd->printLine(2, F("Membaca..."));
                if (finger->image_2_tz(1) != FP_OK) enrollPhase = 6;
                else { fingerprintManager.setLEDScanning(); enrollPhase = 2; }
            }

            // --- FASE TUNGGU ANGKAT JARI ---
            if (enrollPhase == 2) {
                lcd->clear();
                lcd->printLine(0, sideLabel);
                lcd->printLine(1, F("Angkat Jari!"));
                while (finger->get_image() != FP_NOFINGER) {
                    delay(50);
                    if (keypad->getKey() == 'D') { started = false; break; }
                }
                if (!started) { firstEntry = true; currentState = STATE_MENU_DOSEN; needRender = true; break; }
                delay(500);
                enrollPhase = 3;
            }

            // --- FASE SCAN 2 (jari yg SAMA, averaging) ---
            if (enrollPhase == 3) {
                lcd->clear();
                lcd->printLine(0, sideLabel);
                lcd->printLine(1, F("Tempel lagi (2)"));
                while (finger->get_image() != FP_OK) {
                    delay(50);
                    if (keypad->getKey() == 'D') { started = false; break; }
                }
                if (!started) { firstEntry = true; currentState = STATE_MENU_DOSEN; needRender = true; break; }

                lcd->printLine(2, F("Membaca..."));
                if (finger->image_2_tz(2) != FP_OK) enrollPhase = 6;
                else { fingerprintManager.setLEDScanning(); enrollPhase = 4; }
            }

            // --- FASE PROSES + EKSTRAK HEX (per jari) ---
            if (enrollPhase == 4) {
                lcd->clear();
                lcd->printLine(0, sideLabel);
                lcd->printLine(1, F("Memproses..."));

                if (finger->create_model() != FP_OK) {
                    Serial.println(F("[ENROLL_DOSEN] create_model GAGAL"));
                    enrollPhase = 6;
                }
                else if (finger->store_model(targetSlot) != FP_OK) {
                    Serial.printf_P(PSTR("[ENROLL_DOSEN] store_model GAGAL slot %d\n"), targetSlot);
                    enrollPhase = 6;
                }
                else {
                    String hex = fingerprintManager.extractTemplateAsHex(targetSlot);
                    Serial.printf_P(PSTR("[ENROLL_DOSEN] Jari %d hex len=%d\n"), currentFinger, hex.length());

                    if (hex.length() < 100) {
                        finger->delete_model(targetSlot);
                        enrollPhase = 6;
                    }
                    else if (currentFinger == 1) {
                        // ----- Jari 1 (KANAN) selesai -> simpan ke CSV + tempHexJari1, lanjut Jari 2 -----
                        tempHexJari1 = trimHex(hex);

                        // Simpan jari 1 ke CSV (utk login lokal); jari 2 hanya untuk push.
                        bool saveOk = fingerprintDataManager.updateFingerprintSlotWithHex(
                            tempNIP, "dosen", fingerId, hex);
                        if (saveOk) {
                            fingerMap[fingerId] = tempNIP;
                        } else {
                            Serial.println(F("[ENROLL_DOSEN] WARN: save Jari 1 ke CSV gagal"));
                        }

                        fingerprintManager.setLEDSuccess();
                        lcd->clear();
                        lcd->printLine(0, F("JARI 1 OK!"));
                        lcd->printLine(1, F("Tahap 2: KIRI"));
                        lcd->printLine(2, F("Tempel Jempol Kiri"));
                        delay(1800);
                        fingerprintManager.setLEDOff();
                        currentFinger = 2;
                        enrollPhase = 1;     // ulang siklus untuk jari ke-2
                    }
                    else {
                        // ----- Jari 2 (KIRI) selesai -> push ke /api/device/sync -----
                        String hexJari2 = trimHex(hex);

                        lcd->clear();
                        lcd->printLine(0, F("Pushing 2 jari..."));
                        lcd->printLine(1, "NIP: " + tempNIP);
                        lcd->printLine(2, F("API /sync"));

                        bool pushOk = pushSyncOpDualFingers(
                            tempNIP, sessionPin, tempHexJari1, hexJari2);

                        // Hapus slot temp jari 2 (jari 1 tetap permanen utk login).
                        finger->delete_model(ENROLL_TEMP_SLOT_2);

                        lcd->clear();
                        if (pushOk) {
                            fingerprintManager.setLEDSuccess();
                            lcd->printLine(0, F("REGISTRASI SUKSES"));
                            lcd->printLine(1, "NIP: " + tempNIP);
                            lcd->printLine(2, F("2 jari tersinkron"));
                            logSystemActivity("SYNC_OP_2FP", "OK: " + tempNIP);
                        } else {
                            fingerprintManager.setLEDError();
                            lcd->printLine(0, F("PUSH GAGAL"));
                            lcd->printLine(1, F("Cek WiFi/PIN sesi"));
                            lcd->printLine(2, "NIP: " + tempNIP);
                            logSystemActivity("SYNC_OP_2FP_ERR", tempNIP);
                        }
                        delay(2500);
                        fingerprintManager.setLEDOff();

                        // Reset state -> kembali ke menu dosen
                        tempHexJari1 = "";
                        firstEntry = true;
                        currentState = STATE_MENU_DOSEN;
                        needRender = true;
                    }
                }
            }

            // --- FASE GAGAL: ulangi siklus jari yg sama ---
            if (enrollPhase == 6) {
                fingerprintManager.setLEDError();
                lcd->clear();
                lcd->printLine(0, F("GAGAL"));
                lcd->printLine(1, sideLabel);
                lcd->printLine(2, F("Ulangi tempel"));
                delay(2000);
                fingerprintManager.setLEDOff();
                enrollPhase = 1;   // ulang jari yg sama
            }
        }
        break;

        // ================= ADMIN WAIT USB =================
        case STATE_ADMIN_WAIT_USB:
        {
            static bool firstEntry = true;
            static bool ch376Checked = false;

            if (firstEntry) {
                firstEntry = false;
                ch376Checked = false;
                lcd->clear();
                lcd->printLine(0, "Colokkan Flashdisk!");
                lcd->printLine(1, "Menunggu...");
                lcd->printLine(3, MSG_BATAL);

                // Debug: Check CH376 module on first entry
                Serial.println("[USB] Mencari koneksi CH376 di bus SPI...");
            }

            // Check CH376 module only once
            if (!ch376Checked) {
                ch376Checked = true;

                // Try to ping CH376
                bool ch376Responding = ch376.pingCH376();

                if (!ch376Responding) {
                    Serial.println("[USB] ERROR: Modul CH376 tidak merespons! Cek kabel SPI/CS.");
                    lcd->clear();
                    lcd->printLine(0, "ERROR CH376!");
                    lcd->printLine(1, "Modul tidak merespons");
                    lcd->printLine(2, "Cek wiring SPI");
                    delay(3000);
                    firstEntry = true;
                    currentState = STATE_MENU_ADMIN_PAGE4_2;
                    needRender = true;
                    break;
                }

                Serial.println("[USB] CH376 OK. Menunggu Flashdisk dicolok...");
            }

            // Non-blocking USB detection
            bool usbDetected = ch376.isUSBDeviceConnected();

            if (usbDetected) {
                Serial.println("[USB] Flashdisk terdeteksi!");
                // USB found - proceed to copy
                firstEntry = true;
                currentState = STATE_ADMIN_COPY_USB;
                needRender = true;
                break;
            }

            // Show "waiting" animation every second
            static unsigned long lastBlink = 0;
            if (millis() - lastBlink > 1000) {
                lastBlink = millis();
                lcd->setCursor(15, 1);
                lcd->write('.');
            }

            // Check for cancel key
            char key = keypad->getKey();
            if (key == 'D') {
                firstEntry = true;
                currentState = STATE_MENU_ADMIN_PAGE4_2;
                needRender = true;
            }
        }
        break;

        // ================= ADMIN COPY USB =================
        case STATE_ADMIN_COPY_USB:
        {
            static bool firstEntry = true;
            static bool copyStarted = false;
            static bool copyComplete = false;
            static bool copySuccess = false;

            if (firstEntry) {
                firstEntry = false;
                copyStarted = false;
                copyComplete = false;
                copySuccess = false;

                lcd->clear();
                lcd->printLine(0, "Mengekspor Data...");
                lcd->printLine(1, MSG_MOHON_TUNGGU);
                lcd->printLine(3, MSG_BATAL);

                // Start mount USB
                ch376.mountUSB();
            }

            // Non-blocking copy process
            if (!copyComplete && !copyStarted) {
                copyStarted = true;

                // Try to mount and export
                if (ch376.mountUSB()) {
                    copySuccess = exportAllDataToUSB();
                } else {
                    copySuccess = false;
                }

                copyComplete = true;
                ch376.unmountUSB();
            }

            // Show result and wait for user acknowledgment
            if (copyComplete) {
                lcd->clear();
                if (copySuccess) {
                    lcd->printLine(0, "Ekspor Berhasil!");
                    lcd->printLine(1, "Data tersimpan");
                    logSystemActivity("USB_EXPORT", "Data berhasil diekspor ke USB");
                } else {
                    lcd->printLine(0, "Ekspor Gagal!");
                    lcd->printLine(1, "Coba lagi");
                    logSystemActivity("USB_EXPORT", "GAGAL - Data gagal diekspor");
                }
                lcd->printLine(3, "Tekan sembarang...");

                // Wait for key press before going to eject state
                char key = keypad->getKey();
                if (key != NO_KEY) {
                    firstEntry = true;
                    currentState = STATE_ADMIN_EJECT_USB;
                    needRender = true;
                }
            }

            // Check for cancel key
            char key = keypad->getKey();
            if (key == 'D' && !copyComplete) {
                logSystemActivity("USB_EXPORT", "Cancelled by user");
                firstEntry = true;
                ch376.unmountUSB();
                currentState = STATE_MENU_ADMIN_PAGE4;
                needRender = true;
            }
        }
        break;

        // ================= ADMIN EJECT USB =================
        case STATE_ADMIN_EJECT_USB:
        {
            static bool firstEntry = true;
            static bool waitingForRemoval = true;

            if (firstEntry) {
                firstEntry = false;
                waitingForRemoval = true;

                // Send eject command to CH376
                ch376.unmountUSB();

                lcd->clear();
                lcd->printLine(0, MSG_SELESAI_EX);
                lcd->printLine(1, "Cabut Flashdisk");
                lcd->printLine(2, "Sekarang...");
                lcd->printLine(3, "Tunggu sebentar");
            }

            // Non-blocking check for USB removal
            if (waitingForRemoval) {
                if (ch376.isUSBDisconnected()) {
                    waitingForRemoval = false;

                    // Small delay before returning to menu
                    delay(500);
                    firstEntry = true;
                    currentState = STATE_MENU_ADMIN_PAGE4;
                    needRender = true;
                }
            }

            // Also allow manual exit with any key after a short delay
            static unsigned long entryTime = 0;
            if (entryTime == 0) entryTime = millis();

            char key = keypad->getKey();
            if (key != NO_KEY && millis() - entryTime > 2000) {
                entryTime = 0;
                firstEntry = true;
                currentState = STATE_MENU_ADMIN_PAGE4;
                needRender = true;
            }
        }
        break;

        // ================= WIFI BROWSE (TUGAS 1: Browse Saved WiFi List) =================
        case STATE_WIFI_BROWSE:
        {
            static bool isInitialized = false;
            static bool isInputMode = false;
            static bool needsRedraw = true;
            static int currentPage = 0;
            static String tempWifiSelect = "";
            static String wifiListSSID[30];
            static int wifiCount = 0;

            // ========== TUGAS 2: PEMBACAAN WIFI.CSV SAAT INISIALISASI ==========
            if (!isInitialized) {
                lcd->clear();
                lcd->printLine(0, "MEMUAT WIFI...");
                wifiCount = 0;

                File file = SD.open("/wifi.csv", FILE_READ);
                if (file) {
                    if (file.available()) file.readStringUntil('\n'); // Skip header
                    while (file.available() && wifiCount < 30) {
                        String line = file.readStringUntil('\n');
                        line.trim();
                        if (line.length() > 0) {
                            // Asumsi indeks kolom ke-1 adalah SSID (id, ssid, pass, user)
                            wifiListSSID[wifiCount] = apiManager.getCsvColumn(line, 0);
                            wifiCount++;
                        }
                    }
                    file.close();
                }
                isInitialized = true;
                needsRedraw = true;
            }

            // ========== TUGAS 3: RENDERING UI DUA MODE (VIEW & INPUT) ==========
            if (needsRedraw) {
                lcd->clear();
                if (!isInputMode) {
                    // --- MODE VIEW ---
                    int totalPages = (wifiCount + 1) / 2;
                    if (totalPages == 0) totalPages = 1;
                    String pageInfo = "PILIH WIFI===(" + String(currentPage + 1) + "/" + String(totalPages) + ")";
                    lcd->printLine(0, pageInfo);

                    int startIndex = currentPage * 2;
                    if (startIndex < wifiCount) {
                        String line1 = String(startIndex + 1) + "." + wifiListSSID[startIndex];
                        lcd->printLine(1, line1);
                    } else {
                        lcd->printLine(1, "");
                    }
                    if (startIndex + 1 < wifiCount) {
                        String line2 = String(startIndex + 2) + "." + wifiListSSID[startIndex + 1];
                        lcd->printLine(2, line2);
                    } else {
                        lcd->printLine(2, "");
                    }
                    lcd->printLine(3, "A.New C.Next D.Back"); // Tombol A untuk tambah WiFi baru
                    
                } else {
                    // --- MODE INPUT ---
                    lcd->printLine(0, "PILIH WIFI");
                    String inputLine = "Pilih: " + tempWifiSelect + "_";
                    lcd->printLine(1, inputLine);
                    lcd->printLine(3, "#:Connect D:Batal");
                }
                needsRedraw = false;
            }

            // ========== TUGAS 4: LOGIKA KEYPAD (MENDUKUNG 2 DIGIT & TAMBAH WIFI) ==========
            char key = keypad->getKey();

            if (key) {
                if (key >= '0' && key <= '9') {
                    isInputMode = true;
                    if (tempWifiSelect.length() < 2) {
                        tempWifiSelect += key;
                        needsRedraw = true;
                    }
                } else if (key == '*') {
                    if (isInputMode && tempWifiSelect.length() > 0) {
                        tempWifiSelect.remove(tempWifiSelect.length() - 1);
                        if (tempWifiSelect.length() == 0) isInputMode = false;
                        needsRedraw = true;
                    }
                } else if (key == 'D') {
                    if (isInputMode) {
                        tempWifiSelect = "";
                        isInputMode = false;
                        needsRedraw = true;
                    } else {
                        int totalPages = (wifiCount + 1) / 2;
                        if (totalPages > 1 && currentPage >= 1) {
                            currentPage = (currentPage - 1) % totalPages;
                            needsRedraw = true;
                        }
                        else{
                            // Kembali ke menu sebelumnya
                            isInitialized = false;
                            currentPage = 0;
                            currentState = wifiReturnState;
                            needRender = true;
                            goToLogin();
                        }
                    }
                } else if (key == 'C' && !isInputMode) {
                    int totalPages = (wifiCount + 1) / 2;
                    if (totalPages > 1) {
                        currentPage = (currentPage + 1) % totalPages;
                        needsRedraw = true;
                    }
                } else if (key == 'A' && !isInputMode) {
                    // TOMBOL TAMBAH WIFI BARU (Jalankan mode Access Point)
                    logSystemActivity("WIFI_CONFIG", "User memilih tambah WiFi baru (AP mode)");
                    isInitialized = false;
                    currentPage = 0;
                    currentState = STATE_WIFI_CONFIG;
                    needRender = true;
                } else if (key == '#' && isInputMode) {
                    int selectedIndex = tempWifiSelect.toInt() - 1;
                    if (selectedIndex >= 0 && selectedIndex < wifiCount) {
                        String targetSSID = wifiListSSID[selectedIndex];

                        lcd->clear();
                        lcd->printLine(0, "MENGHUBUNGKAN...");
                        lcd->printLine(1, targetSSID);

                        // PANGGIL FUNGSI KONEKSI WIFI
                        bool connectResult = wifiManager.connectToSavedNetwork(targetSSID);

                        if (connectResult) {
                            lcd->clear();
                            lcd->printLine(0, MSG_BERHASIL);
                            lcd->printLine(1, "Terhubung ke:");
                            lcd->printLine(2, targetSSID);
                            logSystemActivity("WIFI_CONNECT", "Berhasil: " + targetSSID);
                        } else {
                            lcd->clear();
                            lcd->printLine(0, MSG_GAGAL);
                            lcd->printLine(1, "Tidak dapat");
                            lcd->printLine(2, "terhubung");
                            logSystemActivity("WIFI_CONNECT_ERROR", "Gagal: " + targetSSID);
                            
                        }

                        delay(2000);

                        // Kembali ke menu setelah mencoba koneksi
                        isInitialized = false;
                        isInputMode = false;
                        tempWifiSelect = "";
                        currentState = wifiReturnState;
                        needRender = true;
                        goToLogin();
                    } else {
                        lcd->printLine(2, "Pilihan Tdk Valid!");
                        delay(1000);
                        tempWifiSelect = "";
                        isInputMode = false;
                        needsRedraw = true;
                    }
                }
            }
        }
        break;

        // ================= WIFI CONFIG (AP Mode) =================
        case STATE_WIFI_CONFIG:
        {
            static bool firstEntry = true;

            if (firstEntry) {
                firstEntry = false;

                // Tampilkan pesan scanning TERLEBIH DAHULU
                lcd->clear();
                lcd->printLine(0, "=== SETUP WIFI ===");
                lcd->printLine(1, "Scanning Network...");
                lcd->printLine(2, "Mohon Tunggu...");
                lcd->printLine(3, MSG_BATAL);

                // Start WiFi AP mode dan scan networks
                if (wifiManager.startConfigMode()) {
                    // Setelah scan selesai, tampilkan instruksi
                    lcd->clear();
                    lcd->printLine(0, "=== SETUP WIFI ===");
                    lcd->printLine(1, "Connect Device anda");
                    lcd->printLine(2, "ke WIFI: " + String(WIFI_AP_SSID));
                    lcd->printLine(3, "Buka: 192.168.4.1");
                } else {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "WiFi tidak tersedia");
                    delay(2000);
                    firstEntry = true;
                    currentState = wifiReturnState;
                    needRender = true;
                }
            }

            // Process web server (non-blocking)
            wifiManager.process();

            // Check if credentials were received from web form
            if (wifiManager.hasCredentialsReceived()) {
                wifiManager.clearCredentialsReceived();
                firstEntry = true;
                currentState = STATE_WIFI_CONNECTING;
                needRender = true;
                break;
            }

            // Check for cancel key
            char key = keypad->getKey();
            if (key == 'D') {
                logSystemActivity("WIFI_CONFIG", "Cancelled by user");
                wifiManager.stopConfigMode();
                firstEntry = true;
                currentState = wifiReturnState;
                needRender = true;
            }
        }
        break;

        // ================= WIFI CONNECTING =================
        case STATE_WIFI_CONNECTING:
        {
            static bool firstEntry = true;
            static unsigned long startTime = 0;
            static String connectSSID = "";

            if (firstEntry) {
                firstEntry = false;
                startTime = millis();

                // Get SSID that was submitted
                connectSSID = wifiManager.getCurrentSSID();
                if (connectSSID == "") {
                    connectSSID = "<unknown>";
                }

                lcd->clear();
                lcd->printLine(0, "Connecting to:");
                lcd->printLine(1, connectSSID);
                lcd->printLine(2, "Mohon Tunggu...");
                lcd->printLine(3, MSG_BATAL);

                // STOP AP DULU sebelum mulai koneksi WiFi
                Serial.println("[WiFi] Stopping AP mode...");
                wifiManager.stopConfigMode();
                delay(200);

                // Mulai koneksi WiFi (pakai credential yang sudah disimpan)
                String password = wifiManager.getTargetPassword();
                String username = wifiManager.getTargetUsername();  // Untuk WPA2-Enterprise

                Serial.println("[WiFi] Starting WiFi connection to: " + connectSSID);
                WiFi.mode(WIFI_STA);

                // TUGAS 3: DUAL-MODE - Cek apakah username diisi (WPA2-Enterprise)
                if (username.length() > 0) {
                    Serial.println("[WiFi] Mode: WPA2-Enterprise");
                    Serial.println("[WiFi] Username: " + username);

                    esp_wifi_sta_wpa2_ent_enable();
                    esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)username.c_str(), username.length());
                    esp_wifi_sta_wpa2_ent_set_username((uint8_t*)username.c_str(), username.length());
                    esp_wifi_sta_wpa2_ent_set_password((uint8_t*)password.c_str(), password.length());

                    WiFi.begin(connectSSID.c_str());
                } else {
                    Serial.println("[WiFi] Mode: WPA2-Personal");
                    WiFi.begin(connectSSID.c_str(), password.c_str());
                }
            }

            // Process connection (non-blocking)
            wifiManager.process();

            // Check if connected
            if (wifiManager.isConnected()) {
                // Success! Save credentials (get from wifiManager)
                String ssid = wifiManager.getCurrentSSID();
                String password = wifiManager.getTargetPassword();
                String username = wifiManager.getTargetUsername();  // Untuk WPA2-Enterprise
                wifiManager.saveCredentials(ssid, password, username);

                firstEntry = true;
                currentState = STATE_WIFI_RESULT;
                needRender = true;
                break;
            }

            // Check if connection failed (timeout) - lebih dari 15 detik dan belum terkoneksi
            if (millis() - startTime > 15000 && !wifiManager.isConnected()) {
                // Connection failed
                WiFi.disconnect(true);
                firstEntry = true;
                currentState = STATE_WIFI_RESULT;
                needRender = true;
                break;
            }

            // Show loading animation di baris 2
            static unsigned long lastDot = 0;
            static int lastDotCount = 0;
            if (millis() - lastDot > 500) {
                lastDot = millis();
                int dots = (millis() - startTime) / 500 % 4;
                if (dots != lastDotCount) {
                    lastDotCount = dots;
                    String loading = "";
                    for (int i = 0; i < 4; i++) {
                        loading += (i < dots ? "." : " ");
                    }
                    lcd->printLine(2, "Mohon Tunggu... " + loading);
                }
            }

            // Check for cancel key
            char key = keypad->getKey();
            if (key == 'D') {
                logSystemActivity("WIFI_CONNECT", "Cancelled by user");
                wifiManager.cancelConnection();
                WiFi.disconnect(true);
                firstEntry = true;
                currentState = wifiReturnState;
                needRender = true;
            }
        }
        break;

        // ================= WIFI RESULT =================
        case STATE_WIFI_RESULT:
        {
            static bool firstEntry = true;
            static unsigned long resultStartTime = 0;
            static const unsigned long RESULT_DISPLAY_TIME = 2000;  // 2 detik

            if (firstEntry) {
                firstEntry = false;
                resultStartTime = millis();

                bool connected = wifiManager.isConnected();

                lcd->clear();
                if (connected) {
                    lcd->printLine(0, "Koneksi Berhasil!");
                    lcd->printLine(1, "SSID: " + wifiManager.getCurrentSSID());
                    logSystemActivity("WIFI_CONNECT", "SSID: " + wifiManager.getCurrentSSID() + " - Berhasil");
                } else {
                    lcd->printLine(0, "Koneksi Gagal!");
                    lcd->printLine(1, "Coba lagi");
                    logSystemActivity("WIFI_CONNECT", "GAGAL - WiFi gagal konek");
                }
                lcd->printLine(2, "");
                lcd->printLine(3, "");
            }

            // Auto-return setelah 2 detik (tidak perlu menekan tombol)
            if (millis() - resultStartTime > RESULT_DISPLAY_TIME) {
                firstEntry = true;

                // TUGAS: Reset input ketika kembali dari WiFi untuk mencegah timeout menelan input pertama
                if (wifiReturnState == STATE_LOGIN) {
                    // Reset input agar lastInputTime di-reset dan tidak ada timeout yang menelan input pertama
                    input->reset(false);
                    mode = INPUT_NIP;
                }

                currentState = wifiReturnState;
                needRender = true;
            }

            // TUGAS: Jangan discard tombol di sini - biarkan tombol masuk ke state berikutnya
            // Hanya flush jika ada tombol yang pending dan state berikutnya membutuhkannya
            // keypad->getKey();  // DIHAPUS - menyebabkan tombol pertama terbuang saat transisi
        }
        break;

        // ========== TUGAS 2 & 4: PRESENSI FLOW - PILIH KELAS (2-DIGIT INPUT) ==========
        case STATE_PRESENSI_PILIH_KELAS:
        {
            static bool isInitialized = false;
            static bool needsRedraw = true;
            static int currentPage = 0;
            static String classListKode[20];
            static String classListKelas[20];
            static int classCount = 0;
            static int classEnrolled[20];
            static int classTotal[20];
            static String tempInput = "";  // TUGAS 2: Input 2 digit
            static bool inputMode = false;

            if (needRender) {
                isInitialized = false;
                needsRedraw = true;
                currentPage = 0;
                classCount = 0;
                tempInput = "";
                inputMode = false;
                needRender = false;
            }

            if (!isInitialized) {
                lcd->clear();
                lcd->printLine(0, "PRESENSI KELAS ");
                lcd->printLine(1, MSG_LOADING);

                classCount = 0;

                // ========== LOAD KELAS MENGGUNAKAN HELPER FUNCTION ==========
                classCount = loadKelasByNIP(tempNIP, classListKode, classListKelas, 20);

                if (classCount == 0) {
                    lcd->clear();
                    lcd->printLine(0, "TIDAK ADA KELAS");
                    lcd->printLine(1, "TERDAFTAR!");
                    delay(1500);
                    currentState = STATE_MENU_DOSEN;
                    needRender = true;
                    break;
                }

                // ========== HITUNG RATIO KELAS (DENGAN CSV FALLBACK) ==========
                calculateClassRatio(classListKode, classListKelas, classCount, classEnrolled, classTotal);

                // ========== FALLBACK: Hitung dari CSV jika cache miss (sama seperti Registrasi) ==========
                for (int i = 0; i < classCount; i++) {
                    // Jika ratio masih 0/0, hitung dari CSV
                    if (classEnrolled[i] == 0 && classTotal[i] == 0) {
                        String targetKode = classListKode[i];
                        String targetKelas = classListKelas[i];
                        targetKode.trim();
                        targetKelas.trim();

                        if (targetKode.length() > 0 && targetKelas.length() > 0) {
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
                                    int p4 = line.indexOf(',', p3 + 1);
                                    int p5 = line.indexOf(',', p4 + 1);

                                    if (p1 > 0 && p2 > 0 && p3 > 0) {
                                        String kodeKelas = line.substring(p1 + 1, p2);
                                        String kelas = line.substring(p2 + 1, p3);
                                        String statusFp = (p5 > 0) ? line.substring(p4 + 1, p5) : line.substring(p4 + 1);

                                        kodeKelas.trim(); kodeKelas.replace("\r", "");
                                        kelas.trim(); kelas.replace("\r", "");
                                        statusFp.trim(); statusFp.replace("\r", "");

                                        if (kodeKelas == targetKode && kelas == targetKelas) {
                                            classTotal[i]++;
                                            if (statusFp == "true" || statusFp == "1") {
                                                classEnrolled[i]++;
                                            }
                                        }
                                    }
                                }
                                kmFile.close();
                            }
                        }
                    }
                    Serial.printf_P(PSTR("[PRESENSI_RATIO] Kelas[%d]: %s|%s - Enrolled: %d/%d\n"),
                        i, classListKode[i].c_str(), classListKelas[i].c_str(), classEnrolled[i], classTotal[i]);
                }

                Serial.printf("[PILIH_KELAS] Ditemukan %d kelas untuk NIP: %s\n", classCount, tempNIP.c_str());
                isInitialized = true;
                needsRedraw = true;
            }

            // TUGAS 3: Render layar input terpisah jika inputMode aktif
            if (needsRedraw) {
                lcd->clear();

                if (inputMode) {
                    // ========== TUGAS 3: LAYAR MODE INPUT TERPISAH ==========
                    lcd->printLine(0, "MASUKKAN PILIHAN:");
                    lcd->printLine(1, "No: " + tempInput + "_");
                    lcd->printLine(2, "Max: " + String(classCount));
                    lcd->printLine(3, "#:OK  D:Batal");
                } else {
                    // ========== TUGAS 1: FORMAT LIST BIASA ==========
                    int totalPages = (classCount + 1) / 2;
                    if (totalPages == 0) totalPages = 1;
                    if (currentPage >= totalPages) currentPage = totalPages - 1;
                    if (currentPage < 0) currentPage = 0;

                    // Format: "1. EL2001-1"
                    lcd->printLine(0, "PILIH KELAS === ");

                    int idx1 = currentPage * 2;
                    int idx2 = idx1 + 1;

                    if (idx1 < classCount) {
                        // Format: EL1200 (3/43)
                        String display1 = String(idx1 + 1) + ". " + classListKode[idx1];
                        lcd->printLine(1, display1);
                    } else {
                        lcd->printLine(1, "");
                    }

                    if (idx2 < classCount) {
                        // Format: EL1200 (3/43)
                        String display2 = String(idx2 + 1) + ". " + classListKode[idx2];
                        lcd->printLine(2, display2);
                    } else {
                        lcd->printLine(2, "");
                    }

                    // TUGAS 1: Navigasi C:Nxt D:Back/Exit
                    if(currentPage == 1){
                        lcd->printLine(3, "C.Next  D.Exit");
                    }
                    else{
                        lcd->printLine(3, "C.Next  D.Back");
                    }
                }
                needsRedraw = false;
            }

            char key = keypad->getKey();

            // TUGAS 2: Logika input 2 digit
            if (key >= '0' && key <= '9') {
                // Mode input angka
                if (tempInput.length() < 2) {
                    tempInput += key;
                    inputMode = true;
                    needsRedraw = true;
                    Serial.printf("[PILIH_KELAS] Input: %s\n", tempInput.c_str());
                }
            }
            else if (key == '#') {
                // Konfirmasi pilihan dengan angka
                if (tempInput.length() > 0) {
                    int selectedIdx = tempInput.toInt() - 1;
                    if (selectedIdx >= 0 && selectedIdx < classCount) {
                        // ========== TUGAS 3: TRIM SETELAH PENGATURAN ==========
                        presensiKodeMk = classListKode[selectedIdx];
                        presensiKelas = classListKelas[selectedIdx];
                        presensiKodeMk.trim(); presensiKodeMk.replace("\r", "");
                        presensiKelas.trim(); presensiKelas.replace("\r", "");

                        Serial.printf("[PILIH_KELAS] Selected: kodeMk=%s, kelas=%s\n", presensiKodeMk.c_str(), presensiKelas.c_str());

                        logSystemActivity("PRESENSI", "Kelas dipilih: " + presensiKodeMk + "-" + presensiKelas + " (Input: " + tempInput + ")");

                        // ========== DPK: Tarik data mahasiswa kelas terpilih (sebelum PULL_FP) ==========
                        lcd->clear();
                        lcd->printLine(0, "Memuat Data...");
                        lcd->printLine(1, presensiKodeMk + "-" + presensiKelas);
                        if (!apiManager.fetchDataMahasiswa(presensiKodeMk, presensiKelas.toInt())) {
                            Serial.println(F("[PRESENSI] DPK gagal - lanjut dengan data lokal"));
                            lcd->printLine(2, "Pakai data lokal");
                            delay(1000);
                        }

                        listHadirCount = 0;
                        for (int i = 0; i < 120; i++) {
                            listHadir[i] = "";
                            listHadirTime[i] = "";
                        }

                        tempInput = "";
                        inputMode = false;
                        isRetryPull = false;
                        currentState = STATE_PRESENSI_PULL_FP;
                        needRender = true;
                    } else {
                        lcd->clear();
                        lcd->printLine(0, "NOMOR TIDAK VALID!");
                        lcd->printLine(1, "Input: " + tempInput);
                        lcd->printLine(2, "Max: " + String(classCount));
                        delay(1500);
                        tempInput = "";
                        inputMode = false;
                        needsRedraw = true;
                    }
                }
            }
            else if (key == 'A') {
                // TUGAS 3: Hapus angka terakhir saat input mode
                if (inputMode) {
                    if (tempInput.length() > 0) {
                        tempInput.remove(tempInput.length() - 1);
                    } else {
                        inputMode = false;
                    }
                    needsRedraw = true;
                }
            }
            else if (key == 'B') {
                // Reserved
            }
            else if (key == 'C') {
                // ========== TUGAS 2: NAVIGASI ABSOLUT (C = NEXT LOOP) ==========
                if (!inputMode) {
                    int totalPages = (classCount + 1) / 2;
                    if (totalPages == 0) totalPages = 1;
                    if (currentPage < totalPages - 1) {
                        currentPage++;
                    } else {
                        currentPage = 0; // Loop kembali ke halaman 1
                    }
                    needsRedraw = true;
                    Serial.printf("[PILIH_KELAS] Page berubah ke: %d\n", currentPage);
                }
            }
            else if (key == 'D') {
                if (inputMode) {
                    // TUGAS 3: Hapus angka terakhir atau Batal
                    if (tempInput.length() > 0) {
                        tempInput.remove(tempInput.length() - 1);
                    } else {
                        inputMode = false;
                    }
                    needsRedraw = true;
                    Serial.printf("[PILIH_KELAS] Input dibatalkan\n");
                } else if (currentPage > 0) {
                    // ========== TUGAS 2: BACK (D = KEMBALI) ==========
                    currentPage--;
                    needsRedraw = true;
                    Serial.printf("[PILIH_KELAS] Kembali ke page: %d\n", currentPage);
                } else {
                    // ========== TUGAS 2: EXIT (D = KELUAR) ==========
                    logSystemActivity("PRESENSI", "Batal - Kembali ke menu dosen");
                    currentState = STATE_MENU_DOSEN;
                    needRender = true;
                }
            }
        }
        break;

        // ========== TUGAS 3: PRESENSI FLOW - PULL FINGERPRINT DENGAN DEEP LOGGING ==========
        case STATE_PRESENSI_PULL_FP:
        {
            static bool isInitialized = false;
            static bool needsRedraw = true;
            static bool pullStarted = false;
            static int pullIndex = 0;
            static String targetNims[200];
            static int targetCount = 0;
            static bool nimsCollected = false;

            if (needRender) {
                isInitialized = false;
                needsRedraw = true;
                pullStarted = false;
                pullIndex = 0;
                pullSuccess = 0;
                pullFailed = 0;
                targetCount = 0;
                nimsCollected = false;
                for (int i = 0; i < 200; i++) targetNims[i] = "";
                needRender = false;
            }

            // TUGAS 3: Step 1 - Kumpulkan NIM Target dari kelas_mahasiswa.csv
            if (!isInitialized) {
                lcd->clear();
                lcd->printLine(0, "PULL FINGERPRINT");
                lcd->printLine(1, "Sinkronisasi data...");

                Serial.println("[PULL_FP] ============================");
                Serial.printf("[PULL_FP] Mencari NIM untuk kelas: %s-%s\n", presensiKodeMk.c_str(), presensiKelas.c_str());

                // ========== ADAPTIVE: bersihkan sidecar fp_2 (akan diisi ulang per NIM) ==========
                clearFp2Sidecar();

                // ========== TAHAP 1: HANYA SINKRON DATA - JANGAN SENTUH SENSOR R503 ==========

                lcd->clear();
                lcd->printLine(0, "PULL FINGERPRINT");
                lcd->printLine(1, "Sinkrosisasi NIM...");

                // ========== OVERWRITE MODE: SELALU tarik ulang dari server ==========
                // Pengecekan lokal dihapus agar update sidik jari di server selalu
                // tersinkronisasi (data lama akan ditimpa oleh ADD_FP_HEX).

                //Baca kelas_mahasiswa.csv dan kumpulkan NIM dengan status_fingerprint=true
                File kmFile = SD.open("/kelas_mahasiswa.csv", FILE_READ);
                if (kmFile) {
                    if (kmFile.available()) kmFile.readStringUntil('\n'); // Skip header

                    while (kmFile.available() && targetCount < 200) {
                        String line = kmFile.readStringUntil('\n');
                        line.trim();
                        if (line.length() < 5) continue;
                        if (line.startsWith("id,") || line.startsWith("id,kode")) continue;

                        // Asumsi header: id,kode_kelas,kelas,nim,sit_in,status_fingerprint
                        String csvKode = apiManager.getCsvColumn(line, 1);
                        String csvKelas = apiManager.getCsvColumn(line, 2);
                        String csvNim = apiManager.getCsvColumn(line, 3);
                        String csvStatusFp = apiManager.getCsvColumn(line, 5);

                        csvKode.trim(); csvKode.replace("\r", "");
                        csvKelas.trim(); csvKelas.replace("\r", "");
                        csvNim.trim(); csvNim.replace("\r", "");
                        csvStatusFp.trim(); csvStatusFp.replace("\r", "");

                        if (csvKode == presensiKodeMk && csvKelas == presensiKelas && csvStatusFp == "true") {
                            // OVERWRITE MODE: masukkan SEMUA NIM valid tanpa cek lokal
                            // KECUALI jika ini adalah retry (isRetryPull = true), maka lewati yang sudah ada di lokal
                            if (isRetryPull) {
                                if (fingerprintDataManager.hasFingerprint(csvNim)) continue;
                            }
                            
                            targetNims[targetCount] = csvNim;
                            Serial.printf("[PULL_FP] Ditemukan NIM: %s (status_fp=%s)\n", csvNim.c_str(), csvStatusFp.c_str());
                            targetCount++;
                        }
                    }
                    kmFile.close();
                }

                Serial.printf("[PULL_FP] Ditemukan %d mhs dengan status_fingerprint=true\n", targetCount);

                lcd->clear();
                lcd->printLine(0, "Pull FP: " + String(targetCount));

                if (targetCount == 0) {
                    // Tidak ada mahasiswa dengan fingerprint
                    lcd->printLine(1, "Tidak ada FP");
                    lcd->printLine(2, "Lewati pull...");
                    Serial.println("[PULL_FP] Tidak ada mahasiswa dengan fingerprint, skip ke pilih pertemuan");
                    delay(1500);
                    currentState = STATE_PRESENSI_PILIH_PERTEMUAN;
                    needRender = true;
                    break;
                }

                isInitialized = true;
                pullStarted = true;
                pullIndex = 0;
                pullSuccess = 0;
                pullFailed = 0;
                nimsCollected = true;
                needsRedraw = true;
            }

            // ========== BATCH PULL: Paginated - Maksimal 5 NIM per request ==========
            if (pullStarted && nimsCollected && pullIndex < targetCount) {
                const int MAX_PULL_BATCH = 11;  // Menghindari JSON Parse Error: NoMemory pada ESP32

                lcd->clear();
                lcd->printLine(0, "Pull FP (" + String(pullIndex + 1) + "/" + String(targetCount) + ")");

                // Step 1: Build NIM list dibatasi MAX_PULL_BATCH
                String nimList = "";
                int batchCount = 0;
                for (int i = pullIndex; i < targetCount && batchCount < MAX_PULL_BATCH; i++) {
                    if (targetNims[i].length() > 0) {
                        if (nimList.length() > 0) nimList += ",";
                        nimList += targetNims[i];
                        batchCount++;
                    }
                }

                Serial.printf("[BATCH_PULL] Request #%d: %s\n", pullIndex + 1, nimList.c_str());
                lcd->printLine(1, "NIM: " + nimList);

                // Step 2: Panggil API untuk batch ini
                // PENTING: batchDoc HARUS hidup selama seluruh loop pemrosesan di bawah,
                // karena batchResult hanya 'view' ke dalam dokumen ini. Jika dokumen mati
                // sebelum loop selesai -> dangling reference -> LoadProhibited crash.
                JsonDocument batchDoc;  // v7: heap-elastik, hidup selama loop di bawah
                JsonArray batchResult = apiManager.fetchBatchStudentFingerprints(nimList, globalJwtToken, batchDoc);

                // ========== OPTIMASI: Cache nextId sekali per batch, bukan per NIM ==========
                int cachedNextId = apiManager.getNextAutoId("/fingerprint_mahasiswa.csv");

                // ========== FIX: Null-check batchResult ==========
                if (!batchResult.isNull() && batchResult.size() > 0) {
                    Serial.printf("[BATCH_PULL] Berhasil ambil %d fingerprints\n", batchResult.size());

                    // Step 3: Proses setiap fingerprint
                    for (size_t i = 0; i < batchResult.size(); i++) {
                        // ========== FIX: Null-check setiap item ==========
                        if (!batchResult[i].is<JsonObject>()) {
                            Serial.printf("[BATCH_PULL] Item %d bukan objek, skip\n", i);
                            pullFailed++;
                            continue;
                        }

                        JsonObject item = batchResult[i];
                        // Endpoint /api/device/fingerprint-mahasiswa: utamakan fp_1_mhsw,
                        // fallback template_data / fp_mhsw (kompatibilitas skema lama).
                        if (item["nim"].isNull() ||
                            (item["fp_1_mhsw"].isNull() &&
                             item["template_data"].isNull() &&
                             item["fp_mhsw"].isNull())) {
                            Serial.printf("[BATCH_PULL] Item %d missing nim/fp_1_mhsw, skip\n", i);
                            pullFailed++;
                            continue;
                        }

                        const char* nimPtr = item["nim"];
                        const char* hexPtr = item["fp_1_mhsw"];        // skema 2-jari (fp_1)
                        if (!hexPtr) hexPtr = item["template_data"];   // fallback skema enroll
                        if (!hexPtr) hexPtr = item["fp_mhsw"];         // fallback skema lama

                        if (!nimPtr || !hexPtr) {
                            Serial.printf("[BATCH_PULL] Item %d null pointer, skip\n", i);
                            pullFailed++;
                            continue;
                        }

                        String nim = String(nimPtr);

                        if (hexPtr && strlen(hexPtr) > 0) {
                            // ========== FIX: Reserve memori SEBELUM padding ==========
                            String hexTemplate = String(hexPtr);
                            hexTemplate = trimHex(hexTemplate);

                            if (hexTemplate.length() > 0 && hexTemplate.length() < 3072) {
                                hexTemplate.reserve(3072);
                                int padCount = 3072 - hexTemplate.length();
                                for (int p = 0; p < padCount; p++) {
                                    hexTemplate += '0';
                                }
                            }

                            Serial.printf("[BATCH_PULL] Proses NIM: %s, HexLen: %d\n", nim.c_str(), hexTemplate.length());

                            // Ambil timestamp dari API
                            String timestamp = fingerprintDataManager.getTimestamp();
                            if (item["timestamp"].is<const char*>()) {
                                timestamp = item["timestamp"].as<String>();
                            } else if (item["updated_at"].is<const char*>()) {
                                timestamp = item["updated_at"].as<String>();
                            } else if (item["fp_updated_at"].is<const char*>()) {
                                timestamp = item["fp_updated_at"].as<String>();
                            }

                            int nextId = cachedNextId;

                            int saveResult = fingerprintDataManager.addFingerprintWithHex(
                                nextId, nim, "mahasiswa", hexTemplate, timestamp
                            );

                            // Jika file baru ditambah (append), increment cached id
                            if (saveResult == 1) {
                                cachedNextId++;
                            }

                            // ========== JARI 2: hanya simpan jika fp1 benar-benar ditulis ==========
                            if (saveResult > 0) {
                                const char* hex2Ptr = item["fp_2_mhsw"];
                                if (hex2Ptr && strlen(hex2Ptr) > 0) {
                                    String fp2 = trimHex(String(hex2Ptr));
                                    if (fp2.length() > 0 && fp2.length() < 3072) {
                                        fp2.reserve(3072);
                                        while (fp2.length() < 3072) fp2 += '0';
                                    }
                                    if (fp2.length() >= 100) {
                                        appendFp2Sidecar(nim, fp2);
                                        Serial.printf("[BATCH_PULL] fp_2 disimpan: %s (%d char)\n", nim.c_str(), fp2.length());
                                    }
                                    fp2 = "";
                                }
                            }

                            if (saveResult) {
                                pullSuccess++;
                                Serial.printf("[BATCH_PULL] Tersimpan ke SD: %s (id %d)\n", nim.c_str(), nextId);
                            } else {
                                pullFailed++;
                                Serial.printf("[BATCH_PULL] Gagal simpan: %s\n", nim.c_str());
                            }

                            // ========== FIX: Bebaskan memori setelah proses ==========
                            hexTemplate = "";

                            // Update LCD progress
                            lcd->printLine(0, "Pull FP (" + String(pullIndex + 1) + "/" + String(targetCount) + ")");
                            lcd->printLine(1, "NIM: " + nim);
                            lcd->printLine(2, "Sukses: " + String(pullSuccess) + " Gagal: " + String(pullFailed));

                            delay(10);  // Minimal yield untuk watchdog
                        } else {
                            pullFailed++;
                            Serial.printf("[BATCH_PULL] Hex kosong untuk NIM: %s\n", nim.c_str());
                        }
                    }
                } else {
                    pullFailed += batchCount;  // hanya batch ini yang gagal, bukan semua
                    Serial.println("[BATCH_PULL] Gagal ambil data dari API");
                }

                // ========== TAHAP 1: MAJU KE BATCH BERIKUTNYA (multi-batch pagination) ==========
                // JANGAN transisi di sini - biarkan state re-enter sampai semua NIM ditarik.
                pullIndex += batchCount;
                Serial.printf("[BATCH_PULL] Progress: %d/%d (Sukses:%d Gagal:%d)\n",
                    pullIndex, targetCount, pullSuccess, pullFailed);
            }

            // TUGAS 3: Selesai - Semua NIM sudah diproses
            if (pullStarted && pullIndex >= targetCount) {
                pullStarted = false;

                // ========== KEEP-ALIVE: tutup sesi persistent TLS di akhir alur ==========
                // Wajib agar socket TCP/TLS dibebaskan; lazy-init akan reset state
                // bila PULL_FP dimasuki lagi (mis. dari RETRY_PULL > C: Retry).
                apiManager.endBatchPullSession();

                // ========== TUGAS 4: PINDAH KE LAYAR RETRY ==========
                Serial.printf("[PULL_FP] Selesai - Sukses: %d, Gagal: %d\n", pullSuccess, pullFailed);

                currentState = STATE_PRESENSI_RETRY_PULL;
                needRender = true;
            }
        }
        break;

        // ========== TUGAS 2 & 4: PRESENSI FLOW - PILIH PERTEMUAN (2-DIGIT INPUT) ==========
        case STATE_PRESENSI_PILIH_PERTEMUAN:
        {
            static bool isInitialized = false;
            static bool needsRedraw = true;
            static int currentPage = 0;
            static String pertemuanListId[20];
            static String pertemuanListTanggal[20];
            static String pertemuanListKe[20];
            static int pertemuanCount = 0;
            static String tempInput = "";  // TUGAS 2: Input 2 digit
            static bool inputMode = false;

            if (needRender) {
                isInitialized = false;
                needsRedraw = true;
                currentPage = 0;
                pertemuanCount = 0;
                tempInput = "";
                inputMode = false;
                needRender = false;
            }

            if (!isInitialized) {
                lcd->clear();
                lcd->printLine(0, "PILIH PERTEMUAN ");
                lcd->printLine(1, MSG_LOADING);

                pertemuanCount = 0;

                // ========== TUGAS 1: DEBUG FILE I/O ==========
                File file = SD.open("/pertemuan.csv", FILE_READ);
                if (!file) {
                    Serial.println(F("[ERROR] File pertemuan.csv tidak bisa dibuka!"));
                } else {
                    // Hitung total baris dulu
                    int totalLines = 0;
                    String allContent = "";
                    while (file.available()) {
                        String line = file.readStringUntil('\n');
                        allContent += line + "\n";
                        totalLines++;
                    }
                    file.close();
                    Serial.printf_P(PSTR("[DEBUG-FILE] Total baris di pertemuan.csv: %d\n"), totalLines);

                    // Buka lagi untuk parsing
                    file = SD.open("/pertemuan.csv", FILE_READ);
                    int readCount = 0;
                    while (file.available() && pertemuanCount < 20) {
                        String line = file.readStringUntil('\n');
                        readCount++;
                        line.trim();
                        if (line.length() < 5) continue;
                        if (line.startsWith("id,") || line.startsWith("id,kode") || line.startsWith("id,server")) continue;

                        Serial.printf_P(PSTR("[DEBUG-FILE] Membaca baris %d: %s\n"), readCount, line.c_str());

                        // ========== TUGAS 1: FORMAT BARU DENGAN server_jadwal_id ==========
                        // Format: id,server_jadwal_id,kode_kelas,kelas,pertemuan_ke,tanggal
                        int p1 = line.indexOf(',');
                        int p2 = line.indexOf(',', p1 + 1);
                        int p3 = line.indexOf(',', p2 + 1);
                        int p4 = line.indexOf(',', p3 + 1);
                        int p5 = line.indexOf(',', p4 + 1);
                        int p6 = line.indexOf(',', p5 + 1);

                        if (p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0) {
                            // ========== TUGAS 1 & 2: GUNAKAN server_jadwal_id, BUKAN id lokal ==========
                            String id = line.substring(0, p1);
                            String serverJadwalId = line.substring(p1 + 1, p2);  // Kolom ke-2 adalah server_jadwal_id
                            String kodeKelas = line.substring(p2 + 1, p3);
                            String kelas = line.substring(p3 + 1, p4);
                            String pertKe = line.substring(p4 + 1, p5);
                            String tanggal = (p6 > 0) ? line.substring(p5 + 1, p6) : line.substring(p5 + 1);

                            // ========== TUGAS 3: KONSISTENSI DATA - TRIM SEMUA ==========
                            kodeKelas.trim(); kodeKelas.replace("\r", "");
                            kelas.trim(); kelas.replace("\r", "");
                            pertKe.trim(); pertKe.replace("\r", "");
                            tanggal.trim(); tanggal.replace("\r", "");

                            Serial.printf("[PILIH_PERTEMUAN] Parsed: kodeKelas=%s, kelas=%s, target kode=%s, target kelas=%s\n",
                                kodeKelas.c_str(), kelas.c_str(), presensiKodeMk.c_str(), presensiKelas.c_str());

                            if (kodeKelas == presensiKodeMk && kelas == presensiKelas) {
                                // ========== TUGAS 2: SIMPAN server_jadwal_id, BUKAN id lokal ==========
                                pertemuanListId[pertemuanCount] = serverJadwalId;
                                pertemuanListTanggal[pertemuanCount] = tanggal;
                                pertemuanListKe[pertemuanCount] = pertKe;
                                Serial.printf("[PILIH_PERTEMUAN] Match: server_jadwal_id=%s, tanggal=%s\n", serverJadwalId.c_str(), tanggal.c_str());
                                pertemuanCount++;
                            }
                        }
                    }
                    file.close();
                    Serial.printf_P(PSTR("[DEBUG-FILE] Total baris dibaca: %d\n"), readCount);
                }

                Serial.printf("[PILIH_PERTEMUAN] Ditemukan %d pertemuan untuk kelas: %s-%s\n", pertemuanCount, presensiKodeMk.c_str(), presensiKelas.c_str());
                isInitialized = true;
                needsRedraw = true;
            }

            // TUGAS 3: Render layar input terpisah jika inputMode aktif
            if (needsRedraw) {
                lcd->clear();

                if (inputMode) {
                    // ========== TUGAS 3: LAYAR MODE INPUT TERPISAH ==========
                    lcd->printLine(0, "MASUKKAN PILIHAN:");
                    lcd->printLine(1, "No: " + tempInput + "_");
                    lcd->printLine(2, "Max: " + String(pertemuanCount));
                    lcd->printLine(3, "#:OK  D:Batal");
                } else {
                    // ========== TUGAS 1: FORMAT LIST TANPA (Prt X) ==========
                    int totalPages = (pertemuanCount + 1) / 2;
                    if (totalPages == 0) totalPages = 1;
                    if (currentPage >= totalPages) currentPage = totalPages - 1;
                    if (currentPage < 0) currentPage = 0;

                    lcd->printLine(0, "PERTEMUAN ===== ");

                    int idx1 = currentPage * 2;
                    int idx2 = idx1 + 1;

                    if (idx1 < pertemuanCount) {
                        // TUGAS 1: Hapus teks (Prt X) agar tidak overflow
                        String display1 = String(idx1 + 1) + ". " + pertemuanListTanggal[idx1];
                        lcd->printLine(1, display1);
                    } else {
                        lcd->printLine(1, "");
                    }

                    if (idx2 < pertemuanCount) {
                        String display2 = String(idx2 + 1) + ". " + pertemuanListTanggal[idx2];
                        lcd->printLine(2, display2);
                    } else {
                        lcd->printLine(2, "");
                    }
                    
                    // TUGAS 1: Navigasi C:Nxt D:Back/Exit
                    if(currentPage == 1){
                        lcd->printLine(3, "C.Next  D.Exit");
                    }
                    else{
                        lcd->printLine(3, "C.Next  D.Back");
                    }
                }
                needsRedraw = false;
            }

            char key = keypad->getKey();

            // TUGAS 2: Logika input 2 digit
            if (key >= '0' && key <= '9') {
                if (tempInput.length() < 2) {
                    tempInput += key;
                    inputMode = true;
                    needsRedraw = true;
                    Serial.printf("[PILIH_PERTEMUAN] Input: %s\n", tempInput.c_str());
                }
            }
            else if (key == '#') {
                if (tempInput.length() > 0) {
                    int selectedIdx = tempInput.toInt() - 1;
                    if (selectedIdx >= 0 && selectedIdx < pertemuanCount) {
                        presensiPertemuan = pertemuanListKe[selectedIdx];
                        presensiTanggal = pertemuanListTanggal[selectedIdx];
                        presensiJadwalId = pertemuanListId[selectedIdx].toInt();
                        // ========== TUGAS 3: LOGGING VALIDASI ==========
                        Serial.printf_P(PSTR("[PRESENSI] Server Jadwal ID yang akan di-push: %d\n"), presensiJadwalId);
                        logSystemActivity("PRESENSI", "Pertemuan dipilih: " + presensiPertemuan + " - " + presensiTanggal + " (Input: " + tempInput + ")");

                        tempInput = "";
                        inputMode = false;
                        // TAHAP 2: siapkan sensor (flush+inject) SEBELUM otorisasi dosen awal
                        currentState = STATE_PRESENSI_INJECT_SENSOR;
                        needRender = true;
                    } else {
                        lcd->clear();
                        lcd->printLine(0, "NOMOR TIDAK VALID!");
                        lcd->printLine(1, "Input: " + tempInput);
                        lcd->printLine(2, "Max: " + String(pertemuanCount));
                        delay(1500);
                        tempInput = "";
                        inputMode = false;
                        needsRedraw = true;
                    }
                }
            }
            else if (key == 'A') {
                // TUGAS 3: Hapus angka terakhir saat input mode
                if (inputMode) {
                    if (tempInput.length() > 0) {
                        tempInput.remove(tempInput.length() - 1);
                    } else {
                        inputMode = false;
                    }
                    needsRedraw = true;
                }
            }
            else if (key == 'B') {
                // Reserved
            }
            else if (key == 'C') {
                // ========== TUGAS 2: NAVIGASI ABSOLUT (C = NEXT LOOP) ==========
                if (!inputMode) {
                    int totalPages = (pertemuanCount + 1) / 2;
                    if (totalPages == 0) totalPages = 1;
                    if (currentPage < totalPages - 1) {
                        currentPage++;
                    } else {
                        currentPage = 0; // Loop kembali ke halaman 1
                    }
                    needsRedraw = true;
                    Serial.printf("[PILIH_PERTEMUAN] Page berubah ke: %d\n", currentPage);
                }
            }
            else if (key == 'D') {
                if (inputMode) {
                    // TUGAS 3: Hapus angka terakhir atau Batal
                    if (tempInput.length() > 0) {
                        tempInput.remove(tempInput.length() - 1);
                    } else {
                        inputMode = false;
                    }
                    needsRedraw = true;
                    Serial.printf("[PILIH_PERTEMUAN] Input dibatalkan\n");
                } else if (currentPage > 0) {
                    // ========== TUGAS 2: BACK (D = KEMBALI) ==========
                    currentPage--;
                    needsRedraw = true;
                    Serial.printf("[PILIH_PERTEMUAN] Kembali ke page: %d\n", currentPage);
                } else {
                    // ========== TUGAS 2: EXIT (D = KELUAR) ==========
                    logSystemActivity("PRESENSI", "Batal - Kembali ke menu dosen");
                    currentState = STATE_MENU_DOSEN;
                    needRender = true;
                }
            }
        }
        break;

        // ========== TUGAS 4: PRESENSI FLOW - RETRY PULL FINGERPRINT ==========
        case STATE_PRESENSI_RETRY_PULL:
        {
            static bool needsRedraw = true;
            static int retrySuccess = 0;
            static int retryFailed = 0;

            if (needRender) {
                needsRedraw = true;
                // Ambil nilai dari variabel static di PULL_FP
                retrySuccess = pullSuccess;
                retryFailed = pullFailed;
                needRender = false;
            }

            if (needsRedraw) {
                lcd->clear();
                lcd->printLine(0, "PULL FP SELESAI");
                lcd->printLine(1, "Sukses: " + String(retrySuccess));
                lcd->printLine(2, "Gagal: " + String(retryFailed));

                if (retryFailed > 0) {
                    lcd->printLine(3, "C.Retry  D.Lanjut");
                } else {
                    lcd->printLine(3, "D.Mulai Presensi");
                }
                needsRedraw = false;
            }

            char key = keypad->getKey();

            // ========== TUGAS 4: LOGIKA RETRY ==========
            if (key == 'C' && retryFailed > 0) {
                // Retry - kembali ke STATE_PRESENSI_PULL_FP
                Serial.println("[RETRY_PULL] Melakukan retry pull fingerprint...");
                isRetryPull = true;
                currentState = STATE_PRESENSI_PULL_FP;
                needRender = true;
            }
            else if (key == 'D') {
                // Lanjut ke pilih pertemuan
                logSystemActivity("PRESENSI", "Pull FP selesai - Sukses: " + String(retrySuccess) + ", Gagal: " + String(retryFailed));
                currentState = STATE_PRESENSI_PILIH_PERTEMUAN;
                needRender = true;
            }
        }
        break;

        // ========== TAHAP 2: PRESENSI FLOW - INJECT SENSOR (sebelum Otorisasi Awal) ==========
        case STATE_PRESENSI_INJECT_SENSOR:
        {
            // Jalankan tepat SEBELUM layar Otorisasi Dosen Awal agar sidik jari dosen
            // (slot 1-2) sudah masuk sensor saat ia diminta menempelkan jari.
            lcd->clear();
            lcd->printLine(0, F("MEMULAI PRESENSI"));
            lcd->printLine(1, F("Menyiapkan Sensor..."));
            lcd->printLine(2, F("Mohon tunggu"));

            // Urutan ketat: Empty Library -> Inject Dosen (1-2) -> Inject Mahasiswa (3 dst)
            bool prepResult = fingerprintManager.flushAndInjectPresensiUsers(presensiKodeMk, presensiKelas);

            if (prepResult) {
                Serial.println(F("[PRESENSI] Sensor preparation SUCCESS"));
            } else {
                Serial.println(F("[PRESENSI] Sensor preparation FAILED - lanjut tetap"));
                lcd->printLine(2, F("Inject gagal sebagian"));
                delay(1000);
            }

            // Lanjut ke otorisasi dosen awal (sensor sudah siap)
            currentState = STATE_PRESENSI_AUTH_START;
            needRender = true;
        }
        break;

        // ========== TUGAS 5: PRESENSI FLOW - AUTH START ==========
        case STATE_PRESENSI_AUTH_START:
        {
            static bool needsRedraw = true;
            static bool authCompleted = false;
            static String inputPinOtorisasi = "";  // BACKUP: buffer PIN dosen

            if (needRender) {
                needsRedraw = true;
                authCompleted = false;
                inputPinOtorisasi = "";
                needRender = false;
            }

            if (needsRedraw) {
                lcd->clear();
                lcd->printLine(0, F("KONFIRMASI PRESENSI"));
                lcd->printLine(1, F("PIN/Fingerprint:"));
                lcd->printLine(2, "_");  // Baris PIN (mask *)
                lcd->printLine(3, F("#.Enter D.Batal"));
                needsRedraw = false;

                fingerprintManager.setLEDScanning();
            }

            char key = keypad->getKey();

            if (key == 'D') {
                fingerprintManager.setLEDOff();
                logSystemActivity("PRESENSI", "Batal otorisasi");
                currentState = STATE_MENU_DOSEN;
                needRender = true;
                break;
            }

            bool authGranted = false;  // di-set true oleh FP cocok ATAU PIN cocok

            // ========== BACKUP: OTORISASI VIA PIN KEYPAD ==========
            if (key >= '0' && key <= '9') {
                if (inputPinOtorisasi.length() < 6) {
                    inputPinOtorisasi += key;
                    // Tutup PIN dengan bintang di Baris 2
                    String mask = "";
                    for (uint8_t i = 0; i < inputPinOtorisasi.length(); i++) mask += '*';
                    lcd->printLine(2, mask);
                }
            }
            else if (key == '*') {
                // Hapus 1 digit (backspace)
                if (inputPinOtorisasi.length() > 0) {
                    inputPinOtorisasi.remove(inputPinOtorisasi.length() - 1);
                    String mask = "";
                    for (uint8_t i = 0; i < inputPinOtorisasi.length(); i++) mask += '*';
                    lcd->printLine(2, mask);
                }
            }
            else if (key == '#') {
                // Validasi PIN dengan dosen yang sedang login
                if (inputPinOtorisasi.length() > 0 && userManager.checkUser(tempNIP, inputPinOtorisasi)) {
                    Serial.println(F("[PRESENSI_AUTH] Otorisasi PIN Berhasil"));
                    authGranted = true;
                } else {
                    fingerprintManager.setLEDError();
                    lcd->clear();
                    lcd->printLine(0, F("PIN SALAH"));
                    delay(1500);
                    fingerprintManager.setLEDOff();
                    inputPinOtorisasi = "";
                    needsRedraw = true;  // kembali ke layar Otorisasi
                }
            }

            // ========== UTAMA: OTORISASI VIA FINGERPRINT ==========
            static unsigned long lastFingerPoll = 0;
            if (!authGranted && millis() - lastFingerPoll > 250) {
                lastFingerPoll = millis();
                int fingerId = fingerprintManager.checkFingerprint();

                if (fingerId > 0) {
                    // Check if this is the lecturer's fingerprint
                    String userId = fingerMap[fingerId];
                    userId.trim();

                    if (userId == tempNIP) {
                        authGranted = true;
                    } else {
                        fingerprintManager.setLEDError();
                        lcd->clear();
                        lcd->printLine(0, F("OTORISASI GAGAL"));
                        // lcd->printLine(1, F("Bukan dosen ini!"));
                        delay(1500);
                        fingerprintManager.setLEDOff();
                        needsRedraw = true;
                    }
                } else if (fingerId == -2) {
                    fingerprintManager.setLEDError();
                    lcd->clear();
                    lcd->printLine(0, F("TDK DIKENALI"));
                    delay(1000);
                    fingerprintManager.setLEDOff();
                    needsRedraw = true;
                }
            }

            // ========== SUKSES (FP atau PIN): persiapan sensor lalu lanjut ==========
            if (authGranted) {
                inputPinOtorisasi = "";
                fingerprintManager.setLEDSuccess();
                lcd->clear();
                lcd->printLine(0, F("OTORISASI BERHASIL"));
                lcd->printLine(1, "Dosen: ");
                lcd->printLine(2, tempNIP);
                delay(1500);
                fingerprintManager.setLEDOff();

                authCompleted = true;
                logSystemActivity("PRESENSI_AUTH", "Otorisasi berhasil: " + tempNIP);

                // Sensor sudah disiapkan di STATE_PRESENSI_INJECT_SENSOR -> langsung scanning
                currentState = STATE_PRESENSI_SCANNING;
                needRender = true;
            }
        }
        break;

        // ========== TUGAS 5: PRESENSI FLOW - SCANNING ==========
        case STATE_PRESENSI_SCANNING:
        {
            static bool needsRedraw = true;
            static unsigned long lastFingerPoll = 0;
            static unsigned long lastLCDUpdate = 0;

            if (needRender) {
                needsRedraw = true;
                lastLCDUpdate = millis();
                needRender = false;
            }

            if (needsRedraw) {
                lcd->clear();
                lcd->printLine(0, "PRESENSI MULAI");
                lcd->printLine(1, "Tempelkan Jari...");
                lcd->printLine(2, "Hadir: " + String(listHadirCount));
                // Opsi 'C.Rusak' hanya muncul di mode adaptif (kelas > 99, fp_2 belum diinjeksi).
                // Kelas <= 99: fp_1 + fp_2 sudah di sensor -> tidak perlu menu cadangan.
                if (isAdaptiveMode) {
                    lcd->printLine(3, F("C.Rusak D.Selesai"));
                } else {
                    lcd->printLine(3, F("D.Selesai"));
                }
                needsRedraw = false;

                fingerprintManager.setLEDScanning();
            }

            char key = keypad->getKey();

            // Tombol D = tutup sesi presensi
            if (key == 'D') {
                fingerprintManager.setLEDOff();
                logSystemActivity("PRESENSI", "Selesai scan - Hadir: " + String(listHadirCount));
                currentState = STATE_PRESENSI_AUTH_END;
                needRender = true;
                break;
            }

            // ========== BAGIAN 2: PRESENSI CADANGAN (hanya mode adaptif) ==========
            // Tombol C aktif HANYA bila isAdaptiveMode (kelas > 99). Di kelas <=99
            // fp_2 sudah di sensor, jadi C diabaikan.
            if (key == 'C' && isAdaptiveMode) {
                fingerprintManager.setLEDOff();
                logSystemActivity("PRESENSI", "Masuk mode cadangan (jari rusak)");
                currentState = STATE_PRESENSI_CADANGAN;
                needRender = true;
                break;
            }

            // Poll fingerprint every 250ms
            if (millis() - lastFingerPoll > 250) {
                lastFingerPoll = millis();
                int fingerId = fingerprintManager.checkFingerprint();

                if (fingerId > 0) {
                    // Resolusi NIM via fingerMap (slot -> NIM) yang diisi saat INJECT_SENSOR.
                    // Mahasiswa berada di slot 3 dst; slot 1-2 milik dosen -> abaikan.
                    String matchedNIM = "";
                    if (fingerId >= 3) {
                        matchedNIM = fingerMap[fingerId];
                        matchedNIM.trim();
                        matchedNIM.replace("\r", "");
                    }

                    if (matchedNIM.length() > 0) {
                        // Check if already in listHadir
                        bool alreadyPresent = false;
                        for (int i = 0; i < listHadirCount; i++) {
                            if (listHadir[i] == matchedNIM) {
                                alreadyPresent = true;
                                break;
                            }
                        }

                        if (!alreadyPresent && listHadirCount < 120) {
                            // Add to listHadir
                            listHadir[listHadirCount] = matchedNIM;
                            listHadirTime[listHadirCount] = fingerprintDataManager.getTimestamp();
                            listHadirCount++;

                            // LED feedback - BLUE/GREEN flash
                            fingerprintManager.setLEDSuccess();
                            lcd->clear();
                            lcd->printLine(0, "HADIR!");
                            lcd->printLine(1, "NIM: " + matchedNIM);
                            // lcd->printLine(2, "Hadir: " + String(listHadirCount));
                            lcd->printLine(3, MSG_SELESAI);
                            delay(1500);
                            needsRedraw = true;

                            logSystemActivity("PRESENSI_SCAN", "Hadir: " + matchedNIM);
                        } else if (alreadyPresent) {
                            // Already present
                            lcd->clear();
                            lcd->printLine(0, "SUDAH ABSEN");
                            lcd->printLine(1, "NIM: " + matchedNIM);
                            delay(1500);
                            needsRedraw = true;
                        }
                    } else {
                        // Fingerprint not found in mahasiswa
                        fingerprintManager.setLEDError();
                        lcd->clear();
                        lcd->printLine(0, "TDK DIKENALI");
                        // lcd->printLine(1, "Slot: " + String(fingerId));
                        delay(1500);
                        needsRedraw = true;
                    }
                } else if (fingerId == -2) {
                    // Not matched
                    fingerprintManager.setLEDError();
                    needsRedraw = true;
                }
            }
        }
        break;

        // ========== BAGIAN 3: PRESENSI CADANGAN (JARI RUSAK -> fp_2 ON-DEMAND) ==========
        case STATE_PRESENSI_CADANGAN:
        {
            // Sub-fase: 0 = input NIM, 1 = scan & match
            // IN-PLACE SWAP: fp_2 ditimpa ke slot fp_1 milik mahasiswa itu sendiri
            // (dicari via fingerMap). Slot 199 hanya fallback bila NIM tak ada di sensor.
            static int  cadPhase = 0;
            static String nimBuffer = "";
            static int  cadSlot = -1;          // slot tujuan inject fp_2 (dinamis)
            static unsigned long lastPoll = 0;
            static bool needsRedraw = true;
            const int CAD_FALLBACK_SLOT = 199; // dipakai HANYA jika NIM tak ditemukan di fingerMap

            if (needRender) {
                cadPhase = 0;
                nimBuffer = "";
                cadSlot = -1;
                needsRedraw = true;
                needRender = false;
                // Bersihkan slot fallback 199 jaga-jaga (sisa sesi sebelumnya)
                fingerprintManager.getFinger()->delete_model(CAD_FALLBACK_SLOT);
            }

            // ---------- FASE 0: INPUT NIM ----------
            if (cadPhase == 0) {
                if (needsRedraw) {
                    lcd->clear();
                    lcd->printLine(0, F("PRESENSI CADANGAN"));
                    lcd->printLine(1, F("Input NIM:"));
                    lcd->printLine(2, nimBuffer.length() ? nimBuffer : String("_"));
                    lcd->printLine(3, F("#.OK  D.Batal"));
                    needsRedraw = false;
                }

                char key = keypad->getKey();
                if (key >= '0' && key <= '9') {
                    if (nimBuffer.length() < 12) nimBuffer += key;
                    needsRedraw = true;
                }
                else if (key == 'A') {           // backspace
                    if (nimBuffer.length() > 0) nimBuffer.remove(nimBuffer.length() - 1);
                    needsRedraw = true;
                }
                else if (key == 'D') {           // batal -> kembali ke scanning normal
                    currentState = STATE_PRESENSI_SCANNING;
                    needRender = true;
                    break;
                }
                else if (key == '#') {
                    if (nimBuffer.length() < 3) {
                        lcd->clear();
                        lcd->printLine(0, F("NIM tidak valid"));
                        delay(1200);
                        needsRedraw = true;
                        break;
                    }

                    // ----- Ekstrak fp_2 dari sidecar -----
                    lcd->clear();
                    lcd->printLine(0, F("Memuat fp_2..."));
                    lcd->printLine(1, "NIM: " + nimBuffer);
                    String fp2 = readFp2Sidecar(nimBuffer);

                    if (fp2.length() < 1000) {
                        lcd->clear();
                        lcd->printLine(0, F("fp_2 TIDAK ADA"));
                        lcd->printLine(1, "NIM: " + nimBuffer);
                        lcd->printLine(2, F("Cek registrasi 2 jari"));
                        logSystemActivity("PRESENSI_CADANGAN", "fp_2 tidak ada: " + nimBuffer);
                        delay(2000);
                        nimBuffer = "";
                        needsRedraw = true;
                        break;
                    }

                    // ----- IN-PLACE SWAP: cari slot fp_1 milik NIM ini di fingerMap -----
                    int targetSlot = -1;
                    for (int i = 1; i <= 200; i++) {
                        if (fingerMap[i] == nimBuffer) { targetSlot = i; break; }
                    }
                    // Slot resmi mahasiswa ditemukan -> timpa di tempat. Else fallback 199.
                    cadSlot = (targetSlot != -1) ? targetSlot : CAD_FALLBACK_SLOT;

                    Serial.printf_P(PSTR("[CADANGAN] NIM %s -> inject fp_2 ke slot %d (%s)\n"),
                        nimBuffer.c_str(), cadSlot,
                        (targetSlot != -1) ? "in-place" : "fallback-199");

                    lcd->printLine(2, "Inject slot " + String(cadSlot) + "...");
                    bool injOk = fingerprintManager.injectSingleFingerprint(nimBuffer, fp2, cadSlot);
                    fp2 = "";  // bebaskan heap

                    if (!injOk) {
                        // Hanya bersihkan bila pakai slot fallback (slot resmi dibiarkan,
                        // akan di-inject ulang saat kelas berikutnya / re-pull).
                        if (cadSlot == CAD_FALLBACK_SLOT) {
                            fingerprintManager.getFinger()->delete_model(CAD_FALLBACK_SLOT);
                        }
                        lcd->clear();
                        lcd->printLine(0, F("Inject GAGAL"));
                        delay(1800);
                        nimBuffer = "";
                        cadSlot = -1;
                        needsRedraw = true;
                        break;
                    }

                    // Lanjut ke fase scan
                    cadPhase = 1;
                    needsRedraw = true;
                    lastPoll = millis();
                    fingerprintManager.setLEDScanning();
                }
            }

            // ---------- FASE 1: SCAN & MATCH (terhadap slot 199) ----------
            else if (cadPhase == 1) {
                if (needsRedraw) {
                    lcd->clear();
                    lcd->printLine(0, F("CADANGAN: SCAN"));
                    lcd->printLine(1, "NIM: " + nimBuffer);
                    lcd->printLine(2, F("Tempelkan jari ke-2"));
                    lcd->printLine(3, F("D.Batal"));
                    needsRedraw = false;
                }

                char key = keypad->getKey();
                if (key == 'D') {
                    // Batal -> hapus HANYA jika pakai slot fallback (in-place dibiarkan)
                    if (cadSlot == CAD_FALLBACK_SLOT) {
                        fingerprintManager.getFinger()->delete_model(CAD_FALLBACK_SLOT);
                    }
                    fingerprintManager.setLEDOff();
                    currentState = STATE_PRESENSI_SCANNING;
                    needRender = true;
                    break;
                }

                if (millis() - lastPoll > 250) {
                    lastPoll = millis();
                    int fid = fingerprintManager.checkFingerprint();

                    if (fid > 0) {
                        // Cocok jika hardware match slot tujuan (cadSlot) ATAU fingerMap-nya = NIM ini
                        String matchedNim = (fid == cadSlot) ? nimBuffer : fingerMap[fid];
                        matchedNim.trim();

                        if (matchedNim == nimBuffer) {
                            // ----- SUKSES: catat ke listHadir (dedup) sama spt presensi normal -----
                            bool already = false;
                            for (int i = 0; i < listHadirCount; i++)
                                if (listHadir[i] == nimBuffer) { already = true; break; }

                            if (!already && listHadirCount < 120) {
                                listHadirTime[listHadirCount] = fingerprintDataManager.getTimestamp();
                                listHadir[listHadirCount++] = nimBuffer;
                                logSystemActivity("PRESENSI_CADANGAN", "Hadir (fp_2) slot " + String(cadSlot) + ": " + nimBuffer);
                            }

                            fingerprintManager.setLEDSuccess();
                            lcd->clear();
                            lcd->printLine(0, already ? F("SUDAH ABSEN") : F("HADIR! (fp_2)"));
                            lcd->printLine(1, "NIM: " + nimBuffer);
                            delay(1800);
                        } else {
                            fingerprintManager.setLEDError();
                            lcd->clear();
                            lcd->printLine(0, F("JARI TIDAK COCOK"));
                            lcd->printLine(1, F("dgn NIM tsb"));
                            delay(1800);
                        }

                        // ----- IN-PLACE: JANGAN hapus slot resmi mahasiswa. fp_2 dibiarkan di
                        // sensor sampai kelas diakhiri. Hanya slot fallback 199 yang dibersihkan. -----
                        if (cadSlot == CAD_FALLBACK_SLOT) {
                            fingerprintManager.getFinger()->delete_model(CAD_FALLBACK_SLOT);
                        }
                        fingerprintManager.setLEDOff();
                        currentState = STATE_PRESENSI_SCANNING;
                        needRender = true;
                        break;
                    } else if (fid == -2) {
                        fingerprintManager.setLEDError();  // tidak dikenali, tetap di fase scan
                    }
                }
            }
        }
        break;

        // ========== TUGAS 5: PRESENSI FLOW - AUTH END ==========
        case STATE_PRESENSI_AUTH_END:
        {
            static bool needsRedraw = true;
            static bool authCompleted = false;
            static String inputPinTutup = "";  // BACKUP: buffer PIN dosen (tutup sesi)

            if (needRender) {
                needsRedraw = true;
                authCompleted = false;
                inputPinTutup = "";
                needRender = false;
            }

            if (needsRedraw) {
                lcd->clear();
                lcd->printLine(0, F("TUTUP SESI"));
                lcd->printLine(1, F("PIN/Fingerprint:"));
                lcd->printLine(2, "_");  // Baris PIN (mask *)
                lcd->printLine(3, F("#.Enter D.Batal"));
                needsRedraw = false;

                fingerprintManager.setLEDScanning();
            }

            char key = keypad->getKey();

            if (key == 'D') {
                fingerprintManager.setLEDOff();
                logSystemActivity("PRESENSI", "Batal tutup sesi");
                currentState = STATE_MENU_DOSEN;
                needRender = true;
                break;
            }

            bool authGranted = false;  // di-set true oleh FP cocok ATAU PIN cocok

            // ========== BACKUP: OTORISASI VIA PIN KEYPAD ==========
            if (key >= '0' && key <= '9') {
                if (inputPinTutup.length() < 6) {
                    inputPinTutup += key;
                    String mask = "";
                    for (uint8_t i = 0; i < inputPinTutup.length(); i++) mask += '*';
                    lcd->printLine(2, mask);
                }
            }
            else if (key == '*') {
                if (inputPinTutup.length() > 0) {
                    inputPinTutup.remove(inputPinTutup.length() - 1);
                    String mask = "";
                    for (uint8_t i = 0; i < inputPinTutup.length(); i++) mask += '*';
                    lcd->printLine(2, mask);
                }
            }
            else if (key == '#') {
                if (inputPinTutup.length() > 0 && userManager.checkUser(tempNIP, inputPinTutup)) {
                    Serial.println(F("[PRESENSI_AUTH_END] Otorisasi PIN Berhasil"));
                    authGranted = true;
                } else {
                    fingerprintManager.setLEDError();
                    lcd->clear();
                    lcd->printLine(0, F("PIN SALAH"));
                    delay(1500);
                    fingerprintManager.setLEDOff();
                    inputPinTutup = "";
                    needsRedraw = true;
                }
            }

            // ========== UTAMA: OTORISASI VIA FINGERPRINT ==========
            static unsigned long lastFingerPoll = 0;
            if (!authGranted && millis() - lastFingerPoll > 250) {
                lastFingerPoll = millis();
                int fingerId = fingerprintManager.checkFingerprint();

                if (fingerId > 0) {
                    String userId = fingerMap[fingerId];
                    userId.trim();

                    if (userId == tempNIP) {
                        authGranted = true;
                    } else {
                        fingerprintManager.setLEDError();
                        lcd->clear();
                        lcd->printLine(0, F("GAGAL"));
                        // lcd->printLine(1, F("Bukan dosen ini!"));
                        delay(1500);
                        fingerprintManager.setLEDOff();
                        needsRedraw = true;
                    }
                } else if (fingerId == -2) {
                    fingerprintManager.setLEDError();
                    lcd->clear();
                    lcd->printLine(0, F("TDK DIKENALI"));
                    delay(1000);
                    fingerprintManager.setLEDOff();
                    needsRedraw = true;
                }
            }

            // ========== SUKSES (FP atau PIN): tutup sesi lalu simpan ==========
            if (authGranted) {
                inputPinTutup = "";
                fingerprintManager.setLEDSuccess();
                lcd->clear();
                lcd->printLine(0, F("SESI DITUTUP"));
                lcd->printLine(1, "Total Hadir: " + String(listHadirCount));
                delay(1500);
                fingerprintManager.setLEDOff();

                authCompleted = true;
                logSystemActivity("PRESENSI_AUTH_END", "Sesi ditutup oleh: " + tempNIP);

                currentState = STATE_PRESENSI_SAVE;
                needRender = true;
            }
        }
        break;

        // ========== TUGAS 5: PRESENSI FLOW - SAVE ==========
        case STATE_PRESENSI_SAVE:
        {
            static bool needsRedraw = true;
            static bool saveCompleted = false;
            static bool pushCompleted = false;
            static int savedCount = 0;
            static bool pushSuccess = false;
            static bool inRetryLoop = false;
            static bool showingSuccess = false;

            if (needRender) {
                needsRedraw = true;
                saveCompleted = false;
                pushCompleted = false;
                savedCount = 0;
                pushSuccess = false;
                inRetryLoop = false;
                showingSuccess = false;
                needRender = false;
            }

            // ========== TUGAS 1: SIMPAN KE SD CARD ==========
            if (!saveCompleted) {
                lcd->clear();
                lcd->printLine(0, "MENYIMPAN DATA...");
                lcd->printLine(1, "Presensi: " + presensiKodeMk + "-" + presensiKelas);
                lcd->printLine(2, "Pertemuan: " + presensiPertemuan);

                // Get timestamp
                String timestamp = apiManager.isTimeSynced() ? getFormattedTime() : "WAKTU_BELUM_SINKRON";
                if (timestamp == "WAKTU_BELUM_SINKRON") {
                    timestamp = "PRESENSI";
                }

                // Generate new presensi_id
                int nextPresensiId = apiManager.getNextAutoId("/presensi.csv");

                // Save each student to presensi.csv
                // Format: id,nim,kode_kelas,pertemuan_id,waktu,status
                File presensiFile = SD.open("/presensi.csv", FILE_APPEND);
                if (presensiFile) {
                    for (int i = 0; i < listHadirCount; i++) {
                        // Combine kode_kelas and kelas for kode_kelas field
                        String fullKodeKelas = presensiKodeMk + "-" + presensiKelas;
                        presensiFile.printf("%d,%s,%s,%d,%s,hadir\n",
                            nextPresensiId + i,
                            listHadir[i].c_str(),
                            fullKodeKelas.c_str(),
                            presensiJadwalId,
                            timestamp.c_str()
                        );
                        savedCount++;
                    }
                    presensiFile.close();
                } else {
                    Serial.println("[PRESENSI_SAVE] ERROR - Gagal membuka presensi.csv");
                }

                saveCompleted = true;

                logSystemActivity("PRESENSI_SAVE", "Disimpan: " + String(savedCount) + " mahasiswa");
                needsRedraw = false;
            }

            // ========== TUGAS 1: KIRIM KE SERVER SETELAH SIMPAN ==========
            if (saveCompleted && !pushCompleted) {
                lcd->clear();
                lcd->printLine(0, "MENGIRIM DATA...");
                lcd->printLine(1, "Mohon Tunggu");

                // Try to push to server
                pushSuccess = pushPresensiToAPI(presensiJadwalId, listHadir, listHadirTime, listHadirCount, presensiKodeMk, presensiKelas.toInt());

                pushCompleted = true;

                if (pushSuccess) {
                    showingSuccess = true;
                    lcd->clear();
                    lcd->printLine(0, "BERHASIL!");
                    lcd->printLine(1, "Data Terkirim");
                    lcd->printLine(3, "D.Menu");
                } else {
                    // ========== TUGAS 1 & 2: TAMPILKAN LAYAR RETRY ==========
                    inRetryLoop = true;
                    lcd->clear();
                    lcd->printLine(0, "GAGAL KIRIM!");
                    lcd->printLine(1, "Tersimpan di SD Card");
                    lcd->printLine(2, "B.Ulangi  D.Selesai");
                }
            }

            // ========== TUGAS 2: LOOP RETRY ==========
            if (inRetryLoop && !showingSuccess) {
                char key = keypad->getKey();

                if (key == 'B') {
                    // ========== TUGAS 2: COBA LAGI PUSH ==========
                    Serial.println("[PRESENSI_SAVE] Retrying push to server...");
                    lcd->clear();
                    lcd->printLine(0, "MENGIRIM ULANG...");
                    lcd->printLine(1, "Mohon Tunggu");

                    // Try push again
                    pushSuccess = pushPresensiToAPI(presensiJadwalId, listHadir, listHadirTime, listHadirCount, presensiKodeMk, presensiKelas.toInt());

                    if (pushSuccess) {
                        // ========== TUGAS 3: BERHASIL - TIDAK PERLU CLEAR ==========
                        showingSuccess = true;
                        inRetryLoop = false;
                        lcd->clear();
                        lcd->printLine(0, "BERHASIL!");
                        lcd->printLine(1, "Data Terkirim");
                        lcd->printLine(3, "D.Menu");
                        Serial.println("[PRESENSI_SAVE] Retry berhasil!");
                    } else {
                        // ========== TUGAS 2: GAGAL LAGI - TETAP DI LAYAR RETRY ==========
                        lcd->clear();
                        lcd->printLine(0, "GAGAL KIRIM!");
                        lcd->printLine(1, "Tersimpan di SD Card");
                        lcd->printLine(2, "B.Ulangi  D.Selesai");
                        Serial.println("[PRESENSI_SAVE] Retry gagal, tetap di layar retry");
                    }
                } else if (key == 'D') {
                    // ========== TUGAS 3: DOSEN MEMILIH SELESAI - CLEAR DATA ==========
                    inRetryLoop = false;
                    showingSuccess = false;
                    // ListHadir akan di-clear di handler key 'D' di bawah
                }
            }

            // ========== TUGAS 2: HANDLE KEY D (EXIT) ==========
            char key = keypad->getKey();

            if (key == 'D') {
                // ========== TUGAS 3: RESET PRESENSI SESSION - CLEAR ALL DATA ==========
                // HANYA clear jika:
                // 1. Push berhasil (showingSuccess=true), ATAU
                // 2. Dosen memilih menyerah dengan menekan D (inRetryLoop=false ATAU pushCompleted tapi tidak success)
                Serial.println("[PRESENSI_SAVE] Menutup sesi presensi");

                presensiKodeMk = "";
                presensiKelas = "";
                presensiPertemuan = "";
                presensiTanggal = "";
                presensiJadwalId = 0;
                listHadirCount = 0;
                for (int i = 0; i < 120; i++) {
                    listHadir[i] = "";
                    listHadirTime[i] = "";
                }

                currentState = STATE_MENU_DOSEN;
                needRender = true;
            }
        }
        break;

        // ========== TUGAS 2: PUSH FINGERPRINT MANUAL ==========
        case STATE_PUSH_FP_MANUAL:
        {
            static bool needsRedraw = true;
            static bool processStarted = false;
            static bool processCompleted = false;
            static bool pushSuccess = false;
            static int totalCount = 0;
            static int currentIndex = 0;
            static String lastError = "";

            if (needRender) {
                needsRedraw = true;
                processStarted = false;
                processCompleted = false;
                pushSuccess = false;
                totalCount = 0;
                currentIndex = 0;
                lastError = "";
                needRender = false;
            }

            // TUGAS 3:Baca fingerprint_mahasiswa.csv dan push ke API
            if (!processStarted && !processCompleted) {
                lcd->clear();
                lcd->printLine(0, "PUSH FINGERPRINT");
                lcd->printLine(1, "Membaca data...");

                // Cek apakah WiFi terhubung
                if (WiFi.status() != WL_CONNECTED) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "WiFi tidak aktif!");
                    lcd->printLine(2, "Tekan D untuk Menu");
                    processCompleted = true;
                    lastError = "WiFi not connected";
                    needsRedraw = false;
                    break;
                }

                // Cek apakah file ada
                if (!SD.exists("/fingerprint_mahasiswa.csv")) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "File tidak ada!");
                    lcd->printLine(2, "Tekan D untuk Menu");
                    processCompleted = true;
                    lastError = "File not found";
                    needsRedraw = false;
                    break;
                }

                // Buka file dan hitung jumlah data
                File fpFile = SD.open("/fingerprint_mahasiswa.csv", FILE_READ);
                if (!fpFile) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "Gagal buka file!");
                    lcd->printLine(2, "Tekan D untuk Menu");
                    processCompleted = true;
                    lastError = "Cannot open file";
                    needsRedraw = false;
                    break;
                }

                // Skip header
                if (fpFile.available()) fpFile.readStringUntil('\n');

                // Hitung total baris
                while (fpFile.available()) {
                    String line = fpFile.readStringUntil('\n');
                    line.trim();
                    if (line.length() > 0) totalCount++;
                }
                fpFile.close();

                if (totalCount == 0) {
                    lcd->clear();
                    lcd->printLine(0, "PUSH FINGERPRINT");
                    lcd->printLine(1, "Tidak ada data!");
                    lcd->printLine(2, "Tekan D untuk Menu");
                    processCompleted = true;
                    lastError = "No data";
                    needsRedraw = false;
                    break;
                }

                Serial.printf("[PUSH_FP_MANUAL] Total data: %d\n", totalCount);
                processStarted = true;
            }

            if (processStarted && !processCompleted) {
                // Proses push data - baca dan push satu per satu atau dalam batch
                File fpFile = SD.open("/fingerprint_mahasiswa.csv", FILE_READ);
                if (!fpFile) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "Gagal buka file!");
                    lcd->printLine(2, "Tekan D untuk Menu");
                    processCompleted = true;
                    lastError = "Cannot open file";
                    needsRedraw = false;
                    break;
                }

                // Skip header
                if (fpFile.available()) fpFile.readStringUntil('\n');

                // Kumpulkan semua data dulu (batch size max 4)
                String batchNim[4];
                String batchNama[4];
                String batchHex[4];
                int batchCount = 0;
                int readIndex = 0;

                while (fpFile.available() && batchCount < 4) {
                    String line = fpFile.readStringUntil('\n');
                    line.trim();
                    if (line.length() == 0) continue;

                    if (readIndex >= currentIndex) {
                        String slotId = apiManager.getCsvColumn(line, 0);
                        String nim = apiManager.getCsvColumn(line, 1);
                        String nama = apiManager.getCsvColumn(line, 2);
                        String hexData = apiManager.getCsvColumn(line, 3);

                        if (nim.length() > 0 && hexData.length() > 0) {
                            batchNim[batchCount] = nim;
                            batchNama[batchCount] = nama;
                            batchHex[batchCount] = hexData;
                            batchCount++;
                            currentIndex++;
                        }
                    }
                    readIndex++;
                }
                fpFile.close();

                if (batchCount == 0) {
                    // Semua data sudah diproses
                    processCompleted = true;
                    pushSuccess = true;
                    lcd->clear();
                    lcd->printLine(0, MSG_BERHASIL);
                    lcd->printLine(1, "Dipush: " + String(totalCount) + " data");
                    lcd->printLine(2, "Tekan D untuk Menu");
                    needsRedraw = false;
                    logSystemActivity("PUSH_FP_MANUAL", "Berhasil push " + String(totalCount) + " fingerprint");
                    break;
                }

                // TUGAS 2: Kirim Hex langsung tanpa Base64 encoding
                // Hex data sudah bersih dari CSV, tidak perlu konversi tambahan

                // Tampilkan progress
                lcd->clear();
                lcd->printLine(0, "PUSH FINGERPRINT");
                lcd->printLine(1, "Mengirim: " + String(currentIndex) + "/" + String(totalCount));
                lcd->printLine(2, "NIM: " + batchNim[0]);

                // Rakit payload JSON
                String payload = "{\"mahasiswa\":[";
                for (int i = 0; i < batchCount; i++) {
                    payload += "{\"nim\":\"" + batchNim[i] + "\",\"nama_mhsw\":\"" + batchNama[i] + "\",\"template_data\":\"" + batchHex[i] + "\"}";
                    if (i < batchCount - 1) payload += ",";
                }
                payload += "]}";

                String endpoint = "https://presensi-elektronik-ta-2526-016.vercel.app/api/device/enroll-mahasiswa";

                // HTTP POST
                HTTPClient http;
                http.setReuse(true);
                http.setTimeout(10000); // Set timeout 10 detik
                http.begin(endpoint);
                http.addHeader("Content-Type", "application/json");
                http.addHeader("x-device-code", DEVICE_CODE);
                http.addHeader("x-device-secret", DEVICE_SECRET);

                if (globalJwtToken.length() > 0) {
                    http.addHeader("Authorization", "Bearer " + globalJwtToken);
                }

                // ===== DEBUG: TAMPILKAN HEADER CONFIG =====
                Serial.println("[DEBUG-PUSH] ===== HEADER CONFIG =====");
                Serial.print("Header Auth: "); Serial.println(http.header("Authorization"));
                Serial.print("Header Device: "); Serial.println(http.header("x-device-code"));

                Serial.printf("[PUSH_FP_MANUAL] Mengirim batch %d data ke API...\n", batchCount);
                Serial.println("[DEBUG-PUSH] ===== PAYLOAD JSON =====");
                Serial.println(payload);

                int httpCode = http.POST(payload);

                // ===== DEBUG: ERROR HANDLING =====
                if (httpCode < 0) {
                    Serial.print("[DEBUG-PUSH] ERROR DETECTED: ");
                    Serial.println(http.errorToString(httpCode).c_str());
                }

                String response = http.getString();
                http.end();

                if (httpCode == 200 || httpCode == 201) {
                    Serial.printf("[PUSH_FP_MANUAL] Berhasil push batch: %d\n", batchCount);
                } else {
                    Serial.printf("[PUSH_FP_MANUAL] Gagal push. HTTP: %d\n", httpCode);
                    Serial.println("[PUSH_FP_MANUAL] Response: " + response);
                    processCompleted = true;
                    pushSuccess = false;
                    lastError = "HTTP " + String(httpCode);
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "Error: " + lastError);
                    lcd->printLine(2, "Tekan D untuk Menu");
                    needsRedraw = false;
                }
            }

            char key = keypad->getKey();

            if (key == 'D' && processCompleted) {
                currentState = STATE_MENU_DOSEN;
                needRender = true;
            }
        }
        break;

        // ========== TUGAS 3: PULL FINGERPRINT MANUAL ==========
        case STATE_PULL_FP_MANUAL:
        {
            static bool needsRedraw = true;
            static bool processStarted = false;
            static bool processCompleted = false;
            static bool pullSuccess = false;
            static int totalCount = 0;
            static int currentIndex = 0;
            static String targetNims[50];  // TUGAS 1: Array untuk prevent duplikasi
            static int targetCount = 0;
            static int successCount = 0;
            static String lastError = "";

            if (needRender) {
                needsRedraw = true;
                processStarted = false;
                processCompleted = false;
                pullSuccess = false;
                totalCount = 0;
                currentIndex = 0;
                successCount = 0;
                lastError = "";
                targetCount = 0;  // TUGAS 1: Reset array
                needRender = false;
            }

            // TUGAS 3: Cari NIM di kelas_mahasiswa.csv dengan status_fingerprint=true
            if (!processStarted && !processCompleted) {
                lcd->clear();
                lcd->printLine(0, "PULL FINGERPRINT");
                lcd->printLine(1, "Membaca data...");

                // Cek apakah WiFi terhubung
                if (WiFi.status() != WL_CONNECTED) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "WiFi tidak aktif!");
                    lcd->printLine(2, "Tekan D untuk Menu");
                    processCompleted = true;
                    lastError = "WiFi not connected";
                    needsRedraw = false;
                    break;
                }

                // Cek apakah file kelas_mahasiswa.csv ada
                if (!SD.exists("/kelas_mahasiswa.csv")) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "File tidak ada!");
                    lcd->printLine(2, "Tekan D untuk Menu");
                    processCompleted = true;
                    lastError = "File not found";
                    needsRedraw = false;
                    break;
                }

                // Buka file dan hitung jumlah NIM dengan status_fingerprint=true
                File klsFile = SD.open("/kelas_mahasiswa.csv", FILE_READ);
                if (!klsFile) {
                    lcd->clear();
                    lcd->printLine(0, MSG_GAGAL);
                    lcd->printLine(1, "Gagal buka file!");
                    lcd->printLine(2, "Tekan D untuk Menu");
                    processCompleted = true;
                    lastError = "Cannot open file";
                    needsRedraw = false;
                    break;
                }

                // Skip header
                if (klsFile.available()) klsFile.readStringUntil('\n');

                // TUGAS 1: Gunakan array static untuk prevent duplikasi
                targetCount = 0;  // Reset array

                // Hitung total baris dengan status_fingerprint=true dan cek duplikasi
                while (klsFile.available()) {
                    String line = klsFile.readStringUntil('\n');
                    line.trim();
                    if (line.length() == 0) continue;

                    // Header: id(0), kode_kelas(1), kelas(2), nim(3), sit_in(4), status_fingerprint(5)
                    String csvNim = apiManager.getCsvColumn(line, 3);
                    String csvStatus = apiManager.getCsvColumn(line, 5);

                    if (csvStatus == "true") {
                        // Cek duplikasi
                        bool isDuplicate = false;
                        for (int i = 0; i < targetCount; i++) {
                            if (targetNims[i] == csvNim) {
                                isDuplicate = true; break;
                            }
                        }
                        if (!isDuplicate && targetCount < 50) {
                            targetNims[targetCount] = csvNim;
                            targetCount++;
                        }
                    }
                }
                klsFile.close();

                totalCount = targetCount;
                Serial.printf_P(PSTR("[PULL_MANUAL] Ditemukan %d NIM unik dengan status=true\n"), totalCount);

                if (totalCount == 0) {
                    lcd->clear();
                    lcd->printLine(0, "PULL FINGERPRINT");
                    lcd->printLine(1, "Tidak ada data!");
                    lcd->printLine(2, "Tekan D untuk Menu");
                    processCompleted = true;
                    lastError = "No data";
                    needsRedraw = false;
                    break;
                }

                Serial.printf("[PULL_FP_MANUAL] Total NIM untuk di-pull: %d\n", totalCount);

                // Hapus file pull sementara jika ada
                if (SD.exists("/pull_fingerprint_temp.csv")) {
                    SD.remove("/pull_fingerprint_temp.csv");
                }

                processStarted = true;
            }

            // TUGAS 2: Langsung gunakan array targetNims - tidak perlu baca file lagi
            if (processStarted && !processCompleted) {
                // Cek apakah semua sudah diproses
                if (currentIndex >= totalCount) {
                    processCompleted = true;
                    pullSuccess = true;

                    // ========== TUGAS 3: SYNC FINGERMAP SETELAH PULL ==========
                    fingerprintManager.syncFromCSV(&userManager);

                    lcd->clear();
                    lcd->printLine(0, MSG_BERHASIL);
                    lcd->printLine(1, "Dipull: " + String(successCount) + "/" + String(totalCount));
                    lcd->printLine(2, "Tekan D untuk Menu");
                    needsRedraw = false;
                    logSystemActivity("PULL_FP_MANUAL", "Berhasil pull " + String(successCount) + " fingerprint");
                    break;
                }

                // Ambil NIM dari array
                String targetNim = targetNims[currentIndex];

                // Tampilkan progress
                lcd->clear();
                lcd->printLine(0, "PULL FINGERPRINT");
                lcd->printLine(1, "Mengambil: " + String(currentIndex + 1) + "/" + String(totalCount));
                lcd->printLine(2, "NIM: " + targetNim);

                // Panggil API untuk mendapatkan fingerprint (sudah binary-safe convert)
                // fetchStudentFingerprint sekarang mengembalikan Hex String langsung
                String hexData = apiManager.fetchStudentFingerprint(targetNim, globalJwtToken);

                if (hexData.length() > 0) {
                    // ========== TUGAS 2: TRIM PADDING ==========
                    // Bersihkan padding di akhir hex sebelum disimpan
                    String cleanHex = trimHex(hexData);

                    // ========== TUGAS 3: PAD HEX JADI 3072 CHARS (1536 BYTES) ==========
                    // Sensor R503 membutuhkan tepat 1536 bytes - pad dengan '0' jika kurang
                    while (cleanHex.length() < 3072) {
                        cleanHex += "0";
                    }
                    Serial.printf_P(PSTR("[API_PULL] Hex di-pad menjadi %d chars\n"), cleanHex.length());

                    // Tampilkan log dengan panjang setelah di-trim
                    Serial.printf_P(PSTR("[INFO] NIM: %s | Final Hex Len: %d\n"),
                        targetNim.c_str(), cleanHex.length());

                    // ========== TUGAS 2: SLOT MANAGEMENT ==========
                    // Cari slot kosong untuk inject fingerprint
                    int nextId = fingerprintManager.getLowestAvailableFingerprintId();

                    if (nextId <= 0) {
                        // Sensor penuh - coba hapus slot 1 (paling lama)
                        Serial.println("[PULL_FP_MANUAL] WARNING: Sensor penuh! Menggunakan slot 1...");
                        fingerprintManager.deleteTemplate(1);
                        nextId = 1;
                    }

                    Serial.printf("[PULL_FP_MANUAL] Menggunakan slot ID: %d untuk NIM: %s\n", nextId, targetNim.c_str());

                    // Simpan ke fingerprint_mahasiswa.csv dengan slot ID yang benar
                    String timestamp = fingerprintDataManager.getTimestamp();
                    int saveResult = fingerprintDataManager.addFingerprintWithHex(
                        nextId, targetNim, "mahasiswa", cleanHex, timestamp
                    );

                    if (saveResult > 0) {
                        // ========== TUGAS 1: INJEKSI KE SENSOR ==========
                        bool injectResult = fingerprintManager.injectSingleFingerprint(
                            targetNim, cleanHex, nextId
                        );

                        if (injectResult) {
                            // Update fingerMap
                            fingerMap[nextId] = targetNim;
                            successCount++;
                            Serial.printf("[PULL_FP_MANUAL] Berhasil simpan & inject NIM: %s ke slot %d\n", targetNim.c_str(), nextId);
                        } else {
                            Serial.printf("[PULL_FP_MANUAL] GAGAL inject NIM: %s (save berhasil, inject gagal)\n", targetNim.c_str());
                        }
                    } else {
                        Serial.printf("[PULL_FP_MANUAL] Gagal simpan ke fingerprint_mahasiswa.csv: %s\n", targetNim.c_str());
                    }

                    // Simpan juga ke file temporary untuk tracking
                    File outFile = SD.open("/pull_fingerprint_temp.csv", FILE_APPEND);
                    if (outFile) {
                        outFile.printf("%s,%s\n", targetNim.c_str(), cleanHex.c_str());
                        outFile.close();
                    }
                } else {
                    Serial.printf("[PULL_FP_MANUAL] Gagal fetch NIM: %s\n", targetNim.c_str());
                }

                currentIndex++;
            }

            char key = keypad->getKey();

            if (key == 'D' && processCompleted) {
                currentState = STATE_MENU_ADMIN;  // dipindah ke Admin
                needRender = true;
            }
        }
        break;

        // ========== TUGAS 2: PUSH PENDING - OTORISASI DOSEN ==========
        case STATE_PUSH_PENDING_AUTH:
        {
            static bool needsRedraw = true;
            static bool authStarted = false;
            static bool authCompleted = false;
            static bool authSuccess = false;
            static int authMode = 0;  // 0=PIN, 1=FP
            static String tempPin = "";
            static int detectedFpId = 0;

            if (needRender) {
                needsRedraw = true;
                authStarted = false;
                authCompleted = false;
                authSuccess = false;
                authMode = 0;
                tempPin = "";
                detectedFpId = 0;
                lcd->clear();
                lcd->printLine(0, "PUSH PENDING");
                lcd->printLine(1, "Otorisasi Dosen");
                lcd->printLine(2, "Scan Jari atau PIN");
                lcd->printLine(3, "D:Batal");
                needRender = false;
            }

            // Cek apakah ada fingerprint yang terdeteksi
            detectedFpId = fingerprintManager.checkFingerprint();

            if (detectedFpId > 0 && !authCompleted) {
                // ========== DEBUG: PRINT FINGERPINT MAP ==========
                printFingerMap();

                // Cek apakah fingerprint ini milik dosen yang sedang login
                String matchedUser = fingerMap[detectedFpId];
                matchedUser.trim();

                // Cek apakah fingerprint ini milik dosen yang sedang login
                if (matchedUser == tempNIP) {
                    // Otorisasi BERHASIL via fingerprint
                    authSuccess = true;
                    authCompleted = true;
                    fingerprintManager.setLEDSuccess();
                    Serial.println("[PUSH_AUTH] Otorisasi FP Berhasil: " + tempNIP);
                    delay(500);
                    fingerprintManager.setLEDOff();
                } else {
                    // Fingerprint tidak cocok
                    fingerprintManager.setLEDError();
                    Serial.println("[PUSH_AUTH] FP tidak cocok: " + matchedUser);
                    delay(500);
                    fingerprintManager.setLEDOff();
                }
            }

            char key = keypad->getKey();

            if (key == 'D' && !authCompleted) {
                // Batal
                authCompleted = true;
                currentState = STATE_MENU_ADMIN;  // dipindah ke Admin
                needRender = true;
            }
            else if (key == '#' && !authCompleted) {
                // Ganti ke mode PIN
                authMode = 1;
                lcd->clear();
                lcd->printLine(0, "PUSH PENDING");
                lcd->printLine(1, "Masukkan PIN:");
                lcd->printLine(2, "_");
                lcd->printLine(3, "D:Batal");
            }
            else if (authMode == 1 && !authCompleted) {
                // Proses input PIN
                if (key >= '0' && key <= '9' && tempPin.length() < 6) {
                    tempPin += key;
                    lcd->setCursor(0, 2);
                    lcd->print(String(tempPin));
                }
                else if (key == '*') {
                    // Konfirmasi PIN - gunakan checkUser untuk verifikasi
                    if (userManager.checkUser(tempNIP, tempPin)) {
                        authSuccess = true;
                        authCompleted = true;
                        Serial.println("[PUSH_AUTH] Otorisasi PIN Berhasil: " + tempNIP);
                        lcd->clear();
                        lcd->printLine(0, "PIN Benar!");
                        delay(1000);
                    } else {
                        lcd->clear();
                        lcd->printLine(0, "PIN Salah!");
                        delay(1000);
                        tempPin = "";
                        lcd->clear();
                        lcd->printLine(0, "PUSH PENDING");
                        lcd->printLine(1, "Masukkan PIN:");
                        lcd->printLine(2, "_");
                    }
                }
            }

            if (authCompleted && authSuccess) {
                // Lanjut ke eksekusi push
                currentState = STATE_PUSH_PENDING_EXEC;
                needRender = true;
            }
        }
        break;

        // ========== TUGAS 3: PUSH PENDING - EKSEKUSI BATCH ==========
        case STATE_PUSH_PENDING_EXEC:
        {
            static bool needsRedraw = true;
            static bool processCompleted = false;
            static bool pushSuccess = false;
            static int pendingCount = 0;

            if (needRender) {
                needsRedraw = true;
                processCompleted = false;
                pushSuccess = false;
                pendingCount = 0;
                needRender = false;
            }

            if (!processCompleted) {
                lcd->clear();
                lcd->printLine(0, "PUSH PENDING");
                lcd->printLine(1, "Mengirim data...");

                // ========== TUGAS 3: PROSES BATCH PUSH ==========
                // Cek apakah ada data di pending_push.csv
                File file = SD.open("/pending_push.csv", FILE_READ);
                if (file) {
                    // Skip header and count lines
                    if (file.available()) file.readStringUntil('\n');
                    while (file.available()) {
                        String line = file.readStringUntil('\n');
                        if (line.length() > 10) pendingCount++;
                    }
                    file.close();
                }

                Serial.printf("[PUSH_PENDING] Found %d pending items\n", pendingCount);

                if (pendingCount == 0) {
                    lcd->clear();
                    lcd->printLine(0, "PUSH PENDING");
                    lcd->printLine(1, "Tidak ada data!");
                    lcd->printLine(2, "Siap di-push");
                    lcd->printLine(3, "D:Menu");
                    processCompleted = true;
                    pushSuccess = true;
                    delay(2000);
                } else {
                    // Proses batch push
                    lcd->printLine(2, "Data: " + String(pendingCount));

                    bool result = processPendingPushQueue();

                    if (result) {
                        pushSuccess = true;
                        lcd->clear();
                        lcd->printLine(0, "PUSH PENDING");
                        lcd->printLine(1, "BERHASIL!");
                        lcd->printLine(2, "Data: " + String(pendingCount));
                        lcd->printLine(3, "D:Menu");
                        Serial.printf("[PUSH_PENDING] Berhasil push %d items\n", pendingCount);
                    } else {
                        pushSuccess = false;
                        lcd->clear();
                        lcd->printLine(0, "PUSH PENDING");
                        lcd->printLine(1, "GAGAL!");
                        lcd->printLine(2, "Cek koneksi WiFi");
                        lcd->printLine(3, "D:Menu");
                        Serial.println("[PUSH_PENDING] Gagal push");
                    }

                    processCompleted = true;
                    delay(2000);
                }
            }

            char key = keypad->getKey();

            if (key == 'D' && processCompleted) {
                currentState = STATE_MENU_ADMIN;  // dipindah ke Admin
                needRender = true;
            }
        }
        break;
    }
}
