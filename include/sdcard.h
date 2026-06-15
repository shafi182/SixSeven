#pragma once

#include <Arduino.h>
#include <FS.h>
#include <time.h>

class SDCardManager {
public:
    bool init();
    File open(const char* path, const char* mode);
};

// ========== AUTO-PROVISIONING (Plug-and-Play SD Card) ==========
// Membuat semua file CSV dasar beserta header-nya jika belum ada, sehingga
// SD Card baru/kosong bisa langsung dipakai tanpa menyiapkan file manual.
// Idempoten: file yang sudah ada tidak disentuh.
void initSDCardFiles();

// System Logger Functions
void logSystemActivity(String event, String detail);
String getFormattedTime();