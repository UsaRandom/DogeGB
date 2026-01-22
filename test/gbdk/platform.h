// Stub GBDK platform header for macOS testing
#ifndef GBDK_PLATFORM_H
#define GBDK_PLATFORM_H

// Include standard headers
#include <stdint.h>

// Basic type definitions
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;

// Stub out platform-specific functions
#define cpu_fast()

// Stub out banking - make sure these are defined before any GBDK includes
#define BANK(name) 0
#define BANKREF(name)
#define BANKREF_EXTERN(name)
#define CURRENT_BANK 0
#define SWITCH_ROM(bank) /* no-op */
#define BANKED

#endif
