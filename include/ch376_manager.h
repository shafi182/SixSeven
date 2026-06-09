#pragma once
#include <Arduino.h>
#include <SPI.h>

/*
 * CH376 USB Manager - Deklarasi Pin dan Koneksi
 * =============================================
 * MODUL: CH376S (mode SPI)
 *
 * KONEKSI KE ESP32 (SPI Mode):
 * -----------------------------
 * Bus SPI (SHARED):
 *   MISO -> ESP32 GPIO 12 (D6)
 *   MOSI -> ESP32 GPIO 13 (D7)
 *   SCK  -> ESP32 GPIO 14 (D5)
 *
 * Chip Select (CS):
 *   SD Card CS   -> ESP32 GPIO 5
 *   CH376 CS     -> ESP32 GPIO 4
 *
 * Interrupt:
 *   CH376 INT    -> ESP32 GPIO 34
 *
 * Wiring tambahan CH376:
 *   VCC -> 5V
 *   GND -> GND
 *   RST -> 5V
 */

// Pin definitions
#define SD_CS_PIN 5
#define CH376_CS_PIN 4
#define CH376_INT_PIN 34

// SPI Settings
#define CH376_SPI_SPEED 1000000  // 1MHz

// CH376 Commands
#define CH376_CMD_SET_USB_MODE 0x15
#define CH376_CMD_GET_IC_VER 0x01
#define CH376_CMD_CHECK_EXIST 0x06
#define CH376_CMD_SET_FILE_NAME 0x2F
#define CH376_CMD_FILE_OPEN 0x32
#define CH376_CMD_FILE_CREATE 0x34
#define CH376_CMD_FILE_WRITE 0x3C
#define CH376_CMD_FILE_CLOSE 0x36
#define CH376_CMD_BYTE_READ 0x37
#define CH376_CMD_BYTE_RD_GO 0x38
#define CH376_CMD_BYTE_WRITE 0x3D
#define CH376_CMD_BYTE_WR_GO 0x39
#define CH376_CMD_DIR_CREATE 0x3F
#define CH376_CMD_GET_STATUS 0x22

// Status codes
#define CH376_USB_READY 0x14
#define CH376_USB_DETECT 0x01
#define CH376_INT_SUCCESS 0x14
#define CH376_INT_DISCONNECT 0x16

class CH376Manager {
private:
    bool initialized;
    bool usbConnected;
    uint8_t lastStatus;

    // Buffer for file operations
    uint8_t buffer[512];

    // Internal methods
    bool sendCommand(uint8_t cmd);
    bool waitResponse(uint8_t timeout_ms = 200);
    uint8_t readData();
    bool writeData(const uint8_t* data, uint16_t len);

public:
    CH376Manager();

    // Test raw SPI connection to CH376
    bool testCH376();

    // Ping CH376 module (public for state machine access)
    bool pingCH376();

    // Initialize CH376 module
    void init();

    // Check if USB device is connected
    bool isUSBDeviceConnected();

    // Check if USB host is ready
    bool isUSBReady();

    // Mount USB drive
    bool mountUSB();

    // Unmount USB drive
    void unmountUSB();

    // Open file for writing (create if not exists)
    bool openFileWrite(const char* filename);

    // Write data to open file
    bool writeToFile(const uint8_t* data, uint16_t len);

    // Close current file
    bool closeFile();

    // Check if USB is disconnected (for eject detection)
    bool isUSBDisconnected();

    // Get last error status
    uint8_t getLastStatus();
};

// ========== HELPER FUNCTIONS ==========
// Copy single file from SD to USB
bool copyFileToUSB(const char* sourcePath, const char* destFilename);

// Export all data files to USB (non-blocking with buffer)
bool exportAllDataToUSB();