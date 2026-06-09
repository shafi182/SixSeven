#pragma once

#include <Arduino.h>   // WAJIB untuk byte
#include <Keypad.h>    // WAJIB untuk class Keypad

class KeypadManager {
private:
    Keypad keypad;

public:
    KeypadManager();
    void init();
    char getKey();
};