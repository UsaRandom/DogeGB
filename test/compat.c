// Game Boy compatibility implementations for macOS testing
#include "compat.h"

// Function stubs for Game Boy functions
void wait_vbl_done(void) {
    // No-op for macOS
}

uint8_t joypad(void) {
    return 0;
}
