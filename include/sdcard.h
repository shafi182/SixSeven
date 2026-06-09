#pragma once

#include <Arduino.h>
#include <FS.h>
#include <time.h>

class SDCardManager {
public:
    bool init();
    File open(const char* path, const char* mode);
};

// System Logger Functions
void logSystemActivity(String event, String detail);
String getFormattedTime();