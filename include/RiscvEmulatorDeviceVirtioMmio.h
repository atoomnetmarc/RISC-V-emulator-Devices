/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#ifndef RiscvEmulatorDeviceVirtioMmio_H_
#define RiscvEmulatorDeviceVirtioMmio_H_

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
#define VIRTIO_MMIO_MAGIC_VALUE 0x74726976
#define VIRTIO_MMIO_VERSION     1

// Descriptor flags.
#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

// Maximum number of queues per device.
#define VIRTIO_MMIO_QUEUE_MAX 2

// Maximum queue size.
#define VIRTIO_MMIO_QUEUE_NUM_MAX 128

/**
 * One virtio descriptor.
 */
typedef struct {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} virtio_mmio_desc_t;

/**
 * One virtio queue.
 */
typedef struct {
    uint32_t queue_num;
    uint32_t queue_align;
    uint32_t queue_pfn;
    uint16_t last_avail_idx;
    uint16_t used_idx;
} virtio_mmio_queue_t;

/**
 * virtio-mmio transport instance.
 */
typedef struct {
    uint32_t base;
    uint32_t size;
    uint8_t *memory;
    uint32_t memory_base;
    uint32_t memory_size;
    uint32_t device_id;
    uint32_t vendor_id;
    uint32_t host_features;
    uint32_t guest_features;
    uint32_t guest_page_size;
    uint32_t queue_sel;
    uint32_t queue_count;
    virtio_mmio_queue_t queues[VIRTIO_MMIO_QUEUE_MAX];
    uint32_t interrupt_status;
    uint32_t status;
    const uint8_t *config;
    uint32_t config_size;
} virtio_mmio_t;

/**
 * Initializes a virtio-mmio transport at the given memory range.
 */
static inline void virtio_mmio_init(virtio_mmio_t *dev, uint32_t base, uint32_t size, uint8_t *guest_memory, uint32_t memory_base, uint32_t memory_size, uint32_t device_id, uint32_t vendor_id, uint32_t host_features, uint32_t queue_count, const uint8_t *config, uint32_t config_size) {
    memset(dev, 0, sizeof(*dev));
    dev->base = base;
    dev->size = size;
    dev->memory = guest_memory;
    dev->memory_base = memory_base;
    dev->memory_size = memory_size;
    dev->device_id = device_id;
    dev->vendor_id = vendor_id;
    dev->host_features = host_features;
    dev->queue_count = queue_count < VIRTIO_MMIO_QUEUE_MAX ? queue_count : VIRTIO_MMIO_QUEUE_MAX;
    dev->config = config;
    dev->config_size = config_size;
}

/**
 * Returns true when the address falls inside the virtio-mmio range.
 */
static inline bool virtio_mmio_contains(const virtio_mmio_t *dev, uint32_t address) {
    return address >= dev->base && address < dev->base + dev->size;
}

/**
 * Reads bytes from guest RAM.
 */
static inline bool virtio_mmio_guest_read(const virtio_mmio_t *dev, uint32_t address, void *destination, uint32_t length) {
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
static inline bool virtio_mmio_guest_write(const virtio_mmio_t *dev, uint32_t address, const void *source, uint32_t length) {
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
static inline bool virtio_mmio_read_desc(const virtio_mmio_t *dev, uint32_t desc_table, uint16_t index, virtio_mmio_desc_t *desc) {
    uint8_t raw[16];

    if (!virtio_mmio_guest_read(dev, desc_table + (uint32_t)index * 16u, raw, sizeof(raw))) {
        return false;
    }

    memcpy(&desc->address, &raw[0], sizeof(desc->address));
    memcpy(&desc->length, &raw[8], sizeof(desc->length));
    memcpy(&desc->flags, &raw[12], sizeof(desc->flags));
    memcpy(&desc->next, &raw[14], sizeof(desc->next));

    return true;
}

/**
 * Returns the descriptor table, available ring and used ring of one queue.
 */
static inline bool virtio_mmio_queue_layout(const virtio_mmio_t *dev, uint32_t queue_index, uint32_t *desc_table, uint32_t *avail, uint32_t *used) {
    if (queue_index >= dev->queue_count) {
        return false;
    }

    const virtio_mmio_queue_t *queue = &dev->queues[queue_index];

    if (queue->queue_pfn == 0 || queue->queue_num == 0) {
        return false;
    }

    uint32_t desc = queue->queue_pfn << 12;
    uint32_t avail_ring = desc + queue->queue_num * 16u;
    uint32_t align = queue->queue_align != 0 ? queue->queue_align : 4096u;
    uint32_t used_ring = (avail_ring + 4u + queue->queue_num * 2u + align - 1u) & ~(align - 1u);

    *desc_table = desc;
    *avail = avail_ring;
    *used = used_ring;
    return true;
}

/**
 * Adds one entry to the used ring and sets the used-ring interrupt.
 */
static inline bool virtio_mmio_add_used(virtio_mmio_t *dev, uint32_t queue_index, uint16_t head, uint32_t length) {
    uint32_t desc_table;
    uint32_t avail;
    uint32_t used;

    if (!virtio_mmio_queue_layout(dev, queue_index, &desc_table, &avail, &used)) {
        return false;
    }

    virtio_mmio_queue_t *queue = &dev->queues[queue_index];
    uint32_t used_entry = used + 4u + (uint32_t)(queue->used_idx % queue->queue_num) * 8u;
    uint32_t id = head;
    uint8_t entry[8];

    memcpy(&entry[0], &id, sizeof(id));
    memcpy(&entry[4], &length, sizeof(length));

    if (!virtio_mmio_guest_write(dev, used_entry, entry, sizeof(entry))) {
        return false;
    }

    queue->used_idx++;

    if (!virtio_mmio_guest_write(dev, used + 2u, &queue->used_idx, sizeof(queue->used_idx))) {
        return false;
    }

    dev->interrupt_status |= 1u;
    return true;
}

/**
 * Returns true when the device has a used-ring interrupt pending.
 */
static inline bool virtio_mmio_irq_pending(const virtio_mmio_t *dev) {
    return (dev->interrupt_status & 1u) != 0;
}

/**
 * Reads a virtio-mmio register or the config space.
 */
static inline bool virtio_mmio_read(virtio_mmio_t *dev, uint32_t address, void *destination, uint8_t length) {
    if (!virtio_mmio_contains(dev, address)) {
        return false;
    }

    uint32_t offset = address - dev->base;

    if (offset >= VIRTIO_MMIO_CONFIG_OFFSET) {
        uint32_t config_offset = offset - VIRTIO_MMIO_CONFIG_OFFSET;
        uint32_t copy = 0;

        if (dev->config != NULL && config_offset < dev->config_size) {
            copy = dev->config_size - config_offset;
            if (copy > length) {
                copy = length;
            }
            memcpy(destination, &dev->config[config_offset], copy);
        }

        if (copy < length) {
            memset((uint8_t *)destination + copy, 0, length - copy);
        }

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
            value = dev->device_id;
            break;
        case VIRTIO_MMIO_VENDOR_ID_OFFSET:
            value = dev->vendor_id;
            break;
        case VIRTIO_MMIO_HOST_FEATURES_OFFSET:
            value = dev->host_features;
            break;
        case VIRTIO_MMIO_QUEUE_NUM_MAX_OFFSET:
            value = dev->queue_sel < dev->queue_count ? VIRTIO_MMIO_QUEUE_NUM_MAX : 0;
            break;
        case VIRTIO_MMIO_QUEUE_PFN_OFFSET:
            value = dev->queue_sel < dev->queue_count ? dev->queues[dev->queue_sel].queue_pfn : 0;
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

    uint8_t bytes[8] = {0};
    memcpy(bytes, &value, sizeof(value));

    uint32_t copy = length < sizeof(bytes) ? length : (uint32_t)sizeof(bytes);
    memcpy(destination, bytes, copy);
    if (copy < length) {
        memset((uint8_t *)destination + copy, 0, length - copy);
    }

    return true;
}

/**
 * Writes a virtio-mmio register and reports a queue notification.
 */
static inline bool virtio_mmio_write(virtio_mmio_t *dev, uint32_t address, const void *source, uint8_t length, uint32_t *notify_queue) {
    if (!virtio_mmio_contains(dev, address)) {
        return false;
    }

    *notify_queue = UINT32_MAX;

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
            if (dev->queue_sel < dev->queue_count) {
                dev->queues[dev->queue_sel].queue_num = value;
            }
            break;
        case VIRTIO_MMIO_QUEUE_ALIGN_OFFSET:
            if (dev->queue_sel < dev->queue_count) {
                dev->queues[dev->queue_sel].queue_align = value;
            }
            break;
        case VIRTIO_MMIO_QUEUE_PFN_OFFSET:
            if (dev->queue_sel < dev->queue_count) {
                dev->queues[dev->queue_sel].queue_pfn = value;
            }
            break;
        case VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET:
            if (value < dev->queue_count) {
                *notify_queue = value;
            }
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
