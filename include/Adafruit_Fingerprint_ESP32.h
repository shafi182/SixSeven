// Adafruit_Fingerprint_ESP32.h
// C++ port of Adafruit_CircuitPython_Fingerprint for ESP32 (Arduino / PlatformIO)
// Targets R503 (and compatible) UART fingerprint sensors.
//
// Original Python library: SPDX-FileCopyrightText: 2017 ladyada for Adafruit Industries
// SPDX-License-Identifier: MIT
//
// This port preserves the low-level packet structure so that the raw template
// data extracted by get_fpdata() is byte-compatible with the Python library
// (and therefore with any database populated by the Python enroller).

#ifndef ADAFRUIT_FINGERPRINT_ESP32_H
#define ADAFRUIT_FINGERPRINT_ESP32_H

#include <Arduino.h>
#include <HardwareSerial.h>

// ---- Packet framing ----
#define FP_STARTCODE          0xEF01
#define FP_COMMANDPACKET      0x01
#define FP_DATAPACKET         0x02
#define FP_ACKPACKET          0x07
#define FP_ENDDATAPACKET      0x08

// ---- Instruction codes ----
#define FP_GETIMAGE           0x01
#define FP_IMAGE2TZ           0x02
#define FP_COMPARE            0x03
#define FP_FINGERPRINTSEARCH  0x04
#define FP_REGMODEL           0x05
#define FP_STORE              0x06
#define FP_LOAD               0x07
#define FP_UPLOAD             0x08   // template -> host
#define FP_DOWNLOAD           0x09   // host -> template buffer
#define FP_UPLOADIMAGE        0x0A
#define FP_DOWNLOADIMAGE      0x0B
#define FP_DELETE             0x0C
#define FP_EMPTY              0x0D
#define FP_SETSYSPARA         0x0E
#define FP_READSYSPARA        0x0F
#define FP_HISPEEDSEARCH      0x1B
#define FP_VERIFYPASSWORD     0x13
#define FP_TEMPLATECOUNT      0x1D
#define FP_TEMPLATEREAD       0x1F
#define FP_SOFTRESET          0x3D
#define FP_GETECHO            0x53
#define FP_SETAURA            0x35   // LED aura (R503)

// ---- Confirmation / error codes (mirror Python constants) ----
#define FP_OK                 0x00
#define FP_PACKETRECEIVEERR   0x01
#define FP_NOFINGER           0x02
#define FP_IMAGEFAIL          0x03
#define FP_IMAGEMESS          0x06
#define FP_FEATUREFAIL        0x07
#define FP_NOMATCH            0x08
#define FP_NOTFOUND           0x09
#define FP_ENROLLMISMATCH     0x0A
#define FP_BADLOCATION        0x0B
#define FP_DBRANGEFAIL        0x0C
#define FP_UPLOADFEATUREFAIL  0x0D
#define FP_PACKETRESPONSEFAIL 0x0E
#define FP_UPLOADFAIL         0x0F
#define FP_DELETEFAIL         0x10
#define FP_DBCLEARFAIL        0x11
#define FP_PASSFAIL           0x13
#define FP_INVALIDIMAGE       0x15
#define FP_FLASHERR           0x18
#define FP_INVALIDREG         0x1A
#define FP_ADDRCODE           0x20
#define FP_PASSVERIFY         0x21
#define FP_MODULEOK           0x55

// Local error codes (do not collide with sensor codes above)
#define FP_TIMEOUT            0xFE
#define FP_BADPACKET          0xFD
#define FP_BUFFER_OVERFLOW    0xFC

// Sensor buffer selectors for get_fpdata / send_fpdata
#define FP_BUFFER_CHAR        0
#define FP_BUFFER_IMAGE       1

// ---- LED helpers (R503 aura) ----
#define FP_LED_RED            1
#define FP_LED_BLUE           2
#define FP_LED_PURPLE         3
#define FP_LED_GREEN          4   // observed working on R503

#define FP_LED_BREATHE        1
#define FP_LED_FLASH          2
#define FP_LED_ON             3
#define FP_LED_OFF            4
#define FP_LED_FADE_ON        5
#define FP_LED_FADE_OFF       6

// Safety cap for a single char-buffer template payload.
// R503 uploads the full char-file: empirically ~1536 bytes (12 x 128B packets
// when data_packet_size = 2). 2048 leaves margin without exhausting ESP32 RAM.
#define FP_TEMPLATE_MAX       2048

class Adafruit_Fingerprint_ESP32 {
public:
    // Construct against a HardwareSerial instance (e.g. &Serial2).
    // password is the 32-bit sensor password (default 0x00000000 like Python).
    Adafruit_Fingerprint_ESP32(HardwareSerial* serial, uint32_t password = 0x00000000UL);

    // Start the UART and verify the sensor handshake.
    // rxPin / txPin: use -1 to keep the HardwareSerial defaults.
    // Returns true if password verifies AND sysparams read back OK.
    bool begin(unsigned long baud = 57600, int8_t rxPin = -1, int8_t txPin = -1);

    // -------- High-level commands (match Python signatures) --------
    uint8_t verify_password();
    uint8_t read_sysparam();
    uint8_t set_sysparam(uint8_t param_num, uint8_t param_val);
    uint8_t get_image();
    uint8_t image_2_tz(uint8_t slot = 1);
    uint8_t create_model();
    uint8_t store_model(uint16_t location, uint8_t slot = 1);
    uint8_t load_model(uint16_t location, uint8_t slot = 1);
    uint8_t delete_model(uint16_t location);
    uint8_t empty_library();
    uint8_t finger_search();        // search across whole library, slot 1
    uint8_t finger_fast_search();   // high-speed variant
    uint8_t compare_templates();    // compare char buffers 1 vs 2
    uint8_t count_templates();
    uint8_t soft_reset();

    // Read the bitmap of occupied flash slots from the sensor.
    // Fills templateList[] with the slot numbers that contain a template.
    // foundCount: (out) number of slots written to templateList.
    // Returns FP_OK or an error code.
    uint8_t read_templates(uint16_t* templateList, size_t maxLen, uint16_t* foundCount);

    // R503 LED control. color/mode constants above.
    uint8_t set_led(uint8_t color = FP_LED_BLUE,
                    uint8_t mode  = FP_LED_ON,
                    uint8_t speed = 0x80,
                    uint8_t cycles = 0);

    // -------- CRITICAL: template I/O for external DB storage --------

    // Read a char-buffer template (or image) out of the sensor.
    //   sensorbuffer: FP_BUFFER_CHAR or FP_BUFFER_IMAGE
    //   slot:         1 or 2 (only meaningful for FP_BUFFER_CHAR)
    //   buffer:       caller-provided byte buffer
    //   bufferSize:   capacity of buffer
    //   outLen:       (out) actual bytes written into buffer
    // Returns FP_OK on success, or an error code (FP_*, FP_TIMEOUT, FP_BUFFER_OVERFLOW...).
    uint8_t get_fpdata(uint8_t* buffer, size_t bufferSize, size_t* outLen,
                       uint8_t sensorbuffer = FP_BUFFER_CHAR, uint8_t slot = 1);

    // Inject a raw template (or image) back into the sensor's buffer.
    //   data/length: the bytes to send (e.g. from your SQL database, hex-decoded)
    //   sensorbuffer / slot: same meaning as get_fpdata
    // Returns FP_OK on success, or an error code.
    uint8_t send_fpdata(const uint8_t* data, size_t length,
                        uint8_t sensorbuffer = FP_BUFFER_CHAR, uint8_t slot = 1);

    // -------- HEX <-> bytes utilities (handy for SQL transport) --------
    static String  bytesToHexString(const uint8_t* data, size_t length);
    // Returns number of bytes decoded, or 0 on parse error.
    static size_t  hexStringToBytes(const String& hex, uint8_t* out, size_t outSize);
    static size_t  hexStringToBytes(const char* hex, size_t hexLen,
                                    uint8_t* out, size_t outSize);

    // -------- Tuning --------
    void setDebug(bool on) { _debug = on; }
    void setIOTimeout(uint32_t ms) { _timeoutMs = ms; }

    // -------- Public state populated by commands (mirrors Python attrs) --------
    uint16_t finger_id      = 0;
    uint16_t confidence     = 0;
    uint16_t template_count = 0;
    uint16_t library_size   = 0;
    uint16_t security_level = 0;
    uint16_t data_packet_size = 2;  // 0=32, 1=64, 2=128, 3=256 bytes
    uint16_t baudrate_param = 0;
    uint16_t system_id      = 0;
    uint16_t status_register = 0;
    uint8_t  device_address[4] = {0xFF, 0xFF, 0xFF, 0xFF};

private:
    HardwareSerial* _uart;
    uint32_t        _password;
    uint8_t         _address[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t        _timeoutMs  = 2000;
    bool            _debug      = false;

    // ---- Low-level packet I/O (faithful port of the Python private helpers) ----

    // Build and transmit a command packet. data is the instruction payload
    // (instruction byte + arguments).
    void _send_packet(const uint8_t* data, uint16_t dataLen);

    // Read an ACK packet. Writes its reply payload into reply[].
    // replyCap MUST be at least (expected - 9 + 2) bytes; in practice 32 is plenty.
    // expected matches the Python "expected" argument: total bytes including header.
    // Returns FP_OK on success, or an error code; *replyLen set to bytes copied.
    uint8_t _get_packet(uint16_t expected, uint8_t* reply, size_t replyCap,
                        size_t* replyLen);

    // Multi-packet data read. Concatenates payloads of DATAPACKETs followed by
    // the final ENDDATAPACKET. Returns FP_OK or an error code.
    uint8_t _get_data(uint8_t* buffer, size_t bufferSize, size_t* outLen);

    // Multi-packet data write. Chunk size derived from data_packet_size.
    void _send_data(const uint8_t* data, size_t length);

    // Helpers
    size_t   _packetChunkSize() const;            // bytes per data sub-packet
    bool     _readBytes(uint8_t* dst, size_t n);  // blocking read with timeout
    void     _drainRx();                          // discard any pending input
    void     _debugHex(const char* label, const uint8_t* buf, size_t len);
};

#endif // ADAFRUIT_FINGERPRINT_ESP32_H
