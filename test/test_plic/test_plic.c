/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <unity.h>

#include <RiscvEmulatorDevices.h>

#define PLIC_BASE 0x0C000000
#define PLIC_SIZE 0x400000

static plic_t plic;

// Resets the PLIC instance before each test.
void setUp(void) {
    plic_init(&plic, PLIC_BASE, PLIC_SIZE);
}

// Runs after each test; nothing to clean up.
void tearDown(void) {
}

// Writes one 32-bit value to a PLIC register.
static void write32(uint32_t offset, uint32_t value) {
    TEST_ASSERT_TRUE(plic_write(&plic, PLIC_BASE + offset, &value, sizeof(value)));
}

// Reads one 32-bit value from a PLIC register.
static uint32_t read32(uint32_t offset) {
    uint32_t value = 0;
    TEST_ASSERT_TRUE(plic_read(&plic, PLIC_BASE + offset, &value, sizeof(value)));
    return value;
}

// Priority registers read back the written value.
void test_priority(void) {
    write32(PLIC_PRIORITY_OFFSET + 1 * sizeof(uint32_t), 7);
    TEST_ASSERT_EQUAL_UINT32(7, read32(PLIC_PRIORITY_OFFSET + 1 * sizeof(uint32_t)));
}

// The pending bit follows plic_set_pending.
void test_pending(void) {
    plic_set_pending(&plic, 1, true);
    TEST_ASSERT_EQUAL_UINT32(1u << 1, read32(PLIC_PENDING_OFFSET));
    plic_set_pending(&plic, 1, false);
    TEST_ASSERT_EQUAL_UINT32(0, read32(PLIC_PENDING_OFFSET));
}

// Enable registers read back the written value.
void test_enable(void) {
    write32(PLIC_ENABLE_OFFSET, 0x2);
    TEST_ASSERT_EQUAL_UINT32(0x2, read32(PLIC_ENABLE_OFFSET));
}

// Threshold registers read back the written value.
void test_threshold(void) {
    write32(PLIC_CONTEXT_OFFSET + PLIC_THRESHOLD_OFFSET, 3);
    TEST_ASSERT_EQUAL_UINT32(3, read32(PLIC_CONTEXT_OFFSET + PLIC_THRESHOLD_OFFSET));
}

// irq_pending requires an enabled pending source above the threshold.
void test_irq_pending(void) {
    write32(PLIC_PRIORITY_OFFSET + 1 * sizeof(uint32_t), 1);
    write32(PLIC_ENABLE_OFFSET, 0x2);
    write32(PLIC_CONTEXT_OFFSET + PLIC_THRESHOLD_OFFSET, 0);
    TEST_ASSERT_FALSE(plic_irq_pending(&plic));

    plic_set_pending(&plic, 1, true);
    TEST_ASSERT_TRUE(plic_irq_pending(&plic));

    write32(PLIC_CONTEXT_OFFSET + PLIC_THRESHOLD_OFFSET, 1);
    TEST_ASSERT_FALSE(plic_irq_pending(&plic));
}

// A claim returns the highest-priority source and clears its pending bit.
void test_claim(void) {
    write32(PLIC_PRIORITY_OFFSET + 1 * sizeof(uint32_t), 1);
    write32(PLIC_PRIORITY_OFFSET + 2 * sizeof(uint32_t), 2);
    write32(PLIC_ENABLE_OFFSET, 0x6);
    write32(PLIC_CONTEXT_OFFSET + PLIC_THRESHOLD_OFFSET, 0);
    plic_set_pending(&plic, 1, true);
    plic_set_pending(&plic, 2, true);

    TEST_ASSERT_EQUAL_UINT32(2, read32(PLIC_CONTEXT_OFFSET + PLIC_CLAIM_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(0x2, read32(PLIC_PENDING_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(1, read32(PLIC_CONTEXT_OFFSET + PLIC_CLAIM_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(0, read32(PLIC_PENDING_OFFSET));
}

// A complete write clears the pending bit of the source.
void test_complete(void) {
    plic_set_pending(&plic, 1, true);
    write32(PLIC_CONTEXT_OFFSET + PLIC_CLAIM_OFFSET, 1);
    TEST_ASSERT_EQUAL_UINT32(0, read32(PLIC_PENDING_OFFSET));
}

// An address outside the range is not claimed.
void test_outside_range(void) {
    uint32_t value = 0;
    TEST_ASSERT_FALSE(plic_read(&plic, PLIC_BASE + PLIC_SIZE, &value, sizeof(value)));
    TEST_ASSERT_FALSE(plic_write(&plic, PLIC_BASE + PLIC_SIZE, &value, sizeof(value)));
}

// An unimplemented register reads as zero.
void test_unimplemented_reads_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, read32(0x100));
}

// Runs the PLIC unit tests.
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_priority);
    RUN_TEST(test_pending);
    RUN_TEST(test_enable);
    RUN_TEST(test_threshold);
    RUN_TEST(test_irq_pending);
    RUN_TEST(test_claim);
    RUN_TEST(test_complete);
    RUN_TEST(test_outside_range);
    RUN_TEST(test_unimplemented_reads_zero);
    return UNITY_END();
}
