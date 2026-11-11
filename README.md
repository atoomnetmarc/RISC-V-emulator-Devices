# Status

The CLINT, 16550 UART, SYSCON, PLIC, virtio-blk and virtio-console models are implemented. The host unit tests exercise each model.

# Description

This is a library of reusable RISC-V platform device models for the [RISC-V-emulator](https://github.com/atoomnetmarc/RISC-V-emulator) family. It provides the memory-mapped devices that a board needs for a Linux console, without pulling platform code into the ISA core or duplicating it in every board port.

The library is header-only. All device functions are `static inline`; there are no function pointers and no dynamic allocation. It targets the same environments as the core: 8-bit AVR, 32-bit SAM and native. The host unit tests run on native.

# Devices

| Device                 | Purpose                               | DT compatible                            |
| :--------------------- | :------------------------------------ | :--------------------------------------- |
| CLINT                  | Per-hart timer and software interrupt | `sifive,clint0`, `riscv,clint0`          |
| 16550 UART             | Serial console                        | `ns16550a`                               |
| SYSCON / test finisher | Poweroff and reboot                   | `sifive,test1`, `sifive,test0`, `syscon` |
| PLIC                   | External interrupt controller         | `sifive,plic-1.0.0`                      |
| virtio-blk             | Block device over virtio-mmio         | `virtio,mmio`                            |
| virtio-console         | Console over virtio-mmio              | `virtio,mmio`                            |

# Memory map

The default memory map follows the QEMU `virt` machine:

| Device         |         Base |       Size |
| :------------- | -----------: | ---------: |
| CLINT          | `0x02000000` |  `0x10000` |
| UART0          | `0x10000000` |    `0x100` |
| Test finisher  | `0x00100000` |   `0x1000` |
| PLIC           | `0x0C000000` | `0x400000` |
| virtio-blk     | `0x10001000` |   `0x1000` |
| virtio-console | `0x10002000` |   `0x1000` |

The device models are address-independent; the board chooses the base addresses.

# Usage

Add the library to your PlatformIO project:

```ini
lib_deps =
  https://github.com/atoomnetmarc/RISC-V-emulator.git
  https://github.com/atoomnetmarc/RISC-V-emulator-Devices.git
```

Include the umbrella header and wire the devices in your `RiscvEmulatorLoad` and `RiscvEmulatorStore` implementation:

```c
#include <RiscvEmulatorDevices.h>
```

The SYSCON, UART and virtio-blk host hooks are macros. Define them before including the umbrella header:

```c
#define RVE_DEV_SYSCON_POWEROFF(pass) board_poweroff(pass)
#define RVE_DEV_SYSCON_REBOOT()       board_reboot()
#define RVE_DEV_UART16550_TX(byte)    board_uart_tx(byte)
#define RVE_DEV_VIRTIO_BLK_READ(sector, buffer, count)  board_disk_read(sector, buffer, count)
#define RVE_DEV_VIRTIO_BLK_WRITE(sector, buffer, count) board_disk_write(sector, buffer, count)
#define RVE_DEV_VIRTIO_BLK_FLUSH()                      board_disk_flush()
#define RVE_DEV_VIRTIO_CONSOLE_TX(buffer, length)       board_console_write(buffer, length)

#include <RiscvEmulatorDevices.h>
```

Each device can be disabled by defining its switch to `0` before including the umbrella header:

| Switch                   | Default | Device                 |
| :----------------------- | :------ | :--------------------- |
| `RVE_DEV_CLINT`          | `1`     | CLINT                  |
| `RVE_DEV_UART16550`      | `1`     | 16550 UART             |
| `RVE_DEV_SYSCON`         | `1`     | SYSCON / test finisher |
| `RVE_DEV_PLIC`           | `1`     | PLIC                   |
| `RVE_DEV_VIRTIO_BLK`     | `1`     | virtio-blk             |
| `RVE_DEV_VIRTIO_CONSOLE` | `1`     | virtio-console         |

The CLINT is a plain instance. Initialize it with `clint_init` and let the board supply the host clock and bridge the pending bits into `mip`:

```c
static clint_t clint;

// Call from board_init().
clint_init(&clint, 0x02000000, 0x10000);

// Host/native example. The board supplies the clock.
static inline void RiscvDevicesTick(RiscvEmulatorState_t *state) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_us = (uint64_t)ts.tv_sec * 1000000u + (uint32_t)ts.tv_nsec / 1000u;
    clint_tick(&clint, now_us);
    if (clint_timer_pending(&clint)) {
        state->csr.mip.mip |= (1u << 7);
    }
    if (clint_software_pending(&clint)) {
        state->csr.mip.mip |= (1u << 3);
    }
}
```

Call `RiscvDevicesTick` before `RiscvEmulatorLoop` so a pending timer wakes WFI. `RiscvDevicesTick` is board code, not part of the library. Set `RVE_TIME_SOURCE(state)` to `(clint.mtime)` so the `time` CSR and the timer share one timebase. The device tree declares `timebase-frequency = 1000000`.

See [docs/clint.md](docs/clint.md) for the register map and the latch behaviour.

The UART is a plain instance. Initialize it with `uart16550_init` and let the board push received bytes into the device FIFO and bridge the interrupt line:

```c
static uart16550_t uart;

// Call from board_init().
uart16550_init(&uart, 0x10000000, 0x100);

static inline void RiscvDevicesUartReceive(uint8_t byte) {
    uart16550_rx_push(&uart, byte);
}
```

See [docs/uart16550.md](docs/uart16550.md) for the register map and the interrupt priority.

The PLIC and virtio-blk models form a block device path. The PLIC has one context and 32 sources; source 1 carries the virtio-blk interrupt and source 2 carries the virtio-console interrupt. The board sets the pending bits from `virtio_blk_irq_pending` and `virtio_console_irq_pending` and bridges `plic_irq_pending` into `mip.MEIP` (bit 11). The virtio-blk model uses the host hooks for all disk I/O and reads the descriptor table and data buffers from guest RAM.

See [docs/diskio.md](docs/diskio.md) for the register map, the queue layout and the request format.

The virtio-console is a plain instance. Initialize it with `virtio_console_init`, push host input into the RX FIFO and bridge the interrupt line:

```c
static virtio_console_t console;

// Call from board_init().
virtio_console_init(&console, 0x10002000, 0x1000, guest_memory, 0x80000000, guest_size);

static inline void RiscvDevicesConsoleReceive(uint8_t byte) {
    virtio_console_rx_push(&console, byte);
}
```

The board defines `RVE_DEV_VIRTIO_CONSOLE_TX(buffer, length)` to write console output to the host. The hook receives a pointer into guest RAM and must not modify the buffer.

See [docs/virtio_console.md](docs/virtio_console.md) for the shared transport, both queues and the RX FIFO.

# Documentation

See [docs/index.md](docs/index.md) for the device documentation and the shared coding rules.

# Testing

The library has host unit tests. Run them with PlatformIO:

```sh
pio test -e native
```

The tests live in `test/` and use the Unity framework.

# License

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
