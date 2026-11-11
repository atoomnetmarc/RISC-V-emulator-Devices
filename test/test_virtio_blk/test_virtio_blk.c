/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <string.h>
#include <unity.h>

#define VIRTIO_BASE 0x10001000
#define VIRTIO_SIZE 0x1000

#define GUEST_BASE 0x80000000
#define GUEST_SIZE 0x10000

#define QUEUE_NUM  8
#define DESC_TABLE 0x80001000
#define AVAIL      (DESC_TABLE + QUEUE_NUM * 16)
#define USED       0x80002000

#define HEADER_ADDR 0x80003000
#define DATA_ADDR   0x80004000
#define STATUS_ADDR 0x80005000

static uint8_t guest[GUEST_SIZE];
static uint8_t disk[4096];
static uint32_t disk_read_count;
static uint32_t disk_write_count;
static uint32_t disk_flush_count;

// Reads sectors from the test disk.
static bool test_disk_read(uint64_t sector, void *buffer, uint32_t count) {
    disk_read_count++;
    memcpy(buffer, &disk[sector * 512u], (size_t)count * 512u);
    return true;
}

// Writes sectors to the test disk.
static bool test_disk_write(uint64_t sector, const void *buffer, uint32_t count) {
    disk_write_count++;
    memcpy(&disk[sector * 512u], buffer, (size_t)count * 512u);
    return true;
}

// Flushes the test disk.
static bool test_disk_flush(void) {
    disk_flush_count++;
    return true;
}

#define RVE_DEV_VIRTIO_BLK_READ(sector, buffer, count)  test_disk_read(sector, buffer, count)
#define RVE_DEV_VIRTIO_BLK_WRITE(sector, buffer, count) test_disk_write(sector, buffer, count)
#define RVE_DEV_VIRTIO_BLK_FLUSH()                      test_disk_flush()

#include <RiscvEmulatorDevices.h>

static virtio_blk_t dev;

// Writes bytes to guest RAM.
static void guest_write(uint32_t address, const void *source, uint32_t length) {
    memcpy(&guest[address - GUEST_BASE], source, length);
}

// Reads bytes from guest RAM.
static void guest_read(uint32_t address, void *destination, uint32_t length) {
    memcpy(destination, &guest[address - GUEST_BASE], length);
}

// Writes one descriptor to the descriptor table.
static void set_desc(uint16_t index, uint64_t address, uint32_t length, uint16_t flags, uint16_t next) {
    uint8_t raw[16];

    memcpy(&raw[0], &address, sizeof(address));
    memcpy(&raw[8], &length, sizeof(length));
    memcpy(&raw[12], &flags, sizeof(flags));
    memcpy(&raw[14], &next, sizeof(next));

    guest_write(DESC_TABLE + (uint32_t)index * 16u, raw, sizeof(raw));
}

// Adds one descriptor head to the available ring.
static void add_avail(uint16_t head) {
    uint16_t idx;
    guest_read(AVAIL + 2u, &idx, sizeof(idx));

    uint16_t ring_index = (uint16_t)(idx % QUEUE_NUM);
    guest_write(AVAIL + 4u + (uint32_t)ring_index * 2u, &head, sizeof(head));

    idx++;
    guest_write(AVAIL + 2u, &idx, sizeof(idx));
}

// Returns the used-ring index.
static uint16_t get_used_idx(void) {
    uint16_t idx;
    guest_read(USED + 2u, &idx, sizeof(idx));
    return idx;
}

// Reads one used-ring entry.
static void get_used_entry(uint16_t index, uint32_t *id, uint32_t *length) {
    guest_read(USED + 4u + (uint32_t)index * 8u, id, sizeof(*id));
    guest_read(USED + 4u + (uint32_t)index * 8u + 4u, length, sizeof(*length));
}

// Writes one 32-bit value to a virtio-mmio register.
static void write32(uint32_t offset, uint32_t value) {
    TEST_ASSERT_TRUE(virtio_blk_write(&dev, VIRTIO_BASE + offset, &value, sizeof(value)));
}

// Reads one 32-bit value from a virtio-mmio register.
static uint32_t read32(uint32_t offset) {
    uint32_t value = 0;
    TEST_ASSERT_TRUE(virtio_blk_read(&dev, VIRTIO_BASE + offset, &value, sizeof(value)));
    return value;
}

// Reads one byte from a virtio-mmio register or the config space.
static uint8_t read8(uint32_t offset) {
    uint8_t value = 0;
    TEST_ASSERT_TRUE(virtio_blk_read(&dev, VIRTIO_BASE + offset, &value, sizeof(value)));
    return value;
}

// Resets the device, the guest RAM and the test disk before each test.
void setUp(void) {
    memset(guest, 0, sizeof(guest));
    memset(disk, 0, sizeof(disk));
    disk_read_count = 0;
    disk_write_count = 0;
    disk_flush_count = 0;

    virtio_blk_init(&dev, VIRTIO_BASE, VIRTIO_SIZE, guest, GUEST_BASE, (uint32_t)sizeof(guest), 8);

    write32(VIRTIO_MMIO_QUEUE_SEL_OFFSET, 0);
    write32(VIRTIO_MMIO_QUEUE_NUM_OFFSET, QUEUE_NUM);
    write32(VIRTIO_MMIO_QUEUE_ALIGN_OFFSET, 4096);
    write32(VIRTIO_MMIO_QUEUE_PFN_OFFSET, DESC_TABLE >> 12);
}

// Runs after each test; nothing to clean up.
void tearDown(void) {
}

// The identity registers report the virtio-mmio block device.
void test_identity(void) {
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_MAGIC_VALUE, read32(VIRTIO_MMIO_MAGIC_VALUE_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_VERSION, read32(VIRTIO_MMIO_VERSION_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_DEVICE_ID_BLOCK, read32(VIRTIO_MMIO_DEVICE_ID_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(RVE_DEV_VIRTIO_BLK_VENDOR_ID, read32(VIRTIO_MMIO_VENDOR_ID_OFFSET));
}

// The host features advertise the block size and flush support.
void test_host_features(void) {
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_FLUSH, read32(VIRTIO_MMIO_HOST_FEATURES_OFFSET));
}

// The maximum queue size is reported.
void test_queue_num_max(void) {
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_BLK_QUEUE_NUM_MAX, read32(VIRTIO_MMIO_QUEUE_NUM_MAX_OFFSET));
}

// The queue page frame number reads back the written value.
void test_queue_pfn(void) {
    TEST_ASSERT_EQUAL_UINT32(DESC_TABLE >> 12, read32(VIRTIO_MMIO_QUEUE_PFN_OFFSET));
}

// The config space reports the capacity and the block size.
void test_config(void) {
    TEST_ASSERT_EQUAL_UINT32(8, read32(VIRTIO_MMIO_CONFIG_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(0, read32(VIRTIO_MMIO_CONFIG_OFFSET + 4));
    TEST_ASSERT_EQUAL_UINT32(512, read32(VIRTIO_MMIO_CONFIG_OFFSET + 20));
}

// The legacy driver reads the config space byte by byte.
void test_config_byte_reads(void) {
    TEST_ASSERT_EQUAL_UINT8(8, read8(VIRTIO_MMIO_CONFIG_OFFSET));
    TEST_ASSERT_EQUAL_UINT8(0, read8(VIRTIO_MMIO_CONFIG_OFFSET + 1));
    TEST_ASSERT_EQUAL_UINT8(0, read8(VIRTIO_MMIO_CONFIG_OFFSET + 4));
    TEST_ASSERT_EQUAL_UINT8(0, read8(VIRTIO_MMIO_CONFIG_OFFSET + 20));
    TEST_ASSERT_EQUAL_UINT8(2, read8(VIRTIO_MMIO_CONFIG_OFFSET + 21));
}

// A read request copies one sector from the host disk to guest RAM.
void test_read_request(void) {
    uint8_t header[16] = {0};
    uint32_t type = VIRTIO_BLK_T_IN;
    uint64_t sector = 0;

    memcpy(&header[0], &type, sizeof(type));
    memcpy(&header[8], &sector, sizeof(sector));
    guest_write(HEADER_ADDR, header, sizeof(header));

    for (uint32_t i = 0; i < 512; i++) {
        disk[i] = (uint8_t)i;
    }

    set_desc(0, HEADER_ADDR, 16, VIRTQ_DESC_F_NEXT, 1);
    set_desc(1, DATA_ADDR, 512, VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE, 2);
    set_desc(2, STATUS_ADDR, 1, VIRTQ_DESC_F_WRITE, 0);

    add_avail(0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    TEST_ASSERT_EQUAL_UINT32(1, disk_read_count);
    TEST_ASSERT_EQUAL_UINT16(1, get_used_idx());

    uint32_t id = 0;
    uint32_t length = 0;
    get_used_entry(0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(512, length);

    uint8_t data[512];
    guest_read(DATA_ADDR, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT8(0, data[0]);
    TEST_ASSERT_EQUAL_UINT8(255, data[255]);
    TEST_ASSERT_EQUAL_UINT8(VIRTIO_BLK_S_OK, guest[STATUS_ADDR - GUEST_BASE]);
    TEST_ASSERT_TRUE(virtio_blk_irq_pending(&dev));
}

// A write request copies one sector from guest RAM to the host disk.
void test_write_request(void) {
    uint8_t header[16] = {0};
    uint32_t type = VIRTIO_BLK_T_OUT;
    uint64_t sector = 1;

    memcpy(&header[0], &type, sizeof(type));
    memcpy(&header[8], &sector, sizeof(sector));
    guest_write(HEADER_ADDR, header, sizeof(header));

    uint8_t data[512];
    for (uint32_t i = 0; i < 512; i++) {
        data[i] = (uint8_t)(i ^ 0xAAu);
    }
    guest_write(DATA_ADDR, data, sizeof(data));

    set_desc(0, HEADER_ADDR, 16, VIRTQ_DESC_F_NEXT, 1);
    set_desc(1, DATA_ADDR, 512, VIRTQ_DESC_F_NEXT, 2);
    set_desc(2, STATUS_ADDR, 1, VIRTQ_DESC_F_WRITE, 0);

    add_avail(0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    TEST_ASSERT_EQUAL_UINT32(1, disk_write_count);
    TEST_ASSERT_EQUAL_UINT8(data[0], disk[512]);
    TEST_ASSERT_EQUAL_UINT8(data[511], disk[1023]);
    TEST_ASSERT_EQUAL_UINT8(VIRTIO_BLK_S_OK, guest[STATUS_ADDR - GUEST_BASE]);
}

// A flush request calls the host flush hook.
void test_flush_request(void) {
    uint8_t header[16] = {0};
    uint32_t type = VIRTIO_BLK_T_FLUSH;

    memcpy(&header[0], &type, sizeof(type));
    guest_write(HEADER_ADDR, header, sizeof(header));

    set_desc(0, HEADER_ADDR, 16, VIRTQ_DESC_F_NEXT, 1);
    set_desc(1, STATUS_ADDR, 1, VIRTQ_DESC_F_WRITE, 0);

    add_avail(0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    TEST_ASSERT_EQUAL_UINT32(1, disk_flush_count);
    TEST_ASSERT_EQUAL_UINT8(VIRTIO_BLK_S_OK, guest[STATUS_ADDR - GUEST_BASE]);
}

// A get-id request writes the device identifier.
void test_get_id_request(void) {
    uint8_t header[16] = {0};
    uint32_t type = VIRTIO_BLK_T_GET_ID;

    memcpy(&header[0], &type, sizeof(type));
    guest_write(HEADER_ADDR, header, sizeof(header));

    set_desc(0, HEADER_ADDR, 16, VIRTQ_DESC_F_NEXT, 1);
    set_desc(1, DATA_ADDR, 20, VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE, 2);
    set_desc(2, STATUS_ADDR, 1, VIRTQ_DESC_F_WRITE, 0);

    add_avail(0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    char identifier[20];
    guest_read(DATA_ADDR, identifier, sizeof(identifier));
    TEST_ASSERT_EQUAL_STRING_LEN("rv32i-virtio-blk", identifier, 16);
    TEST_ASSERT_EQUAL_UINT8(VIRTIO_BLK_S_OK, guest[STATUS_ADDR - GUEST_BASE]);
}

// An unsupported request reports the unsupported status.
void test_unsupported_request(void) {
    uint8_t header[16] = {0};
    uint32_t type = 99;

    memcpy(&header[0], &type, sizeof(type));
    guest_write(HEADER_ADDR, header, sizeof(header));

    set_desc(0, HEADER_ADDR, 16, VIRTQ_DESC_F_NEXT, 1);
    set_desc(1, STATUS_ADDR, 1, VIRTQ_DESC_F_WRITE, 0);

    add_avail(0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    TEST_ASSERT_EQUAL_UINT8(VIRTIO_BLK_S_UNSUPP, guest[STATUS_ADDR - GUEST_BASE]);
}

// The interrupt acknowledge clears the used-ring interrupt.
void test_interrupt_ack(void) {
    uint8_t header[16] = {0};
    uint32_t type = VIRTIO_BLK_T_FLUSH;

    memcpy(&header[0], &type, sizeof(type));
    guest_write(HEADER_ADDR, header, sizeof(header));

    set_desc(0, HEADER_ADDR, 16, VIRTQ_DESC_F_NEXT, 1);
    set_desc(1, STATUS_ADDR, 1, VIRTQ_DESC_F_WRITE, 0);

    add_avail(0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    TEST_ASSERT_TRUE(virtio_blk_irq_pending(&dev));
    write32(VIRTIO_MMIO_INTERRUPT_ACK_OFFSET, 1);
    TEST_ASSERT_FALSE(virtio_blk_irq_pending(&dev));
}

// An address outside the range is not claimed.
void test_outside_range(void) {
    uint32_t value = 0;
    TEST_ASSERT_FALSE(virtio_blk_read(&dev, VIRTIO_BASE + VIRTIO_SIZE, &value, sizeof(value)));
    TEST_ASSERT_FALSE(virtio_blk_write(&dev, VIRTIO_BASE + VIRTIO_SIZE, &value, sizeof(value)));
}

// Runs the virtio-blk unit tests.
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_identity);
    RUN_TEST(test_host_features);
    RUN_TEST(test_queue_num_max);
    RUN_TEST(test_queue_pfn);
    RUN_TEST(test_config);
    RUN_TEST(test_config_byte_reads);
    RUN_TEST(test_read_request);
    RUN_TEST(test_write_request);
    RUN_TEST(test_flush_request);
    RUN_TEST(test_get_id_request);
    RUN_TEST(test_unsupported_request);
    RUN_TEST(test_interrupt_ack);
    RUN_TEST(test_outside_range);
    return UNITY_END();
}
