/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#ifndef RiscvEmulatorDeviceVirtioBlk_H_
#define RiscvEmulatorDeviceVirtioBlk_H_

#ifndef RVE_DEV_VIRTIO_BLK
#define RVE_DEV_VIRTIO_BLK 1
#endif

#if (RVE_DEV_VIRTIO_BLK == 1)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "RiscvEmulatorDeviceVirtioMmio.h"

// virtio-mmio identity value.
#define VIRTIO_MMIO_DEVICE_ID_BLOCK 2

// virtio-blk feature bits.
#define VIRTIO_BLK_F_BLK_SIZE (1u << 6)
#define VIRTIO_BLK_F_FLUSH    (1u << 9)

// virtio-blk request types.
#define VIRTIO_BLK_T_IN     0
#define VIRTIO_BLK_T_OUT    1
#define VIRTIO_BLK_T_FLUSH  4
#define VIRTIO_BLK_T_GET_ID 8

// virtio-blk request status values.
#define VIRTIO_BLK_S_OK     0
#define VIRTIO_BLK_S_IOERR  1
#define VIRTIO_BLK_S_UNSUPP 2

// Maximum queue size.
#define VIRTIO_BLK_QUEUE_NUM_MAX VIRTIO_MMIO_QUEUE_NUM_MAX

// Sector size in bytes.
#define VIRTIO_BLK_SECTOR_SIZE 512

// Host I/O buffer size in bytes.
#define VIRTIO_BLK_BUFFER_SIZE 4096

// Host hooks. A board defines these before including this header.
#ifndef RVE_DEV_VIRTIO_BLK_READ
#define RVE_DEV_VIRTIO_BLK_READ(sector, buffer, count) ((void)(sector), (void)(buffer), (void)(count), false)
#endif

#ifndef RVE_DEV_VIRTIO_BLK_WRITE
#define RVE_DEV_VIRTIO_BLK_WRITE(sector, buffer, count) ((void)(sector), (void)(buffer), (void)(count), false)
#endif

#ifndef RVE_DEV_VIRTIO_BLK_FLUSH
#define RVE_DEV_VIRTIO_BLK_FLUSH() (false)
#endif

#ifndef RVE_DEV_VIRTIO_BLK_VENDOR_ID
#define RVE_DEV_VIRTIO_BLK_VENDOR_ID 0
#endif

/**
 * virtio-blk instance.
 */
typedef struct {
    virtio_mmio_t mmio;
    uint64_t capacity;
    uint8_t config[24];
} virtio_blk_t;

/**
 * Initializes a virtio-blk instance at the given memory range.
 */
static inline void virtio_blk_init(virtio_blk_t *dev, uint32_t base, uint32_t size, uint8_t *guest_memory, uint32_t memory_base, uint32_t memory_size, uint64_t capacity) {
    memset(dev, 0, sizeof(*dev));
    dev->capacity = capacity;
    memcpy(&dev->config[0], &capacity, sizeof(capacity));

    uint32_t block_size = VIRTIO_BLK_SECTOR_SIZE;
    memcpy(&dev->config[20], &block_size, sizeof(block_size));

    virtio_mmio_init(&dev->mmio, base, size, guest_memory, memory_base, memory_size, VIRTIO_MMIO_DEVICE_ID_BLOCK, RVE_DEV_VIRTIO_BLK_VENDOR_ID, VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_FLUSH, 1, dev->config, sizeof(dev->config));
}

/**
 * Returns true when the address falls inside the virtio-mmio range.
 */
static inline bool virtio_blk_contains(const virtio_blk_t *dev, uint32_t address) {
    return virtio_mmio_contains(&dev->mmio, address);
}

/**
 * Processes one descriptor chain and returns the request status.
 */
static inline uint8_t virtio_blk_process_chain(virtio_blk_t *dev, uint32_t desc_table, uint16_t head, uint32_t *used_length) {
    virtio_mmio_desc_t desc;
    uint8_t header[16];
    uint8_t sector_buffer[VIRTIO_BLK_BUFFER_SIZE];
    uint32_t type;
    uint64_t sector;
    uint32_t written = 0;
    uint8_t status = VIRTIO_BLK_S_OK;

    if (!virtio_mmio_read_desc(&dev->mmio, desc_table, head, &desc)) {
        return VIRTIO_BLK_S_IOERR;
    }

    if ((desc.flags & VIRTQ_DESC_F_WRITE) != 0 || desc.length < sizeof(header) || desc.address > UINT32_MAX) {
        return VIRTIO_BLK_S_IOERR;
    }

    if (!virtio_mmio_guest_read(&dev->mmio, (uint32_t)desc.address, header, sizeof(header))) {
        return VIRTIO_BLK_S_IOERR;
    }

    memcpy(&type, &header[0], sizeof(type));
    memcpy(&sector, &header[8], sizeof(sector));

    if (type != VIRTIO_BLK_T_IN && type != VIRTIO_BLK_T_OUT && type != VIRTIO_BLK_T_FLUSH && type != VIRTIO_BLK_T_GET_ID) {
        status = VIRTIO_BLK_S_UNSUPP;
    }

    if (type == VIRTIO_BLK_T_FLUSH && !RVE_DEV_VIRTIO_BLK_FLUSH()) {
        status = VIRTIO_BLK_S_IOERR;
    }

    bool has_next = (desc.flags & VIRTQ_DESC_F_NEXT) != 0;
    uint16_t index = desc.next;

    while (has_next) {
        if (!virtio_mmio_read_desc(&dev->mmio, desc_table, index, &desc)) {
            status = VIRTIO_BLK_S_IOERR;
            break;
        }

        has_next = (desc.flags & VIRTQ_DESC_F_NEXT) != 0;

        if (!has_next) {
            if ((desc.flags & VIRTQ_DESC_F_WRITE) == 0 || desc.length < 1 || desc.address > UINT32_MAX) {
                status = VIRTIO_BLK_S_IOERR;
                break;
            }

            if (!virtio_mmio_guest_write(&dev->mmio, (uint32_t)desc.address, &status, 1)) {
                status = VIRTIO_BLK_S_IOERR;
            }
            break;
        }

        if (status != VIRTIO_BLK_S_OK) {
            index = desc.next;
            continue;
        }

        if (desc.address > UINT32_MAX) {
            status = VIRTIO_BLK_S_IOERR;
            index = desc.next;
            continue;
        }

        if (type == VIRTIO_BLK_T_IN) {
            if ((desc.flags & VIRTQ_DESC_F_WRITE) == 0) {
                status = VIRTIO_BLK_S_IOERR;
                index = desc.next;
                continue;
            }

            uint32_t remaining = desc.length;
            uint32_t offset = 0;

            while (remaining > 0) {
                uint32_t chunk = remaining > sizeof(sector_buffer) ? (uint32_t)sizeof(sector_buffer) : remaining;
                uint32_t sectors = (chunk + VIRTIO_BLK_SECTOR_SIZE - 1) / VIRTIO_BLK_SECTOR_SIZE;

                if (!RVE_DEV_VIRTIO_BLK_READ(sector, sector_buffer, sectors)) {
                    status = VIRTIO_BLK_S_IOERR;
                    break;
                }

                if (!virtio_mmio_guest_write(&dev->mmio, (uint32_t)desc.address + offset, sector_buffer, chunk)) {
                    status = VIRTIO_BLK_S_IOERR;
                    break;
                }

                sector += sectors;
                offset += chunk;
                remaining -= chunk;
                written += chunk;
            }
        } else if (type == VIRTIO_BLK_T_OUT) {
            if ((desc.flags & VIRTQ_DESC_F_WRITE) != 0) {
                status = VIRTIO_BLK_S_IOERR;
                index = desc.next;
                continue;
            }

            uint32_t remaining = desc.length;
            uint32_t offset = 0;

            while (remaining > 0) {
                uint32_t chunk = remaining > sizeof(sector_buffer) ? (uint32_t)sizeof(sector_buffer) : remaining;
                uint32_t sectors = (chunk + VIRTIO_BLK_SECTOR_SIZE - 1) / VIRTIO_BLK_SECTOR_SIZE;

                if (!virtio_mmio_guest_read(&dev->mmio, (uint32_t)desc.address + offset, sector_buffer, chunk)) {
                    status = VIRTIO_BLK_S_IOERR;
                    break;
                }

                if (!RVE_DEV_VIRTIO_BLK_WRITE(sector, sector_buffer, sectors)) {
                    status = VIRTIO_BLK_S_IOERR;
                    break;
                }

                sector += sectors;
                offset += chunk;
                remaining -= chunk;
            }
        } else if (type == VIRTIO_BLK_T_GET_ID) {
            static const char identifier[] = "rv32i-virtio-blk";
            uint32_t length = desc.length < sizeof(identifier) ? desc.length : (uint32_t)sizeof(identifier);

            if ((desc.flags & VIRTQ_DESC_F_WRITE) == 0) {
                status = VIRTIO_BLK_S_IOERR;
                index = desc.next;
                continue;
            }

            if (!virtio_mmio_guest_write(&dev->mmio, (uint32_t)desc.address, identifier, length)) {
                status = VIRTIO_BLK_S_IOERR;
            } else {
                written += length;
            }
        }

        index = desc.next;
    }

    *used_length = written;
    return status;
}

/**
 * Processes all available requests in the queue.
 */
static inline void virtio_blk_process(virtio_blk_t *dev) {
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

        uint32_t used_length = 0;
        virtio_blk_process_chain(dev, desc_table, head, &used_length);

        if (!virtio_mmio_add_used(&dev->mmio, 0, head, used_length)) {
            return;
        }

        queue->last_avail_idx++;
    }
}

/**
 * Returns true when the device has a used-ring interrupt pending.
 */
static inline bool virtio_blk_irq_pending(const virtio_blk_t *dev) {
    return virtio_mmio_irq_pending(&dev->mmio);
}

/**
 * Reads a virtio-mmio register or the config space.
 */
static inline bool virtio_blk_read(virtio_blk_t *dev, uint32_t address, void *destination, uint8_t length) {
    return virtio_mmio_read(&dev->mmio, address, destination, length);
}

/**
 * Writes a virtio-mmio register. A queue-notify write processes the queue.
 */
static inline bool virtio_blk_write(virtio_blk_t *dev, uint32_t address, const void *source, uint8_t length) {
    uint32_t notify_queue;

    if (!virtio_mmio_write(&dev->mmio, address, source, length, &notify_queue)) {
        return false;
    }

    if (notify_queue == 0) {
        virtio_blk_process(dev);
    }

    return true;
}

#endif

#endif
