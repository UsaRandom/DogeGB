#ifndef BITROT_ROM_H
#define BITROT_ROM_H

#include <stdbool.h>

bool rom_verify_integrity(void);
bool quick_rom_verify_integrity(void);

#endif