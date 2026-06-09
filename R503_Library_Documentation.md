# Library sensor R503:

---

# 1. Library yang Digunakan

Proyek ini menggunakan tiga layer library fingerprint:

| Layer   | File                              | Keterangan                        |
| ------- | --------------------------------- | --------------------------------- |
| Level 1 | Adafruit_Fingerprint.h/.cpp       | Library resmi Adafruit (original) |
| Level 2 | Adafruit_Fingerprint_ESP32.h/.cpp | Port khusus ESP32 (custom)        |
| Level 3 | fingerprint.h/.cpp                | Wrapper class FingerprintManager  |

Library Adafruit_Fingerprint_ESP32 adalah port dari library Python Adafruit CircuitPython yang di-porting khusus untuk ESP32, dengan fitur tambahan untuk transfer template via UART (untuk backup/restore ke SD card/database).

---

# 2. Konstanta Penting (Error Codes & Commands)

## Error Codes (Kode Respons Sensor)

```cpp
#define FP_OK                 0x00  // Berhasil
#define FP_NOFINGER           0x02  // Tidak ada jari
#define FP_IMAGEMESS          0x06  // Gambar terlalu kabur
#define FP_FEATUREFAIL        0x07  // Fitur sidik jari tidak terdeteksi
#define FP_NOMATCH            0x08  // Sidik jari tidak cocok
#define FP_NOTFOUND           0x09  // Sidik jari tidak ditemukan di database
#define FP_ENROLLMISMATCH     0x0
#define FP_BADLOCATION        0x0B  // Lokasi template invalid
#define FP_FLASHERR           0x18  // Error writing ke flash
```

## Command Codes (Instruksi ke Sensor)

```cpp
#define FP_GETIMAGE           0x01  // Ambil gambar jari
#define FP_IMAGE2TZ           0x02  // Konversi gambar ke template
#define FP_FINGERPRINTSEARCH  0x04  // Pencarian fingerprint
#define FP_REGMODEL           0x05  // Gabungkan 2 template untuk enroll
#define FP_STORE              0x06  // Simpan template ke flash sensor
#define FP_LOAD               0x07  // Load template dari flash ke buffer
#define FP_UPLOAD             0x08  // Kirim template ke host (baca)
#define FP_DOWNLOAD           0x09  // Terima template dari host (tulis)
#define FP_DELETE             0x0C  // Hapus template
#define FP_EMPTY              0x0D  // Kosongkan seluruh database
#define FP_READSYSPARA        0x0F  // Baca parameter sistem
#define FP_HISPEEDSEARCH      0x1B  // Pencarian cepat
#define FP_TEMPLATECOUNT      0x1D  // Hitung jumlah template
#define FP_SETAURA            0x35  // Kontrol LED Aura
```

---

# 3. Fungsi-Fungsi di Adafruit_Fingerprint_ESP32

## A. Inisialisasi & Komunikasi

### Adafruit_Fingerprint_ESP32(HardwareSerial* serial, uint32_t password)

* Fungsi: Konstruktor - membuat objek fingerprint dengan Serial interface
* Parameter:

  * serial - pointer ke HardwareSerial (misal &Serial2)
  * password - password 32-bit sensor (default 0x00000000)

### bool begin(unsigned long baud, int8_t rxPin, int8_t txPin)

* Fungsi: Inisialisasi UART dan verifikasi koneksi sensor
* Cara Kerja:
  1. Mulai komunikasi UART pada baud rate specified
  2. Buang garbage di buffer serial
  3. Panggil verify_password() untuk handshake
  4. Panggil read_sysparam() untuk baca parameter
* Return: true jika berhasil, false jika gagal

### uint8_t verify_password()

* Fungsi: Verifikasi password sensor
* Cara Kerja: Kirim command FP_VERIFYPASSWORD dengan password, terima ACK
* Return: FP_OK jika password benar

### uint8_t read_sysparam()

* Fungsi: Baca parameter sistem dari sensor
* Parameter yang Diisi:

  * library_size - kapasitas total template
  * security_level - level keamanan (1-5)
  * data_packet_size - ukuran packet data
  * baudrate_param - parameter baud rate

---

## B. Operasi Capture & Enroll

### uint8_t get_image()

* Fungsi: Minta sensor mengambil gambar sidik jari
* Cara Kerja:
  1. Kirim command FP_GETIMAGE
  2. Sensor melakukan scan optical
  3. Jika ada jari, simpan sebagai image buffer internal
* Return: FP_OK jika berhasil, FP_NOFINGER jika tidak ada jari

### uint8_t image_2_tz(uint8_t slot)

* Fungsi: Konversi image menjadi template karakter
* Parameter: slot (1 atau 2) - buffer tujuan
* Cara Kerja:
  1. Ambil image dari buffer
  2. Ekstrak fitur minutiae
  3. Simpan sebagai template di buffer karakter (slot 1 atau 2)
* Return: FP_OK jika berhasil, FP_FEATUREFAIL jika fitur tidak terdeteksi

### uint8_t create_model()

* Fungsi: Gabungkan dua template untuk membuat model enroll
* Cara Kerja:
  1. Ambil template dari slot 1 dan slot 2
  2. Bandingkan dan cocokkan keduanya
  3. Jika cocok, buat model tunggal
* Return: FP_OK jika berhasil, FP_ENROLLMISMATCH jika tidak cocok

### uint8_t store_model(uint16_t location, uint8_t slot)

* Fungsi: Simpan model ke flash memory sensor
* Parameter:

  * location - nomor slot (1-200)
  * slot - buffer sumber (biasanya 1)
* Cara Kerja:
  1. Ambil model dari buffer karakter
  2. Tulis ke flash pada lokasi yang specified
* Return: FP_OK jika berhasil, FP_BADLOCATION jika lokasi invalid

---

## C. Operasi Pencarian & Verifikasi

### uint8_t finger_search()

* Fungsi: Cari kecocokan fingerprint di seluruh database
* Cara Kerja:
  1. Konversi image di buffer 1 ke template
  2. Bandingkan dengan semua template di flash
  3. Return ID dan confidence tertinggi yang match
* Variabel Output:

  * finger_id - nomor slot template yang match
  * confidence - skor kepercayaan (semakin tinggi semakin akurat)
* Return: FP_OK jika match, FP_NOTFOUND jika tidak ada match

### uint8_t finger_fast_search()

* Fungsi: Pencarian cepat (high-speed search)
* Cara Kerja: Sama seperti finger_search() tapi menggunakan algoritma optimized
* Return: FP_OK jika match, FP_NOTFOUND jika tidak ada match

### uint8_t load_model(uint16_t location, uint8_t slot)

* Fungsi: Load template dari flash ke buffer karakter
* Parameter:

  * location - nomor slot template
  * slot - buffer tujuan (1 atau 2)
* Return: FP_OK jika berhasil

---

## D. Manajemen Database

### uint8_t delete_model(uint16_t location)

* Fungsi: Hapus satu template dari sensor
* Parameter: location - nomor slot yang akan dihapus
* Return: FP_OK jika berhasil

### uint8_t empty_library()

* Fungsi: Hapus SEMUA template dari sensor
* Return: FP_OK jika berhasil

### uint8_t count_templates()

* Fungsi: Hitung jumlah template yang tersimpan
* Variabel Output: template_count - jumlah template
* Return: FP_OK jika berhasil

### uint8_t read_templates(uint16_t* templateList, size_t maxLen, uint16_t* foundCount)

* Fungsi: Baca bitmap semua slot yang terisi
* Return: Array berisi nomor-nomor slot yang terpakai

---

## E. Template I/O (Kritis untuk Database External)

### uint8_t get_fpdata(uint8_t* buffer, size_t bufferSize, size_t* outLen, uint8_t sensorbuffer, uint8_t slot)

* Fungsi: KELUARKAN template dari sensor ke host (untuk backup ke database/SD)
* Parameter:

  * sensorbuffer: FP_BUFFER_CHAR (template) atau FP_BUFFER_IMAGE (gambar)
  * slot: 1 atau 2 (untuk char buffer)
* Cara Kerja:
  1. Kirim command FP_UPLOAD
  2. Sensor streaming data packet via UART
  3. Terima dan simpan ke buffer
* Return: FP_OK jika berhasil
* Output: Data template dalam bentuk bytes (bisa dikonversi ke hex untuk CSV)

### uint8_t send_fpdata(const uint8_t* data, size_t length, uint8_t sensorbuffer, uint8_t slot)

* Fungsi: MASUKKAN template dari host ke sensor (untuk restore dari database)
* Parameter:

  * data - array bytes template
  * length - panjang data
  * sensorbuffer - buffer tujuan
  * slot - slot buffer
* Cara Kerja:
  1. Kirim command FP_DOWNLOAD
  2. Tunggu ACK dari sensor
  3. Stream data dalam chunks (32/64/128/256 bytes per packet)
* Return: FP_OK jika berhasil

---

## F. LED Control (R503 Aura LED)

### uint8_t set_led(uint8_t color, uint8_t mode, uint8_t speed, uint8_t cycles)

* Fungsi: Kontrol LED bawaan sensor R503
* Parameter:

  * color:

    * FP_LED_RED (1)
    * FP_LED_BLUE (2)
    * FP_LED_PURPLE (3)
    * FP_LED_GREEN (4)
  * mode:

    * FP_LED_BREATHE (1) - napas
    * FP_LED_FLASH (2) - berkedip
    * FP_LED_ON (3) - selalu on
    * FP_LED_OFF (4) - off
    * FP_LED_FADE_ON (5) - menyala perlahan
    * FP_LED_FADE_OFF (6) - mati perlahan
  * speed: kecepatan (0x00-0xFF)
  * cycles: jumlah pengulangan

---

## G. Utility Functions

### static String bytesToHexString(const uint8_t* data, size_t length)

* Fungsi: Konversi bytes ke string hexadecimal
* Gunaan: Untuk simpan template ke CSV/SQL

### static size_t hexStringToBytes(const char* hex, size_t hexLen, uint8_t* out, size_t outSize)

* Fungsi: Konversi string hexadecimal ke bytes
* Gunaan: Untuk load template dari CSV/SQL ke sensor

---

# 4. Fungsi-Fungsi di FingerprintManager (Wrapper)

| Fungsi                        | Keterangan                                      |
| ----------------------------- | ----------------------------------------------- |
| init()                        | Inisialisasi sensor, upgrade baud ke 115200     |
| loadTemplatesFromCSV()        | Load semua template dari CSV saat boot          |
| injectSingleFingerprint()     | Inject satu template dari hex ke sensor         |
| scan()                        | Scan fingerprint sederhana (blocking)           |
| enroll()                      | Enroll jari baru ke slot tertentu               |
| deleteTemplate()              | Hapus template dari slot                        |
| getTemplateCount()            | Ambil jumlah template                           |
| findEmptySlot()               | Cari slot kosong                                |
| scanAndAuthenticate()         | Scan + cari user di fingerMap                   |
| checkFingerprint()            | Non-blocking fingerprint check                  |
| setLED*()                     | Kontrol LED (standby, scanning, success, error) |
| hardResetDB()                 | Kosongkan seluruh database sensor               |
| syncFromCSV()                 | Flush + re-inject semua user                    |
| extractTemplateAsHex()        | Ambil template sebagai hex string               |
| flushAndInjectPresensiUsers() | Flush + inject dosen + mahasiswa kelas tertentu |

---

# 5. Alur Kerja Lengkap

## Enroll Fingerprint Baru

```text
1. get_image()        -> Ambil gambar jari pertama
2. image_2_tz(1)      -> Konversi ke template buffer 1
3. get_image()        -> Ambil gambar jari kedua
4. image_2_tz(2)      -> Konversi ke template buffer 2
5. create_model()     -> Gabungkan kedu
```

```text
1. get_image()        -> Ambil gambar jari
2. image_2_tz(1)      -> Konversi ke template
3. finger_search()    -> Cari di database
   -> Returns: finger_id + confidence
```

## Backup Template ke Database

```text
1. load_model(slot, 1)      -> Load dari flash ke buffer
2. get_fpdata(buffer)       -> Ambil sebagai bytes
3. bytesToHexString()       -> Konversi ke hex
4. Simpan ke CSV/SQL
```

## Restore Template dari Database

```text
1. hexStringToBytes()       -> Konversi hex ke bytes
2. send_fpdata()            -> Kirim ke buffer sensor
3. store_model(slot)        -> Simpan ke flash
```

---

# 6. Komunikasi UART Packet Structure

Setiap komunikasi mengikuti format packet ini:

```text
[Header 9 byte] [Data N byte] [Checksum 2 byte]
```

* Header: EF01 (start code) + 4 byte address + packet type + length
* Checksum: Penjumlahan semua byte dari packet type sampai data terakhir

Library ini menggunakan HardwareSerial(2) dengan pin:

* RX: GPIO 32
* TX: GPIO 33
* Baud: 115200 bps (setelah upgrade dari 57600)

---

## Referensi file

* Library asli: `.pio\libdeps\esp32doit-devkit-v1\Adafruit Fingerprint Sensor Library\`
* Custom ESP32 port: `include/Adafruit_Fingerprint_ESP32.h`, `src/Adafruit_Fingerprint_ESP32.cpp`
* Wrapper: `include/fingerprint.h`, `src/fingerprint.cpp`
