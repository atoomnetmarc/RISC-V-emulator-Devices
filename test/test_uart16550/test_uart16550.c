/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <unity.h>

static uint8_t tx_bytes[32];
static uint8_t tx_count;

// Captures one transmitted byte for the TX hook.
static void capture_tx(uint8_t byte) {
    if (tx_count < sizeof(tx_bytes)) {
        tx_bytes[tx_count++] = byte;
    }
}

#define RVE_DEV_UART16550_TX(byte) capture_tx(byte)

#include <RiscvEmulatorDevices.h>

#define UART_BASE 0x10000000
#define UART_SIZE 0x100

static uart16550_t uart;

// Resets the UART instance and the TX capture before each test.
void setUp(void) {
    tx_count = 0;
    uart16550_init(&uart, UART_BASE, UART_SIZE);
}

// Runs after each test; nothing to clean up.
void tearDown(void) {
}

// Writes one byte to a UART register.
static void write8(uint32_t offset, uint8_t value) {
    TEST_ASSERT_TRUE(uart16550_write(&uart, UART_BASE + offset, &value, sizeof(value)));
}

// Reads one byte from a UART register.
static uint8_t read8(uint32_t offset) {
    uint8_t value = 0;
    TEST_ASSERT_TRUE(uart16550_read(&uart, UART_BASE + offset, &value, sizeof(value)));
    return value;
}

// DLAB switches offset 0 and 1 between data and divisor registers.
void test_dlab_switch(void) {
    write8(UART16550_LCR_OFFSET, UART16550_LCR_DLAB);
    write8(UART16550_DLL_OFFSET, 0x12);
    write8(UART16550_DLM_OFFSET, 0x34);
    TEST_ASSERT_EQUAL_UINT8(0x12, read8(UART16550_DLL_OFFSET));
    TEST_ASSERT_EQUAL_UINT8(0x34, read8(UART16550_DLM_OFFSET));

    write8(UART16550_LCR_OFFSET, 0x00);
    write8(UART16550_IER_OFFSET, 0x03);
    TEST_ASSERT_EQUAL_UINT8(0x03, read8(UART16550_IER_OFFSET));
}

// The line status register reports data ready and the empty transmitter.
void test_lsr_bits(void) {
    TEST_ASSERT_EQUAL_UINT8(UART16550_LSR_THR_EMPTY | UART16550_LSR_TRANSMIT_EMPTY, read8(UART16550_LSR_OFFSET));
    uart16550_rx_push(&uart, 'x');
    TEST_ASSERT_EQUAL_UINT8(UART16550_LSR_DATA_READY | UART16550_LSR_THR_EMPTY | UART16550_LSR_TRANSMIT_EMPTY, read8(UART16550_LSR_OFFSET));
    TEST_ASSERT_EQUAL_UINT8('x', read8(UART16550_RBR_OFFSET));
    TEST_ASSERT_EQUAL_UINT8(UART16550_LSR_THR_EMPTY | UART16550_LSR_TRANSMIT_EMPTY, read8(UART16550_LSR_OFFSET));
}

// A THR write calls the TX hook with the lowest byte.
void test_tx_hook(void) {
    uint32_t value = 0x00000041;
    TEST_ASSERT_TRUE(uart16550_write(&uart, UART_BASE + UART16550_THR_OFFSET, &value, sizeof(value)));
    TEST_ASSERT_EQUAL_UINT8(1, tx_count);
    TEST_ASSERT_EQUAL_UINT8('A', tx_bytes[0]);
}

// The RX FIFO returns bytes in order and sets overrun when full.
void test_rx_fifo(void) {
    write8(UART16550_FCR_OFFSET, UART16550_FCR_ENABLE);
    for (uint8_t i = 0; i < UART16550_FIFO_SIZE; i++) {
        uart16550_rx_push(&uart, i);
    }
    uart16550_rx_push(&uart, 0xFF);
    TEST_ASSERT_EQUAL_UINT8(UART16550_LSR_DATA_READY | UART16550_LSR_OVERRUN | UART16550_LSR_THR_EMPTY | UART16550_LSR_TRANSMIT_EMPTY, read8(UART16550_LSR_OFFSET));
    for (uint8_t i = 0; i < UART16550_FIFO_SIZE; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, read8(UART16550_RBR_OFFSET));
    }
    TEST_ASSERT_EQUAL_UINT8(UART16550_LSR_THR_EMPTY | UART16550_LSR_TRANSMIT_EMPTY, read8(UART16550_LSR_OFFSET));
}

// With the FIFO disabled, the second byte overwrites the first and sets overrun.
void test_fifo_disabled(void) {
    write8(UART16550_FCR_OFFSET, 0x00);
    uart16550_rx_push(&uart, 'a');
    uart16550_rx_push(&uart, 'b');
    TEST_ASSERT_EQUAL_UINT8(UART16550_LSR_DATA_READY | UART16550_LSR_OVERRUN | UART16550_LSR_THR_EMPTY | UART16550_LSR_TRANSMIT_EMPTY, read8(UART16550_LSR_OFFSET));
    TEST_ASSERT_EQUAL_UINT8('b', read8(UART16550_RBR_OFFSET));
    TEST_ASSERT_EQUAL_UINT8(UART16550_LSR_THR_EMPTY | UART16550_LSR_TRANSMIT_EMPTY, read8(UART16550_LSR_OFFSET));
}

// FCR clear RX empties the FIFO.
void test_fcr_clear_rx(void) {
    write8(UART16550_FCR_OFFSET, UART16550_FCR_ENABLE);
    uart16550_rx_push(&uart, 'a');
    uart16550_rx_push(&uart, 'b');
    write8(UART16550_FCR_OFFSET, UART16550_FCR_ENABLE | UART16550_FCR_CLEAR_RX);
    TEST_ASSERT_EQUAL_UINT8(UART16550_LSR_THR_EMPTY | UART16550_LSR_TRANSMIT_EMPTY, read8(UART16550_LSR_OFFSET));
}

// IIR reports RX data before THR empty and clears THR on read.
void test_iir_priority(void) {
    write8(UART16550_IER_OFFSET, UART16550_IER_RX_DATA | UART16550_IER_THR);
    write8(UART16550_FCR_OFFSET, UART16550_FCR_ENABLE);
    uart16550_rx_push(&uart, 'a');
    TEST_ASSERT_EQUAL_UINT8(UART16550_IIR_FIFO_ENABLED | UART16550_IIR_RX_DATA, read8(UART16550_IIR_OFFSET));
    TEST_ASSERT_EQUAL_UINT8('a', read8(UART16550_RBR_OFFSET));
    TEST_ASSERT_EQUAL_UINT8(UART16550_IIR_FIFO_ENABLED | UART16550_IIR_THR, read8(UART16550_IIR_OFFSET));
    TEST_ASSERT_EQUAL_UINT8(UART16550_IIR_FIFO_ENABLED | UART16550_IIR_NONE, read8(UART16550_IIR_OFFSET));
}

// A FIFO below the trigger level reports a character timeout.
void test_iir_timeout(void) {
    write8(UART16550_IER_OFFSET, UART16550_IER_RX_DATA);
    write8(UART16550_FCR_OFFSET, UART16550_FCR_ENABLE | 0x80);
    uart16550_rx_push(&uart, 'a');
    uart16550_rx_push(&uart, 'b');
    TEST_ASSERT_EQUAL_UINT8(UART16550_IIR_FIFO_ENABLED | UART16550_IIR_TIMEOUT, read8(UART16550_IIR_OFFSET));
}

// irq_pending follows the enabled interrupt sources and the MCR global mask.
void test_irq_pending(void) {
    TEST_ASSERT_FALSE(uart16550_irq_pending(&uart));
    write8(UART16550_IER_OFFSET, UART16550_IER_RX_DATA);
    uart16550_rx_push(&uart, 'a');
    TEST_ASSERT_FALSE(uart16550_irq_pending(&uart));
    write8(UART16550_MCR_OFFSET, UART16550_MCR_OUT2);
    TEST_ASSERT_TRUE(uart16550_irq_pending(&uart));
}

// MCR and SCR read back the written value.
void test_mcr_scr(void) {
    write8(UART16550_MCR_OFFSET, 0x0B);
    TEST_ASSERT_EQUAL_UINT8(0x0B, read8(UART16550_MCR_OFFSET));
    write8(UART16550_SCR_OFFSET, 0x5A);
    TEST_ASSERT_EQUAL_UINT8(0x5A, read8(UART16550_SCR_OFFSET));
}

// IER bits 4 and 5 read as zero.
void test_ier_reserved_bits(void) {
    write8(UART16550_IER_OFFSET, 0xFF);
    TEST_ASSERT_EQUAL_UINT8(0xCF, read8(UART16550_IER_OFFSET));
}

// MCR bits 5 to 7 read as zero.
void test_mcr_reserved_bits(void) {
    write8(UART16550_MCR_OFFSET, 0xFF);
    TEST_ASSERT_EQUAL_UINT8(UART16550_MCR_MASK, read8(UART16550_MCR_OFFSET));
}

// FCR ignores the trigger bits when the FIFO enable bit is clear.
void test_fcr_requires_enable(void) {
    write8(UART16550_IER_OFFSET, UART16550_IER_RX_DATA);
    write8(UART16550_FCR_OFFSET, 0x80);
    uart16550_rx_push(&uart, 'a');
    TEST_ASSERT_EQUAL_UINT8(UART16550_IIR_RX_DATA, read8(UART16550_IIR_OFFSET));
}

// Changing the FCR enable bit resets the RX FIFO.
void test_fcr_enable_resets_fifo(void) {
    write8(UART16550_FCR_OFFSET, UART16550_FCR_ENABLE);
    uart16550_rx_push(&uart, 'a');
    write8(UART16550_FCR_OFFSET, 0x00);
    TEST_ASSERT_EQUAL_UINT8(UART16550_LSR_THR_EMPTY | UART16550_LSR_TRANSMIT_EMPTY, read8(UART16550_LSR_OFFSET));
}

// A non-byte write to a non-THR register is ignored.
void test_write_width(void) {
    uint32_t value = 0x12345678;
    TEST_ASSERT_TRUE(uart16550_write(&uart, UART_BASE + UART16550_LCR_OFFSET, &value, sizeof(value)));
    TEST_ASSERT_EQUAL_UINT8(0x00, read8(UART16550_LCR_OFFSET));
}

// An address outside the range is not claimed.
void test_outside_range(void) {
    uint8_t value = 0;
    TEST_ASSERT_FALSE(uart16550_read(&uart, UART_BASE + UART_SIZE, &value, sizeof(value)));
    TEST_ASSERT_FALSE(uart16550_write(&uart, UART_BASE + UART_SIZE, &value, sizeof(value)));
}

// Runs the 16550 UART unit tests.
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dlab_switch);
    RUN_TEST(test_lsr_bits);
    RUN_TEST(test_tx_hook);
    RUN_TEST(test_rx_fifo);
    RUN_TEST(test_fifo_disabled);
    RUN_TEST(test_fcr_clear_rx);
    RUN_TEST(test_iir_priority);
    RUN_TEST(test_iir_timeout);
    RUN_TEST(test_irq_pending);
    RUN_TEST(test_mcr_scr);
    RUN_TEST(test_ier_reserved_bits);
    RUN_TEST(test_mcr_reserved_bits);
    RUN_TEST(test_fcr_requires_enable);
    RUN_TEST(test_fcr_enable_resets_fifo);
    RUN_TEST(test_write_width);
    RUN_TEST(test_outside_range);
    return UNITY_END();
}
