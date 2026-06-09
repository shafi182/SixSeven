# Offline Mode - Sistem Fingerprint

## Overview
Sistem ini menggunakan mode **offline-first** untuk enrollment dan verifikasi fingerprint:
1. Enrollment dilakukan **sekali saja** di salah satu alat
2. Template fingerprint disimpan ke **SD card** dalam format `.bin`
3. Setiap alat bisa verify tanpa koneksi ke cloud

## File Structure di SD Card

```
/fingerprint.csv              # Index: ID -> NIM
/mahasiswa.csv                # Data mahasiswa: NIM -> Nama
/templates/
    ├── 1.bin                 # Template fingerprint ID 1
    ├── 2.bin                 # Template fingerprint ID 2
    └── ...
```

## Cara Menggunakan

### 1. Enroll Mahasiswa Baru

```cpp
// Di main.cpp
FingerprintManager fingerprintManager;
FingerprintDataManager fingerprintDataManager;
MahasiswaManager mahasiswaManager;

// Enroll dan simpan ke SD card
if (fingerprintManager.enrollAndSave(&fingerprintDataManager, "2024001")) {
    // Simpan data mahasiswa
    mahasiswaManager.addMahasiswa("2024001", "Nama Mahasiswa");

    Serial.println("Enrollment SUCCESS!");
}
```

### 2. Verifikasi Fingerprint

```cpp
// Verifikasi dari template yang tersimpan di SD
bool result = fingerprintManager.verifyFromSD(&fingerprintDataManager, "2024001");

if (result) {
    Serial.println("ACCESS GRANTED");
} else {
    Serial.println("ACCESS DENIED");
}
```

### 3. Melihat Template yang Tersimpan

```cpp
int count = fingerprintDataManager.getTemplateCount();
Serial.println("Total templates: " + String(count));

std::vector<String> allNIM = fingerprintDataManager.getAllNIM();
for (int i = 0; i < allNIM.size(); i++) {
    String nim = allNIM[i];
    int id = fingerprintDataManager.getID(nim);
    Serial.println("ID: " + String(id) + ", NIM: " + nim);
}
```

## Algoritma Verifikasi Offline

```
1. User scan fingerprint
   ↓
2. Get ID dari NIM (cari di /fingerprint.csv)
   ↓
3. Load template dari /templates/ID.bin ke buffer
   ↓
4. Upload template ke sensor R503
   ↓
5. ambil image baru dari sensor
   ↓
6. Compare dengan template yang di-upload
   ↓
7. Return MATCH / NO MATCH
```

## Format Template

- **Format**: Binary (`.bin`)
- **Ukuran**: 512 bytes (untuk R503/R505)
- **Encoding**: Raw bytes dari sensor (tidak di-encode)

## Multi-Device Setup

Untuk menggunakan di lebih dari 1 alat:

1. **Enroll** di alat 1 → template tersimpan di SD card
2. **Copy SD card** ke alat 2 (atau download dari cloud)
3. Alat 2 bisa langsung **verify** tanpa enrollment ulang

Atau gunakan mode online untuk sync template dari cloud ke SD card.
