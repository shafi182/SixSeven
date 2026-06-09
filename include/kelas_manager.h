#pragma once
#include <Arduino.h>
#include <vector>
#include "sdcard.h"

class KelasManager {
public:
    bool init();

    // Get all classes
    std::vector<String> getAllKelas();

    // Get classes by dosen (from dosen_kelas.csv)
    std::vector<String> getKelasByDosen(String nip);

    // Get mahasiswa by kelas (from kelas_mahasiswa.csv)
    std::vector<String> getMahasiswaByKelas(String kodeKelas);

    // Check if mahasiswa is in kelas
    bool isMahasiswaInKelas(String nim, String kodeKelas);

    // Delete mahasiswa from kelas
    bool deleteMahasiswaFromKelas(String nim, String kodeKelas);
};