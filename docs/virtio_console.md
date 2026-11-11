# Virtio console device model

## Purpose

The virtio console gives the Linux guest a console over virtio-mmio. The guest driver batches console output in virtqueues, so the model needs fewer MMIO traps and fewer host calls than the 16550 UART. The model also accepts host input through a fixed RX FIFO.

The model is header-only, `static inline`, and has no dynamic allocation. It shares the virtio-mmio transport with the virtio-blk model.

## Architecture

The transport is device-independent. The blk and console models are thin wrappers around it.

```
Linux guest
  virtio-console driver
    virtio-mmio legacy v1 (MMIO)
      PLIC source 2
        PLIC context 0
          mip.MEIP
            hart
```

The transport lives in `RiscvEmulatorDeviceVirtioMmio.h`. The blk model in `RiscvEmulatorDeviceVirtioBlk.h` and the console model in `RiscvEmulatorDeviceVirtioConsole.h` embed a `virtio_mmio_t` and add only their device-specific state and queue processing.

## Memory map

The console follows the QEMU `virt` machine and sits next to the blk device.

| Device         |         Base |     Size | PLIC source |
| :------------- | -----------: | -------: | ----------: |
| virtio-blk     | `0x10001000` | `0x1000` |           1 |
| virtio-console | `0x10002000` | `0x1000` |           2 |

The device models are address-independent. The board chooses the base, the size and the PLIC source.

## Shared transport

The transport implements the virtio-mmio legacy version 1 register layout. The register map is the same as the blk transport in [diskio.md](diskio.md).

The transport stores one state block per queue:

| Field            | Purpose                                    |
| :--------------- | :----------------------------------------- |
| `queue_num`      | Queue size written by the driver.          |
| `queue_align`    | Used-ring alignment written by the driver. |
| `queue_pfn`      | Descriptor table page frame number.        |
| `last_avail_idx` | Next available-ring entry to process.      |
| `used_idx`       | Next used-ring entry to write.             |

`QueueSel` selects the queue for the `QueueNum`, `QueueAlign` and `QueuePFN` registers. `QueueNumMax` reports the maximum size of the selected queue. `QueueNotify` reports the selected queue to the device wrapper.

The transport exposes these functions:

| Function                   | Purpose                                                        |
| :------------------------- | :------------------------------------------------------------- |
| `virtio_mmio_init`         | Initializes the transport, the identity and the config space.  |
| `virtio_mmio_read`         | Reads a register or the config space.                          |
| `virtio_mmio_write`        | Writes a register and reports a queue notification.            |
| `virtio_mmio_queue_layout` | Returns the descriptor table, available ring and used ring.    |
| `virtio_mmio_read_desc`    | Reads one descriptor from the descriptor table.                |
| `virtio_mmio_guest_read`   | Reads bytes from guest RAM.                                    |
| `virtio_mmio_guest_write`  | Writes bytes to guest RAM.                                     |
| `virtio_mmio_add_used`     | Adds one entry to the used ring and sets the interrupt status. |
| `virtio_mmio_irq_pending`  | Returns true when the used-ring interrupt is pending.          |

The transport has no function pointers. `virtio_mmio_write` returns the notified queue index through an out-parameter. The device wrapper dispatches with a `switch`.

## Console device

The console reports device ID 3 and no feature bits. Without `VIRTIO_CONSOLE_F_MULTIPORT` the driver uses one port and two queues.

| Queue | Direction       | Purpose                                      |
| :---- | :-------------- | :------------------------------------------- |
| 0     | device to guest | Console input. The driver pre-fills buffers. |
| 1     | guest to device | Console output.                              |

The config space is unused because the model offers no features. Reads return zero.

### Output path

The driver adds a descriptor chain with one or more device-readable buffers. The device walks the chain, calls `RVE_DEV_VIRTIO_CONSOLE_TX(buffer, length)` for each descriptor, adds the chain head to the used ring with the total length, and sets the interrupt status.

The hook receives a pointer into guest RAM. The board must not modify the buffer.

### Input path

The driver pre-fills queue 0 with device-writable buffers and notifies the device. The device does not consume those buffers until input is available.

`virtio_console_rx_push` appends one byte to a fixed RX FIFO. The FIFO has the same ring layout as the 16550 RX FIFO. When the FIFO is full, the model sets an overrun flag and drops the byte.

After a push, and on a queue 0 notification, the device drains the FIFO into the available buffers. For each buffer it writes at most the descriptor length, adds the used entry with the number of bytes written, and advances `last_avail_idx`. The driver re-fills the queue after it reads the data.

The FIFO size is `VIRTIO_CONSOLE_RX_FIFO_SIZE`, default 16 bytes.

## Host hook

The board defines the hook before including the umbrella header.

```c
#define RVE_DEV_VIRTIO_CONSOLE_TX(buffer, length) board_console_write(buffer, length)
```

The default hook discards the buffer.

## API

| Function                     | Purpose                                                   |
| :--------------------------- | :-------------------------------------------------------- |
| `virtio_console_init`        | Initializes the console and the shared transport.         |
| `virtio_console_read`        | Reads a transport register or the config space.           |
| `virtio_console_write`       | Writes a transport register and processes a queue notify. |
| `virtio_console_process`     | Processes one queue.                                      |
| `virtio_console_rx_push`     | Pushes one input byte into the RX FIFO and drains it.     |
| `virtio_console_irq_pending` | Returns true when the used-ring interrupt is pending.     |

## Kernel integration

The guest needs these changes:

- `CONFIG_VIRTIO_CONSOLE=y` in the kernel configuration.
- A `virtio,mmio` node at `0x10002000` with `interrupts = <2>` and the PLIC as interrupt parent.
- `console=hvc0` in the boot arguments.
- `earlycon=uart8250,mmio,0x10000000,1000000` stays on the 16550 because virtio is not available during the early boot.

The 16550 remains in the device tree for the early console and for diagnosis.

## Constraints

- No `malloc`.
- No function pointers.
- No 64-bit division.
- The device library does not know the core type. The board owns the `mip` bridge.
- The board owns the PLIC source assignment and the host console.
