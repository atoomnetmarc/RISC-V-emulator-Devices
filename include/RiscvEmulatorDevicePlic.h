/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#ifndef RiscvEmulatorDevicePlic_H_
#define RiscvEmulatorDevicePlic_H_

#ifndef RVE_DEV_PLIC
#define RVE_DEV_PLIC 1
#endif

#if (RVE_DEV_PLIC == 1)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Number of interrupt sources, including the reserved source 0.
#define PLIC_SOURCE_COUNT 32

// Number of contexts. The native board has one M-mode hart.
#define PLIC_CONTEXT_COUNT 1

// Register offsets from the PLIC base.
#define PLIC_PRIORITY_OFFSET  0x000000
#define PLIC_PENDING_OFFSET   0x001000
#define PLIC_ENABLE_OFFSET    0x002000
#define PLIC_CONTEXT_OFFSET   0x200000
#define PLIC_CONTEXT_STRIDE   0x001000
#define PLIC_THRESHOLD_OFFSET 0x000000
#define PLIC_CLAIM_OFFSET     0x000004

/**
 * PLIC instance.
 */
typedef struct {
    uint32_t base;
    uint32_t size;
    uint32_t priority[PLIC_SOURCE_COUNT];
    uint32_t pending;
    uint32_t enable[PLIC_CONTEXT_COUNT];
    uint32_t threshold[PLIC_CONTEXT_COUNT];
} plic_t;

/**
 * Initializes a PLIC instance at the given memory range.
 */
static inline void plic_init(plic_t *plic, uint32_t base, uint32_t size) {
    memset(plic, 0, sizeof(*plic));
    plic->base = base;
    plic->size = size;
}

/**
 * Returns true when the address falls inside the PLIC range.
 */
static inline bool plic_contains(const plic_t *plic, uint32_t address) {
    return address >= plic->base && address < plic->base + plic->size;
}

/**
 * Returns the highest-priority pending source above the context threshold, or zero.
 */
static inline uint32_t plic_highest_pending(const plic_t *plic, uint32_t context) {
    uint32_t best = 0;
    uint32_t best_priority = 0;

    for (uint32_t source = 1; source < PLIC_SOURCE_COUNT; source++) {
        uint32_t bit = 1u << source;

        if ((plic->pending & plic->enable[context] & bit) == 0) {
            continue;
        }

        if (plic->priority[source] <= plic->threshold[context]) {
            continue;
        }

        if (plic->priority[source] > best_priority) {
            best_priority = plic->priority[source];
            best = source;
        }
    }

    return best;
}

/**
 * Returns true when an enabled pending source has a priority above the threshold.
 */
static inline bool plic_irq_pending(const plic_t *plic) {
    return plic_highest_pending(plic, 0) != 0;
}

/**
 * Sets or clears the pending bit of one source.
 */
static inline void plic_set_pending(plic_t *plic, uint8_t source, bool pending) {
    if (source == 0 || source >= PLIC_SOURCE_COUNT) {
        return;
    }

    if (pending) {
        plic->pending |= 1u << source;
    } else {
        plic->pending &= ~(1u << source);
    }
}

/**
 * Reads a PLIC register. A claim read returns the source and clears its pending bit.
 */
static inline bool plic_read(plic_t *plic, uint32_t address, void *destination, uint8_t length) {
    if (!plic_contains(plic, address)) {
        return false;
    }

    uint32_t offset = address - plic->base;
    uint32_t value = 0;

    if (offset < PLIC_PENDING_OFFSET) {
        uint32_t source = offset / sizeof(uint32_t);
        if (source < PLIC_SOURCE_COUNT && (offset % sizeof(uint32_t)) == 0) {
            value = plic->priority[source];
        }
    } else if (offset < PLIC_ENABLE_OFFSET) {
        uint32_t word = (offset - PLIC_PENDING_OFFSET) / sizeof(uint32_t);
        if (word == 0) {
            value = plic->pending;
        }
    } else if (offset < PLIC_CONTEXT_OFFSET) {
        uint32_t word = (offset - PLIC_ENABLE_OFFSET) / sizeof(uint32_t);
        if (word < PLIC_CONTEXT_COUNT) {
            value = plic->enable[word];
        }
    } else {
        uint32_t context_offset = offset - PLIC_CONTEXT_OFFSET;
        uint32_t context = context_offset / PLIC_CONTEXT_STRIDE;
        uint32_t reg = context_offset % PLIC_CONTEXT_STRIDE;

        if (context < PLIC_CONTEXT_COUNT) {
            if (reg == PLIC_THRESHOLD_OFFSET) {
                value = plic->threshold[context];
            } else if (reg == PLIC_CLAIM_OFFSET) {
                value = plic_highest_pending(plic, context);
                if (value != 0) {
                    plic->pending &= ~(1u << value);
                }
            }
        }
    }

    if (length == sizeof(uint32_t)) {
        memcpy(destination, &value, sizeof(value));
    } else {
        memset(destination, 0, length);
    }

    return true;
}

/**
 * Writes a PLIC register. A claim/complete write clears the pending bit of the source.
 */
static inline bool plic_write(plic_t *plic, uint32_t address, const void *source, uint8_t length) {
    if (!plic_contains(plic, address)) {
        return false;
    }

    if (length != sizeof(uint32_t)) {
        return true;
    }

    uint32_t value;
    memcpy(&value, source, sizeof(value));

    uint32_t offset = address - plic->base;

    if (offset < PLIC_PENDING_OFFSET) {
        uint32_t source_index = offset / sizeof(uint32_t);
        if (source_index < PLIC_SOURCE_COUNT && (offset % sizeof(uint32_t)) == 0) {
            plic->priority[source_index] = value;
        }
    } else if (offset < PLIC_ENABLE_OFFSET) {
        // Pending bits are set by the device, not by software.
    } else if (offset < PLIC_CONTEXT_OFFSET) {
        uint32_t word = (offset - PLIC_ENABLE_OFFSET) / sizeof(uint32_t);
        if (word < PLIC_CONTEXT_COUNT) {
            plic->enable[word] = value;
        }
    } else {
        uint32_t context_offset = offset - PLIC_CONTEXT_OFFSET;
        uint32_t context = context_offset / PLIC_CONTEXT_STRIDE;
        uint32_t reg = context_offset % PLIC_CONTEXT_STRIDE;

        if (context < PLIC_CONTEXT_COUNT) {
            if (reg == PLIC_THRESHOLD_OFFSET) {
                plic->threshold[context] = value;
            } else if (reg == PLIC_CLAIM_OFFSET) {
                if (value != 0 && value < PLIC_SOURCE_COUNT) {
                    plic->pending &= ~(1u << value);
                }
            }
        }
    }

    return true;
}

#endif

#endif
