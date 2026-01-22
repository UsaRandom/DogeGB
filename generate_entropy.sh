#!/bin/bash

# Generate 512 bytes of random data for entropy pool for salting.
# This script creates entropy_data.h with fresh random data on each build

set -e

HEADER_FILE="entropy_data.h"
C_FILE="entropy_data.c"

ENTROPY_POOL_SIZE=512
SALT_SIZE=128

# Generate 512 random bytes and convert to C array format
# Use openssl for cross-platform compatibility, fallback to /dev/urandom
if command -v openssl >/dev/null 2>&1; then
    ENTROPY_POOL=$(openssl rand -hex $ENTROPY_POOL_SIZE)
    SALT=$(openssl rand -hex $SALT_SIZE)

else
    ENTROPY_POOL=$(od -An -tx1 -N$ENTROPY_POOL_SIZE /dev/urandom | tr -d ' \n')
    SALT=$(od -An -tx1 -N$SALT_SIZE /dev/urandom | tr -d ' \n')
fi

ENTROPY_POOL_ARRAY=""
for ((i=0; i<${#ENTROPY_POOL}; i+=2)); do
    if [ $i -gt 0 ]; then
        ENTROPY_POOL_ARRAY+=", "
    fi
    ENTROPY_POOL_ARRAY+="0x${ENTROPY_POOL:i:2}"
done


SALT_ARRAY=""
for ((i=0; i<${#SALT}; i+=2)); do
    if [ $i -gt 0 ]; then
        SALT_ARRAY+=", "
    fi
    SALT_ARRAY+="0x${SALT:i:2}"
done


# Create the header file
cat > "build/$HEADER_FILE" << EOF
#ifndef ENTROPY_DATA_H
#define ENTROPY_DATA_H

#include <stdint.h>

#define ENTROPY_POOL_SIZE $ENTROPY_POOL_SIZE
#define BUILD_SALT_SIZE $SALT_SIZE

#endif // ENTROPY_DATA_H
EOF
cat > "build/$C_FILE" << EOF

#include "$HEADER_FILE"

uint8_t entropy_pool[ENTROPY_POOL_SIZE] = { $ENTROPY_POOL_ARRAY };
uint8_t build_salt[BUILD_SALT_SIZE] = { $SALT_ARRAY };

EOF

