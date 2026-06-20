// Adafruit_Fingerprint_ESP32.cpp
// Implementation -- see header for context.

#include "Adafruit_Fingerprint_ESP32.h"

// ===================== Construction & startup =====================

Adafruit_Fingerprint_ESP32::Adafruit_Fingerprint_ESP32(HardwareSerial* serial,
                                                       uint32_t password)
    : _uart(serial), _password(password) {}

bool Adafruit_Fingerprint_ESP32::begin(unsigned long baud,
                                       int8_t rxPin, int8_t txPin) {
    if (_uart == nullptr) return false;

    if (rxPin >= 0 && txPin >= 0) {
        _uart->begin(baud, SERIAL_8N1, rxPin, txPin);
    } else {
        _uart->begin(baud);
    }
    // HardwareSerial::setTimeout governs readBytes(); keep it generous.
    _uart->setTimeout(_timeoutMs);

    // Discard whatever garbage may be in the line after a reset.
    delay(100);
    _drainRx();

    if (verify_password() != FP_OK)   return false;
    if (read_sysparam()   != FP_OK)   return false;
    return true;
}

// ===================== Low-level packet I/O =====================

void Adafruit_Fingerprint_ESP32::_send_packet(const uint8_t* data, uint16_t dataLen) {
    // length field = payload bytes + 2 (the trailing checksum)
    uint16_t length = dataLen + 2;

    // Header
    uint8_t header[9];
    header[0] = (FP_STARTCODE >> 8) & 0xFF;
    header[1] = FP_STARTCODE & 0xFF;
    header[2] = _address[0];
    header[3] = _address[1];
    header[4] = _address[2];
    header[5] = _address[3];
    header[6] = FP_COMMANDPACKET;
    header[7] = (length >> 8) & 0xFF;
    header[8] = length & 0xFF;

    // Checksum is the sum from packet_type onward.
    uint16_t checksum = header[6] + header[7] + header[8];
    for (uint16_t i = 0; i < dataLen; i++) checksum += data[i];

    _uart->write(header, 9);
    if (dataLen) _uart->write(data, dataLen);
    uint8_t cksum[2] = { (uint8_t)((checksum >> 8) & 0xFF),
                         (uint8_t)(checksum & 0xFF) };
    _uart->write(cksum, 2);
    _uart->flush();

    if (_debug) {
        Serial.print("[TX] cmd=");
        Serial.print(data[0], HEX);
        Serial.print(" len=");
        Serial.println(dataLen);
    }
}

uint8_t Adafruit_Fingerprint_ESP32::_get_packet(uint16_t expected,
                                                uint8_t* reply, size_t replyCap,
                                                size_t* replyLen) {
    if (replyLen) *replyLen = 0;
    if (expected < 12) return FP_BADPACKET; // minimum ACK frame size

    // Header is always 9 bytes; payload+checksum is (expected - 9).
    uint8_t header[9];
    if (!_readBytes(header, 9)) return FP_TIMEOUT;

    uint16_t start = ((uint16_t)header[0] << 8) | header[1];
    if (start != FP_STARTCODE) {
        _debugHex("bad-start", header, 9);
        return FP_BADPACKET;
    }
    for (int i = 0; i < 4; i++) {
        if (header[2 + i] != _address[i]) return FP_BADPACKET;
    }
    uint8_t  packetType = header[6];
    uint16_t length     = ((uint16_t)header[7] << 8) | header[8];
    if (packetType != FP_ACKPACKET) return FP_BADPACKET;

    // payload is (length - 2) bytes, then 2 bytes of checksum (ignored).
    uint16_t payloadLen = (length >= 2) ? (length - 2) : 0;
    if (payloadLen > replyCap) return FP_BUFFER_OVERFLOW;

    if (payloadLen) {
        if (!_readBytes(reply, payloadLen)) return FP_TIMEOUT;
    }
    uint8_t cksum[2];
    if (!_readBytes(cksum, 2)) return FP_TIMEOUT;

    // expected acts as a sanity hint for the caller; not strictly enforced
    // beyond the bounds check above.
    (void)expected;

    if (replyLen) *replyLen = payloadLen;
    if (_debug) _debugHex("[RX-ACK]", reply, payloadLen);
    return FP_OK;
}

uint8_t Adafruit_Fingerprint_ESP32::_get_data(uint8_t* buffer, size_t bufferSize,
                                               size_t* outLen) {
    size_t pos = 0;
    while (true) {
        uint8_t header[9];
        if (!_readBytes(header, 9)) return FP_TIMEOUT;

        uint16_t start = ((uint16_t)header[0] << 8) | header[1];
        if (start != FP_STARTCODE) return FP_BADPACKET;
        for (int i = 0; i < 4; i++) {
            if (header[2 + i] != _address[i]) return FP_BADPACKET;
        }
        uint8_t  packetType = header[6];
        uint16_t length     = ((uint16_t)header[7] << 8) | header[8];

        if (packetType != FP_DATAPACKET && packetType != FP_ENDDATAPACKET) {
            return FP_BADPACKET;
        }

        uint16_t payloadLen = (length >= 2) ? (length - 2) : 0;
        if (pos + payloadLen > bufferSize) {
            // Caller buffer too small. Drain the rest of THIS packet plus any
            // remaining DATA/END packets so the UART stays in sync for the
            // next command. Otherwise leftover bytes corrupt subsequent ACKs.
            uint8_t junk;
            for (uint16_t k = 0; k < payloadLen; k++) _readBytes(&junk, 1);
            _readBytes(&junk, 1); _readBytes(&junk, 1); // checksum
            if (packetType != FP_ENDDATAPACKET) {
                // Keep draining until we see ENDDATAPACKET or read times out.
                while (true) {
                    uint8_t hdr2[9];
                    if (!_readBytes(hdr2, 9)) break;
                    uint16_t l2 = ((uint16_t)hdr2[7] << 8) | hdr2[8];
                    uint16_t pl2 = (l2 >= 2) ? (l2 - 2) : 0;
                    for (uint16_t k = 0; k < pl2 + 2; k++) {
                        if (!_readBytes(&junk, 1)) break;
                    }
                    if (hdr2[6] == FP_ENDDATAPACKET) break;
                }
            }
            return FP_BUFFER_OVERFLOW;
        }

        if (payloadLen) {
            if (!_readBytes(buffer + pos, payloadLen)) return FP_TIMEOUT;
            pos += payloadLen;
        }
        uint8_t cksum[2];
        if (!_readBytes(cksum, 2)) return FP_TIMEOUT;

        if (packetType == FP_ENDDATAPACKET) break;
    }
    if (outLen) *outLen = pos;
    if (_debug) {
        Serial.print("[RX-DATA total]: ");
        Serial.println((uint32_t)pos);
    }
    return FP_OK;
}

void Adafruit_Fingerprint_ESP32::_send_data(const uint8_t* data, size_t length) {
    size_t chunk = _packetChunkSize();
    size_t left  = length;
    size_t whole = length / chunk; // mirrors Python's integer division

    for (size_t i = 0; i < whole; i++) {
        size_t start = i * chunk;
        left -= chunk;
        bool isEnd = (left == 0);
        uint8_t packetType = isEnd ? FP_ENDDATAPACKET : FP_DATAPACKET;
        uint16_t pktLen    = (uint16_t)chunk + 2;

        uint8_t header[9];
        header[0] = (FP_STARTCODE >> 8) & 0xFF;
        header[1] = FP_STARTCODE & 0xFF;
        header[2] = _address[0];
        header[3] = _address[1];
        header[4] = _address[2];
        header[5] = _address[3];
        header[6] = packetType;
        header[7] = (pktLen >> 8) & 0xFF;
        header[8] = pktLen & 0xFF;

        uint32_t checksum = (uint32_t)packetType + header[7] + header[8];
        for (size_t j = 0; j < chunk; j++) checksum += data[start + j];

        uint8_t cksum[2] = { (uint8_t)((checksum >> 8) & 0xFF),
                             (uint8_t)(checksum & 0xFF) };

        _uart->write(header, 9);
        _uart->write(data + start, chunk);
        _uart->write(cksum, 2);
        _uart->flush();

        // Inter-packet breathing room; the sensor's MCU is slow.
        delay(5);
    }
}

size_t Adafruit_Fingerprint_ESP32::_packetChunkSize() const {
    switch (data_packet_size) {
        case 0: return 32;
        case 1: return 64;
        case 2: return 128;
        case 3: return 256;
        default: return 128;
    }
}

bool Adafruit_Fingerprint_ESP32::_readBytes(uint8_t* dst, size_t n) {
    // HardwareSerial::readBytes already honors setTimeout(), but it resets the
    // deadline per call; for a single logical packet that's fine.
    size_t got = _uart->readBytes(dst, n);
    return got == n;
}

void Adafruit_Fingerprint_ESP32::_drainRx() {
    while (_uart->available()) _uart->read();
}

void Adafruit_Fingerprint_ESP32::_debugHex(const char* label,
                                            const uint8_t* buf, size_t len) {
    if (!_debug) return;
    Serial.print(label);
    Serial.print(' ');
    for (size_t i = 0; i < len; i++) {
        if (buf[i] < 0x10) Serial.print('0');
        Serial.print(buf[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
}

// ===================== High-level commands =====================

uint8_t Adafruit_Fingerprint_ESP32::verify_password() {
    uint8_t cmd[5];
    cmd[0] = FP_VERIFYPASSWORD;
    cmd[1] = (_password >> 24) & 0xFF;
    cmd[2] = (_password >> 16) & 0xFF;
    cmd[3] = (_password >>  8) & 0xFF;
    cmd[4] =  _password        & 0xFF;
    _send_packet(cmd, sizeof(cmd));

    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::read_sysparam() {
    uint8_t cmd[1] = { FP_READSYSPARA };
    _send_packet(cmd, 1);

    uint8_t reply[32]; size_t n = 0;
    uint8_t st = _get_packet(28, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    if (n < 17 || reply[0] != FP_OK) return reply[0];

    status_register   = ((uint16_t)reply[1] << 8) | reply[2];
    system_id         = ((uint16_t)reply[3] << 8) | reply[4];
    library_size      = ((uint16_t)reply[5] << 8) | reply[6];
    security_level    = ((uint16_t)reply[7] << 8) | reply[8];
    device_address[0] = reply[9];
    device_address[1] = reply[10];
    device_address[2] = reply[11];
    device_address[3] = reply[12];
    data_packet_size  = ((uint16_t)reply[13] << 8) | reply[14];
    baudrate_param    = ((uint16_t)reply[15] << 8) | reply[16];

    return FP_OK;
}

uint8_t Adafruit_Fingerprint_ESP32::set_sysparam(uint8_t param_num, uint8_t param_val) {
    uint8_t cmd[3] = { 0x0E, param_num, param_val };
    _send_packet(cmd, 3);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::get_image() {
    uint8_t cmd[1] = { FP_GETIMAGE };
    _send_packet(cmd, 1);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::image_2_tz(uint8_t slot) {
    uint8_t cmd[2] = { FP_IMAGE2TZ, slot };
    _send_packet(cmd, 2);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::create_model() {
    uint8_t cmd[1] = { FP_REGMODEL };
    _send_packet(cmd, 1);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::store_model(uint16_t location, uint8_t slot) {
    uint8_t cmd[4] = {
        FP_STORE,
        slot,
        (uint8_t)((location >> 8) & 0xFF),
        (uint8_t)(location & 0xFF)
    };
    _send_packet(cmd, 4);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::load_model(uint16_t location, uint8_t slot) {
    uint8_t cmd[4] = {
        FP_LOAD,
        slot,
        (uint8_t)((location >> 8) & 0xFF),
        (uint8_t)(location & 0xFF)
    };
    _send_packet(cmd, 4);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::delete_model(uint16_t location) {
    uint8_t cmd[5] = {
        FP_DELETE,
        (uint8_t)((location >> 8) & 0xFF),
        (uint8_t)(location & 0xFF),
        0x00, 0x01
    };
    _send_packet(cmd, 5);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::empty_library() {
    uint8_t cmd[1] = { FP_EMPTY };
    _send_packet(cmd, 1);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::finger_search() {
    // Refresh capacity (matches Python which calls read_sysparam first).
    read_sysparam();
    uint16_t capacity = library_size ? library_size : 200;
    uint8_t cmd[6] = {
        FP_FINGERPRINTSEARCH,
        0x01,
        0x00, 0x00,
        (uint8_t)((capacity >> 8) & 0xFF),
        (uint8_t)(capacity & 0xFF)
    };
    _send_packet(cmd, 6);

    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(16, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    if (n < 5) return FP_PACKETRECEIVEERR;
    finger_id  = ((uint16_t)reply[1] << 8) | reply[2];
    confidence = ((uint16_t)reply[3] << 8) | reply[4];
    return reply[0];
}

uint8_t Adafruit_Fingerprint_ESP32::finger_fast_search() {
    read_sysparam();
    uint16_t capacity = library_size ? library_size : 200;
    uint8_t cmd[6] = {
        FP_HISPEEDSEARCH,
        0x01,
        0x00, 0x00,
        (uint8_t)((capacity >> 8) & 0xFF),
        (uint8_t)(capacity & 0xFF)
    };
    _send_packet(cmd, 6);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(16, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    if (n < 5) return FP_PACKETRECEIVEERR;
    finger_id  = ((uint16_t)reply[1] << 8) | reply[2];
    confidence = ((uint16_t)reply[3] << 8) | reply[4];
    return reply[0];
}

uint8_t Adafruit_Fingerprint_ESP32::compare_templates() {
    uint8_t cmd[1] = { FP_COMPARE };
    _send_packet(cmd, 1);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(14, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    if (n >= 3) confidence = ((uint16_t)reply[1] << 8) | reply[2];
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::count_templates() {
    uint8_t cmd[1] = { FP_TEMPLATECOUNT };
    _send_packet(cmd, 1);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(14, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    if (n >= 3) template_count = ((uint16_t)reply[1] << 8) | reply[2];
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

uint8_t Adafruit_Fingerprint_ESP32::read_templates(uint16_t* templateList,
                                                    size_t maxLen,
                                                    uint16_t* foundCount) {
    if (foundCount) *foundCount = 0;
    read_sysparam();

    // Each TEMPLATEREAD page covers 256 slots (32-byte bitmap, 8 bits each).
    uint16_t numPages = (library_size + 255) / 256;
    uint8_t lastStatus = FP_OK;
    uint16_t found = 0;

    for (uint16_t page = 0; page < numPages; page++) {
        uint8_t cmd[2] = { FP_TEMPLATEREAD, (uint8_t)(page & 0xFF) };
        _send_packet(cmd, 2);

        // Response: 1 status byte + 32 bitmap bytes = 33 payload bytes.
        // Total frame = 9 header + 33 payload + 2 cksum = 44.
        uint8_t reply[40]; size_t n = 0;
        uint8_t st = _get_packet(44, reply, sizeof(reply), &n);
        if (st != FP_OK || n < 33) { lastStatus = (st != FP_OK) ? st : FP_PACKETRECEIVEERR; continue; }
        if (reply[0] != FP_OK)     { lastStatus = reply[0]; continue; }

        for (uint16_t i = 0; i < 32; i++) {
            uint8_t bitmapByte = reply[i + 1];
            for (uint16_t bit = 0; bit < 8; bit++) {
                if (bitmapByte & (1u << bit)) {
                    uint16_t slotNum = (i * 8) + bit + (page * 256);
                    if (templateList && found < maxLen) templateList[found] = slotNum;
                    found++;
                }
            }
        }
        lastStatus = FP_OK;
    }
    if (foundCount) *foundCount = found;
    return lastStatus;
}

uint8_t Adafruit_Fingerprint_ESP32::soft_reset() {
    uint8_t cmd[1] = { FP_SOFTRESET };
    _send_packet(cmd, 1);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    if (!n || reply[0] != FP_OK) return n ? reply[0] : FP_PACKETRECEIVEERR;

    // After OK, sensor emits a single MODULEOK handshake byte.
    uint8_t handshake = 0;
    if (!_readBytes(&handshake, 1)) return FP_TIMEOUT;
    return (handshake == FP_MODULEOK) ? FP_OK : FP_BADPACKET;
}

uint8_t Adafruit_Fingerprint_ESP32::set_led(uint8_t color, uint8_t mode,
                                             uint8_t speed, uint8_t cycles) {
    // Order from Python: [_SETAURA, mode, speed, color, cycles]
    uint8_t cmd[5] = { FP_SETAURA, mode, speed, color, cycles };
    _send_packet(cmd, 5);
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    return n ? reply[0] : FP_PACKETRECEIVEERR;
}

// ===================== CRITICAL: template transfer =====================

uint8_t Adafruit_Fingerprint_ESP32::get_fpdata(uint8_t* buffer, size_t bufferSize,
                                                size_t* outLen,
                                                uint8_t sensorbuffer, uint8_t slot) {
    if (outLen) *outLen = 0;
    if (slot != 1 && slot != 2) slot = 2;

    if (sensorbuffer == FP_BUFFER_IMAGE) {
        uint8_t cmd[1] = { FP_UPLOADIMAGE };
        _send_packet(cmd, 1);
    } else {
        uint8_t cmd[2] = { FP_UPLOAD, slot };
        _send_packet(cmd, 2);
    }

    // First the sensor ACKs the command itself.
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    if (!n || reply[0] != FP_OK) return n ? reply[0] : FP_PACKETRECEIVEERR;

    // Then it streams the data packets until ENDDATAPACKET.
    return _get_data(buffer, bufferSize, outLen);
}

uint8_t Adafruit_Fingerprint_ESP32::send_fpdata(const uint8_t* data, size_t length,
                                                 uint8_t sensorbuffer, uint8_t slot) {
    if (data == nullptr || length == 0) return FP_BADPACKET;
    if (slot != 1 && slot != 2) slot = 2;

    if (sensorbuffer == FP_BUFFER_IMAGE) {
        uint8_t cmd[1] = { FP_DOWNLOADIMAGE };
        _send_packet(cmd, 1);
    } else {
        uint8_t cmd[2] = { FP_DOWNLOAD, slot };
        _send_packet(cmd, 2);
    }

    // Sensor must ACK before we stream data.
    uint8_t reply[16]; size_t n = 0;
    uint8_t st = _get_packet(12, reply, sizeof(reply), &n);
    if (st != FP_OK) return st;
    if (!n || reply[0] != FP_OK) return n ? reply[0] : FP_PACKETRECEIVEERR;

    _send_data(data, length);
    return FP_OK;
}

// ===================== HEX <-> bytes utilities =====================

String Adafruit_Fingerprint_ESP32::bytesToHexString(const uint8_t* data, size_t length) {
    static const char* hexChars = "0123456789ABCDEF";
    String out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; i++) {
        out += hexChars[(data[i] >> 4) & 0x0F];
        out += hexChars[data[i] & 0x0F];
    }
    return out;
}

static int _hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

size_t Adafruit_Fingerprint_ESP32::hexStringToBytes(const char* hex, size_t hexLen,
                                                     uint8_t* out, size_t outSize) {
    if ((hexLen % 2) != 0) return 0;
    size_t needed = hexLen / 2;
    if (needed > outSize) return 0;
    for (size_t i = 0; i < needed; i++) {
        int hi = _hexNibble(hex[2 * i]);
        int lo = _hexNibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return needed;
}

size_t Adafruit_Fingerprint_ESP32::hexStringToBytes(const String& hex,
                                                     uint8_t* out, size_t outSize) {
    return hexStringToBytes(hex.c_str(), hex.length(), out, outSize);
}
