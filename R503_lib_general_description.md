# Penjelasan Rinci Cara Kerja Library Fingerprint `developed`

Library yang Anda bangun di dalam folder `developed` terbagi menjadi dua lapisan utama:
1. **Low-Level Layer (`Adafruit_Fingerprint_ESP32`)**: Bertugas untuk komunikasi langsung ke *hardware* (sensor R503) melalui paket byte serial.
2. **High-Level Layer (`FingerprintManager`)**: Bertindak sebagai *controller* aplikasi yang mengurus logika bisnis seperti inisialisasi, sinkronisasi file CSV, pencarian ID, dan pendaftaran sidik jari secara *non-blocking*.

Berikut adalah penjelasan teknis langkah demi langkah mengenai cara kerja sistem yang Anda bangun:

---

## 1. Proses Booting dan Negosiasi Baud Rate (`init()`)
Sensor sidik jari secara default (dari pabrik) berkomunikasi pada kecepatan 57600 bps. Namun, untuk memindahkan data gambar sidik jari atau template ke memori ESP32 dibutuhkan kecepatan yang lebih tinggi agar tidak *lag*. 

Cara kerjanya:
- Saat fungsi `init()` dipanggil, ESP32 mencoba menyapa sensor di **115200 bps** terlebih dahulu. Jika ini bukan pertama kali mesin dinyalakan, sensor akan langsung merespons karena kecepatannya sudah disetel sebelumnya.
- Jika sensor tidak menjawab (karena masih berada di 57600 bps), ESP32 akan melakukan *fallback* turun ke 57600 bps. 
- Pada 57600 bps, ESP32 menyapa sensor dan mengirim *packet command* mentah `SetSysPara` (pengaturan parameter sistem) untuk mengubah *Baud Rate* modul menjadi skala 12 (12 * 9600 = 115200 bps).
- ESP32 kemudian melakukan reset koneksi *Hardware Serial*-nya sendiri, naik kembali ke 115200 bps, dan mengamankan koneksi. 

---

## 2. Sistem Manajemen Slot (Dosen vs Mahasiswa)
Memori internal modul R503 biasanya terbatas (misalnya, menampung hingga 200 data sidik jari). Anda membuat aturan spesifik:
- **Slot 1 & 2** dilindungi dan diprioritaskan hanya untuk pengguna berstatus **Dosen/Admin**.
- **Slot 3 hingga 200** dikhususkan untuk **Mahasiswa**.

### Mengelola Slot Kosong
Saat mendaftarkan mahasiswa baru, fungsi `getLowestAvailableFingerprintId()` berjalan untuk mencari ID kosong:
1. Sistem akan mengecek langsung ke memori internal sensor mulai dari slot 3. Jika ada slot kosong, maka itu akan dipakai.
2. Jika semua slot di sensor terlihat penuh, sistem mengecek file `fingerprint_mahasiswa.csv` di SD Card. Tujuannya adalah mencari slot mana yang ada di CSV tetapi bisa ditimpa (*overwrite*).

---

## 3. Sinkronisasi Data dari SD Card ke Sensor (`syncFromCSV()`)
Anda merancang sistem ini sedemikian rupa sehingga Sensor bertindak hanya sebagai "mesin validasi biometrik", sementara **Source of Truth** (sumber data utama yang valid) adalah SD Card (CSV).

Setiap kali sistem dinyalakan, aliran kerjanya adalah:
1. **Force Flush (Pengosongan Sensor):** Memanggil `empty_library()` untuk menghapus semua memori dari otak sensor R503. Jika gagal, sistem mencoba menghapusnya per-slot menggunakan perintah `delete_model()`.
2. **Inject (Membangun Ulang):** Sistem membaca `fingerprint_users.csv` dan `fingerprint_mahasiswa.csv`. Di sana, data mentah bentuk teks Hexadesimal (`hexData`) dari masing-masing jari akan diproses:
   - Teks Hexadesimal diubah kembali menjadi deretan byte mentah melalui fungsi `hexStringToBytes()`.
   - Byte mentah tersebut dimasukkan ke dalam memori penyangga (buffer) sensor menggunakan `send_fpdata()`.
   - Perintah `store_model()` dipanggil untuk memindahkan data dari buffer sensor ke ruang penyimpanan internal sensor (Slot ID).
3. **Mekanisme Ketahanan (Retry & Backup):** Saat melakukan langkah injeksi di atas, jika terjadi *error* komunikasi (seperti buffer sibuk), sistem mencoba (`retry`) sebanyak 2 kali per jari. Jika gagal secara absolut, NIM mahasiswa akan dicatat ke `failed_inject.csv`.

---

## 4. Kecepatan Otentikasi Super Cepat (RAM Caching O(1))
Saat seseorang meletakkan sidik jari di sensor untuk melakukan absensi:
- Fungsi `scan()` berjalan: Mengambil gambar, mengonversinya menjadi fitur biometrik (`image_2_tz`), lalu mencari kecocokannya dengan data internal sensor (`finger_search`).
- Sensor mengembalikan hasil berupa **Slot ID (Misalnya: Slot 45)**.
- **Masalah:** Slot 45 itu NIM-nya berapa? Membaca SD Card (CSV) di tengah proses sangat lambat.
- **Solusi Anda:** Anda memiliki Array bernama `fingerMap` di RAM. Saat proses `syncFromCSV()` atau `loadTemplatesFromCSV()` di awal *booting*, setiap NIM dimasukkan ke Array memori berdasarkan indeks slotnya (`fingerMap[45] = "20211022"`).
- Karena itu, saat sensor menjawab "Slot 45", sistem Anda langsung mengekstrak NIM tanpa _delay_ lewat fungsi `scanAndAuthenticate()`:
  ```cpp
  String userId = fingerMap[slotId];
  ```

---

## 5. Pendaftaran Sidik Jari secara *Non-Blocking* (`State Machine`)
Saat mendaftarkan jari, library asal membuat mikrokontroler "menunggu diam" (blocking/while-loop) hingga jari ditempelkan. Anda membangun konsep *State Machine* (Mesin Kondisi) di dalam `FingerprintManager`:
- `ENROLL_IDLE`: Siaga.
- `ENROLL_PLACE_FINGER`: Meminta user menempelkan jarinya yang pertama kali. Di tahap ini, program utama (looping utama ESP32) tidak terhenti, melainkan dapat terus memperbarui layar atau hal lainnya sambil sesekali memantau fungsi `processEnrollment()`.
- `ENROLL_PLACE_AGAIN`: Meminta pengguna mengangkat dan menempelkan ulang jarinya.
- Setelah sukses, data dikombinasikan menjadi model menggunakan `create_model()`, disimpan dengan `store_model()`, dan kemudian ID tersebut dicatat dalam memori dan dikembalikan ke antarmuka aplikasi.

---

## 6. Penarikan Template Ke Database (`extractTemplateAsHex`)
Setelah jari sukses didaftarkan (disimpan di suatu slot pada sensor), data tersebut perlu ditarik kembali keluar agar bisa disimpan ke CSV:
- Sistem memanggil `load_model()` untuk mengangkat profil dari slot penyimpanan internal R503 ke buffer sementara sensor.
- Kemudian fungsi khusus Anda, `get_fpdata()`, memompa data mentah tersebut dari buffer R503 menuju RAM ESP32.
- Data byte mentah tersebut diubah menjadi barisan string teks panjang menggunakan `bytesToHexString()`, yang mana string teks inilah yang ditulis rapi pada baris CSV (`data_jari`).

## Kesimpulan Cara Kerja
Secara keseluruhan, library Anda bekerja seperti sistem cache bertingkat. CSV di SD Card bertindak sebagai "Database Utama", sensor R503 bertindak sebagai "Koprosesor Biometrik", dan memori RAM (`fingerMap`) bertindak sebagai "Indeks Cepat". Teknik auto-baud rate serta ketahanan lewat *retry mechanism* menunjukkan bahwa rancangan perangkat lunak ini sangat disiapkan untuk kondisi mesin yang harus aktif terus-menerus (24/7).
