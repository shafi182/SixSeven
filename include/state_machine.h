#pragma once
#include <Arduino.h>

#include "lcd.h"
#include "keypad_manager.h"
#include "input_handler.h"
#include "auth_manager.h"

enum State {
    STATE_LOGIN,
    STATE_MENU_ADMIN,
    STATE_MENU_ADMIN_AKUN,       // Akun Dosen (Tambah/Data) - dipindah dari menu utama Admin
    STATE_MENU_ADMIN_MORE,
    STATE_MENU_ADMIN_PAGE3,
    STATE_MENU_ADMIN_PAGE4,
    STATE_MENU_ADD_USER,
    STATE_VIEW_DOSEN,
    STATE_VIEW_MAHASISWA,
    STATE_CHANGE_PASSWORD,
    STATE_MENU_DOSEN,
    // STATE_MENU_DOSEN_MORE dihapus - Setup WiFi langsung di Menu Dosen page 2 (B)
    STATE_DELETE_USER,
    STATE_DELETE_MAHASISWA,

    // Registrasi Mahasiswa (Enhanced - Dosen)
    STATE_REG_MHS_PILIH_KELAS,        // Pilih kelas yang diampu
    STATE_REG_MHS_CONFIRM_START,      // Konfirmasi sebelum mulai (PIN/FP)
    STATE_REG_MHS_INPUT_NIM,           // Input NIM mahasiswa
    STATE_REG_MHS_SIT_IN_CONFIRM,      // Konfirmasi jika NIM tidak terdaftar (Sit-In)
    STATE_REG_MHS_ENROLL,              // Proses enrollment fingerprint
    STATE_REG_MHS_SUMMARY,             // Ringkasan sebelum push ke server

    // Registrasi Mahasiswa (by Admin -单独 state)
    STATE_REG_ADMIN_MHS_NIM,           // Input NIM mahasiswa (by Admin)
    STATE_REG_ADMIN_MHS_FINGERPRINT,   // Enrollment fingerprint (by Admin)

    // View Mahasiswa by Kelas
    STATE_SELECT_KELAS,
    STATE_VIEW_MAHASISWA_BY_KELAS,

    // Delete Mahasiswa
    STATE_DELETE_MHS_SELECT_KELAS,
    STATE_DELETE_MHS_LIST,
    STATE_DELETE_MHS_CONFIRM_PIN,

    // Delete Dosen Fingerprint
    STATE_DELETE_DOSEN_LIST,
    STATE_DELETE_DOSEN_CONFIRM_PIN,

    // Enroll Fingerprint
    STATE_ENROLL_FINGERPRINT,

    // Dosen Enroll Fingerprint
    STATE_ENROLL_DOSEN_FINGERPRINT,

    // USB Export
    STATE_ADMIN_WAIT_USB,
    STATE_ADMIN_COPY_USB,
    STATE_ADMIN_EJECT_USB,

    // Admin Lainnya Page 2
    STATE_MENU_ADMIN_PAGE4_2,

    // WiFi Setup (Universal - bisa diakses Admin & Dosen)
    STATE_WIFI_BROWSE,     // TUGAS 1: Browse saved WiFi list with pagination
    STATE_WIFI_CONFIG,
    STATE_WIFI_CONNECTING,
    STATE_WIFI_RESULT,

    // TUGAS 3: Presensi Kuliah (Alur Baru)
    STATE_PRESENSI_PILIH_KELAS,      // Pilih kelas yang akan dipresensi
    STATE_PRESENSI_PULL_FP,          // Pull fingerprint mahasiswa dari server
    STATE_PRESENSI_RETRY_PULL,       // Retry pull fingerprint jika gagal
    STATE_PRESENSI_PILIH_PERTEMUAN,  // Pilih pertemuan yang akan dipresensi
    STATE_PRESENSI_INJECT_SENSOR,    // TAHAP 2: Flush + inject dosen & mahasiswa ke sensor R503
    STATE_PRESENSI_AUTH_START,       // Otorisasi dosen (awal) - PIN/FP
    STATE_PRESENSI_SCANNING,         // Proses scanning fingerprint mahasiswa
    STATE_PRESENSI_MENU_LAINNYA,     // Menu Lainnya (Jari Terkendala, Sit-In)
    STATE_PRESENSI_CADANGAN,         // Presensi darurat (jari rusak) - input NIM + inject fp_2 on-demand
    STATE_PRESENSI_SITIN,            // Presensi untuk Mahasiswa Sit-In (Input NIM, Pull, & Scan)
    STATE_PRESENSI_AUTH_END,         // Otorisasi dosen (akhir) - PIN/FP untuk tutup sesi
    STATE_PRESENSI_SAVE,             // Simpan data presensi ke SD Card

    // TUGAS 2: Push Fingerprint Manual
    STATE_PUSH_FP_MANUAL,            // Push semua fingerprint mahasiswa ke API
    STATE_PULL_FP_MANUAL,            // Pull fingerprint mahasiswa dari API

    // Fitur Randomizer Mahasiswa
    STATE_RANDOM_SELECT_KELAS,
    STATE_RANDOM_EXECUTE,
    STATE_RANDOM_RESULT,

    // TUGAS 2: Push Pending dengan Otorisasi Dosen
    STATE_PUSH_PENDING_AUTH,        // Otorisasi dosen sebelum push batch
    STATE_PUSH_PENDING_EXEC,         // Eksekusi push batch
    STATE_EKSPOR_AP
};

class StateMachine {
private:
    State currentState;
    Role currentRole;

    LCDManager* lcd;
    KeypadManager* keypad;
    InputHandler* input;
    AuthManager* auth;

    // ========== TUGAS 6: VARIABEL SEMENTARA REGISTRASI MAHASISWA ==========
    // Penyimpanan aman untuk data registrasi (bersih saat crash)
    String tempKodeMk;           // Kode mata kuliah yang dipilih
    String tempKelas;            // Kelas yang dipilih
    String tempNIM;              // NIM mahasiswa yang sedang didaftarkan
    bool tempIsSitIn;            // Flag jika mahasiswa Sit-In
    int tempFingerId;            // ID fingerprint yang didaftarkan

    // Statistik sesi registrasi
    int regSessionCount;         // Jumlah mahasiswa yang berhasil didaftarkan
    int regSessionSitInCount;   // Jumlah mahasiswa Sit-In

    // State variables for enrollment process
    bool enrolling;              // Flag for enrollment state
    unsigned long lastLCDUpdate; // Last LCD update timestamp

    void goToLogin();
    void goToMainMenu();

public:
    StateMachine(LCDManager* l, KeypadManager* k, InputHandler* i, AuthManager* a);

    void init();
    void update();

    // State aktif saat ini (dipakai main.cpp agar sm.init() pasca-sync tidak
    // mereset sesi yang sedang berjalan / sesi yang baru di-resume).
    State getState() const { return currentState; }
};

// Peek (sebelum sm dibuat resume) apakah ada sesi PRESENSI ACTIVE di SD.
// Dipakai main.cpp untuk MELEWATI flush+inject sensor saat boot, agar template
// di flash R503 (yang persisten) tidak terhapus dan tidak perlu di-inject ulang.
bool hasActivePresensiResume();