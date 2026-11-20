# Goldfish RTC device model

## Purpose

The goldfish RTC supplies wall-clock time to the guest. U-Boot reads it for the `date` and `rtc` commands and Linux registers it as `rtc0` and sets the system time from it at boot. The model is header-only, `static inline`, and has no dynamic allocation. It uses the `google,goldfish-rtc` device-tree binding that both U-Boot and Linux already support.

## Architecture

The device is a small MMIO register file. The board samples the host realtime clock at init and advances the emulated time from the host monotonic clock. Guest writes to the time registers change the emulated base only; the host clock is never written.

```
U-Boot / Linux guest
  goldfish RTC driver
    MMIO registers at 0x10005000
      emulated base + host monotonic delta
        host realtime clock at init
```

## Memory map

The device sits after the virtio-mmio devices in the native board.

| Device         |         Base |     Size | PLIC source |
| :------------- | -----------: | -------: | ----------: |
| virtio-blk     | `0x10001000` | `0x1000` |           1 |
| virtio-console | `0x10002000` | `0x1000` |           2 |
| virtio-net     | `0x10003000` | `0x1000` |           3 |
| 16550 UART     | `0x10000000` |  `0x100` |           4 |
| virtio-rng     | `0x10004000` | `0x1000` |           5 |
| goldfish RTC   | `0x10005000` | `0x1000` |           6 |

The device models are address-independent. The board chooses the base and the size. The device tree declares an interrupt line for the driver probe; the model does not raise alarms.

## Registers

| Offset | Register           | Access | Purpose                                  |
| :----- | :----------------- | :----- | :--------------------------------------- |
| `0x00` | `TIME_LOW`         | R/W    | Low 32 bits of nanoseconds; latches high |
| `0x04` | `TIME_HIGH`        | R/W    | High 32 bits of nanoseconds              |
| `0x08` | `ALARM_LOW`        | R/W    | Low 32 bits of the alarm                 |
| `0x0c` | `ALARM_HIGH`       | R/W    | High 32 bits of the alarm                |
| `0x10` | `IRQ_ENABLED`      | R/W    | Alarm interrupt enable                   |
| `0x14` | `CLEAR_ALARM`      | W      | Clears the local alarm status            |
| `0x18` | `ALARM_STATUS`     | R      | Alarm status, always zero                |
| `0x1c` | `CLEAR_INTERRUPT`  | R/W    | Reads as zero, writes are ignored        |

Reading `TIME_LOW` latches the high word so the driver can read a consistent 64-bit value. Writing either time word sets the emulated base and re-anchors the monotonic clock. Alarm registers keep local state; no interrupt is asserted.

## Time model

The device stores a base in nanoseconds and a monotonic anchor in nanoseconds. A read returns `base + (monotonic_now - anchor)`. The host supplies both clocks through hooks. The model uses addition, subtraction and shifts only; it has no 64-bit division.

## Host hooks

The board defines the hooks before including the umbrella header.

```c
#define RVE_DEV_GOLDFISH_RTC_REALTIME_NS()  board_rtc_realtime_ns()
#define RVE_DEV_GOLDFISH_RTC_MONOTONIC_NS() board_rtc_monotonic_ns()
```

The default hooks return zero. A board that boots a guest must define both hooks with real host clocks.

## API

| Function               | Purpose                                          |
| :--------------------- | :----------------------------------------------- |
| `goldfish_rtc_init`    | Samples the host clocks and initializes the device. |
| `goldfish_rtc_contains`| Returns true for an address in the device range. |
| `goldfish_rtc_now`     | Returns the current emulated time in nanoseconds. |
| `goldfish_rtc_read`    | Reads a register.                                |
| `goldfish_rtc_write`   | Writes a register.                               |

## Board integration

The board owns the host clocks, the base address and the interrupt line.

- Initialize the device with `goldfish_rtc_init`.
- Define both host clock hooks.
- Route load and store accesses through `goldfish_rtc_read` and `goldfish_rtc_write`.

The device tree adds a `google,goldfish-rtc` node at `0x10005000` with PLIC source 6.

```dts
rtc@10005000 {
    compatible = "google,goldfish-rtc";
    reg = <0x00 0x10005000 0x00 0x1000>;
    interrupts = <0x06>;
    interrupt-parent = <0x05>;
};
```

## Guest integration

U-Boot binds the device through its goldfish RTC driver. `CONFIG_DM_RTC`, `CONFIG_RTC_GOLDFISH`, `CONFIG_CMD_DATE` and `CONFIG_CMD_RTC` enable the driver and the shell commands.

Linux needs these kernel options:

| Option                  | Value |
| :---------------------- | :---- |
| `CONFIG_RTC_CLASS`      | `y`   |
| `CONFIG_RTC_HCTOSYS`    | `y`   |
| `CONFIG_RTC_HCTOSYS_DEVICE` | `"rtc0"` |
| `CONFIG_RTC_DRV_GOLDFISH` | `y` |

## Test plan

- Unity unit tests for init, free-run, the high-word latch, guest writes, alarm registers, unimplemented registers and the address range.
- Boot U-Boot and run `rtc list` and `date`.
- Boot Linux and read `date` and `/sys/class/rtc/rtc0/time`.

## Acceptance criteria

- U-Boot `rtc list` reports a goldfish RTC and `date` prints the host time.
- Linux registers `rtc0` and `date` shows the host time after boot.
- Guest writes change the emulated clock and leave the host clock unchanged.

## Constraints

- No `malloc`.
- No function pointers.
- No 64-bit division.
- No alarm interrupt handling.
