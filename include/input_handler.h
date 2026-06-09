#pragma once
#include <Arduino.h>
#include "keypad_manager.h"
#include "lcd.h"

class InputHandler {
private:
    String buffer;
    int maxLength;

    bool maskInput;       // untuk NIP vs PIN
    String label;         // teks LCD

    unsigned long lastInputTime;

    KeypadManager* keypad;
    LCDManager* lcd;

public:
    InputHandler(KeypadManager* k, LCDManager* l, int maxLen = 10);

    void setMode(String text, bool mask);
    void reset(bool showCancel = true);  // Default true: tampilkan Batal (kecuali login awal)

    // TUGAS 1 & 3: Overload - jika key sudah tersedia, gunakan langsung
    bool update();                     // Tanpa parameter: ambil key dari keypad
    bool update(char preFetchedKey);   // Dengan parameter: gunakan key yang sudah diambil

    String getValue();
};