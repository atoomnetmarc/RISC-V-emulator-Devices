/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#ifndef RiscvEmulatorDeviceUart16550_H_
#define RiscvEmulatorDeviceUart16550_H_

#ifndef RVE_DEV_UART16550
#define RVE_DEV_UART16550 1
#endif

#if (RVE_DEV_UART16550 == 1)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Register offsets from the UART base.
#define UART16550_RBR_OFFSET 0x00
#define UART16550_THR_OFFSET 0x00
#define UART16550_DLL_OFFSET 0x00
#define UART16550_IER_OFFSET 0x01
#define UART16550_DLM_OFFSET 0x01
#define UART16550_IIR_OFFSET 0x02
#define UART16550_FCR_OFFSET 0x02
#define UART16550_LCR_OFFSET 0x03
#define UART16550_MCR_OFFSET 0x04
#define UART16550_LSR_OFFSET 0x05
#define UART16550_MSR_OFFSET 0x06
#define UART16550_SCR_OFFSET 0x07

// Line status register bits.
#define UART16550_LSR_DATA_READY     0x01
#define UART16550_LSR_OVERRUN        0x02
#define UART16550_LSR_THR_EMPTY      0x20
#define UART16550_LSR_TRANSMIT_EMPTY 0x40

// Line control register bits.
#define UART16550_LCR_DLAB 0x80

// Interrupt enable register bits.
#define UART16550_IER_RX_DATA 0x01
#define UART16550_IER_THR     0x02
#define UART16550_IER_LSR     0x04
#define UART16550_IER_MSR     0x08
#define UART16550_IER_DMA_RX  0x40
#define UART16550_IER_DMA_TX  0x80

// Modem control register bits.
#define UART16550_MCR_OUT2 0x08
#define UART16550_MCR_MASK 0x1F

// FIFO control register bits.
#define UART16550_FCR_ENABLE   0x01
#define UART16550_FCR_CLEAR_RX 0x02
#define UART16550_FCR_CLEAR_TX 0x04
#define UART16550_FCR_DMA      0x08
#define UART16550_FCR_TRIGGER  0xC0

// Interrupt identification register codes.
#define UART16550_IIR_NONE         0x01
#define UART16550_IIR_THR          0x02
#define UART16550_IIR_RX_DATA      0x04
#define UART16550_IIR_LSR          0x06
#define UART16550_IIR_TIMEOUT      0x0C
#define UART16550_IIR_FIFO_ENABLED 0xC0

#define UART16550_FIFO_SIZE 16

// Host hook. A board defines this before including this header.
#ifndef RVE_DEV_UART16550_TX
#define RVE_DEV_UART16550_TX(byte) ((void)(byte))
#endif

/**
 * 16550 UART instance.
 */
typedef struct {
    uint32_t base;
    uint32_t size;
    uint8_t ier;
    uint8_t fcr;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t scr;
    uint8_t dll;
    uint8_t dlm;
    uint8_t fifo[UART16550_FIFO_SIZE];
    uint8_t fifo_read;
    uint8_t fifo_write;
    uint8_t fifo_count;
    bool overrun;
    bool thre_pending;
} uart16550_t;

/**
 * Initializes a UART instance at the given memory range.
 */
static inline void uart16550_init(uart16550_t *uart, uint32_t base, uint32_t size) {
    memset(uart, 0, sizeof(*uart));
    uart->base = base;
    uart->size = size;
    uart->thre_pending = true;
}

/**
 * Returns true when the address falls inside the UART range.
 */
static inline bool uart16550_contains(const uart16550_t *uart, uint32_t address) {
    return address >= uart->base && address < uart->base + uart->size;
}

/**
 * Returns the RX trigger level in bytes.
 */
static inline uint8_t uart16550_trigger(const uart16550_t *uart) {
    if ((uart->fcr & UART16550_FCR_ENABLE) == 0) {
        return 1;
    }

    switch (uart->fcr & UART16550_FCR_TRIGGER) {
        case 0x00:
            return 1;
        case 0x40:
            return 4;
        case 0x80:
            return 8;
        default:
            return 14;
    }
}

/**
 * Pushes one received byte into the RX FIFO; sets overrun when the FIFO is full.
 */
static inline void uart16550_rx_push(uart16550_t *uart, uint8_t data) {
    uint8_t depth = (uart->fcr & UART16550_FCR_ENABLE) ? UART16550_FIFO_SIZE : 1;

    if (uart->fifo_count >= depth) {
        uart->overrun = true;
        if (depth == 1) {
            uart->fifo[uart->fifo_read] = data;
        }
        return;
    }

    uart->fifo[uart->fifo_write] = data;
    uart->fifo_write = (uint8_t)((uart->fifo_write + 1) % UART16550_FIFO_SIZE);
    uart->fifo_count++;
}

/**
 * Pops the oldest byte from the RX FIFO, or returns zero when it is empty.
 */
static inline uint8_t uart16550_rx_pop(uart16550_t *uart) {
    if (uart->fifo_count == 0) {
        return 0;
    }

    uint8_t data = uart->fifo[uart->fifo_read];
    uart->fifo_read = (uint8_t)((uart->fifo_read + 1) % UART16550_FIFO_SIZE);
    uart->fifo_count--;

    return data;
}

/**
 * Writes the FIFO control register and runs the self-clearing actions.
 */
static inline void uart16550_fcr_write(uart16550_t *uart, uint8_t value) {
    bool enabled = (value & UART16550_FCR_ENABLE) != 0;
    bool was_enabled = (uart->fcr & UART16550_FCR_ENABLE) != 0;

    if (enabled != was_enabled) {
        uart->fifo_read = 0;
        uart->fifo_write = 0;
        uart->fifo_count = 0;
        uart->overrun = false;
    }

    if (!enabled) {
        uart->fcr = 0;
        return;
    }

    uart->fcr = value & (UART16550_FCR_ENABLE | UART16550_FCR_DMA | UART16550_FCR_TRIGGER);

    if (value & UART16550_FCR_CLEAR_RX) {
        uart->fifo_read = 0;
        uart->fifo_write = 0;
        uart->fifo_count = 0;
        uart->overrun = false;
    }
}

/**
 * Reads the line status register and clears the overrun bit.
 */
static inline uint8_t uart16550_lsr_read(uart16550_t *uart) {
    uint8_t value = UART16550_LSR_THR_EMPTY | UART16550_LSR_TRANSMIT_EMPTY;

    if (uart->fifo_count > 0) {
        value |= UART16550_LSR_DATA_READY;
    }

    if (uart->overrun) {
        value |= UART16550_LSR_OVERRUN;
    }

    uart->overrun = false;

    return value;
}

/**
 * Reads the interrupt identification register and clears a pending THR-empty interrupt.
 */
static inline uint8_t uart16550_iir_read(uart16550_t *uart) {
    uint8_t fifo = (uart->fcr & UART16550_FCR_ENABLE) ? UART16550_IIR_FIFO_ENABLED : 0;
    uint8_t trigger = uart16550_trigger(uart);

    if ((uart->ier & UART16550_IER_LSR) && uart->overrun) {
        return fifo | UART16550_IIR_LSR;
    }

    if ((uart->ier & UART16550_IER_RX_DATA) && uart->fifo_count >= trigger) {
        return fifo | UART16550_IIR_RX_DATA;
    }

    if ((uart->ier & UART16550_IER_RX_DATA) && uart->fifo_count > 0) {
        return fifo | UART16550_IIR_TIMEOUT;
    }

    if ((uart->ier & UART16550_IER_THR) && uart->thre_pending) {
        uart->thre_pending = false;
        return fifo | UART16550_IIR_THR;
    }

    return fifo | UART16550_IIR_NONE;
}

/**
 * Returns true when an enabled interrupt source is pending.
 */
static inline bool uart16550_irq_pending(const uart16550_t *uart) {
    if ((uart->mcr & UART16550_MCR_OUT2) == 0) {
        return false;
    }

    if ((uart->ier & UART16550_IER_LSR) && uart->overrun) {
        return true;
    }

    if ((uart->ier & UART16550_IER_RX_DATA) && uart->fifo_count > 0) {
        return true;
    }

    if ((uart->ier & UART16550_IER_THR) && uart->thre_pending) {
        return true;
    }

    return false;
}

/**
 * Reads a UART register. Only byte reads are supported.
 */
static inline bool uart16550_read(uart16550_t *uart, uint32_t address, void *destination, uint8_t length) {
    if (!uart16550_contains(uart, address)) {
        return false;
    }

    if (length != 1) {
        memset(destination, 0, length);
        return true;
    }

    uint32_t offset = address - uart->base;
    uint8_t value = 0;

    switch (offset) {
        case UART16550_RBR_OFFSET:
            value = (uart->lcr & UART16550_LCR_DLAB) ? uart->dll : uart16550_rx_pop(uart);
            break;
        case UART16550_IER_OFFSET:
            value = (uart->lcr & UART16550_LCR_DLAB) ? uart->dlm : uart->ier;
            break;
        case UART16550_IIR_OFFSET:
            value = uart16550_iir_read(uart);
            break;
        case UART16550_LCR_OFFSET:
            value = uart->lcr;
            break;
        case UART16550_MCR_OFFSET:
            value = uart->mcr;
            break;
        case UART16550_LSR_OFFSET:
            value = uart16550_lsr_read(uart);
            break;
        case UART16550_MSR_OFFSET:
            value = 0;
            break;
        case UART16550_SCR_OFFSET:
            value = uart->scr;
            break;
        default:
            value = 0;
            break;
    }

    *(uint8_t *)destination = value;

    return true;
}

/**
 * Writes a UART register. A THR write accepts any length and uses the lowest byte.
 */
static inline bool uart16550_write(uart16550_t *uart, uint32_t address, const void *source, uint8_t length) {
    if (!uart16550_contains(uart, address)) {
        return false;
    }

    uint32_t offset = address - uart->base;
    uint8_t value;
    memcpy(&value, source, 1);

    if (offset == UART16550_THR_OFFSET && (uart->lcr & UART16550_LCR_DLAB) == 0) {
        RVE_DEV_UART16550_TX(value);
        uart->thre_pending = true;
        return true;
    }

    if (length != 1) {
        return true;
    }

    switch (offset) {
        case UART16550_DLL_OFFSET:
            uart->dll = value;
            break;
        case UART16550_IER_OFFSET:
            if (uart->lcr & UART16550_LCR_DLAB) {
                uart->dlm = value;
            } else {
                uart->ier = value & (UART16550_IER_RX_DATA | UART16550_IER_THR | UART16550_IER_LSR | UART16550_IER_MSR | UART16550_IER_DMA_RX | UART16550_IER_DMA_TX);
            }
            break;
        case UART16550_FCR_OFFSET:
            uart16550_fcr_write(uart, value);
            break;
        case UART16550_LCR_OFFSET:
            uart->lcr = value;
            break;
        case UART16550_MCR_OFFSET:
            uart->mcr = value & UART16550_MCR_MASK;
            break;
        case UART16550_SCR_OFFSET:
            uart->scr = value;
            break;
        default:
            break;
    }

    return true;
}

#endif

#endif
