/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#ifndef RiscvEmulatorDeviceVirtioRng_H_
#define RiscvEmulatorDeviceVirtioRng_H_

#ifndef RVE_DEV_VIRTIO_RNG
#define RVE_DEV_VIRTIO_RNG 1
#endif

#if (RVE_DEV_VIRTIO_RNG == 1)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "RiscvEmulatorDeviceVirtioMmio.h"

// virtio-mmio identity value.
#define VIRTIO_MMIO_DEVICE_ID_RNG 4

// Host entropy buffer size in bytes.
#ifndef VIRTIO_RNG_BUFFER_SIZE
#define VIRTIO_RNG_BUFFER_SIZE 32
#endif

// Host hook. A board defines this before including this header.
#ifndef RVE_DEV_VIRTIO_RNG_FILL
#define RVE_DEV_VIRTIO_RNG_FILL(buffer, length) memset((buffer), 0, (length))
#endif

/**
 * virtio-rng instance.
 */
typedef struct {
    virtio_mmio_t mmio;
} virtio_rng_t;

/**
 * Initializes a virtio-rng instance at the given memory range.
 */
static inline void virtio_rng_init(virtio_rng_t *dev, uint32_t base, uint32_t size, uint8_t *guest_memory, uint32_t memory_base, uint32_t memory_size) {
    memset(dev, 0, sizeof(*dev));
    virtio_mmio_init(&dev->mmio, base, size, guest_memory, memory_base, memory_size, VIRTIO_MMIO_DEVICE_ID_RNG, 0, 0, 1, NULL, 0);
}

/**
 * Returns true when the address falls inside the virtio-mmio range.
 */
static inline bool virtio_rng_contains(const virtio_rng_t *dev, uint32_t address) {
    return virtio_mmio_contains(&dev->mmio, address);
}

/**
 * Fills one descriptor chain with host entropy and returns the byte count.
 */
static inline uint32_t virtio_rng_process_chain(virtio_rng_t *dev, uint32_t desc_table, uint16_t head) {
    virtio_mmio_desc_t desc;
    uint8_t buffer[VIRTIO_RNG_BUFFER_SIZE];
    uint32_t written = 0;
    uint16_t index = head;
    bool has_next = true;

    while (has_next) {
        if (!virtio_mmio_read_desc(&dev->mmio, desc_table, index, &desc)) {
            break;
        }

        has_next = (desc.flags & VIRTQ_DESC_F_NEXT) != 0;

        if ((desc.flags & VIRTQ_DESC_F_WRITE) == 0 || desc.length == 0 || desc.address > UINT32_MAX) {
            index = desc.next;
            continue;
        }

        uint32_t remaining = desc.length;
        uint32_t offset = 0;

        while (remaining > 0) {
            uint32_t chunk = remaining > sizeof(buffer) ? (uint32_t)sizeof(buffer) : remaining;

            RVE_DEV_VIRTIO_RNG_FILL(buffer, chunk);

            if (!virtio_mmio_guest_write(&dev->mmio, (uint32_t)desc.address + offset, buffer, chunk)) {
                break;
            }

            offset += chunk;
            remaining -= chunk;
            written += chunk;
        }

        index = desc.next;
    }

    return written;
}

/**
 * Processes all available requests in the queue.
 */
static inline void virtio_rng_process(virtio_rng_t *dev) {
    uint32_t desc_table;
    uint32_t avail;
    uint32_t used;

    if (!virtio_mmio_queue_layout(&dev->mmio, 0, &desc_table, &avail, &used)) {
        return;
    }

    virtio_mmio_queue_t *queue = &dev->mmio.queues[0];
    uint16_t avail_idx;

    if (!virtio_mmio_guest_read(&dev->mmio, avail + 2u, &avail_idx, sizeof(avail_idx))) {
        return;
    }

    while (queue->last_avail_idx != avail_idx) {
        uint16_t ring_index = (uint16_t)(queue->last_avail_idx % queue->queue_num);
        uint16_t head;

        if (!virtio_mmio_guest_read(&dev->mmio, avail + 4u + (uint32_t)ring_index * 2u, &head, sizeof(head))) {
            return;
        }

        uint32_t used_length = virtio_rng_process_chain(dev, desc_table, head);

        if (!virtio_mmio_add_used(&dev->mmio, 0, head, used_length)) {
            return;
        }

        queue->last_avail_idx++;
    }
}

/**
 * Returns true when the device has a used-ring interrupt pending.
 */
static inline bool virtio_rng_irq_pending(const virtio_rng_t *dev) {
    return virtio_mmio_irq_pending(&dev->mmio);
}

/**
 * Reads a virtio-mmio register or the config space.
 */
static inline bool virtio_rng_read(virtio_rng_t *dev, uint32_t address, void *destination, uint8_t length) {
    return virtio_mmio_read(&dev->mmio, address, destination, length);
}

/**
 * Writes a virtio-mmio register. A queue-notify write processes the queue.
 */
static inline bool virtio_rng_write(virtio_rng_t *dev, uint32_t address, const void *source, uint8_t length) {
    uint32_t notify_queue;

    if (!virtio_mmio_write(&dev->mmio, address, source, length, &notify_queue)) {
        return false;
    }

    if (notify_queue == 0) {
        virtio_rng_process(dev);
    }

    return true;
}

#endif

#endif
