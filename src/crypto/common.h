#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#include <stdint.h>
#include <string.h>
#include <stddef.h>

#define MAX(a, b) (((a) > (b))? (a) : (b))
#define MIN(a, b) (((a) < (b))? (a) : (b))

#define PROGMEM
#define LOOKUP_DWORD(x) (x)
#define LOOKUP_BYTE(x) (x)

#endif
