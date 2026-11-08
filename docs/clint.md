# CLINT device model

The CLINT provides the per-hart timer and software interrupt. The model is header-only, `static inline`, and has no dynamic allocation.

## Register map

| Register   |    Offset | Width           |
| :--------- | --------: | :-------------- |
| `msip`     | `+0x0000` | 32 bit per hart |
| `mtimecmp` | `+0x4000` | 64 bit per hart |
| `mtime`    | `+0xBFF8` | 64 bit shared   |

The offsets are relative to the CLINT base. The board chooses the base and size.

## Time source

`mtime` follows the host clock in microseconds. The board reads `clock_gettime(CLOCK_MONOTONIC)` and computes `(uint64_t)tv_sec * 1000000 + (uint32_t)tv_nsec / 1000`. The 32-bit division of `tv_nsec` avoids a 64-bit division.

`clint_tick(clint_t *clint, uint64_t now_us)` assigns `mtime = now_us` directly. `CLOCK_MONOTONIC` is non-decreasing, so no clamp is needed.

## Storage and register access

`mtime` and `mtimecmp` are stored as `uint64_t`. The register interface exposes two 32-bit halves.

A 32-bit read of the low `mtime` word latches the high word. A 32-bit read of the high word returns the latched value and clears the latch. A high-word read without a preceding low-word read returns the current high word, tracked by a valid flag. An 8-byte read at `+0xBFF8` returns the full value and bypasses the latch.

`mtimecmp` accepts 32-bit writes to each half immediately and 8-byte writes. This matches the Linux write sequence: high word to `0xFFFFFFFF`, low word, high word.

`msip` is a 32-bit register. A non-zero value makes `clint_software_pending` true; a zero value clears it.

## Pending interrupts

`clint_timer_pending` returns true when `mtime >= mtimecmp`. `clint_software_pending` returns true when `msip != 0`.

The board implements a tick helper, for example `RiscvDevicesTick`, and calls it before `RiscvEmulatorLoop`. That helper calls `clint_tick` and sets `mip.MTIP` (bit 7) and `mip.MSIP` (bit 3). Setting `mip` before the loop lets WFI wake on a pending timer. The library does not provide the helper.

The board defines `RVE_TIME_SOURCE(state)` to return the CLINT `mtime`, so the `time` CSR and the timer share one timebase. The device tree declares `timebase-frequency = 1000000`.

## API

| Function                 | Purpose                                                |
| :----------------------- | :----------------------------------------------------- |
| `clint_init`             | Initializes the instance and its base and size.        |
| `clint_read`             | Reads a register and returns false outside the range.  |
| `clint_write`            | Writes a register and returns false outside the range. |
| `clint_tick`             | Sets `mtime` from the host clock in microseconds.      |
| `clint_timer_pending`    | Returns true when `mtime >= mtimecmp`.                 |
| `clint_software_pending` | Returns true when `msip != 0`.                         |

## Constraints

- No 64-bit division.
- No `malloc`.
- No function pointers.
- The device library does not know the core type. The board owns the `mip` bridge.
