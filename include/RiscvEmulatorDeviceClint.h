/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#ifndef RiscvEmulatorDeviceClint_H_
#define RiscvEmulatorDeviceClint_H_

#ifndef RVE_DEV_CLINT
#define RVE_DEV_CLINT 1
#endif

#if (RVE_DEV_CLINT == 1)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Register offsets from the CLINT base.
#define CLINT_MSIP_OFFSET     0x0000
#define CLINT_MTIMECMP_OFFSET 0x4000
#define CLINT_MTIME_OFFSET    0xBFF8

/**
 * CLINT instance.
 */
typedef struct {
    uint32_t base;
    uint32_t size;
    uint64_t mtime;
    uint64_t mtimecmp;
    uint32_t msip;
    uint32_t mtime_high_latched;
    bool mtime_high_latch_valid;
} clint_t;

/**
 * Initializes a CLINT instance at the given memory range.
 */
static inline void clint_init(clint_t *clint, uint32_t base, uint32_t size) {
    memset(clint, 0, sizeof(*clint));
    clint->base = base;
    clint->size = size;
    clint->mtimecmp = UINT64_MAX;
}

/**
 * Returns true when the address falls inside the CLINT range.
 */
static inline bool clint_contains(const clint_t *clint, uint32_t address) {
    return address >= clint->base && address < clint->base + clint->size;
}

/**
 * Reads a CLINT register. The low mtime word latches the high word.
 */
static inline bool clint_read(clint_t *clint, uint32_t address, void *destination, uint8_t length) {
    if (!clint_contains(clint, address)) {
        return false;
    }

    uint32_t offset = address - clint->base;

    if (offset == CLINT_MSIP_OFFSET && length == sizeof(uint32_t)) {
        memcpy(destination, &clint->msip, sizeof(clint->msip));
        return true;
    }

    if (offset == CLINT_MTIMECMP_OFFSET && length == sizeof(uint64_t)) {
        memcpy(destination, &clint->mtimecmp, sizeof(clint->mtimecmp));
        return true;
    }

    if (offset == CLINT_MTIMECMP_OFFSET && length == sizeof(uint32_t)) {
        uint32_t value = (uint32_t)clint->mtimecmp;
        memcpy(destination, &value, sizeof(value));
        return true;
    }

    if (offset == CLINT_MTIMECMP_OFFSET + sizeof(uint32_t) && length == sizeof(uint32_t)) {
        uint32_t value = (uint32_t)(clint->mtimecmp >> 32);
        memcpy(destination, &value, sizeof(value));
        return true;
    }

    if (offset == CLINT_MTIME_OFFSET && length == sizeof(uint64_t)) {
        memcpy(destination, &clint->mtime, sizeof(clint->mtime));
        return true;
    }

    if (offset == CLINT_MTIME_OFFSET && length == sizeof(uint32_t)) {
        clint->mtime_high_latched = (uint32_t)(clint->mtime >> 32);
        clint->mtime_high_latch_valid = true;
        uint32_t value = (uint32_t)clint->mtime;
        memcpy(destination, &value, sizeof(value));
        return true;
    }

    if (offset == CLINT_MTIME_OFFSET + sizeof(uint32_t) && length == sizeof(uint32_t)) {
        uint32_t value;
        if (clint->mtime_high_latch_valid) {
            value = clint->mtime_high_latched;
            clint->mtime_high_latch_valid = false;
        } else {
            value = (uint32_t)(clint->mtime >> 32);
        }
        memcpy(destination, &value, sizeof(value));
        return true;
    }

    // Unimplemented registers read as zero.
    memset(destination, 0, length);
    return true;
}

/**
 * Writes a CLINT register. Writes to mtime are transient because clint_tick overwrites them.
 */
static inline bool clint_write(clint_t *clint, uint32_t address, const void *source, uint8_t length) {
    if (!clint_contains(clint, address)) {
        return false;
    }

    uint32_t offset = address - clint->base;

    if (offset == CLINT_MSIP_OFFSET && length == sizeof(uint32_t)) {
        memcpy(&clint->msip, source, sizeof(clint->msip));
        return true;
    }

    if (offset == CLINT_MTIMECMP_OFFSET && length == sizeof(uint64_t)) {
        memcpy(&clint->mtimecmp, source, sizeof(clint->mtimecmp));
        return true;
    }

    if (offset == CLINT_MTIMECMP_OFFSET && length == sizeof(uint32_t)) {
        uint32_t value;
        memcpy(&value, source, sizeof(value));
        clint->mtimecmp = (clint->mtimecmp & UINT64_C(0xFFFFFFFF00000000)) | value;
        return true;
    }

    if (offset == CLINT_MTIMECMP_OFFSET + sizeof(uint32_t) && length == sizeof(uint32_t)) {
        uint32_t value;
        memcpy(&value, source, sizeof(value));
        clint->mtimecmp = (clint->mtimecmp & UINT64_C(0x00000000FFFFFFFF)) | ((uint64_t)value << 32);
        return true;
    }

    if (offset == CLINT_MTIME_OFFSET && length == sizeof(uint64_t)) {
        memcpy(&clint->mtime, source, sizeof(clint->mtime));
        clint->mtime_high_latch_valid = false;
        return true;
    }

    if (offset == CLINT_MTIME_OFFSET && length == sizeof(uint32_t)) {
        uint32_t value;
        memcpy(&value, source, sizeof(value));
        clint->mtime = (clint->mtime & UINT64_C(0xFFFFFFFF00000000)) | value;
        clint->mtime_high_latch_valid = false;
        return true;
    }

    if (offset == CLINT_MTIME_OFFSET + sizeof(uint32_t) && length == sizeof(uint32_t)) {
        uint32_t value;
        memcpy(&value, source, sizeof(value));
        clint->mtime = (clint->mtime & UINT64_C(0x00000000FFFFFFFF)) | ((uint64_t)value << 32);
        clint->mtime_high_latch_valid = false;
        return true;
    }

    // Writes to unimplemented registers are ignored.
    return true;
}

/**
 * Sets mtime from the host clock in microseconds.
 */
static inline void clint_tick(clint_t *clint, uint64_t now_us) {
    clint->mtime = now_us;
}

/**
 * Returns true when mtime has reached mtimecmp.
 */
static inline bool clint_timer_pending(const clint_t *clint) {
    return clint->mtime >= clint->mtimecmp;
}

/**
 * Returns true when msip is non-zero.
 */
static inline bool clint_software_pending(const clint_t *clint) {
    return clint->msip != 0;
}

#endif

#endif
