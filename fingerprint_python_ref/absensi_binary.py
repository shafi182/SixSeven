import time
import serial
import sys
import adafruit_fingerprint
import binascii
from datetime import datetime

# --- KONFIGURASI ---
# Gunakan /dev/ttyS0 (Pin 8 & 10 pada Raspberry Pi)
try:
    uart = serial.Serial("/dev/ttyS0", baudrate=57600, timeout=1)
    finger = adafruit_fingerprint.Adafruit_Fingerprint(uart)
except Exception as e:
    print(f"❌ Error membuka Serial Port: {e}")
    sys.exit(1)

FILE_DATABASE = "database_siswa.csv"     # Sumber data (NIM, HEX_DATA)
FILE_LAPORAN = "laporan_absensi.csv"     # Hasil absensi
TIMEOUT_DETIK = 10                       # Waktu tunggu jari

# List global untuk menyimpan pemetaan ID Sensor (sementara) -> NIM
cache_nim_siswa = [] 

# --- FUNGSI LED R503 (RGB) ---
def set_lampu(warna, mode):
    """
    KODE WARNA: 1=Merah, 2=Biru, 3=Ungu, 4=Hijau
    MODE: 1=Nafas, 2=Kedip, 3=Nyala Terus, 4=Mati
    """
    try:
        finger.set_led(color=warna, mode=mode)
    except:
        pass

# --- FUNGSI UTAMA: LOAD CSV KE SENSOR ---
def load_database_ke_sensor():
    """
    Membaca CSV (NIM, HEX), lalu upload ke sensor dengan jeda waktu aman.
    """
    print("\n🔄 Memuat database ke sensor... Mohon tunggu.")
    
    # 1. Kosongkan memori sensor dulu agar bersih
    if finger.read_templates() != adafruit_fingerprint.OK:
        print("⚠️ Gagal membaca status sensor.")
        
    if finger.empty_library() != adafruit_fingerprint.OK:
        print("❌ Gagal membersihkan memori sensor. Cek koneksi kabel.")
        sys.exit(1)
    
    global cache_nim_siswa
    cache_nim_siswa = [] # Reset cache
    
    try:
        with open(FILE_DATABASE, "r") as file:
            # Lewati Header jika ada
            first_line = file.readline()
            if not first_line.startswith("NIM"):
                file.seek(0) # Jika tidak ada header, kembali ke awal
            
            id_counter = 0 # Kita mulai isi dari ID 0 di sensor
            
            for line in file:
                line = line.strip()
                if not line: continue # Lewati baris kosong
                
                parts = line.split(",")
                if len(parts) >= 2:
                    nim = parts[0]
                    hex_data = parts[1]
                    
                    print(f" -> Proses NIM {nim}...", end="", flush=True)
                    
                    try:
                        # Convert Hex String kembali ke Byte Array
                        template_bytes = binascii.unhexlify(hex_data)
                        
                        # A. Upload ke Buffer 1 Sensor
                        # send_fpdata mengembalikan True jika sukses
                        if not finger.send_fpdata(template_bytes, "char", 1):
                            print(" [GAGAL UPLOAD]")
                            continue
                            
                        # Jeda 0.1 detik setelah upload agar buffer stabil
                        time.sleep(0.1) 
                        
                        # B. Simpan Buffer 1 ke Flash Memory Sensor
                        if finger.store_model(id_counter) == adafruit_fingerprint.OK:
                            # Simpan mapping di Python
                            cache_nim_siswa.append(nim)
                            print(f" [OK] -> ID {id_counter}")
                            id_counter += 1
                        else:
                            print(" [GAGAL STORE]")
                        
                        # --- PENTING: Jeda 0.5 detik agar sensor sempat menulis ke flash ---
                        time.sleep(0.5) 
                        
                    except Exception as e:
                        print(f" [ERROR: {e}]")
                        continue

        print(f"✅ Selesai! {len(cache_nim_siswa)} sidik jari berhasil dimuat.")
        print("--------------------------------------------------")
        
    except FileNotFoundError:
        print("\n❌ File database belum ada. Silakan jalankan enroll dulu.")
        sys.exit(0)

# --- FUNGSI SIMPAN LAPORAN ---
def simpan_laporan(nim):
    waktu_skrg = datetime.now()
    timestamp = waktu_skrg.strftime("%d/%m/%Y %H:%M:%S")
    
    print("---------------------------------")
    print(f"✅ STATUS  : HADIR")
    print(f"👤 NIM     : {nim}")
    print(f"⏰ WAKTU   : {timestamp}")
    print("---------------------------------")
    
    try:
        with open(FILE_LAPORAN, "a") as file:
            file.write(f"{nim},{timestamp}\n")
    except Exception as e:
        print(f"Gagal simpan laporan: {e}")

# --- PROGRAM UTAMA ---
print("=== ABSENSI (MODUL BINARY LOADER - SAFE MODE) ===")

if finger.read_templates() != adafruit_fingerprint.OK:
    print("❌ Gagal komunikasi dengan sensor.")
    sys.exit(1)

# 1. Load data dari CSV ke Sensor setiap kali program jalan
load_database_ke_sensor()

# 2. Masuk ke Mode Standby
set_lampu(2, 1) # Biru Nafas
print(f"Sensor Siap. Menunggu jari ({TIMEOUT_DETIK} detik)...", end="", flush=True)

start_time = time.time()
jari_terdeteksi = False

# 3. Loop Menunggu Jari
while True:
    # Cek Waktu
    durasi = time.time() - start_time
    if durasi > TIMEOUT_DETIK:
        print(f"\n⏰ Waktu Habis! Tidak ada jari.")
        set_lampu(2, 4) # Matikan lampu
        sys.exit(0)

    # Cek Sensor
    if finger.get_image() == adafruit_fingerprint.OK:
        jari_terdeteksi = True
        break

# 4. Proses Jari (Jika terdeteksi)
if jari_terdeteksi:
    print("\nMemproses...", end="", flush=True)
    
    if finger.image_2_tz(1) != adafruit_fingerprint.OK:
        print("\n❌ Gagal memproses gambar.")
        set_lampu(1, 2) # Merah Kedip
        sys.exit(0)
    
    print(" Mencari...", end="", flush=True)
    
    # Cari di memori sensor (yang baru saja kita isi dari CSV)
    if finger.finger_search() != adafruit_fingerprint.OK:
        print("\n❌ Jari tidak dikenali (Tidak ada di database CSV).")
        set_lampu(1, 2) # Merah Kedip
        time.sleep(1)
        set_lampu(1, 4)
        sys.exit(0)

    # --- JIKA SUKSES ---
    # finger.finger_id adalah lokasi di sensor (0, 1, 2...)
    lokasi_id = finger.finger_id
    
    # Kita cek ID tersebut milik NIM siapa dari list cache kita
    if lokasi_id < len(cache_nim_siswa):
        nim_ketemu = cache_nim_siswa[lokasi_id]
        
        set_lampu(4, 3) # Hijau Nyala Terus
        simpan_laporan(nim_ketemu)
    else:
        print(f"\n❌ Error: ID Sensor ({lokasi_id}) diluar index cache.")

    time.sleep(2)
    set_lampu(4, 4) # Matikan Lampu
    print("Selesai.")
