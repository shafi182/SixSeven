#include "kelas_manager.h"
#include <SD.h>

extern SDCardManager sd;

// ================= INIT =================
bool KelasManager::init() {
    // Initialize kelas.csv
    File f = sd.open("/kelas.csv", FILE_READ);
    if (!f) {
        f = sd.open("/kelas.csv", FILE_WRITE);
        if (f) {
            f.println("kode_kelas,nama_kelas");
            f.close();
        }
    } else {
        f.close();
    }

    // Initialize dosen_kelas.csv
    f = sd.open("/dosen_kelas.csv", FILE_READ);
    if (!f) {
        f = sd.open("/dosen_kelas.csv", FILE_WRITE);
        if (f) {
            f.println("id,nip,kode_kelas");
            f.close();
        }
    } else {
        f.close();
    }

    // Initialize kelas_mahasiswa.csv
    f = sd.open("/kelas_mahasiswa.csv", FILE_READ);
    if (!f) {
        f = sd.open("/kelas_mahasiswa.csv", FILE_WRITE);
        if (f) {
            // TUGAS 2: Header 6 kolom sesuai spesifikasi
            f.println("id,kode_kelas,kelas,nim,sit_in,status_fingerprint");
            f.close();
        }
    } else {
        f.close();
    }

    return true;
}

// ================= GET ALL KELAS =================
std::vector<String> KelasManager::getAllKelas() {
    std::vector<String> list;

    File f = sd.open("/kelas.csv", FILE_READ);
    if (!f) return list;

    bool isFirstLine = true;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        // Skip header
        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }

        int comma = line.indexOf(',');
        if (comma <= 0) continue;

        String kode = line.substring(0, comma);
        list.push_back(kode);
    }
    f.close();
    return list;
}

// ================= GET KELAS BY DOSEN =================
std::vector<String> KelasManager::getKelasByDosen(String nip) {
    std::vector<String> list;

    File f = sd.open("/dosen_kelas.csv", FILE_READ);
    if (!f) return list;

    bool isFirstLine = true;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        // Skip header
        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);

        if (p1 <= 0 || p2 <= 0) continue;

        String fileNip = line.substring(p1 + 1, p2);
        String kodeKelas = line.substring(p2 + 1);

        if (fileNip == nip) {
            list.push_back(kodeKelas);
        }
    }
    f.close();
    return list;
}

// ================= GET MAHASISWA BY KELAS =================
std::vector<String> KelasManager::getMahasiswaByKelas(String kodeKelas) {
    std::vector<String> list;

    File f = sd.open("/kelas_mahasiswa.csv", FILE_READ);
    if (!f) return list;

    bool isFirstLine = true;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        // Skip header
        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);

        if (p1 <= 0 || p2 <= 0) continue;

        String fileKelas = line.substring(p1 + 1, p2);
        String nim = line.substring(p2 + 1);

        if (fileKelas == kodeKelas) {
            list.push_back(nim);
        }
    }
    f.close();
    return list;
}

// ================= IS MAHASISWA IN KELAS =================
bool KelasManager::isMahasiswaInKelas(String nim, String kodeKelas) {
    File f = sd.open("/kelas_mahasiswa.csv", FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);

        if (p1 <= 0 || p2 <= 0) continue;

        String fileKelas = line.substring(p1 + 1, p2);
        String fileNim = line.substring(p2 + 1);

        if (fileKelas == kodeKelas && fileNim == nim) {
            f.close();
            return true;
        }
    }
    f.close();
    return false;
}

// ================= DELETE MAHASISWA FROM KELAS =================
bool KelasManager::deleteMahasiswaFromKelas(String nim, String kodeKelas) {
    File f = sd.open("/kelas_mahasiswa.csv", FILE_READ);
    if (!f) return false;

    String temp = "";
    bool deleted = false;

    bool isFirstLine = true;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            temp += line + "\n";
            continue;
        }

        // Skip header
        if (isFirstLine) {
            isFirstLine = false;
            temp += line + "\n";
            continue;
        }

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);

        if (p1 <= 0 || p2 <= 0) {
            temp += line + "\n";
            continue;
        }

        String fileKelas = line.substring(p1 + 1, p2);
        String fileNim = line.substring(p2 + 1);

        if (fileKelas == kodeKelas && fileNim == nim) {
            deleted = true;
            continue;
        }
        temp += line + "\n";
    }
    f.close();

    if (deleted) {
        SD.remove("/kelas_mahasiswa.csv");
        File fw = sd.open("/kelas_mahasiswa.csv", FILE_WRITE);
        fw.print(temp);
        fw.close();
    }

    return deleted;
}