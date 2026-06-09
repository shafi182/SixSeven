# Dokumentasi Modifikasi Library Sensor Fingerprint R503

Dokumentasi komprehensif mengenai arsitektur, alur data, dan fungsi-fungsi modifikasi pada library sensor sidik jari R503, yang dioptimalkan untuk sistem presensi berbasis mikrokontroler.

---

## 1. Arsitektur Komunikasi UART

Sensor R503 berkomunikasi via UART dengan protocol packet yang terstruktur.

### 1.1 Format Packet

```text
┌──────────┬──────────┬─────────┬─────────┬─────────┬────────────┬─────────┬────────────┐
│ Start    │ Address  │ Packet  │ Length  │ Data... │ Checksum   │         │            │
│ Code     │ (4 byte) │ Type    │ (2 byte)│         │ (2 byte)   │         │            │
│ (2 byte) │          │         │         │         │            │         │            │
└──────────┴──────────┴─────────┴─────────┴─────────┴────────────┴─────────┴────────────┘
  EF01       FFFFFFFF   1/2/7/8   XX XX     ...                   HH HH

Komponen,Nilai,Keterangan
Start Code,0xEF01,Wakeup identifier
Address,0xFFFFFFFF,Default device address
Packet Type,"1=CMD, 2=DATA, 7=ACK, 8=END",Jenis packet
Checksum,sum(packet_type + length + data),Validasi integritas

// Dari Adafruit_Fingerprint.h
#define FINGERPRINT_GETIMAGE   0x01  // Ambil gambar jari
#define FINGERPRINT_IMAGE2TZ   0x02  // Generate character file
#define FINGERPRINT_REGMODEL   0x05  // Gabungkan 2 character -> template
#define FINGERPRINT_STORE      0x06  // Simpan template ke slot
#define FINGERPRINT_UPLOAD     0x08  // Kirim template ke host (EXTRACT)
#define FINGERPRINT_DOWNLOAD   0x09  // Terima template dari host (INJECT)
#define FINGERPRINT_DELETE     0x0C  // Hapus template
#define FINGERPRINT_EMPTY      0x0D  // Kosongkan library

// Adafruit_Fingerprint_ESP32.h:80-81
#define FP_BUFFER_CHAR   0   // Character buffer (hasil prosesing)
#define FP_BUFFER_IMAGE  1   // Raw image buffer

┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Finger     │────>│ Image Buffer│────>│ Char Buffer │────>│ Template    │
│  (physical) │     │ (raw data)  │     │ (features)  │     │ (combined)  │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
         │                   │                   │                   │
    get_image()         image2Tz(1)         image2Tz(2)        create_model()

// Adafruit_Fingerprint_ESP32.cpp:489-511
uint8_t Adafruit_Fingerprint_ESP32::get_fpdata(uint8_t* buffer, size_t bufferSize, 
                                               size_t* outLen, 
                                               uint8_t sensorbuffer, uint8_t slot)

// Step 1: Kirim perintah UPLOAD (0x08)
uint8_t cmd[2] = { FP_UPLOAD, slot };
_send_packet(cmd, 2);

// Step 2: Tunggu ACK dari sensor
_get_packet(12, reply, ...);

// Step 3: Terima data streaming via multiple packets
_get_data(buffer, bufferSize, outLen);

// Dari enroll_sql_esp32.cpp.bak:77-126
static String enroll_binary() {
    // 1. Capture gambar pertama
    if (!waitForFinger(15000)) return "";
    finger.image_2_tz(1);  // Simpan ke char buffer 1

    // 2. Angkat jari, tempel lagi
    waitForFingerRemoval(5000);
    finger.image_2_tz(2);  // Simpan ke char buffer 2

    // 3. Gabungkan jadi template
    finger.create_model();  // Hasil di char buffer 1

    // 4. EXTRACT: Ambil template dari sensor
    uint8_t buf[FP_TEMPLATE_MAX];
    size_t outLen = 0;
    finger.get_fpdata(buf, sizeof(buf), &outLen, FP_BUFFER_CHAR, 1);

    // 5. Convert bytes ke hex string
    return Adafruit_Fingerprint_ESP32::bytesToHexString(buf, outLen);
}

id,user_id,role,data_jari,created_at
1,12345,dosen,EF01FFFFFFFF... (hex 1024+ karakter),2024-01-01

// Adafruit_Fingerprint_ESP32.cpp:513-534
uint8_t Adafruit_Fingerprint_ESP32::send_fpdata(const uint8_t* data, size_t length, 
                                                uint8_t sensorbuffer, uint8_t slot)

// fingerprint.cpp:352-441
bool FingerprintManager::injectSingleFingerprint(String nim, String hexData, int slot) {
    // 1. Parse hex string ke bytes
    uint8_t raw[FP_TEMPLATE_MAX];
    size_t rawLen = Adafruit_Fingerprint_ESP32::hexStringToBytes(hexData.c_str(),
                                                                 hexData.length(),
                                                                 raw,
                                                                 FP_TEMPLATE_MAX);

    // 2. Hapus template lama di slot tersebut (jika ada)
    finger->delete_model(slot);
    delay(100);

    // 3. Kirim data ke sensor buffer
    result = finger->send_fpdata(raw, rawLen, FP_BUFFER_CHAR, 1);

    // 4. Simpan ke slot permanen di sensor
    result = finger->store_model(slot, 1);

    // 5. Update RAM cache
    fingerMap[slot] = nim;
}

// Adafruit_Fingerprint_ESP32.cpp:171-208
void Adafruit_Fingerprint_ESP32::_send_data(const uint8_t* data, size_t length) {
    // Data dikirim dalam chunk (32/64/128/256 bytes per packet)
    // bergantung pada packet size setting

    for (size_t i = 0; i < whole; i++) {
        // Kirim header 9 byte
        // Kirim data chunk
        // Kirim checksum 2 byte
        delay(5);  // Inter-packet delay (sensor MCU lambat)
    }
}

┌──────────────────────────────────────────────────────────────────────┐
│                    ENROLLMENT FLOW                                   │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  USER SIDE                   SENSOR                    SD/CSV        │
│  ────────                    ──────                    ──────        │
│     │                          │                         │           │
│     │  tempel jari             │                         │           │
│     │─────────────────────────>│                         │           │
│     │                          │                         │           │
│     │                    get_image()                     │           │
│     │                    image_2_tz(1)                   │           │
│     │                          │                         │           │
│     │  angkat jari             │                         │           │
│     │<─────────────────────────│                         │           │
│     │                          │                         │           │
│     │  tempel jari lagi        │                         │           │
│     │─────────────────────────>│                         │           │
│     │                          │                         │           │
│     │                    get_image()                     │           │
│     │                    image_2_tz(2)                   │           │
│     │                          │                         │           │
│     │                    create_model()                  │           │
│     │                    (gabungkan buffer 1 & 2)        │           │
│     │                          │                         │           │
│     │                    get_fpdata()                    │           │
│     │<─────────────────────────│ ( Kirim template )      │           │
│     │                          │                         │           │
│  bytesToHexString()            │                         │           │
│     │                          │                         │           │
│     │                    store_model(slot)               │           │
│     │                          │                         │           │
│     │                          │                   SIMPAN KE CSV     │
│     │                          │─────────────────────────>│
│     │                          │                         │           │
└──────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│                    INJECTION FLOW                                    │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  CSV FILE                  SENSOR                     RAM            │
│  ───────                   ──────                     ────           │
│     │                          │                         │           │
│ Baca hex string                │                         │           │
│     │                          │                         │           │
│  hexToBytes()                  │                         │           │
│     │                          │                         │           │
│     │                    delete_model(slot)              │           │
│     │                          │                         │           │
│     │                    send_fpdata()                   │           │
│     │───────────────────────>│ ( Kirim bytes )           │           │
│     │                          │                         │           │
│     │                    store_model(slot)               │           │
│     │                          │                         │           │
│     │                          │                   fingerMap[slot]=nim│
│     │                          │─────────────────────────>│           │
│     │                          │                         │           │
└──────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│                    VERIFICATION FLOW                                 │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  USER                    SENSOR                     RAM              │
│  ────                    ──────                     ────             │
│     │                          │                         │           │
│  tempel jari                   │                         │           │
│     │───────────────────────>│                         │           │
│     │                          │                         │           │
│     │                    get_image()                   │           │
│     │                    image_2_tz(1)                 │           │
│     │                          │                         │           │
│     │                    finger_search()               │           │
│     │                    atau                          │           │
│     │                    finger_fast_search()          │           │
│     │                          │                         │           │
│     │                    Returns:                      │           │
│     │                    - finger_id (slot)            │           │
│     │                    - confidence score            │           │
│     │<───────────────────────│                         │           │
│     │                          │                         │           │
│  fingerMap[finger_id] ───────────────────────────────────────────>│
│     │                          │                         │           │
│  user_id ditemukan!            │                         │           │
│     │                          │                         │           │
└──────────────────────────────────────────────────────────────────────┘

// include/Adafruit_Fingerprint_ESP32.h
#define FP_TEMPLATE_MAX        2048   // Max buffer untuk template
#define FP_STARTCODE           0xEF01
#define FP_COMMANDPACKET       0x1
#define FP_DATAPACKET          0x2
#define FP_ACKPACKET           0x7
#define FP_ENDDATAPACKET       0x8

Kode,Konstanta,Arti
0x00,FP_OK,Berhasil
0x01,FP_PACKETRECEIVEERR,Error penerimaan packet
0x02,FP_NOFINGER,Tidak ada jari
0x03,FP_IMAGEFAIL,Gagal enroll gambar
0x07,FP_FEATUREFAIL,Gagal generate character
0x08,FP_NOMATCH,Jari tidak cocok
0x09,FP_NOTFOUND,Jari tidak ditemukan di library
0x0A,FP_ENROLLMISMATCH,Dua jari tidak cocok saat enroll
0x0B,FP_BADLOCATION,Slot tidak valid
0x18,FP_FLASHERR,Error writing flash

Operasi,Fungsi,Keterangan
Capture Image,get_image(),Ambil gambar dari sensor
Process to Char,image_2_tz(slot),Generate feature template
Combine Template,create_model(),Gabungkan 2 char buffer
Extract,get_fpdata(),Ambil template dari sensor ke host
Inject,send_fpdata(),Kirim template dari host ke sensor
Save Permanently,store_model(slot),Simpan ke slot di flash sensor
Delete,delete_model(slot),Hapus template
Search,finger_search() / finger_fast_search(),Cari match