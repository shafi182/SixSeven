#pragma once
#include <Arduino.h>

enum Role {
    ROLE_NONE,
    ROLE_ADMIN,
    ROLE_DOSEN
};

class AuthManager {
public:
    Role login(String nip, String pin);
};