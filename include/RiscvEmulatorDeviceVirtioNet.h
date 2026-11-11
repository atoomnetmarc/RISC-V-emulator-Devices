/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#ifndef RiscvEmulatorDeviceVirtioNet_H_
#define RiscvEmulatorDeviceVirtioNet_H_

#ifndef RVE_DEV_VIRTIO_NET
#define RVE_DEV_VIRTIO_NET 1
#endif

#if (RVE_DEV_VIRTIO_NET == 1)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "RiscvEmulatorDeviceVirtioMmio.h"

// virtio-mmio identity value.
#define VIRTIO_MMIO_DEVICE_ID_NET 1

// virtio-net feature bits.
#define VIRTIO_NET_F_MAC    (1u << 5)
#define VIRTIO_NET_F_STATUS (1u << 16)

// virtio-net link status values.
#define VIRTIO_NET_S_LINK_UP 1

// Network queue indexes.
#define VIRTIO_NET_QUEUE_RX 0
#define VIRTIO_NET_QUEUE_TX 1

// Legacy virtio-net header size in bytes.
#define VIRTIO_NET_HDR_SIZE 10

// Maximum Ethernet frame size in bytes.
#define VIRTIO_NET_BUFFER_SIZE 1514

// Host hook. A board defines this before including this header.
#ifndef RVE_DEV_VIRTIO_NET_TX
#define RVE_DEV_VIRTIO_NET_TX(buffer, length) ((void)(buffer), (void)(length))
#endif

#ifndef RVE_DEV_VIRTIO_NET_VENDOR_ID
#define RVE_DEV_VIRTIO_NET_VENDOR_ID 0
#endif

/**
 * virtio-net instance.
 */
typedef struct {
    virtio_mmio_t mmio;
    uint8_t config[8];
} virtio_net_t;

/**
 * Initializes a virtio-net instance at the given memory range.
 */
static inline void virtio_net_init(virtio_net_t *dev, uint32_t base, uint32_t size, uint8_t *guest_memory, uint32_t memory_base, uint32_t memory_size, const uint8_t mac[6]) {
    memset(dev, 0, sizeof(*dev));
    memcpy(&dev->config[0], mac, 6);

    uint16_t status = VIRTIO_NET_S_LINK_UP;
    memcpy(&dev->config[6], &status, sizeof(status));

    virtio_mmio_init(&dev->mmio, base, size, guest_memory, memory_base, memory_size, VIRTIO_MMIO_DEVICE_ID_NET, RVE_DEV_VIRTIO_NET_VENDOR_ID, VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS, 2, dev->config, sizeof(dev->config));
}

/**
 * Returns true when the address falls inside the virtio-mmio range.
 */
static inline bool virtio_net_contains(const virtio_net_t *dev, uint32_t address) {
    return virtio_mmio_contains(&dev->mmio, address);
}

/**
 * Gathers one transmit chain and calls the host hook.
 */
static inline void virtio_net_tx_chain(virtio_net_t *dev, uint32_t desc_table, uint16_t head) {
    virtio_mmio_desc_t desc;
    uint8_t frame[VIRTIO_NET_BUFFER_SIZE];
    uint32_t frame_length;

    if (!virtio_mmio_read_desc(&dev->mmio, desc_table, head, &desc)) {
        return;
    }

    if ((desc.flags & VIRTQ_DESC_F_WRITE) != 0 || desc.address > UINT32_MAX || desc.length < VIRTIO_NET_HDR_SIZE) {
        return;
    }

    frame_length = desc.length - VIRTIO_NET_HDR_SIZE;

    if (frame_length > sizeof(frame)) {
        return;
    }

    if (!virtio_mmio_guest_read(&dev->mmio, (uint32_t)desc.address + VIRTIO_NET_HDR_SIZE, frame, frame_length)) {
        return;
    }

    bool has_next = (desc.flags & VIRTQ_DESC_F_NEXT) != 0;
    uint16_t index = desc.next;

    while (has_next) {
        if (!virtio_mmio_read_desc(&dev->mmio, desc_table, index, &desc)) {
            return;
        }

        has_next = (desc.flags & VIRTQ_DESC_F_NEXT) != 0;

        if ((desc.flags & VIRTQ_DESC_F_WRITE) != 0 || desc.address > UINT32_MAX) {
            index = desc.next;
            continue;
        }

        if (desc.length > sizeof(frame) - frame_length) {
            return;
        }

        if (!virtio_mmio_guest_read(&dev->mmio, (uint32_t)desc.address, &frame[frame_length], desc.length)) {
            return;
        }

        frame_length += desc.length;
        index = desc.next;
    }

    if (frame_length > 0) {
        RVE_DEV_VIRTIO_NET_TX(frame, frame_length);
    }
}

/**
 * Processes all available transmit requests in the queue.
 */
static inline void virtio_net_process_tx(virtio_net_t *dev) {
    uint32_t desc_table;
    uint32_t avail;
    uint32_t used;

    if (!virtio_mmio_queue_layout(&dev->mmio, VIRTIO_NET_QUEUE_TX, &desc_table, &avail, &used)) {
        return;
    }

    virtio_mmio_queue_t *queue = &dev->mmio.queues[VIRTIO_NET_QUEUE_TX];
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

        virtio_net_tx_chain(dev, desc_table, head);

        if (!virtio_mmio_add_used(&dev->mmio, VIRTIO_NET_QUEUE_TX, head, 0)) {
            return;
        }

        queue->last_avail_idx++;
    }
}

/**
 * Delivers one frame to the next receiveq buffer.
 */
static inline bool virtio_net_rx_push(virtio_net_t *dev, const uint8_t *packet, uint32_t length) {
    uint32_t desc_table;
    uint32_t avail;
    uint32_t used;

    if (!virtio_mmio_queue_layout(&dev->mmio, VIRTIO_NET_QUEUE_RX, &desc_table, &avail, &used)) {
        return false;
    }

    virtio_mmio_queue_t *queue = &dev->mmio.queues[VIRTIO_NET_QUEUE_RX];
    uint16_t avail_idx;

    if (!virtio_mmio_guest_read(&dev->mmio, avail + 2u, &avail_idx, sizeof(avail_idx))) {
        return false;
    }

    if (queue->last_avail_idx == avail_idx) {
        return false;
    }

    uint16_t ring_index = (uint16_t)(queue->last_avail_idx % queue->queue_num);
    uint16_t head;

    if (!virtio_mmio_guest_read(&dev->mmio, avail + 4u + (uint32_t)ring_index * 2u, &head, sizeof(head))) {
        return false;
    }

    virtio_mmio_desc_t desc;

    if (!virtio_mmio_read_desc(&dev->mmio, desc_table, head, &desc)) {
        return false;
    }

    if ((desc.flags & VIRTQ_DESC_F_WRITE) == 0 || desc.address > UINT32_MAX || desc.length < VIRTIO_NET_HDR_SIZE + length) {
        if (!virtio_mmio_add_used(&dev->mmio, VIRTIO_NET_QUEUE_RX, head, 0)) {
            return false;
        }
        queue->last_avail_idx++;
        return false;
    }

    uint8_t header[VIRTIO_NET_HDR_SIZE] = {0};

    if (!virtio_mmio_guest_write(&dev->mmio, (uint32_t)desc.address, header, sizeof(header))) {
        return false;
    }

    if (!virtio_mmio_guest_write(&dev->mmio, (uint32_t)desc.address + VIRTIO_NET_HDR_SIZE, packet, length)) {
        return false;
    }

    if (!virtio_mmio_add_used(&dev->mmio, VIRTIO_NET_QUEUE_RX, head, VIRTIO_NET_HDR_SIZE + length)) {
        return false;
    }

    queue->last_avail_idx++;
    return true;
}

/**
 * Returns true when the device has a used-ring interrupt pending.
 */
static inline bool virtio_net_irq_pending(const virtio_net_t *dev) {
    return virtio_mmio_irq_pending(&dev->mmio);
}

/**
 * Reads a virtio-mmio register or the config space.
 */
static inline bool virtio_net_read(virtio_net_t *dev, uint32_t address, void *destination, uint8_t length) {
    return virtio_mmio_read(&dev->mmio, address, destination, length);
}

/**
 * Writes a virtio-mmio register. A transmit-queue notify processes the queue.
 */
static inline bool virtio_net_write(virtio_net_t *dev, uint32_t address, const void *source, uint8_t length) {
    uint32_t notify_queue;

    if (!virtio_mmio_write(&dev->mmio, address, source, length, &notify_queue)) {
        return false;
    }

    if (notify_queue == VIRTIO_NET_QUEUE_TX) {
        virtio_net_process_tx(dev);
    }

    return true;
}

#endif

#endif
