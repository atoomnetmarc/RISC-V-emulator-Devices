# Status

The CLINT, 16550 UART and SYSCON models are implemented. The host unit tests exercise each model.

# Description

This is a library of reusable RISC-V platform device models for the [RISC-V-emulator](https://github.com/atoomnetmarc/RISC-V-emulator) family. It provides the memory-mapped devices that a board needs for a Linux console, without pulling platform code into the ISA core or duplicating it in every board port.

The library is header-only. All device functions are `static inline`; there are no function pointers and no dynamic allocation. It targets the same environments as the core: 8-bit AVR, 32-bit SAM and native. The host unit tests run on native.

# Devices

| Device                 | Purpose                               | DT compatible                            |
| :--------------------- | :------------------------------------ | :--------------------------------------- |
| CLINT                  | Per-hart timer and software interrupt | `sifive,clint0`, `riscv,clint0`          |
| 16550 UART             | Serial console                        | `ns16550a`                               |
| SYSCON / test finisher | Poweroff and reboot                   | `sifive,test1`, `sifive,test0`, `syscon` |

# Memory map

The default memory map follows the QEMU `virt` machine:

| Device        |         Base |      Size |
| :------------ | -----------: | --------: |
| CLINT         | `0x02000000` | `0x10000` |
| UART0         | `0x10000000` |   `0x100` |
| Test finisher | `0x00100000` |  `0x1000` |

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

The SYSCON and UART host hooks are macros. Define them before including the umbrella header:

```c
#define RVE_DEV_SYSCON_POWEROFF(pass) board_poweroff(pass)
#define RVE_DEV_SYSCON_REBOOT()       board_reboot()
#define RVE_DEV_UART16550_TX(byte)    board_uart_tx(byte)

#include <RiscvEmulatorDevices.h>
```

Each device can be disabled by defining its switch to `0` before including the umbrella header:

| Switch              | Default | Device                 |
| :------------------ | :------ | :--------------------- |
| `RVE_DEV_CLINT`     | `1`     | CLINT                  |
| `RVE_DEV_UART16550` | `1`     | 16550 UART             |
| `RVE_DEV_SYSCON`    | `1`     | SYSCON / test finisher |

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
