import time
import serial
import adafruit_fingerprint
import binascii

# --- KONFIGURASI ---
uart = serial.Serial("/dev/ttyS0", baudrate=57600, timeout=1)
finger = adafruit_fingerprint.Adafruit_Fingerprint(uart)

FILE_DATABASE = "database_siswa.csv"

def set_lampu(warna, mode):
    try:
        finger.set_led(color=warna, mode=mode)
    except:
        pass

def simpan_ke_csv(nim, data_hex):
    """
    Menyimpan NIM dan Data Hexadecimal sidik jari ke CSV.
    Format: NIM, DATA_SENSOR_HEX
    """
    # Cek header
    try:
        with open(FILE_DATABASE, "r") as f:
            pass
    except FileNotFoundError:
        with open(FILE_DATABASE, "w") as f:
            f.write("NIM,DATA_SENSOR\n")

    # Simpan data
    with open(FILE_DATABASE, "a") as f:
        # Kita tulis NIM, lalu koma, lalu string panjang hex
        f.write(f"{nim},{data_hex}\n")
    print(f"📝 Data Binary untuk NIM {nim} tersimpan!")

def enroll_dan_ambil_binary():
    """
    Proses: Ambil gambar -> Convert ke Template -> Tarik data Binary ke Python
    """
    print("Tempelkan jari...")
    set_lampu(2, 1) # Biru Nafas
    
    # --- PENGAMBILAN GAMBAR 1 ---
    while True:
        if finger.get_image() == adafruit_fingerprint.OK:
            set_lampu(2, 3) 
            break

    if finger.image_2_tz(1) != adafruit_fingerprint.OK:
        print("Gagal proses gambar 1.")
        set_lampu(1, 2)
        return None

    print("Angkat jari...")
    set_lampu(2, 4)
    time.sleep(1)
    while finger.get_image() != adafruit_fingerprint.NOFINGER:
        pass

    # --- PENGAMBILAN GAMBAR 2 ---
    print("Tempelkan jari LAGI...")
    set_lampu(3, 1) # Ungu
    
    while True:
        if finger.get_image() == adafruit_fingerprint.OK:
            set_lampu(3, 3)
            break

    if finger.image_2_tz(2) != adafruit_fingerprint.OK:
        print("Gagal proses gambar 2.")
        set_lampu(1, 2)
        return None

    # --- BUAT MODEL ---
    print("Mencocokkan...")
    if finger.create_model() != adafruit_fingerprint.OK:
        print("Jari tidak cocok/goyang.")
        set_lampu(1, 2)
        return None
    
    # --- EKSTRAKSI DATA BINARY (THE MAGIC PART) ---
    print("Mengambil data binary dari sensor...")
    # get_fpdata mengambil template dari CharBuffer1 (tempat create_model menyimpan hasil)
    data_bytes = finger.get_fpdata(sensorbuffer="char", slot=1)
    
    if data_bytes is None:
        print("Gagal menarik data dari sensor.")
        return None

    # Konversi byte array ke Hex String agar aman masuk CSV
    # Contoh hasil: "ef01ffff..."
    data_hex = binascii.hexlify(bytearray(data_bytes)).decode('utf-8')
    
    set_lampu(4, 3) # Hijau
    return data_hex

# --- MAIN LOOP ---
print("=== ENROLLMENT BINARY MODE ===")

try:
    while True:
        print("\n-----------------------------------")
        nim_input = input("Masukkan NIM (atau 'q' keluar): ")
        if nim_input.lower() == 'q':
            set_lampu(4, 4)
            break
        
        if not nim_input: 
            continue

        # Proses Enroll
        hex_template = enroll_dan_ambil_binary()
        
        if hex_template:
            # Simpan ke CSV
            simpan_ke_csv(nim_input, hex_template)
            print("Sukses!")
            time.sleep(1)
            set_lampu(4, 4)
        else:
            print("Gagal Enroll.")

except KeyboardInterrupt:
    set_lampu(4, 4)
