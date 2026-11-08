/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <unity.h>

#include <RiscvEmulatorDevices.h>

#define CLINT_BASE 0x02000000
#define CLINT_SIZE 0x10000

static clint_t clint;

// Resets the CLINT instance before each test.
void setUp(void) {
    clint_init(&clint, CLINT_BASE, CLINT_SIZE);
}

// Runs after each test; nothing to clean up.
void tearDown(void) {
}

// Writes one 32-bit value to a CLINT register.
static void write32(uint32_t offset, uint32_t value) {
    TEST_ASSERT_TRUE(clint_write(&clint, CLINT_BASE + offset, &value, sizeof(value)));
}

// Reads one 32-bit value from a CLINT register.
static uint32_t read32(uint32_t offset) {
    uint32_t value = 0;
    TEST_ASSERT_TRUE(clint_read(&clint, CLINT_BASE + offset, &value, sizeof(value)));
    return value;
}

// Writes one 64-bit value to a CLINT register.
static void write64(uint32_t offset, uint64_t value) {
    TEST_ASSERT_TRUE(clint_write(&clint, CLINT_BASE + offset, &value, sizeof(value)));
}

// Reads one 64-bit value from a CLINT register.
static uint64_t read64(uint32_t offset) {
    uint64_t value = 0;
    TEST_ASSERT_TRUE(clint_read(&clint, CLINT_BASE + offset, &value, sizeof(value)));
    return value;
}

// A non-zero msip makes the software interrupt pending; zero clears it.
void test_msip_set_clear(void) {
    write32(CLINT_MSIP_OFFSET, 1);
    TEST_ASSERT_TRUE(clint_software_pending(&clint));
    write32(CLINT_MSIP_OFFSET, 0);
    TEST_ASSERT_FALSE(clint_software_pending(&clint));
}

// Any non-zero msip value makes the software interrupt pending.
void test_msip_nonzero(void) {
    write32(CLINT_MSIP_OFFSET, 0xFFFFFFFF);
    TEST_ASSERT_TRUE(clint_software_pending(&clint));
}

// The timer is pending when mtime reaches mtimecmp.
void test_mtimecmp_compare(void) {
    clint_tick(&clint, 100);
    write32(CLINT_MTIMECMP_OFFSET, 200);
    write32(CLINT_MTIMECMP_OFFSET + sizeof(uint32_t), 0);
    TEST_ASSERT_FALSE(clint_timer_pending(&clint));
    clint_tick(&clint, 200);
    TEST_ASSERT_TRUE(clint_timer_pending(&clint));
}

// mtimecmp resets above mtime, so the timer is not pending at reset.
void test_mtimecmp_reset_not_pending(void) {
    clint_tick(&clint, 0);
    TEST_ASSERT_FALSE(clint_timer_pending(&clint));
}

// A 64-bit write to mtimecmp reads back unchanged.
void test_mtimecmp_64bit_write(void) {
    write64(CLINT_MTIMECMP_OFFSET, UINT64_C(0x0000000100000002));
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x0000000100000002), read64(CLINT_MTIMECMP_OFFSET));
}

// The two 32-bit mtimecmp halves read back the written values.
void test_mtimecmp_halves(void) {
    write32(CLINT_MTIMECMP_OFFSET, 0xFFFFFFFF);
    write32(CLINT_MTIMECMP_OFFSET + sizeof(uint32_t), 0x00000001);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, read32(CLINT_MTIMECMP_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(0x00000001, read32(CLINT_MTIMECMP_OFFSET + sizeof(uint32_t)));
}

// The two 32-bit mtime halves read back the ticked value.
void test_mtime_read_halves(void) {
    clint_tick(&clint, UINT64_C(0x0000000200000001));
    TEST_ASSERT_EQUAL_UINT32(0x00000001, read32(CLINT_MTIME_OFFSET));
    TEST_ASSERT_EQUAL_UINT32(0x00000002, read32(CLINT_MTIME_OFFSET + sizeof(uint32_t)));
}

// A low-word read latches the high word until the high word is read.
void test_mtime_latch(void) {
    clint_tick(&clint, UINT64_C(0x0000000100000001));
    TEST_ASSERT_EQUAL_UINT32(0x00000001, read32(CLINT_MTIME_OFFSET));
    clint_tick(&clint, UINT64_C(0x0000000200000002));
    TEST_ASSERT_EQUAL_UINT32(0x00000001, read32(CLINT_MTIME_OFFSET + sizeof(uint32_t)));
    TEST_ASSERT_EQUAL_UINT32(0x00000002, read32(CLINT_MTIME_OFFSET + sizeof(uint32_t)));
}

// A high-word read without a preceding low-word read returns the current high word.
void test_mtime_high_first(void) {
    clint_tick(&clint, UINT64_C(0x0000000200000001));
    TEST_ASSERT_EQUAL_UINT32(0x00000002, read32(CLINT_MTIME_OFFSET + sizeof(uint32_t)));
}

// An 8-byte read returns the full mtime value.
void test_mtime_8byte_read(void) {
    clint_tick(&clint, UINT64_C(0x0000000200000001));
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x0000000200000001), read64(CLINT_MTIME_OFFSET));
}

// A 64-bit write to mtime reads back unchanged.
void test_mtime_64bit_write(void) {
    write64(CLINT_MTIME_OFFSET, UINT64_C(0x0000000300000004));
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x0000000300000004), read64(CLINT_MTIME_OFFSET));
}

// An address outside the range is not claimed.
void test_outside_range(void) {
    uint32_t value = 0;
    TEST_ASSERT_FALSE(clint_read(&clint, CLINT_BASE + CLINT_SIZE, &value, sizeof(value)));
    TEST_ASSERT_FALSE(clint_write(&clint, CLINT_BASE + CLINT_SIZE, &value, sizeof(value)));
}

// An unimplemented register reads as zero.
void test_unimplemented_reads_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, read32(0x1000));
}

// Runs the CLINT unit tests.
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_msip_set_clear);
    RUN_TEST(test_msip_nonzero);
    RUN_TEST(test_mtimecmp_compare);
    RUN_TEST(test_mtimecmp_reset_not_pending);
    RUN_TEST(test_mtimecmp_64bit_write);
    RUN_TEST(test_mtimecmp_halves);
    RUN_TEST(test_mtime_read_halves);
    RUN_TEST(test_mtime_latch);
    RUN_TEST(test_mtime_high_first);
    RUN_TEST(test_mtime_8byte_read);
    RUN_TEST(test_mtime_64bit_write);
    RUN_TEST(test_outside_range);
    RUN_TEST(test_unimplemented_reads_zero);
    return UNITY_END();
}
