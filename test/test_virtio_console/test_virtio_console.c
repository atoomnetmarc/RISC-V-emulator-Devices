/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <string.h>
#include <unity.h>

#define VIRTIO_BASE 0x10002000
#define VIRTIO_SIZE 0x1000

#define GUEST_BASE 0x80000000
#define GUEST_SIZE 0x10000

#define QUEUE_NUM 8
#define DESC0     0x80001000
#define AVAIL0    (DESC0 + QUEUE_NUM * 16)
#define USED0     0x80002000
#define DESC1     0x80003000
#define AVAIL1    (DESC1 + QUEUE_NUM * 16)
#define USED1     0x80004000

#define TX0_ADDR 0x80005000
#define TX1_ADDR 0x80006000
#define RX_ADDR  0x80007000

static uint8_t guest[GUEST_SIZE];
static char tx_buffer[64];
static uint32_t tx_length;

// Copies console output into the test buffer.
static void test_console_tx(const void *buffer, uint32_t length) {
    if (tx_length + length > sizeof(tx_buffer)) {
        length = (uint32_t)sizeof(tx_buffer) - tx_length;
    }

    memcpy(&tx_buffer[tx_length], buffer, length);
    tx_length += length;
}

#define RVE_DEV_VIRTIO_CONSOLE_TX(buffer, length) test_console_tx(buffer, length)

#include <RiscvEmulatorDevices.h>

static virtio_console_t dev;

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
    TEST_ASSERT_TRUE(virtio_console_write(&dev, VIRTIO_BASE + offset, &value, sizeof(value)));
}

// Reads one 32-bit value from a virtio-mmio register.
static uint32_t read32(uint32_t offset) {
    uint32_t value = 0;
    TEST_ASSERT_TRUE(virtio_console_read(&dev, VIRTIO_BASE + offset, &value, sizeof(value)));
    return value;
}

// Resets the device, the guest RAM and the test buffer before each test.
void setUp(void) {
    memset(guest, 0, sizeof(guest));
    memset(tx_buffer, 0, sizeof(tx_buffer));
    tx_length = 0;

    virtio_console_init(&dev, VIRTIO_BASE, VIRTIO_SIZE, guest, GUEST_BASE, (uint32_t)sizeof(guest));

    write32(VIRTIO_MMIO_QUEUE_SEL_OFFSET, 0);
    write32(VIRTIO_MMIO_QUEUE_NUM_OFFSET, QUEUE_NUM);
    write32(VIRTIO_MMIO_QUEUE_ALIGN_OFFSET, 4096);
    write32(VIRTIO_MMIO_QUEUE_PFN_OFFSET, DESC0 >> 12);

    write32(VIRTIO_MMIO_QUEUE_SEL_OFFSET, 1);
    write32(VIRTIO_MMIO_QUEUE_NUM_OFFSET, QUEUE_NUM);
    write32(VIRTIO_MMIO_QUEUE_ALIGN_OFFSET, 4096);
    write32(VIRTIO_MMIO_QUEUE_PFN_OFFSET, DESC1 >> 12);
}

// Runs after each test; nothing to clean up.
void tearDown(void) {
}

// The identity registers report the virtio-mmio console device.
void test_identity(void) {
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_MAGIC_VALUE, read32(VIRTIO_MMIO_MAGIC_VALUE_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_VERSION, read32(VIRTIO_MMIO_VERSION_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_DEVICE_ID_CONSOLE, read32(VIRTIO_MMIO_DEVICE_ID_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(0, read32(VIRTIO_MMIO_VENDOR_ID_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(0, read32(VIRTIO_MMIO_HOST_FEATURES_OFFSET));
}

// The maximum queue size is reported for both queues.
void test_queue_num_max(void) {
    write32(VIRTIO_MMIO_QUEUE_SEL_OFFSET, 0);
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_QUEUE_NUM_MAX, read32(VIRTIO_MMIO_QUEUE_NUM_MAX_OFFSET));

    write32(VIRTIO_MMIO_QUEUE_SEL_OFFSET, 1);
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_QUEUE_NUM_MAX, read32(VIRTIO_MMIO_QUEUE_NUM_MAX_OFFSET));
}

// Each queue keeps its own page frame number.
void test_queue_pfn(void) {
    write32(VIRTIO_MMIO_QUEUE_SEL_OFFSET, 0);
    TEST_ASSERT_EQUAL_UINT32(DESC0 >> 12, read32(VIRTIO_MMIO_QUEUE_PFN_OFFSET));

    write32(VIRTIO_MMIO_QUEUE_SEL_OFFSET, 1);
    TEST_ASSERT_EQUAL_UINT32(DESC1 >> 12, read32(VIRTIO_MMIO_QUEUE_PFN_OFFSET));
}

// The config space reads as zero because the console offers no features.
void test_config_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, read32(VIRTIO_MMIO_CONFIG_OFFSET));
}

// An output chain calls the host hook for every descriptor.
void test_output(void) {
    const char *first = "hello";
    const char *second = " world";

    guest_write(TX0_ADDR, first, 5);
    guest_write(TX1_ADDR, second, 6);

    set_desc(DESC1, 0, TX0_ADDR, 5, VIRTQ_DESC_F_NEXT, 1);
    set_desc(DESC1, 1, TX1_ADDR, 6, 0, 0);

    add_avail(AVAIL1, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 1);

    TEST_ASSERT_EQUAL_UINT32(11, tx_length);
    TEST_ASSERT_EQUAL_MEMORY("hello world", tx_buffer, 11);
    TEST_ASSERT_EQUAL_UINT16(1, get_used_idx(USED1));

    uint32_t id = 0;
    uint32_t length = 0;
    get_used_entry(USED1, 0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(11, length);
    TEST_ASSERT_TRUE(virtio_console_irq_pending(&dev));
}

// The RX FIFO drains into a buffer when the guest adds one.
void test_input(void) {
    virtio_console_rx_push(&dev, 'a');
    virtio_console_rx_push(&dev, 'b');
    virtio_console_rx_push(&dev, 'c');

    set_desc(DESC0, 0, RX_ADDR, 16, VIRTQ_DESC_F_WRITE, 0);
    add_avail(AVAIL0, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    TEST_ASSERT_EQUAL_UINT16(1, get_used_idx(USED0));

    uint32_t id = 0;
    uint32_t length = 0;
    get_used_entry(USED0, 0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(3, length);

    uint8_t data[3];
    guest_read(RX_ADDR, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT8('a', data[0]);
    TEST_ASSERT_EQUAL_UINT8('b', data[1]);
    TEST_ASSERT_EQUAL_UINT8('c', data[2]);
    TEST_ASSERT_TRUE(virtio_console_irq_pending(&dev));
}

// A push drains immediately when a buffer is already available.
void test_input_immediate(void) {
    set_desc(DESC0, 0, RX_ADDR, 16, VIRTQ_DESC_F_WRITE, 0);
    add_avail(AVAIL0, 0);

    virtio_console_rx_push(&dev, 'x');

    TEST_ASSERT_EQUAL_UINT16(1, get_used_idx(USED0));

    uint32_t id = 0;
    uint32_t length = 0;
    get_used_entry(USED0, 0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(1, length);

    uint8_t data = 0;
    guest_read(RX_ADDR, &data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT8('x', data);
}

// A full RX FIFO drops new bytes and sets the overrun flag.
void test_input_overrun(void) {
    for (uint32_t i = 0; i < VIRTIO_CONSOLE_RX_FIFO_SIZE + 4u; i++) {
        virtio_console_rx_push(&dev, (uint8_t)('A' + i));
    }

    TEST_ASSERT_TRUE(dev.rx_overrun);

    set_desc(DESC0, 0, RX_ADDR, 32, VIRTQ_DESC_F_WRITE, 0);
    add_avail(AVAIL0, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 0);

    TEST_ASSERT_EQUAL_UINT16(1, get_used_idx(USED0));

    uint32_t id = 0;
    uint32_t length = 0;
    get_used_entry(USED0, 0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_CONSOLE_RX_FIFO_SIZE, length);

    uint8_t data[VIRTIO_CONSOLE_RX_FIFO_SIZE];
    guest_read(RX_ADDR, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT8('A', data[0]);
    TEST_ASSERT_EQUAL_UINT8('A' + VIRTIO_CONSOLE_RX_FIFO_SIZE - 1, data[VIRTIO_CONSOLE_RX_FIFO_SIZE - 1]);
}

// An interrupt acknowledge clears the pending bit.
void test_interrupt_ack(void) {
    const char *text = "x";

    guest_write(TX0_ADDR, text, 1);
    set_desc(DESC1, 0, TX0_ADDR, 1, 0, 0);
    add_avail(AVAIL1, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 1);

    TEST_ASSERT_TRUE(virtio_console_irq_pending(&dev));

    write32(VIRTIO_MMIO_INTERRUPT_ACK_OFFSET, 1);
    TEST_ASSERT_FALSE(virtio_console_irq_pending(&dev));
}

// An address outside the range is not claimed.
void test_outside_range(void) {
    uint32_t value = 0;
    TEST_ASSERT_FALSE(virtio_console_read(&dev, VIRTIO_BASE + VIRTIO_SIZE, &value, sizeof(value)));
    TEST_ASSERT_FALSE(virtio_console_write(&dev, VIRTIO_BASE + VIRTIO_SIZE, &value, sizeof(value)));
}

// Runs the virtio-console unit tests.
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_identity);
    RUN_TEST(test_queue_num_max);
    RUN_TEST(test_queue_pfn);
    RUN_TEST(test_config_zero);
    RUN_TEST(test_output);
    RUN_TEST(test_input);
    RUN_TEST(test_input_immediate);
    RUN_TEST(test_input_overrun);
    RUN_TEST(test_interrupt_ack);
    RUN_TEST(test_outside_range);
    return UNITY_END();
}
