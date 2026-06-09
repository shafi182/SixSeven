#include <Arduino.h>
#include <Keypad.h>
#include "keypad_manager.h"
#include "config.h"

static char keys[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static byte rowPins[4] = {ROW1, ROW2, ROW3, ROW4};
static byte colPins[4] = {COL1, COL2, COL3, COL4};

// 🔥 CONSTRUCTOR FIX
KeypadManager::KeypadManager() 
    : keypad(makeKeymap(keys), rowPins, colPins, 4, 4)
{}

char KeypadManager::getKey() {
    return keypad.getKey();
}

void KeypadManager::init() {
    // Keypad library doesn't need explicit init
    // Just a placeholder for consistency
}