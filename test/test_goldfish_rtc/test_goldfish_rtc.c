/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <string.h>
#include <unity.h>

#define RTC_BASE 0x10005000
#define RTC_SIZE 0x1000

static uint64_t realtime_ns;
static uint64_t monotonic_ns;

#define RVE_DEV_GOLDFISH_RTC_REALTIME_NS()  realtime_ns
#define RVE_DEV_GOLDFISH_RTC_MONOTONIC_NS() monotonic_ns

#include <RiscvEmulatorDevices.h>

static goldfish_rtc_t dev;

// Writes one 32-bit value to a goldfish RTC register.
static void write32(uint32_t offset, uint32_t value) {
    TEST_ASSERT_TRUE(goldfish_rtc_write(&dev, RTC_BASE + offset, &value, sizeof(value)));
}

// Reads one 32-bit value from a goldfish RTC register.
static uint32_t read32(uint32_t offset) {
    uint32_t value = 0;
    TEST_ASSERT_TRUE(goldfish_rtc_read(&dev, RTC_BASE + offset, &value, sizeof(value)));
    return value;
}

// Checks the 64-bit time through the low and high time registers.
static void assert_time(uint64_t expected) {
    uint32_t low = read32(GOLDFISH_RTC_TIME_LOW);
    uint32_t high = read32(GOLDFISH_RTC_TIME_HIGH);

    TEST_ASSERT_EQUAL_UINT32((uint32_t)expected, low);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(expected >> 32), high);
}

// Resets the device and the host clocks before each test.
void setUp(void) {
    realtime_ns = 1700000000000000000ULL;
    monotonic_ns = 5000000000ULL;
    goldfish_rtc_init(&dev, RTC_BASE, RTC_SIZE);
}

// Runs after each test; nothing to clean up.
void tearDown(void) {
}

// Init samples the host realtime clock.
void test_init(void) {
    assert_time(realtime_ns);
}

// The clock advances from the host monotonic clock.
void test_free_run(void) {
    monotonic_ns += 2000000000ULL;
    assert_time(realtime_ns + 2000000000ULL);
}

// Reading the low word latches the high word.
void test_latch(void) {
    realtime_ns = UINT64_C(0x00000001FFFFFFFF);
    monotonic_ns = 0;
    goldfish_rtc_init(&dev, RTC_BASE, RTC_SIZE);

    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, read32(GOLDFISH_RTC_TIME_LOW));

    monotonic_ns = 2;
    TEST_ASSERT_EQUAL_UINT32(1, read32(GOLDFISH_RTC_TIME_HIGH));
}

// Guest writes set the emulated base without changing the host clock.
void test_guest_write(void) {
    uint64_t host_before = realtime_ns;
    uint64_t written = UINT64_C(0x0000000200000004);

    write32(GOLDFISH_RTC_TIME_HIGH, (uint32_t)(written >> 32));
    write32(GOLDFISH_RTC_TIME_LOW, (uint32_t)written);

    assert_time(written);
    TEST_ASSERT_TRUE(realtime_ns == host_before);

    monotonic_ns += 3;
    assert_time(written + 3);
}

// Alarm and interrupt registers keep local state.
void test_alarm_registers(void) {
    write32(GOLDFISH_RTC_ALARM_LOW, 0x12345678);
    write32(GOLDFISH_RTC_ALARM_HIGH, 0x9abcdef0);
    write32(GOLDFISH_RTC_IRQ_ENABLED, 1);

    TEST_ASSERT_EQUAL_UINT32(0x12345678, read32(GOLDFISH_RTC_ALARM_LOW));
    TEST_ASSERT_EQUAL_UINT32(0x9abcdef0, read32(GOLDFISH_RTC_ALARM_HIGH));
    TEST_ASSERT_EQUAL_UINT32(1, read32(GOLDFISH_RTC_IRQ_ENABLED));
    TEST_ASSERT_EQUAL_UINT32(0, read32(GOLDFISH_RTC_ALARM_STATUS));

    write32(GOLDFISH_RTC_CLEAR_ALARM, 1);
    TEST_ASSERT_EQUAL_UINT32(0, read32(GOLDFISH_RTC_ALARM_STATUS));
}

// Unimplemented registers read as zero and writes are ignored.
void test_unimplemented(void) {
    TEST_ASSERT_EQUAL_UINT32(0, read32(GOLDFISH_RTC_CLEAR_INTERRUPT));
    write32(GOLDFISH_RTC_CLEAR_INTERRUPT, 1);
    TEST_ASSERT_EQUAL_UINT32(0, read32(GOLDFISH_RTC_CLEAR_INTERRUPT));
}

// Addresses outside the device range are not mapped.
void test_address_range(void) {
    uint32_t value = 0;
    TEST_ASSERT_FALSE(goldfish_rtc_read(&dev, RTC_BASE - 4, &value, sizeof(value)));
    TEST_ASSERT_FALSE(goldfish_rtc_read(&dev, RTC_BASE + RTC_SIZE, &value, sizeof(value)));
    TEST_ASSERT_FALSE(goldfish_rtc_write(&dev, RTC_BASE - 4, &value, sizeof(value)));
    TEST_ASSERT_FALSE(goldfish_rtc_write(&dev, RTC_BASE + RTC_SIZE, &value, sizeof(value)));
}

// Runs the goldfish RTC unit tests.
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init);
    RUN_TEST(test_free_run);
    RUN_TEST(test_latch);
    RUN_TEST(test_guest_write);
    RUN_TEST(test_alarm_registers);
    RUN_TEST(test_unimplemented);
    RUN_TEST(test_address_range);
    return UNITY_END();
}
