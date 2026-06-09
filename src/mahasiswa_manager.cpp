#include "mahasiswa_manager.h"
#include "sdcard.h"
#include <SD.h>

extern SDCardManager sd;

// ================= INIT =================
bool MahasiswaManager::init() {
    File f = sd.open("/mahasiswa.csv", FILE_READ);

    if (!f) {
        f = sd.open("/mahasiswa.csv", FILE_WRITE);
        if (f) {
            f.println("nim,nama_lengkap,created_at");
            f.close();
        }
    } else {
        f.close();
    }

    return true;
}

// ================= ADD MAHASISWA =================
// TUGAS 1: Header: nim,nama,created_at
bool MahasiswaManager::addMahasiswa(String nim, String nama) {
    if (exists(nim)) return false;

    File f = sd.open("/mahasiswa.csv", FILE_APPEND);
    if (!f) return false;

    // TUGAS 1: Jika nama kosong, isi dengan "BELUM_ADA_DATA"
    String namaFix = (nama == "") ? "BELUM_ADA_DATA" : nama;

    // TUGAS 1: Get timestamp dari fungsi waktu (DD-MM-YYYY HH:MM:SS)
    String currentTime = getFormattedTime();

    // Format: nim,nama,created_at
    f.printf("%s,%s,%s\n", nim.c_str(), namaFix.c_str(), currentTime.c_str());

    f.close();
    return true;
}

// ================= EXISTS =================
bool MahasiswaManager::exists(String nim) {
    File f = sd.open("/mahasiswa.csv", FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');

        int comma = line.indexOf(',');
        if (comma <= 0) continue;

        String fileNim = line.substring(0, comma);
        if (fileNim == nim) {
            f.close();
            return true;
        }
    }

    f.close();
    return false;
}

// ================= GET NAMA =================
String MahasiswaManager::getNama(String nim) {
    File f = sd.open("/mahasiswa.csv", FILE_READ);
    if (!f) return "";

    while (f.available()) {
        String line = f.readStringUntil('\n');

        int p1 = line.indexOf(',');
        if (p1 <= 0) continue;

        String fileNim = line.substring(0, p1);
        if (fileNim == nim) {
            int p2 = line.indexOf(',', p1 + 1);
            String nama = line.substring(p1 + 1, p2);
            f.close();
            return nama;
        }
    }

    f.close();
    return "";
}

// ================= SET NAMA =================
bool MahasiswaManager::setNama(String nim, String nama) {
    File f = sd.open("/mahasiswa.csv", FILE_READ);
    if (!f) return false;

    String temp = "";
    bool updated = false;

    while (f.available()) {
        String line = f.readStringUntil('\n');

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);

        if (p1 <= 0 || p2 <= 0) {
            temp += line + "\n";
            continue;
        }

        String fileNim = line.substring(0, p1);
        String createdAt = line.substring(p2 + 1);

        if (fileNim == nim) {
            temp += nim + "," + nama + "," + createdAt + "\n";
            updated = true;
        } else {
            temp += line + "\n";
        }
    }

    f.close();

    if (!updated) return false;

    SD.remove("/mahasiswa.csv");

    File fw = sd.open("/mahasiswa.csv", FILE_WRITE);
    fw.print(temp);
    fw.close();

    return true;
}

// ================= DELETE MAHASISWA =================
bool MahasiswaManager::deleteMahasiswa(String nim) {
    File f = sd.open("/mahasiswa.csv", FILE_READ);
    if (!f) return false;

    String temp = "";
    bool deleted = false;

    while (f.available()) {
        String line = f.readStringUntil('\n');

        int comma = line.indexOf(',');
        if (comma <= 0) {
            temp += line + "\n";
            continue;
        }

        String fileNim = line.substring(0, comma);
        if (fileNim == nim) {
            deleted = true;
            continue;
        }

        temp += line + "\n";
    }

    f.close();

    if (!deleted) return false;

    SD.remove("/mahasiswa.csv");

    File fw = sd.open("/mahasiswa.csv", FILE_WRITE);
    fw.print(temp);
    fw.close();

    return true;
}

// ================= GET ALL NIM =================
std::vector<String> MahasiswaManager::getAllNIM() {
    std::vector<String> list;

    File f = sd.open("/mahasiswa.csv", FILE_READ);
    if (!f) return list;

    while (f.available()) {
        String line = f.readStringUntil('\n');

        int comma = line.indexOf(',');
        if (comma <= 0) continue;

        String nim = line.substring(0, comma);
        list.push_back(nim);
    }

    f.close();
    return list;
}

// ================= GET ALL MAHASISWA =================
std::vector<MahasiswaData> MahasiswaManager::getAllMahasiswa() {
    std::vector<MahasiswaData> list;

    File f = sd.open("/mahasiswa.csv", FILE_READ);
    if (!f) return list;

    bool isFirstLine = true;
    while (f.available()) {
        String line = f.readStringUntil('\n');

        // Skip header
        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);

        if (p1 <= 0 || p2 <= 0) continue;

        MahasiswaData mhs;
        mhs.nim = line.substring(0, p1);
        mhs.nama = line.substring(p1 + 1, p2);
        list.push_back(mhs);
    }

    f.close();
    return list;
}