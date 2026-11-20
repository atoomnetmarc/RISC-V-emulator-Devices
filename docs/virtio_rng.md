# Virtio random number generator device model

## Purpose

The virtio-rng device supplies host entropy to the guest. U-Boot reads it for the `kaslr-seed` device-tree property and Linux registers it as a hardware random number source. The model is header-only, `static inline`, and has no dynamic allocation. It shares the virtio-mmio transport with the blk, console and net models.

## Architecture

The transport is device-independent. The rng model is a thin wrapper around it.

```
Linux guest
  virtio-rng driver
    virtio-mmio legacy v1 (MMIO)
      PLIC source 5
        PLIC context 0
          mip.MEIP
            hart
```

The transport lives in `RiscvEmulatorDeviceVirtioMmio.h`. The rng model in `RiscvEmulatorDeviceVirtioRng.h` embeds a `virtio_mmio_t` and adds the request queue and the host entropy hook.

## Memory map

The rng device follows the QEMU `virt` machine and sits next to the blk, console and net devices.

| Device         |         Base |     Size | PLIC source |
| :------------- | -----------: | -------: | ----------: |
| virtio-blk     | `0x10001000` | `0x1000` |           1 |
| virtio-console | `0x10002000` | `0x1000` |           2 |
| virtio-net     | `0x10003000` | `0x1000` |           3 |
| 16550 UART     | `0x10000000` |  `0x100` |           4 |
| virtio-rng     | `0x10004000` | `0x1000` |           5 |

The device models are address-independent. The board chooses the base, the size and the PLIC source.

## Shared transport

The transport implements the virtio-mmio legacy version 1 register layout. The register map is the same as the blk transport in [diskio.md](diskio.md).

The transport supports two queues. The rng model uses queue 0 only.

## Random number device

The rng device reports device ID 4. It advertises no feature bits and has no config space.

| Queue | Direction       | Purpose   |
| :---- | :-------------- | :-------- |
| 0     | device to guest | requestq  |

The driver places device-writable buffers in the requestq. The device fills each buffer with host entropy and reports the number of bytes written in the used ring. A buffer larger than `VIRTIO_RNG_BUFFER_SIZE` is filled in chunks. A device-readable descriptor returns an empty used entry.

## Host hook

The board defines the hook before including the umbrella header.

```c
#define RVE_DEV_VIRTIO_RNG_FILL(buffer, length) board_rng_fill(buffer, length)
```

The default hook fills the buffer with zeros. A board that boots a guest must define the hook with a real entropy source. The hook receives a pointer to a device buffer, not guest RAM.

## API

| Function                 | Purpose                                                    |
| :----------------------- | :--------------------------------------------------------- |
| `virtio_rng_init`        | Initializes the rng device and the shared transport.       |
| `virtio_rng_contains`    | Returns true for an address in the device range.           |
| `virtio_rng_process`     | Fills all available request buffers.                       |
| `virtio_rng_irq_pending` | Returns true when the used-ring interrupt is pending.      |
| `virtio_rng_read`        | Reads a transport register or the config space.            |
| `virtio_rng_write`       | Writes a transport register and processes a queue notify.  |

## Board integration

The board owns the entropy source, the base address and the PLIC source.

- Initialize the device with `virtio_rng_init`.
- Define `RVE_DEV_VIRTIO_RNG_FILL` to read host entropy.
- Route load and store accesses through `virtio_rng_read` and `virtio_rng_write`.
- Set PLIC source 5 pending from `virtio_rng_irq_pending`.

The device tree adds a `virtio,mmio` node at `0x10004000` with PLIC source 5.

```dts
virtio_mmio@10004000 {
    compatible = "virtio,mmio";
    reg = <0x00 0x10004000 0x00 0x1000>;
    interrupts = <0x05>;
    interrupt-parent = <0x05>;
};
```

## Guest integration

U-Boot binds the device through its virtio-rng driver. `CONFIG_VIRTIO_RNG` enables the driver and `CONFIG_CMD_RNG` enables the `rng` command for manual checks.

The guest needs these kernel options:

| Option                  | Value |
| :---------------------- | :---- |
| `CONFIG_VIRTIO_MMIO`    | `y`   |
| `CONFIG_HW_RANDOM`      | `y`   |
| `CONFIG_HW_RANDOM_VIRTIO` | `y` |

## Test plan

- Unity unit tests for the rng device: identity, features, config space, queue count, a single descriptor, a descriptor chain, a buffer larger than the host entropy buffer, a device-readable descriptor, the interrupt status and the address range.
- Boot U-Boot and run `rng list` and `rng 0 16`.
- Boot Linux and read `/proc/sys/kernel/random/entropy_avail` and `/dev/hwrng`.

## Acceptance criteria

- U-Boot no longer prints `No RNG device` during boot.
- U-Boot `rng list` reports a virtio-rng device and `rng 0 16` prints 16 bytes.
- Linux registers `virtio_rng.0` as a hardware random number generator.

## Constraints

- No `malloc`.
- No function pointers.
- No 64-bit division.
