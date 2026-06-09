#pragma once
#include <Arduino.h>
#include <vector>
#include "sdcard.h"

// Status presensi
#define PRESENSI_HADIR "hadir"
#define PRESENSI_TIDAK_HADIR "tidak hadir"
#define PRESENSI_IZIN "izin"
#define PRESENSI_ALPHA "alpha"

class PresensiManager {
public:
    bool init();

    // ================= PRESENSI OPERATIONS =================
    // Tambah presensi baru
    bool addPresensi(String nim, String kodeKelas, int pertemuanId, String status = PRESENSI_HADIR);

    // Get presensi by NIM dan kelas
    std::vector<int> getPertemuanIds(String nim, String kodeKelas);

    // Check jika mahasiswa sudah presensi di pertemuan tertentu
    bool isPresensi(String nim, String kodeKelas, int pertemuanId);

    // Get all presensi untuk kelas tertentu
    std::vector<String> getPresensiByKelas(String kodeKelas);

    // Get presensi count untuk kelas tertentu
    int getPresensiCount(String kodeKelas);

    // Update status presensi
    bool updateStatus(String nim, String kodeKelas, int pertemuanId, String newStatus);

    // Delete presensi
    bool deletePresensi(String nim, String kodeKelas, int pertemuanId);
};