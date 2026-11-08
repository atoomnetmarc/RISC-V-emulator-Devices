/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <unity.h>

static uint8_t poweroff_called;
static uint8_t poweroff_pass;
static uint8_t reboot_called;

#define RVE_DEV_SYSCON_POWEROFF(pass) \
    do {                              \
        poweroff_called = 1;          \
        poweroff_pass = (pass);       \
    } while (0)

#define RVE_DEV_SYSCON_REBOOT() \
    do {                        \
        reboot_called = 1;      \
    } while (0)

#include <RiscvEmulatorDevices.h>

#define SYSCON_BASE 0x00100000
#define SYSCON_SIZE 0x1000

static syscon_t syscon;

// Resets the host hooks and the device instance before each test.
void setUp(void) {
    poweroff_called = 0;
    poweroff_pass = 0;
    reboot_called = 0;
    syscon_init(&syscon, SYSCON_BASE, SYSCON_SIZE);
}

// Runs after each test; nothing to clean up.
void tearDown(void) {
}

// Writes one 32-bit value to the SYSCON register.
static void write_value(uint32_t value) {
    TEST_ASSERT_TRUE(syscon_write(&syscon, SYSCON_BASE, &value, sizeof(value)));
}

// A pass value calls the poweroff hook with pass set.
void test_poweroff_pass(void) {
    write_value(SYSCON_POWEROFF_PASS);
    TEST_ASSERT_TRUE(poweroff_called);
    TEST_ASSERT_TRUE(poweroff_pass);
    TEST_ASSERT_FALSE(reboot_called);
}

// A fail value calls the poweroff hook with pass cleared.
void test_poweroff_fail(void) {
    write_value(SYSCON_POWEROFF_FAIL);
    TEST_ASSERT_TRUE(poweroff_called);
    TEST_ASSERT_FALSE(poweroff_pass);
    TEST_ASSERT_FALSE(reboot_called);
}

// The reboot value calls the reboot hook only.
void test_reboot(void) {
    write_value(SYSCON_REBOOT);
    TEST_ASSERT_FALSE(poweroff_called);
    TEST_ASSERT_TRUE(reboot_called);
}

// An address outside the range is not claimed and runs no hook.
void test_outside_range(void) {
    uint32_t value = SYSCON_POWEROFF_PASS;
    TEST_ASSERT_FALSE(syscon_write(&syscon, SYSCON_BASE + SYSCON_SIZE, &value, sizeof(value)));
    TEST_ASSERT_FALSE(poweroff_called);
}

// A read inside the range returns zero.
void test_read_returns_zero(void) {
    uint32_t value = 0xFFFFFFFF;
    TEST_ASSERT_TRUE(syscon_read(&syscon, SYSCON_BASE, &value, sizeof(value)));
    TEST_ASSERT_EQUAL_UINT32(0, value);
}

// Runs the SYSCON unit tests.
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_poweroff_pass);
    RUN_TEST(test_poweroff_fail);
    RUN_TEST(test_reboot);
    RUN_TEST(test_outside_range);
    RUN_TEST(test_read_returns_zero);
    return UNITY_END();
}
