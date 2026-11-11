/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#ifndef RiscvEmulatorDeviceVirtioConsole_H_
#define RiscvEmulatorDeviceVirtioConsole_H_

#ifndef RVE_DEV_VIRTIO_CONSOLE
#define RVE_DEV_VIRTIO_CONSOLE 1
#endif

#if (RVE_DEV_VIRTIO_CONSOLE == 1)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "RiscvEmulatorDeviceVirtioMmio.h"

// virtio-mmio identity value.
#define VIRTIO_MMIO_DEVICE_ID_CONSOLE 3

// Console queue indexes.
#define VIRTIO_CONSOLE_QUEUE_RX 0
#define VIRTIO_CONSOLE_QUEUE_TX 1

// RX FIFO size in bytes.
#ifndef VIRTIO_CONSOLE_RX_FIFO_SIZE
#define VIRTIO_CONSOLE_RX_FIFO_SIZE 16
#endif

#if (VIRTIO_CONSOLE_RX_FIFO_SIZE > 255)
#error "VIRTIO_CONSOLE_RX_FIFO_SIZE must fit in uint8_t"
#endif

// Host hook. A board defines this before including this header.
#ifndef RVE_DEV_VIRTIO_CONSOLE_TX
#define RVE_DEV_VIRTIO_CONSOLE_TX(buffer, length) ((void)(buffer), (void)(length))
#endif

/**
 * virtio-console instance.
 */
typedef struct {
    virtio_mmio_t mmio;
    uint8_t rx_fifo[VIRTIO_CONSOLE_RX_FIFO_SIZE];
    uint8_t rx_read;
    uint8_t rx_write;
    uint8_t rx_count;
    bool rx_overrun;
} virtio_console_t;

/**
 * Initializes a virtio-console instance at the given memory range.
 */
static inline void virtio_console_init(virtio_console_t *dev, uint32_t base, uint32_t size, uint8_t *guest_memory, uint32_t memory_base, uint32_t memory_size) {
    memset(dev, 0, sizeof(*dev));
    virtio_mmio_init(&dev->mmio, base, size, guest_memory, memory_base, memory_size, VIRTIO_MMIO_DEVICE_ID_CONSOLE, 0, 0, 2, NULL, 0);
}

/**
 * Returns true when the address falls inside the virtio-mmio range.
 */
static inline bool virtio_console_contains(const virtio_console_t *dev, uint32_t address) {
    return virtio_mmio_contains(&dev->mmio, address);
}

/**
 * Drains the RX FIFO into the available input buffers.
 */
static inline void virtio_console_drain(virtio_console_t *dev) {
    if (dev->rx_count == 0) {
        return;
    }

    uint32_t desc_table;
    uint32_t avail;
    uint32_t used;

    if (!virtio_mmio_queue_layout(&dev->mmio, VIRTIO_CONSOLE_QUEUE_RX, &desc_table, &avail, &used)) {
        return;
    }

    virtio_mmio_queue_t *queue = &dev->mmio.queues[VIRTIO_CONSOLE_QUEUE_RX];
    uint16_t avail_idx;

    if (!virtio_mmio_guest_read(&dev->mmio, avail + 2u, &avail_idx, sizeof(avail_idx))) {
        return;
    }

    while (queue->last_avail_idx != avail_idx && dev->rx_count > 0) {
        uint16_t ring_index = (uint16_t)(queue->last_avail_idx % queue->queue_num);
        uint16_t head;

        if (!virtio_mmio_guest_read(&dev->mmio, avail + 4u + (uint32_t)ring_index * 2u, &head, sizeof(head))) {
            return;
        }

        virtio_mmio_desc_t desc;

        if (!virtio_mmio_read_desc(&dev->mmio, desc_table, head, &desc)) {
            return;
        }

        if ((desc.flags & VIRTQ_DESC_F_WRITE) == 0 || desc.address > UINT32_MAX || desc.length == 0) {
            if (!virtio_mmio_add_used(&dev->mmio, VIRTIO_CONSOLE_QUEUE_RX, head, 0)) {
                return;
            }
            queue->last_avail_idx++;
            continue;
        }

        uint32_t length = desc.length < dev->rx_count ? desc.length : dev->rx_count;
        uint32_t first = VIRTIO_CONSOLE_RX_FIFO_SIZE - dev->rx_read;

        if (first > length) {
            first = length;
        }

        if (!virtio_mmio_guest_write(&dev->mmio, (uint32_t)desc.address, &dev->rx_fifo[dev->rx_read], first)) {
            return;
        }

        if (length > first) {
            if (!virtio_mmio_guest_write(&dev->mmio, (uint32_t)desc.address + first, &dev->rx_fifo[0], length - first)) {
                return;
            }
        }

        dev->rx_read = (uint8_t)((dev->rx_read + length) % VIRTIO_CONSOLE_RX_FIFO_SIZE);
        dev->rx_count = (uint8_t)(dev->rx_count - length);

        if (!virtio_mmio_add_used(&dev->mmio, VIRTIO_CONSOLE_QUEUE_RX, head, length)) {
            return;
        }

        queue->last_avail_idx++;
    }
}

/**
 * Processes all available output buffers and calls the host hook.
 */
static inline void virtio_console_process_tx(virtio_console_t *dev) {
    uint32_t desc_table;
    uint32_t avail;
    uint32_t used;

    if (!virtio_mmio_queue_layout(&dev->mmio, VIRTIO_CONSOLE_QUEUE_TX, &desc_table, &avail, &used)) {
        return;
    }

    virtio_mmio_queue_t *queue = &dev->mmio.queues[VIRTIO_CONSOLE_QUEUE_TX];
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

        uint32_t total = 0;
        bool has_next = true;
        uint16_t index = head;

        while (has_next) {
            virtio_mmio_desc_t desc;

            if (!virtio_mmio_read_desc(&dev->mmio, desc_table, index, &desc)) {
                break;
            }

            has_next = (desc.flags & VIRTQ_DESC_F_NEXT) != 0;

            if ((desc.flags & VIRTQ_DESC_F_WRITE) == 0 && desc.length > 0 && desc.address >= dev->mmio.memory_base && desc.length <= dev->mmio.memory_size && desc.address - dev->mmio.memory_base <= dev->mmio.memory_size - desc.length) {
                uint32_t offset = (uint32_t)(desc.address - dev->mmio.memory_base);
                RVE_DEV_VIRTIO_CONSOLE_TX(&dev->mmio.memory[offset], desc.length);
                total += desc.length;
            }

            index = desc.next;
        }

        if (!virtio_mmio_add_used(&dev->mmio, VIRTIO_CONSOLE_QUEUE_TX, head, total)) {
            return;
        }

        queue->last_avail_idx++;
    }
}

/**
 * Processes one console queue.
 */
static inline void virtio_console_process(virtio_console_t *dev, uint32_t queue_index) {
    if (queue_index == VIRTIO_CONSOLE_QUEUE_RX) {
        virtio_console_drain(dev);
    } else if (queue_index == VIRTIO_CONSOLE_QUEUE_TX) {
        virtio_console_process_tx(dev);
    }
}

/**
 * Returns true when the device has a used-ring interrupt pending.
 */
static inline bool virtio_console_irq_pending(const virtio_console_t *dev) {
    return virtio_mmio_irq_pending(&dev->mmio);
}

/**
 * Reads a virtio-mmio register or the config space.
 */
static inline bool virtio_console_read(virtio_console_t *dev, uint32_t address, void *destination, uint8_t length) {
    return virtio_mmio_read(&dev->mmio, address, destination, length);
}

/**
 * Writes a virtio-mmio register. A queue-notify write processes the queue.
 */
static inline bool virtio_console_write(virtio_console_t *dev, uint32_t address, const void *source, uint8_t length) {
    uint32_t notify_queue;

    if (!virtio_mmio_write(&dev->mmio, address, source, length, &notify_queue)) {
        return false;
    }

    if (notify_queue != UINT32_MAX) {
        virtio_console_process(dev, notify_queue);
    }

    return true;
}

/**
 * Pushes one input byte into the RX FIFO and drains it into the guest.
 */
static inline void virtio_console_rx_push(virtio_console_t *dev, uint8_t byte) {
    if (dev->rx_count >= VIRTIO_CONSOLE_RX_FIFO_SIZE) {
        dev->rx_overrun = true;
        return;
    }

    dev->rx_fifo[dev->rx_write] = byte;
    dev->rx_write = (uint8_t)((dev->rx_write + 1) % VIRTIO_CONSOLE_RX_FIFO_SIZE);
    dev->rx_count++;

    virtio_console_drain(dev);
}

#endif

#endif
