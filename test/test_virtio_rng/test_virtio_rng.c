/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <string.h>
#include <unity.h>

#define VIRTIO_BASE 0x10004000
#define VIRTIO_SIZE 0x1000

#define GUEST_BASE 0x80000000
#define GUEST_SIZE 0x10000

#define QUEUE_NUM 8
#define DESC0     0x80001000
#define AVAIL0    (DESC0 + QUEUE_NUM * 16)
#define USED0     0x80002000

#define BUF_ADDR 0x80005000

static uint8_t guest[GUEST_SIZE];
static uint8_t entropy_byte;

// Fills a buffer with a known byte sequence.
static void test_rng_fill(void *buffer, uint32_t length) {
    uint8_t *bytes = buffer;

    for (uint32_t i = 0; i < length; i++) {
        bytes[i] = entropy_byte++;
    }
}

#define RVE_DEV_VIRTIO_RNG_FILL(buffer, length) test_rng_fill(buffer, length)

#include <RiscvEmulatorDevices.h>

static virtio_rng_t dev;

// Writes bytes to guest RAM.
static void guest_write(uint32_t address, const void *source, uint32_t length) {
    memcpy(&guest[address - GUEST_BASE], source, length);
}

// Reads bytes from guest RAM.
static void guest_read(uint32_t address, void *destination, uint32_t length) {
    memcpy(destination, &guest[address - GUEST_BASE], length);
}

// Writes one descriptor to a descriptor table.
static void set_desc(uint32_t table, uint16_t index, uint64_t address, uint32_t length, uint16_t flags, uint16_t next) {
    uint8_t raw[16];

    memcpy(&raw[0], &address, sizeof(address));
    memcpy(&raw[8], &length, sizeof(length));
    memcpy(&raw[12], &flags, sizeof(flags));
    memcpy(&raw[14], &next, sizeof(next));

    guest_write(table + (uint32_t)index * 16u, raw, sizeof(raw));
}

// Adds one descriptor head to an available ring.
static void add_avail(uint32_t avail, uint16_t head) {
    uint16_t idx;
    guest_read(avail + 2u, &idx, sizeof(idx));

    uint16_t ring_index = (uint16_t)(idx % QUEUE_NUM);
    guest_write(avail + 4u + (uint32_t)ring_index * 2u, &head, sizeof(head));

    idx++;
    guest_write(avail + 2u, &idx, sizeof(idx));
}

// Returns the used-ring index.
static uint16_t get_used_idx(uint32_t used) {
    uint16_t idx;
    guest_read(used + 2u, &idx, sizeof(idx));
    return idx;
}

// Reads one used-ring entry.
static void get_used_entry(uint32_t used, uint16_t index, uint32_t *id, uint32_t *length) {
    guest_read(used + 4u + (uint32_t)index * 8u, id, sizeof(*id));
    guest_read(used + 4u + (uint32_t)index * 8u + 4u, length, sizeof(*length));
}

// Writes one 32-bit value to a virtio-mmio register.
static void write32(uint32_t offset, uint32_t value) {
    TEST_ASSERT_TRUE(virtio_rng_write(&dev, VIRTIO_BASE + offset, &value, sizeof(value)));
}

// Reads one 32-bit value from a virtio-mmio register.
static uint32_t read32(uint32_t offset) {
    uint32_t value = 0;
    TEST_ASSERT_TRUE(virtio_rng_read(&dev, VIRTIO_BASE + offset, &value, sizeof(value)));
    return value;
}

// Resets the device and the guest RAM before each test.
void setUp(void) {
    memset(guest, 0, sizeof(guest));
    entropy_byte = 0;

    virtio_rng_init(&dev, VIRTIO_BASE, VIRTIO_SIZE, guest, GUEST_BASE, (uint32_t)sizeof(guest));

    write32(VIRTIO_MMIO_QUEUE_SEL_OFFSET, 0);
    write32(VIRTIO_MMIO_QUEUE_NUM_OFFSET, QUEUE_NUM);
    write32(VIRTIO_MMIO_QUEUE_ALIGN_OFFSET, 4096);
    write32(VIRTIO_MMIO_QUEUE_PFN_OFFSET, DESC0 >> 12);
}

// Runs after each test; nothing to clean up.
void tearDown(void) {
}

// The identity registers report the virtio-mmio rng device.
void test_identity(void) {
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_MAGIC_VALUE, read32(VIRTIO_MMIO_MAGIC_VALUE_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_VERSION, read32(VIRTIO_MMIO_VERSION_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_DEVICE_ID_RNG, read32(VIRTIO_MMIO_DEVICE_ID_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(0, read32(VIRTIO_MMIO_VENDOR_ID_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(0, read32(VIRTIO_MMIO_HOST_FEATURES_OFFSET));
}

// Only queue 0 exists and it reports the maximum queue size.
void test_queue_num_max(void) {
    write32(VIRTIO_MMIO_QUEUE_SEL_OFFSET, 0);
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_QUEUE_NUM_MAX, read32(VIRTIO_MMIO_QUEUE_NUM_MAX_OFFSET));

    write32(VIRTIO_MMIO_QUEUE_SEL_OFFSET, 1);
    TEST_ASSERT_EQUAL_UINT32(0, read32(VIRTIO_MMIO_QUEUE_NUM_MAX_OFFSET));
}

// The device has no config space.
void test_config(void) {
    uint8_t config[8];
    TEST_ASSERT_TRUE(virtio_rng_read(&dev, VIRTIO_BASE + VIRTIO_MMIO_CONFIG_OFFSET, config, sizeof(config)));

    for (uint32_t i = 0; i < sizeof(config); i++) {
        TEST_ASSERT_EQUAL_UINT8(0, config[i]);
    }
}

// One writable descriptor is filled and returned to the used ring.
void test_single(void) {
    set_desc(DESC0, 0, BUF_ADDR, 5, VIRTQ_DESC_F_WRITE, 0);
    add_avail(AVAIL0, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    uint8_t data[5];
    guest_read(BUF_ADDR, data, sizeof(data));
    const uint8_t expected[5] = {0, 1, 2, 3, 4};
    TEST_ASSERT_EQUAL_MEMORY(expected, data, sizeof(data));

    TEST_ASSERT_EQUAL_UINT16(1, get_used_idx(USED0));

    uint32_t id = 0;
    uint32_t length = 0;
    get_used_entry(USED0, 0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(5, length);
    TEST_ASSERT_TRUE(virtio_rng_irq_pending(&dev));
}

// A descriptor chain is filled across both descriptors.
void test_chain(void) {
    set_desc(DESC0, 0, BUF_ADDR, 3, VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT, 1);
    set_desc(DESC0, 1, BUF_ADDR + 3, 4, VIRTQ_DESC_F_WRITE, 0);
    add_avail(AVAIL0, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    uint8_t data[7];
    guest_read(BUF_ADDR, data, sizeof(data));
    const uint8_t expected[7] = {0, 1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_MEMORY(expected, data, sizeof(data));

    uint32_t id = 0;
    uint32_t length = 0;
    get_used_entry(USED0, 0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(7, length);
}

// A buffer larger than the host entropy buffer is filled in chunks.
void test_large_buffer(void) {
    uint32_t length = VIRTIO_RNG_BUFFER_SIZE + 8;
    set_desc(DESC0, 0, BUF_ADDR, length, VIRTQ_DESC_F_WRITE, 0);
    add_avail(AVAIL0, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    uint8_t data[VIRTIO_RNG_BUFFER_SIZE + 8];
    guest_read(BUF_ADDR, data, length);

    for (uint32_t i = 0; i < length; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, data[i]);
    }

    uint32_t id = 0;
    uint32_t used_length = 0;
    get_used_entry(USED0, 0, &id, &used_length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(length, used_length);
}

// A device-readable descriptor returns an empty used entry.
void test_readable_descriptor(void) {
    set_desc(DESC0, 0, BUF_ADDR, 5, 0, 0);
    add_avail(AVAIL0, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    uint8_t data[5];
    guest_read(BUF_ADDR, data, sizeof(data));
    const uint8_t expected[5] = {0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_MEMORY(expected, data, sizeof(data));

    uint32_t id = 0;
    uint32_t length = 1;
    get_used_entry(USED0, 0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(0, length);
}

// Addresses outside the device range are not mapped.
void test_address_range(void) {
    uint32_t value = 0;
    TEST_ASSERT_FALSE(virtio_rng_read(&dev, VIRTIO_BASE - 4, &value, sizeof(value)));
    TEST_ASSERT_FALSE(virtio_rng_read(&dev, VIRTIO_BASE + VIRTIO_SIZE, &value, sizeof(value)));
    TEST_ASSERT_FALSE(virtio_rng_write(&dev, VIRTIO_BASE - 4, &value, sizeof(value)));
    TEST_ASSERT_FALSE(virtio_rng_write(&dev, VIRTIO_BASE + VIRTIO_SIZE, &value, sizeof(value)));
}

// Runs the virtio-rng unit tests.
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_identity);
    RUN_TEST(test_queue_num_max);
    RUN_TEST(test_config);
    RUN_TEST(test_single);
    RUN_TEST(test_chain);
    RUN_TEST(test_large_buffer);
    RUN_TEST(test_readable_descriptor);
    RUN_TEST(test_address_range);
    return UNITY_END();
}
