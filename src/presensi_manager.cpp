#include "presensi_manager.h"
#include <SD.h>

extern SDCardManager sd;

// ================= INIT =================
bool PresensiManager::init() {
    File f = sd.open("/presensi.csv", FILE_READ);
    if (!f) {
        f = sd.open("/presensi.csv", FILE_WRITE);
        if (f) {
            f.println("id,nim,kode_kelas,pertemuan_id,waktu,status");
            f.close();
        }
    } else {
        f.close();
    }

    return true;
}

// ================= GET NEXT ID =================
int getNextPresensiID() {
    File f = sd.open("/presensi.csv", FILE_READ);
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

// ================= ADD PRESENSI =================
bool PresensiManager::addPresensi(String nim, String kodeKelas, int pertemuanId, String status) {
    // Check if already presensi
    if (isPresensi(nim, kodeKelas, pertemuanId)) {
        Serial.println("Presensi already exists for NIM: " + nim + ", Pertemuan: " + String(pertemuanId));
        return false;
    }

    File f = sd.open("/presensi.csv", FILE_APPEND);
    if (!f) return false;

    int id = getNextPresensiID();
    uint32_t waktu = millis();

    f.print(id); f.print(",");
    f.print(nim); f.print(",");
    f.print(kodeKelas); f.print(",");
    f.print(pertemuanId); f.print(",");
    f.print(waktu); f.print(",");
    f.println(status);

    f.close();
    Serial.println("Presensi added: NIM=" + nim + ", Kelas=" + kodeKelas + ", Pertemuan=" + String(pertemuanId));
    return true;
}

// ================= IS PRESENSI =================
bool PresensiManager::isPresensi(String nim, String kodeKelas, int pertemuanId) {
    File f = sd.open("/presensi.csv", FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);

        if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

        String fileNim = line.substring(p1 + 1, p2);
        String fileKelas = line.substring(p2 + 1, p3);
        int filePertemuan = line.substring(p3 + 1, line.indexOf(',', p3 + 1)).toInt();

        if (fileNim == nim && fileKelas == kodeKelas && filePertemuan == pertemuanId) {
            f.close();
            return true;
        }
    }
    f.close();
    return false;
}

// ================= GET PERTEMUAN IDS =================
std::vector<int> PresensiManager::getPertemuanIds(String nim, String kodeKelas) {
    std::vector<int> list;

    File f = sd.open("/presensi.csv", FILE_READ);
    if (!f) return list;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);

        if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

        String fileNim = line.substring(p1 + 1, p2);
        String fileKelas = line.substring(p2 + 1, p3);

        if (fileNim == nim && fileKelas == kodeKelas) {
            int p4 = line.indexOf(',', p3 + 1);
            int pertemuanId = line.substring(p3 + 1, p4).toInt();
            list.push_back(pertemuanId);
        }
    }
    f.close();
    return list;
}

// ================= GET PRESENSI BY KELAS =================
std::vector<String> PresensiManager::getPresensiByKelas(String kodeKelas) {
    std::vector<String> list;

    File f = sd.open("/presensi.csv", FILE_READ);
    if (!f) return list;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);

        if (p1 <= 0 || p2 <= 0 || p3 <= 0) continue;

        String fileKelas = line.substring(p2 + 1, p3);
        if (fileKelas == kodeKelas) {
            list.push_back(line);
        }
    }
    f.close();
    return list;
}

// ================= GET PRESENSI COUNT =================
int PresensiManager::getPresensiCount(String kodeKelas) {
    int count = 0;

    File f = sd.open("/presensi.csv", FILE_READ);
    if (!f) return 0;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p2 = line.indexOf(',', line.indexOf(',') + 1);
        int p3 = line.indexOf(',', p2 + 1);

        if (p2 <= 0 || p3 <= 0) continue;

        String fileKelas = line.substring(p2 + 1, p3);
        if (fileKelas == kodeKelas) {
            count++;
        }
    }
    f.close();
    return count;
}

// ================= UPDATE STATUS =================
bool PresensiManager::updateStatus(String nim, String kodeKelas, int pertemuanId, String newStatus) {
    File f = sd.open("/presensi.csv", FILE_READ);
    if (!f) return false;

    String temp = "";
    bool updated = false;

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
        int p4 = line.indexOf(',', p3 + 1);

        if (p1 <= 0 || p2 <= 0 || p3 <= 0 || p4 <= 0) {
            temp += line + "\n";
            continue;
        }

        String id = line.substring(0, p1);
        String fileNim = line.substring(p1 + 1, p2);
        String fileKelas = line.substring(p2 + 1, p3);
        int filePertemuan = line.substring(p3 + 1, p4).toInt();
        String waktu = line.substring(p4 + 1, line.lastIndexOf(','));
        String oldStatus = line.substring(line.lastIndexOf(',') + 1);

        if (fileNim == nim && fileKelas == kodeKelas && filePertemuan == pertemuanId) {
            temp += id + "," + fileNim + "," + fileKelas + "," + String(filePertemuan) + "," + waktu + "," + newStatus + "\n";
            updated = true;
        } else {
            temp += line + "\n";
        }
    }
    f.close();

    if (!updated) return false;

    SD.remove("/presensi.csv");
    File fw = sd.open("/presensi.csv", FILE_WRITE);
    fw.print(temp);
    fw.close();

    return true;
}

// ================= DELETE PRESENSI =================
bool PresensiManager::deletePresensi(String nim, String kodeKelas, int pertemuanId) {
    File f = sd.open("/presensi.csv", FILE_READ);
    if (!f) return false;

    String temp = "";
    bool deleted = false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);

        if (p1 <= 0 || p2 <= 0 || p3 <= 0) {
            temp += line + "\n";
            continue;
        }

        String fileNim = line.substring(p1 + 1, p2);
        String fileKelas = line.substring(p2 + 1, p3);
        int filePertemuan = line.substring(p3 + 1, line.indexOf(',', p3 + 1)).toInt();

        if (fileNim == nim && fileKelas == kodeKelas && filePertemuan == pertemuanId) {
            deleted = true;
            continue;
        }
        temp += line + "\n";
    }
    f.close();

    if (!deleted) return false;

    SD.remove("/presensi.csv");
    File fw = sd.open("/presensi.csv", FILE_WRITE);
    fw.print(temp);
    fw.close();

    return true;
}