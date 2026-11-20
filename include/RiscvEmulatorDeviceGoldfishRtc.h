/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#ifndef RiscvEmulatorDeviceGoldfishRtc_H_
#define RiscvEmulatorDeviceGoldfishRtc_H_

#ifndef RVE_DEV_GOLDFISH_RTC
#define RVE_DEV_GOLDFISH_RTC 1
#endif

#if (RVE_DEV_GOLDFISH_RTC == 1)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Register offsets from the goldfish RTC base.
#define GOLDFISH_RTC_TIME_LOW        0x00
#define GOLDFISH_RTC_TIME_HIGH       0x04
#define GOLDFISH_RTC_ALARM_LOW       0x08
#define GOLDFISH_RTC_ALARM_HIGH      0x0c
#define GOLDFISH_RTC_IRQ_ENABLED     0x10
#define GOLDFISH_RTC_CLEAR_ALARM     0x14
#define GOLDFISH_RTC_ALARM_STATUS    0x18
#define GOLDFISH_RTC_CLEAR_INTERRUPT 0x1c

// Host hooks. A board defines these before including this header.
#ifndef RVE_DEV_GOLDFISH_RTC_REALTIME_NS
#define RVE_DEV_GOLDFISH_RTC_REALTIME_NS() 0
#endif

#ifndef RVE_DEV_GOLDFISH_RTC_MONOTONIC_NS
#define RVE_DEV_GOLDFISH_RTC_MONOTONIC_NS() 0
#endif

/**
 * Goldfish RTC instance.
 */
typedef struct {
    uint32_t base;
    uint32_t size;
    uint64_t base_ns;
    uint64_t anchor_ns;
    uint32_t latch_high;
    uint32_t alarm_low;
    uint32_t alarm_high;
    uint32_t irq_enabled;
    uint32_t alarm_status;
} goldfish_rtc_t;

/**
 * Initializes a goldfish RTC instance at the given memory range.
 */
static inline void goldfish_rtc_init(goldfish_rtc_t *dev, uint32_t base, uint32_t size) {
    memset(dev, 0, sizeof(*dev));
    dev->base = base;
    dev->size = size;
    dev->base_ns = RVE_DEV_GOLDFISH_RTC_REALTIME_NS();
    dev->anchor_ns = RVE_DEV_GOLDFISH_RTC_MONOTONIC_NS();
}

/**
 * Returns true when the address falls inside the goldfish RTC range.
 */
static inline bool goldfish_rtc_contains(const goldfish_rtc_t *dev, uint32_t address) {
    return address >= dev->base && address < dev->base + dev->size;
}

/**
 * Returns the current emulated time in nanoseconds.
 */
static inline uint64_t goldfish_rtc_now(const goldfish_rtc_t *dev) {
    uint64_t monotonic = RVE_DEV_GOLDFISH_RTC_MONOTONIC_NS();
    uint64_t elapsed = monotonic >= dev->anchor_ns ? monotonic - dev->anchor_ns : 0;

    return dev->base_ns + elapsed;
}

/**
 * Reads a goldfish RTC register. The low time word latches the high word.
 */
static inline bool goldfish_rtc_read(goldfish_rtc_t *dev, uint32_t address, void *destination, uint8_t length) {
    if (!goldfish_rtc_contains(dev, address)) {
        return false;
    }

    uint32_t offset = address - dev->base;

    if (offset == GOLDFISH_RTC_TIME_LOW && length == sizeof(uint32_t)) {
        uint64_t now = goldfish_rtc_now(dev);
        dev->latch_high = (uint32_t)(now >> 32);
        uint32_t value = (uint32_t)now;
        memcpy(destination, &value, sizeof(value));
        return true;
    }

    if (offset == GOLDFISH_RTC_TIME_HIGH && length == sizeof(uint32_t)) {
        memcpy(destination, &dev->latch_high, sizeof(dev->latch_high));
        return true;
    }

    if (offset == GOLDFISH_RTC_ALARM_LOW && length == sizeof(uint32_t)) {
        memcpy(destination, &dev->alarm_low, sizeof(dev->alarm_low));
        return true;
    }

    if (offset == GOLDFISH_RTC_ALARM_HIGH && length == sizeof(uint32_t)) {
        memcpy(destination, &dev->alarm_high, sizeof(dev->alarm_high));
        return true;
    }

    if (offset == GOLDFISH_RTC_IRQ_ENABLED && length == sizeof(uint32_t)) {
        memcpy(destination, &dev->irq_enabled, sizeof(dev->irq_enabled));
        return true;
    }

    if (offset == GOLDFISH_RTC_ALARM_STATUS && length == sizeof(uint32_t)) {
        memcpy(destination, &dev->alarm_status, sizeof(dev->alarm_status));
        return true;
    }

    // Unimplemented registers read as zero.
    memset(destination, 0, length);
    return true;
}

/**
 * Writes a goldfish RTC register. Time writes set the emulated base and anchor.
 */
static inline bool goldfish_rtc_write(goldfish_rtc_t *dev, uint32_t address, const void *source, uint8_t length) {
    if (!goldfish_rtc_contains(dev, address)) {
        return false;
    }

    uint32_t offset = address - dev->base;

    if (length != sizeof(uint32_t)) {
        return true;
    }

    uint32_t value;
    memcpy(&value, source, sizeof(value));

    if (offset == GOLDFISH_RTC_TIME_LOW) {
        dev->base_ns = (dev->base_ns & UINT64_C(0xFFFFFFFF00000000)) | value;
        dev->anchor_ns = RVE_DEV_GOLDFISH_RTC_MONOTONIC_NS();
        return true;
    }

    if (offset == GOLDFISH_RTC_TIME_HIGH) {
        dev->base_ns = (dev->base_ns & UINT64_C(0x00000000FFFFFFFF)) | ((uint64_t)value << 32);
        dev->anchor_ns = RVE_DEV_GOLDFISH_RTC_MONOTONIC_NS();
        return true;
    }

    if (offset == GOLDFISH_RTC_ALARM_LOW) {
        dev->alarm_low = value;
        return true;
    }

    if (offset == GOLDFISH_RTC_ALARM_HIGH) {
        dev->alarm_high = value;
        return true;
    }

    if (offset == GOLDFISH_RTC_IRQ_ENABLED) {
        dev->irq_enabled = value;
        return true;
    }

    if (offset == GOLDFISH_RTC_CLEAR_ALARM) {
        dev->alarm_status = 0;
        return true;
    }

    // Writes to unimplemented registers are ignored.
    return true;
}

#endif

#endif
