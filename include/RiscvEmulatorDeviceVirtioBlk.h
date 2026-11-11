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

// virtio-mmio legacy v1 register offsets.
#define VIRTIO_MMIO_MAGIC_VALUE_OFFSET        0x000
#define VIRTIO_MMIO_VERSION_OFFSET            0x004
#define VIRTIO_MMIO_DEVICE_ID_OFFSET          0x008
#define VIRTIO_MMIO_VENDOR_ID_OFFSET          0x00C
#define VIRTIO_MMIO_HOST_FEATURES_OFFSET      0x010
#define VIRTIO_MMIO_HOST_FEATURES_SEL_OFFSET  0x014
#define VIRTIO_MMIO_GUEST_FEATURES_OFFSET     0x020
#define VIRTIO_MMIO_GUEST_FEATURES_SEL_OFFSET 0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE_OFFSET    0x028
#define VIRTIO_MMIO_QUEUE_SEL_OFFSET          0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX_OFFSET      0x034
#define VIRTIO_MMIO_QUEUE_NUM_OFFSET          0x038
#define VIRTIO_MMIO_QUEUE_ALIGN_OFFSET        0x03C
#define VIRTIO_MMIO_QUEUE_PFN_OFFSET          0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET       0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS_OFFSET   0x060
#define VIRTIO_MMIO_INTERRUPT_ACK_OFFSET      0x064
#define VIRTIO_MMIO_STATUS_OFFSET             0x070
#define VIRTIO_MMIO_CONFIG_OFFSET             0x100

// virtio-mmio identity values.
#define VIRTIO_MMIO_MAGIC_VALUE     0x74726976
#define VIRTIO_MMIO_VERSION         1
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

// Descriptor flags.
#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

// Maximum queue size.
#define VIRTIO_BLK_QUEUE_NUM_MAX 128

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
 * One virtio descriptor.
 */
typedef struct {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} virtio_blk_desc_t;

/**
 * virtio-blk instance.
 */
typedef struct {
    uint32_t base;
    uint32_t size;
    uint8_t *memory;
    uint32_t memory_base;
    uint32_t memory_size;
    uint64_t capacity;
    uint32_t host_features;
    uint32_t guest_features;
    uint32_t guest_page_size;
    uint32_t queue_sel;
    uint32_t queue_num;
    uint32_t queue_align;
    uint32_t queue_pfn;
    uint32_t interrupt_status;
    uint32_t status;
    uint16_t last_avail_idx;
    uint16_t used_idx;
} virtio_blk_t;

/**
 * Initializes a virtio-blk instance at the given memory range.
 */
static inline void virtio_blk_init(virtio_blk_t *dev, uint32_t base, uint32_t size, uint8_t *guest_memory, uint32_t memory_base, uint32_t memory_size, uint64_t capacity) {
    memset(dev, 0, sizeof(*dev));
    dev->base = base;
    dev->size = size;
    dev->memory = guest_memory;
    dev->memory_base = memory_base;
    dev->memory_size = memory_size;
    dev->capacity = capacity;
    dev->host_features = VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_FLUSH;
}

/**
 * Returns true when the address falls inside the virtio-mmio range.
 */
static inline bool virtio_blk_contains(const virtio_blk_t *dev, uint32_t address) {
    return address >= dev->base && address < dev->base + dev->size;
}

/**
 * Reads bytes from guest RAM.
 */
static inline bool virtio_blk_guest_read(const virtio_blk_t *dev, uint32_t address, void *destination, uint32_t length) {
    if (address < dev->memory_base || length > dev->memory_size) {
        return false;
    }

    if (address - dev->memory_base > dev->memory_size - length) {
        return false;
    }

    memcpy(destination, &dev->memory[address - dev->memory_base], length);
    return true;
}

/**
 * Writes bytes to guest RAM.
 */
static inline bool virtio_blk_guest_write(const virtio_blk_t *dev, uint32_t address, const void *source, uint32_t length) {
    if (address < dev->memory_base || length > dev->memory_size) {
        return false;
    }

    if (address - dev->memory_base > dev->memory_size - length) {
        return false;
    }

    memcpy(&dev->memory[address - dev->memory_base], source, length);
    return true;
}

/**
 * Reads one descriptor from the descriptor table.
 */
static inline bool virtio_blk_read_desc(const virtio_blk_t *dev, uint32_t desc_table, uint16_t index, virtio_blk_desc_t *desc) {
    uint8_t raw[16];

    if (!virtio_blk_guest_read(dev, desc_table + (uint32_t)index * 16u, raw, sizeof(raw))) {
        return false;
    }

    memcpy(&desc->address, &raw[0], sizeof(desc->address));
    memcpy(&desc->length, &raw[8], sizeof(desc->length));
    memcpy(&desc->flags, &raw[12], sizeof(desc->flags));
    memcpy(&desc->next, &raw[14], sizeof(desc->next));

    return true;
}

/**
 * Processes one descriptor chain and returns the request status.
 */
static inline uint8_t virtio_blk_process_chain(virtio_blk_t *dev, uint32_t desc_table, uint16_t head, uint32_t *used_length) {
    virtio_blk_desc_t desc;
    uint8_t header[16];
    uint8_t sector_buffer[VIRTIO_BLK_BUFFER_SIZE];
    uint32_t type;
    uint64_t sector;
    uint32_t written = 0;
    uint8_t status = VIRTIO_BLK_S_OK;

    if (!virtio_blk_read_desc(dev, desc_table, head, &desc)) {
        return VIRTIO_BLK_S_IOERR;
    }

    if ((desc.flags & VIRTQ_DESC_F_WRITE) != 0 || desc.length < sizeof(header) || desc.address > UINT32_MAX) {
        return VIRTIO_BLK_S_IOERR;
    }

    if (!virtio_blk_guest_read(dev, (uint32_t)desc.address, header, sizeof(header))) {
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
        if (!virtio_blk_read_desc(dev, desc_table, index, &desc)) {
            status = VIRTIO_BLK_S_IOERR;
            break;
        }

        has_next = (desc.flags & VIRTQ_DESC_F_NEXT) != 0;

        if (!has_next) {
            if ((desc.flags & VIRTQ_DESC_F_WRITE) == 0 || desc.length < 1 || desc.address > UINT32_MAX) {
                status = VIRTIO_BLK_S_IOERR;
                break;
            }

            if (!virtio_blk_guest_write(dev, (uint32_t)desc.address, &status, 1)) {
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

                if (!virtio_blk_guest_write(dev, (uint32_t)desc.address + offset, sector_buffer, chunk)) {
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

                if (!virtio_blk_guest_read(dev, (uint32_t)desc.address + offset, sector_buffer, chunk)) {
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

            if (!virtio_blk_guest_write(dev, (uint32_t)desc.address, identifier, length)) {
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
    if (dev->queue_pfn == 0 || dev->queue_num == 0) {
        return;
    }

    uint32_t desc_table = dev->queue_pfn << 12;
    uint32_t avail = desc_table + dev->queue_num * 16u;
    uint32_t align = dev->queue_align != 0 ? dev->queue_align : 4096u;
    uint32_t used = (avail + 4u + dev->queue_num * 2u + align - 1u) & ~(align - 1u);

    uint16_t avail_idx;

    if (!virtio_blk_guest_read(dev, avail + 2u, &avail_idx, sizeof(avail_idx))) {
        return;
    }

    bool processed = false;

    while (dev->last_avail_idx != avail_idx) {
        uint16_t ring_index = (uint16_t)(dev->last_avail_idx % dev->queue_num);
        uint16_t head;

        if (!virtio_blk_guest_read(dev, avail + 4u + (uint32_t)ring_index * 2u, &head, sizeof(head))) {
            return;
        }

        uint32_t used_length = 0;
        virtio_blk_process_chain(dev, desc_table, head, &used_length);

        uint32_t used_entry = used + 4u + (uint32_t)(dev->used_idx % dev->queue_num) * 8u;
        uint32_t id = head;
        uint8_t entry[8];

        memcpy(&entry[0], &id, sizeof(id));
        memcpy(&entry[4], &used_length, sizeof(used_length));

        if (!virtio_blk_guest_write(dev, used_entry, entry, sizeof(entry))) {
            return;
        }

        dev->used_idx++;
        dev->last_avail_idx++;
        processed = true;
    }

    if (!processed) {
        return;
    }

    if (!virtio_blk_guest_write(dev, used + 2u, &dev->used_idx, sizeof(dev->used_idx))) {
        return;
    }

    dev->interrupt_status |= 1u;
}

/**
 * Returns true when the device has a used-ring interrupt pending.
 */
static inline bool virtio_blk_irq_pending(const virtio_blk_t *dev) {
    return (dev->interrupt_status & 1u) != 0;
}

/**
 * Reads a virtio-mmio register or the config space.
 */
static inline bool virtio_blk_read(virtio_blk_t *dev, uint32_t address, void *destination, uint8_t length) {
    if (!virtio_blk_contains(dev, address)) {
        return false;
    }

    uint32_t offset = address - dev->base;
    uint8_t bytes[8] = {0};

    if (offset >= VIRTIO_MMIO_CONFIG_OFFSET) {
        uint32_t config_offset = offset - VIRTIO_MMIO_CONFIG_OFFSET;
        uint64_t value = 0;
        uint32_t base = 0;
        uint32_t width = 0;

        if (config_offset < 8) {
            value = dev->capacity;
            base = 0;
            width = 8;
        } else if (config_offset >= 20 && config_offset < 24) {
            value = VIRTIO_BLK_SECTOR_SIZE;
            base = 20;
            width = 4;
        }

        if (width != 0 && config_offset - base < width) {
            uint32_t start = config_offset - base;
            uint32_t available = width - start;
            uint32_t copy = length < available ? length : available;

            memcpy(bytes, &value, sizeof(value));
            memcpy(destination, &bytes[start], copy);
            if (copy < length) {
                memset((uint8_t *)destination + copy, 0, length - copy);
            }
            return true;
        }

        memset(destination, 0, length);
        return true;
    }

    uint32_t value = 0;

    switch (offset) {
        case VIRTIO_MMIO_MAGIC_VALUE_OFFSET:
            value = VIRTIO_MMIO_MAGIC_VALUE;
            break;
        case VIRTIO_MMIO_VERSION_OFFSET:
            value = VIRTIO_MMIO_VERSION;
            break;
        case VIRTIO_MMIO_DEVICE_ID_OFFSET:
            value = VIRTIO_MMIO_DEVICE_ID_BLOCK;
            break;
        case VIRTIO_MMIO_VENDOR_ID_OFFSET:
            value = RVE_DEV_VIRTIO_BLK_VENDOR_ID;
            break;
        case VIRTIO_MMIO_HOST_FEATURES_OFFSET:
            value = dev->host_features;
            break;
        case VIRTIO_MMIO_QUEUE_NUM_MAX_OFFSET:
            value = VIRTIO_BLK_QUEUE_NUM_MAX;
            break;
        case VIRTIO_MMIO_QUEUE_PFN_OFFSET:
            value = dev->queue_pfn;
            break;
        case VIRTIO_MMIO_INTERRUPT_STATUS_OFFSET:
            value = dev->interrupt_status;
            break;
        case VIRTIO_MMIO_STATUS_OFFSET:
            value = dev->status;
            break;
        default:
            break;
    }

    memcpy(bytes, &value, sizeof(value));

    uint32_t copy = length < sizeof(bytes) ? length : (uint32_t)sizeof(bytes);
    memcpy(destination, bytes, copy);
    if (copy < length) {
        memset((uint8_t *)destination + copy, 0, length - copy);
    }

    return true;
}

/**
 * Writes a virtio-mmio register. A queue-notify write processes the queue.
 */
static inline bool virtio_blk_write(virtio_blk_t *dev, uint32_t address, const void *source, uint8_t length) {
    if (!virtio_blk_contains(dev, address)) {
        return false;
    }

    if (length != sizeof(uint32_t)) {
        return true;
    }

    uint32_t value;
    memcpy(&value, source, sizeof(value));

    uint32_t offset = address - dev->base;

    switch (offset) {
        case VIRTIO_MMIO_HOST_FEATURES_SEL_OFFSET:
            break;
        case VIRTIO_MMIO_GUEST_FEATURES_OFFSET:
            dev->guest_features = value;
            break;
        case VIRTIO_MMIO_GUEST_FEATURES_SEL_OFFSET:
            break;
        case VIRTIO_MMIO_GUEST_PAGE_SIZE_OFFSET:
            dev->guest_page_size = value;
            break;
        case VIRTIO_MMIO_QUEUE_SEL_OFFSET:
            dev->queue_sel = value;
            break;
        case VIRTIO_MMIO_QUEUE_NUM_OFFSET:
            dev->queue_num = value;
            break;
        case VIRTIO_MMIO_QUEUE_ALIGN_OFFSET:
            dev->queue_align = value;
            break;
        case VIRTIO_MMIO_QUEUE_PFN_OFFSET:
            dev->queue_pfn = value;
            break;
        case VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET:
            virtio_blk_process(dev);
            break;
        case VIRTIO_MMIO_INTERRUPT_ACK_OFFSET:
            dev->interrupt_status &= ~value;
            break;
        case VIRTIO_MMIO_STATUS_OFFSET:
            dev->status = value;
            break;
        default:
            break;
    }

    return true;
}

#endif

#endif
