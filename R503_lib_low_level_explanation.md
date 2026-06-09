# Dokumentasi Fungsi Low-Level R503 (Layer ESP32)

Library `Adafruit_Fingerprint_ESP32.cpp` yang Anda buat bertindak sebagai jembatan komunikasi tingkat rendah (Low-Level) antara mikrokontroler ESP32 dengan perangkat keras sensor R503. Komunikasi ini sepenuhnya dilakukan melalui jalur Serial UART (TX/RX) dengan protokol pengiriman dan penerimaan paket byte khusus.

Berikut adalah penjelasan rincian untuk **setiap fungsi teknis** yang ada di dalam library tersebut:

---

## 1. Lapisan Inti Paket Serial (Packet I/O)

Bagian ini bertugas mengatur perakitan (assembly) dan pembongkaran paket data. Sensor R503 memiliki standar protokol yang sangat kaku, terdiri dari:
`Header(2) + Address(4) + PacketID(1) + Length(2) + Payload(Length-2) + Checksum(2)`

### `_send_packet(const uint8_t* data, uint16_t dataLen)`
- **Fungsi**: Membungkus data perintah (command payload) menjadi paket protokol R503 yang utuh lalu mengirimkannya via UART.
- **Cara Kerja**:
  1. Menyiapkan Array `header` sebanyak 9 byte.
  2. Mengisi 2 byte awal dengan `FP_STARTCODE` (`0xEF 0x01`).
  3. Mengisi 4 byte alamat (`_address`, standarnya `0xFFFFFFFF`).
  4. Menetapkan ID paket (PacketType) menjadi `FP_COMMANDPACKET`.
  5. Menghitung `Length` = Panjang *Payload* (`dataLen`) + 2 byte *Checksum*.
  6. Melakukan operasi matematika untuk menghitung *Checksum* (Jumlah dari: PacketType + Length + Data Payload).
  7. Mengirimkan Header, Data Payload, dan Checksum ke sensor secara berurutan menggunakan `_uart->write()`.
  8. Fungsi diakhiri dengan `_uart->flush()` agar memastikan antrean (buffer) transmisi ESP32 telah tereksekusi penuh sebelum masuk ke baris kode selanjutnya.

### `_get_packet(uint16_t expected, uint8_t* reply, size_t replyCap, size_t* replyLen)`
- **Fungsi**: Menangkap dan membongkar respons (ACK) dari sensor R503.
- **Cara Kerja**:
  1. Melakukan validasi dasar dengan memanggil `_readBytes()` guna mengambil 9 byte Header terlebih dahulu.
  2. Memeriksa `FP_STARTCODE` dan Alamat, jika salah atau kacau, fungsi akan melempar kode `FP_BADPACKET`.
  3. Memastikan bahwa paket yang diterima adalah paket konfirmasi `FP_ACKPACKET`.
  4. Membaca nilai Payload Length untuk mengetahui sisa byte yang harus ditunggu (dibaca) dari UART.
  5. Memasukkan *Payload* ke dalam array penampung `reply` melalui sisa byte length tersebut.
  6. Terakhir, membuang 2 byte Checksum dari buffer pembacaan. (Checksum sengaja diabaikan untuk efisiensi eksekusi).

### `_readBytes()` & `_drainRx()`
- **`_readBytes()`**: Fungsi sinkron/bloking yang menunggu kedatangan jumlah `n` byte ke UART ESP32 hingga batas waku (timeout) yang diizinkan (standarnya 2 detik).
- **`_drainRx()`**: Berfungsi seperti penyapu sampah. Ini dipanggil di awal komunikasi untuk membersihkan antrean sisa-sisa byte lama (`_uart->read()`) yang mungkin tersangkut sebelum transaksi baru dimulai.

---

## 2. Operasi Data Massal / Streaming (Template Transfer)

Fungsi di bawah ini adalah fitur krusial yang Anda tambahkan guna mengekstrak atau menyuntikkan template sidik jari berukuran besar (~512 bytes). Data besar dari/ke R503 tidak bisa dikirim dalam satu paket, melainkan dipecah (chunking) menjadi paket data.

### `_send_data(const uint8_t* data, size_t length)`
- **Fungsi**: Mentransmisikan data berukuran besar secara bertahap menuju modul sidik jari.
- **Cara Kerja**:
  1. Menghitung ukuran bongkahan (chunk) dari paket, misal 128 bytes (sesuai setting `data_packet_size` dari sensor).
  2. Mengulang (Loop) pengiriman array besar ke potongan kecil (chunk).
  3. Menyusun header untuk setiap chunk. Jenis paket di-set ke `FP_DATAPACKET` untuk potongan biasa, dan jika sudah mencapai potongan paling akhir akan menggunakan tipe `FP_ENDDATAPACKET`.
  4. Menambahkan jeda `delay(5)` antar-paket agar otak lambat sensor (internal MCU R503) dapat menyimpan byte ke buffer SRAM tanpa hilang (Dropped Frames).

### `_get_data(uint8_t* buffer, size_t bufferSize, size_t* outLen)`
- **Fungsi**: Mengumpulkan beberapa paket data (chunk) beruntun dari sensor R503 dan merangkainya menjadi satu array panjang utuh di ESP32.
- **Cara Kerja**:
  1. Mengunci ESP32 dalam *While(true)* loop.
  2. Setiap putaran, ESP32 membaca 1 paket (`_readBytes()`).
  3. Memastikan jenis paket yang masuk adalah `FP_DATAPACKET`.
  4. Menulis rentang *Payload* tersebut ke Array memori utama (`buffer` RAM ESP32).
  5. Membuang Checksum setiap paket.
  6. Loop akan terus berputar menyambungkan data hingga mendeteksi tipe paket `FP_ENDDATAPACKET` dari sensor, yang menandai bahwa transmisi selesai dan fungsi bisa `break` loop.

---

## 3. Pembungkus (Wrapper) Spesifik Data Template

### `get_fpdata()`
1. Mengirimkan perintah `FP_UPLOAD` (untuk model karakter) atau `FP_UPLOADIMAGE` (untuk gambar raw) via `_send_packet()`.
2. Menunggu Sensor mengiyakan perintah ini via `_get_packet(12, reply...)`.
3. Setelah sensor bilang "OK", fungsi ini melepaskan kendali dan menyerahkannya ke fungsi *streamer* `_get_data()` untuk menyedot sisa paket dari UART.

### `send_fpdata()`
1. Kebalikannya, mengirim instruksi `FP_DOWNLOAD` (siap-siap terima data) via `_send_packet()`.
2. Menunggu respons "OK" (ACK) dari sensor via `_get_packet()`.
3. Segera setelah ACK diterima, langsung memuntahkan rentetan paket *chunk* ke UART via `_send_data()`.

---

## 4. Pembungkus Perintah Sensor (High-Level Wrappers)

Terdapat puluhan fungsi operasional, contohnya:
- `begin()`: Mengatur kecepatan awal UART dan membersihkan jalur. Lalu melakukan verifikasi *password* ke modul serta membaca kapasitas sensor.
- `verify_password()`: Mengirimkan perintah `FP_VERIFYPASSWORD` ditambah 4-byte *password* rahasia, lalu mengecek apakah nilainya cocok.
- `read_sysparam()`: Meminta pengaturan sensor (seperti `baud_rate`, `library_size`, `data_packet_size`). Hasil responsnya (16 bytes payload) dibongkar lalu disalin ke masing-masing variabel anggota objek (misal `this->library_size = ...`).
- `get_image()` & `image_2_tz()`: Memerintahkan modul R503 untuk mengambil gambar dari lensa optik, dan mengubah/ekstrak gambar tersebut ke format biometrik/char buffer internal (Tz).
- `store_model()` & `load_model()`: Memerintahkan sensor menyimpan buffer Tz ke dalam Slot Flash, atau memuat model dari Slot Flash ke RAM internal R503 (buffer Tz).
- `finger_search()`: Mengeksekusi pencarian sidik jari massal (Looping di dalam R503). Menghasilkan `finger_id` (slot cocok) dan `confidence` (tingkat kemiripan sidik jari).
- `read_templates()`: Menggunakan perintah langka `FP_TEMPLATEREAD` untuk membaca peta indeks (Bitmap) halaman flash R503. Ini sangat low-level dan mengembalikan *bitmask* mana saja slot 1 hingga 200 yang telah terisi (angka bit 1).

---

## 5. Alat Bantu Konversi (Hex Utilities)

Database seperti MySQL atau CSV tidak bagus dalam menyimpan tipe data `byte` mentah. Oleh karenanya, Anda membuat modul parser mandiri:
- `bytesToHexString()`: Mengambil deretan byte (misal `0x0F 0x1A`), dipisah menjadi nibble atas dan bawah melalui operasi bitwise (geser `>> 4` dan operasi AND `& 0x0F`), kemudian dipetakan ke karakter teks *"0123456789ABCDEF"*.
- `hexStringToBytes()`: Fungsi pengubah String panjang ("0F1A...") kembali ke byte. Berjalan dengan mengambil 2 karakter teks `char`, melihat nilai desimalnya `_hexNibble()`, menggabungkannya kembali memakai operasi bitwise shift (`<< 4`), menjadi 1 byte murni.

Semua penulisan fungsi *Low-Level* di atas dirancang untuk mengeksploitasi R503 tanpa batasan library Arduino standar, dengan kontrol *buffer* dan *timeout* tingkat tinggi.
