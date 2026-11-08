/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#ifndef RiscvEmulatorDeviceSyscon_H_
#define RiscvEmulatorDeviceSyscon_H_

#ifndef RVE_DEV_SYSCON
#define RVE_DEV_SYSCON 1
#endif

#if (RVE_DEV_SYSCON == 1)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Values written to the register at offset 0 of the SiFive test finisher.
#define SYSCON_POWEROFF_PASS 0x5555
#define SYSCON_POWEROFF_FAIL 0x3333
#define SYSCON_REBOOT        0x7777

// Host hooks. A board defines these before including this header.
#ifndef RVE_DEV_SYSCON_POWEROFF
#define RVE_DEV_SYSCON_POWEROFF(pass) ((void)(pass))
#endif

#ifndef RVE_DEV_SYSCON_REBOOT
#define RVE_DEV_SYSCON_REBOOT() ((void)0)
#endif

/**
 * SYSCON / SiFive test finisher instance.
 */
typedef struct {
    uint32_t base;
    uint32_t size;
} syscon_t;

/**
 * Initializes a SYSCON instance at the given memory range.
 */
static inline void syscon_init(syscon_t *syscon, uint32_t base, uint32_t size) {
    syscon->base = base;
    syscon->size = size;
}

/**
 * Returns true when the address falls inside the SYSCON range.
 */
static inline bool syscon_contains(const syscon_t *syscon, uint32_t address) {
    return address >= syscon->base && address < syscon->base + syscon->size;
}

/**
 * Reads from the SYSCON range. The register is write-only, so reads return zero.
 */
static inline bool syscon_read(const syscon_t *syscon, uint32_t address, void *destination, uint8_t length) {
    if (!syscon_contains(syscon, address)) {
        return false;
    }

    memset(destination, 0, length);
    return true;
}

/**
 * Writes to the SYSCON range and runs the matching host hook.
 */
static inline bool syscon_write(const syscon_t *syscon, uint32_t address, const void *source, uint8_t length) {
    if (!syscon_contains(syscon, address)) {
        return false;
    }

    if (address != syscon->base || length != sizeof(uint32_t)) {
        return true;
    }

    uint32_t value;
    memcpy(&value, source, sizeof(value));

    switch (value) {
        case SYSCON_POWEROFF_PASS:
            RVE_DEV_SYSCON_POWEROFF(1);
            break;
        case SYSCON_POWEROFF_FAIL:
            RVE_DEV_SYSCON_POWEROFF(0);
            break;
        case SYSCON_REBOOT:
            RVE_DEV_SYSCON_REBOOT();
            break;
        default:
            break;
    }

    return true;
}

#endif

#endif
