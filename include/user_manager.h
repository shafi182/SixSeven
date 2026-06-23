#pragma once
#include <Arduino.h>
#include <vector>
#include "sdcard.h"

class UserManager {
public:
    bool init();

    // users.csv operations (authentication only)
    bool addUser(String nip, String pin, String role);
    bool changePassword(String nip, String newPin);
    bool deleteUser(String nip);
    bool checkUser(String nip, String pin);
    bool isAdmin(String nip);
    bool isDosen(String nip);
    String getUserHash(String nip);

    std::vector<String> getAllDosen();

    // dosen.csv operations (profil dosen)
    bool addDosen(String nip, String nama);
    bool deleteDosen(String nip);
    String getDosenNama(String nip);
    bool dosenExists(String nip);

    // fingerprint.csv operations
    bool deleteFingerprint(String user_id, String role);
    bool saveOrUpdateFingerprint(String user_id, String role, String data_jari);
};