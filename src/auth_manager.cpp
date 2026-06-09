#include "auth_manager.h"
#include "user_manager.h"

extern UserManager userManager;

Role AuthManager::login(String nip, String pin) {

    // ADMIN (hardcode)
    // if (nip == "123" && pin == "123") {
    //     return ROLE_ADMIN;
    // }

    // DOSEN (cek SD)
    if (userManager.checkUser(nip, pin)) {
        if(userManager.isAdmin(nip)){
            return ROLE_ADMIN;
        }
        else{
            return ROLE_DOSEN;
        }
    }

    return ROLE_NONE;
}