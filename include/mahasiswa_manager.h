#pragma once
#include <Arduino.h>
#include <vector>
#include "sdcard.h"

struct MahasiswaData {
    String nim;
    String nama;
};

class MahasiswaManager {
public:
    bool init();

    bool addMahasiswa(String nim, String nama = "");
    bool exists(String nim);
    String getNama(String nim);
    bool setNama(String nim, String nama);
    bool deleteMahasiswa(String nim);

    std::vector<String> getAllNIM();
    std::vector<MahasiswaData> getAllMahasiswa();
};