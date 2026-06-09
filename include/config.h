#pragma once

//-------------------------------------
// Fingerprint UART
#define FP_RX 32
#define FP_TX 33

//-------------------------------------
// Keypad
#define COL1 13
#define COL2 15
#define COL3 14
#define COL4 27

#define ROW1 26
#define ROW2 25
#define ROW3 16 
#define ROW4 17


//-------------------------------------
// SPI Bus
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18

// Micro SD
#define SD_CS 5

// USB (CH376)
#define USB_CS 4
#define USB_INT 34

//-------------------------------------
// LCD I2C
#define LCD_ADDR 0x27
#define LCD_COLS 20
#define LCD_ROWS 4
#define I2C_SDA 21
#define I2C_SCL 22

//-------------------------------------
// RESET BUTTON
#define RESET_PIN 35

//-------------------------------------
// ========== CONTEXT-AWARE SYNC STATUS (3 FASE) ==========
// Setiap flag dipakai oleh halaman/state tertentu via getCurrentSyncStatus().
extern bool syncStatusUsers;      // Fase 1: Login / Tarik data users (/api/device/users)
extern bool syncStatusDashboard;  // Fase 2: Menu Dosen/Admin (dashboard)
extern bool syncStatusFeature;    // Fase 3: Dalam fitur (DPK, Batch Pull FP, dll)

// Snapshot state machine saat ini, dipublikasikan oleh StateMachine::update()
// agar getCurrentSyncStatus() bisa memilih flag yang sesuai konteks.
extern int g_currentState;

// Pilih flag mana yang dibaca berdasarkan g_currentState.
bool getCurrentSyncStatus();

// ADAPTIVE LOADING: true bila kelas > 99 mhs (hanya fp_1 diinjeksi -> butuh menu Jari Cadangan).
// Di-set di flushAndInjectPresensiUsers(), dibaca di STATE_PRESENSI_SCANNING.
extern bool isAdaptiveMode;

//-------------------------------------
// ========== RAM CACHE FINGERPRINT MAPPING ==========
// Index = Slot ID (1-200), Value = user_id (NIP/NIM)
// Diisi saat boot dari fingerprint.csv, di-update saat enrollment baru
// Lookup O(1) - tidak perlu baca CSV yang bisa >3000 karakter
extern String fingerMap[201];