/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <string.h>
#include <unity.h>

#define VIRTIO_BASE 0x10003000
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

#define TX_ADDR 0x80005000
#define RX_ADDR 0x80006000

static const uint8_t test_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

static uint8_t guest[GUEST_SIZE];
static uint8_t tx_buffer[2048];
static uint32_t tx_length;

// Copies network output into the test buffer.
static void test_net_tx(const void *buffer, uint32_t length) {
    if (length > sizeof(tx_buffer)) {
        length = (uint32_t)sizeof(tx_buffer);
    }

    memcpy(tx_buffer, buffer, length);
    tx_length = length;
}

#define RVE_DEV_VIRTIO_NET_TX(buffer, length) test_net_tx(buffer, length)

#include <RiscvEmulatorDevices.h>

static virtio_net_t dev;

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
    TEST_ASSERT_TRUE(virtio_net_write(&dev, VIRTIO_BASE + offset, &value, sizeof(value)));
}

// Reads one 32-bit value from a virtio-mmio register.
static uint32_t read32(uint32_t offset) {
    uint32_t value = 0;
    TEST_ASSERT_TRUE(virtio_net_read(&dev, VIRTIO_BASE + offset, &value, sizeof(value)));
    return value;
}

// Reads bytes from the config space.
static void read_config(uint32_t offset, void *destination, uint8_t length) {
    TEST_ASSERT_TRUE(virtio_net_read(&dev, VIRTIO_BASE + VIRTIO_MMIO_CONFIG_OFFSET + offset, destination, length));
}

// Resets the device, the guest RAM and the test buffer before each test.
void setUp(void) {
    memset(guest, 0, sizeof(guest));
    memset(tx_buffer, 0, sizeof(tx_buffer));
    tx_length = 0;

    virtio_net_init(&dev, VIRTIO_BASE, VIRTIO_SIZE, guest, GUEST_BASE, (uint32_t)sizeof(guest), test_mac);

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

// The identity registers report the virtio-mmio net device.
void test_identity(void) {
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_MAGIC_VALUE, read32(VIRTIO_MMIO_MAGIC_VALUE_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_VERSION, read32(VIRTIO_MMIO_VERSION_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_MMIO_DEVICE_ID_NET, read32(VIRTIO_MMIO_DEVICE_ID_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(0, read32(VIRTIO_MMIO_VENDOR_ID_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS, read32(VIRTIO_MMIO_HOST_FEATURES_OFFSET));
}

// The config space reports the MAC and a link that is up.
void test_config(void) {
    uint8_t mac[6];
    read_config(0, mac, sizeof(mac));
    TEST_ASSERT_EQUAL_MEMORY(test_mac, mac, sizeof(mac));

    uint16_t status = 0;
    read_config(6, &status, sizeof(status));
    TEST_ASSERT_EQUAL_UINT16(VIRTIO_NET_S_LINK_UP, status);
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

// A single-descriptor transmit calls the host hook without the header.
void test_tx_single(void) {
    uint8_t frame[VIRTIO_NET_HDR_SIZE + 5] = {0};
    memcpy(&frame[VIRTIO_NET_HDR_SIZE], "hello", 5);

    guest_write(TX_ADDR, frame, sizeof(frame));
    set_desc(DESC1, 0, TX_ADDR, sizeof(frame), 0, 0);
    add_avail(AVAIL1, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 1);

    TEST_ASSERT_EQUAL_UINT32(5, tx_length);
    TEST_ASSERT_EQUAL_MEMORY("hello", tx_buffer, 5);
    TEST_ASSERT_EQUAL_UINT16(1, get_used_idx(USED1));

    uint32_t id = 0;
    uint32_t length = 0;
    get_used_entry(USED1, 0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(0, length);
    TEST_ASSERT_TRUE(virtio_net_irq_pending(&dev));
}

// A multi-descriptor transmit gathers the frame across the chain.
void test_tx_multi(void) {
    uint8_t header[VIRTIO_NET_HDR_SIZE] = {0};
    const char *first = "abc";
    const char *second = "de";

    guest_write(TX_ADDR, header, sizeof(header));
    guest_write(TX_ADDR + VIRTIO_NET_HDR_SIZE, first, 3);
    guest_write(TX_ADDR + VIRTIO_NET_HDR_SIZE + 3, second, 2);

    set_desc(DESC1, 0, TX_ADDR, VIRTIO_NET_HDR_SIZE + 3, VIRTQ_DESC_F_NEXT, 1);
    set_desc(DESC1, 1, TX_ADDR + VIRTIO_NET_HDR_SIZE + 3, 2, 0, 0);
    add_avail(AVAIL1, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 1);

    TEST_ASSERT_EQUAL_UINT32(5, tx_length);
    TEST_ASSERT_EQUAL_MEMORY("abcde", tx_buffer, 5);
    TEST_ASSERT_EQUAL_UINT16(1, get_used_idx(USED1));
}

// A receive push writes the header and the frame into the next buffer.
void test_rx_push(void) {
    const uint8_t packet[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    set_desc(DESC0, 0, RX_ADDR, 64, VIRTQ_DESC_F_WRITE, 0);
    add_avail(AVAIL0, 0);

    TEST_ASSERT_TRUE(virtio_net_rx_push(&dev, packet, sizeof(packet)));
    TEST_ASSERT_EQUAL_UINT16(1, get_used_idx(USED0));

    uint32_t id = 0;
    uint32_t length = 0;
    get_used_entry(USED0, 0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_NET_HDR_SIZE + sizeof(packet), length);

    uint8_t received[VIRTIO_NET_HDR_SIZE + sizeof(packet)];
    guest_read(RX_ADDR, received, sizeof(received));
    TEST_ASSERT_EQUAL_UINT8(0, received[0]);
    TEST_ASSERT_EQUAL_MEMORY(packet, &received[VIRTIO_NET_HDR_SIZE], sizeof(packet));
    TEST_ASSERT_TRUE(virtio_net_irq_pending(&dev));
}

// A receive push without a buffer drops the packet.
void test_rx_no_buffer(void) {
    const uint8_t packet[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    TEST_ASSERT_FALSE(virtio_net_rx_push(&dev, packet, sizeof(packet)));
    TEST_ASSERT_EQUAL_UINT16(0, get_used_idx(USED0));
}

// A receive push with a small buffer returns the buffer with length zero.
void test_rx_small_buffer(void) {
    const uint8_t packet[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    set_desc(DESC0, 0, RX_ADDR, VIRTIO_NET_HDR_SIZE + sizeof(packet) - 1u, VIRTQ_DESC_F_WRITE, 0);
    add_avail(AVAIL0, 0);

    TEST_ASSERT_FALSE(virtio_net_rx_push(&dev, packet, sizeof(packet)));
    TEST_ASSERT_EQUAL_UINT16(1, get_used_idx(USED0));

    uint32_t id = 0;
    uint32_t length = 0;
    get_used_entry(USED0, 0, &id, &length);
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_UINT32(0, length);
}

// An interrupt acknowledge clears the pending bit.
void test_interrupt_ack(void) {
    uint8_t frame[VIRTIO_NET_HDR_SIZE + 1] = {0};

    guest_write(TX_ADDR, frame, sizeof(frame));
    set_desc(DESC1, 0, TX_ADDR, sizeof(frame), 0, 0);
    add_avail(AVAIL1, 0);
    write32(VIRTIO_MMIO_QUEUE_NOTIFY_OFFSET, 1);

    TEST_ASSERT_TRUE(virtio_net_irq_pending(&dev));

    write32(VIRTIO_MMIO_INTERRUPT_ACK_OFFSET, 1);
    TEST_ASSERT_FALSE(virtio_net_irq_pending(&dev));
}

// An address outside the range is not claimed.
void test_outside_range(void) {
    uint32_t value = 0;
    TEST_ASSERT_FALSE(virtio_net_read(&dev, VIRTIO_BASE + VIRTIO_SIZE, &value, sizeof(value)));
    TEST_ASSERT_FALSE(virtio_net_write(&dev, VIRTIO_BASE + VIRTIO_SIZE, &value, sizeof(value)));
}

// Runs the virtio-net unit tests.
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_identity);
    RUN_TEST(test_config);
    RUN_TEST(test_queue_num_max);
    RUN_TEST(test_queue_pfn);
    RUN_TEST(test_tx_single);
    RUN_TEST(test_tx_multi);
    RUN_TEST(test_rx_push);
    RUN_TEST(test_rx_no_buffer);
    RUN_TEST(test_rx_small_buffer);
    RUN_TEST(test_interrupt_ack);
    RUN_TEST(test_outside_range);
    return UNITY_END();
}
