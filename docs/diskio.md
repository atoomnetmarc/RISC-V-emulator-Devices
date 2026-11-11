# Disk I/O design

## Purpose

This document records the design for a block device in the RISC-V emulator family. The target is the `rv32i-linux` image on the native board. The design adds a writable disk to the Linux guest and keeps the device models in this library.

## Goals

- A writable ext4 root filesystem on a host file.
- Reusable, header-only device models with host hooks.
- An unmodified Linux kernel.

## Scope

- A local host file backs the disk.
- The disk image carries a GPT partition table with one ext4 partition.
- The disk is present at boot.
- The device model is virtio-blk over virtio-mmio.

## Decisions

| #   | Decision                                       | Rationale                                                                                                                                          |
| --- | :--------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | virtio-mmio legacy v1 with `virtio-blk`.       | The kernel enables `VIRTIO_MMIO`; version 1 is the smallest queue interface; `virtio-blk` is the standard block device.                            |
| 2   | SiFive PLIC with a device tree node.           | The kernel enables `SIFIVE_PLIC`; the CPU-INTC maps only local interrupts; the PLIC is the standard external controller; the core supports `MEIP`. |
| 3   | Host-hook macros; the board owns the file I/O. | The pattern matches the UART and SYSCON hooks; the library stays header-only, portable and `stdio`-free.                                           |
| 4   | ext4 with `EXT4_USE_FOR_EXT2`.                 | ext4 is the standard; the host assembles the image; the kernel mounts ext4 as the rootfs.                                                          |
| 5   | GPT partition table with one ext4 partition.   | The root device is `/dev/vda1`; the layout matches a normal disk and leaves room for the backup GPT.                                               |

## Architecture

The guest sees a virtio-mmio transport at a fixed MMIO range and a virtio-blk device behind it. The transport uses the legacy version 1 register layout. A SiFive PLIC delivers the virtio interrupt to the hart as `mip.MEIP`. The board owns the host file and the guest memory.

```
Linux guest
  virtio-blk driver
    virtio-mmio legacy v1 (MMIO)
      PLIC source 1
        PLIC context 0
          mip.MEIP
            hart
```

## Memory map

The ranges follow the QEMU `virt` machine and leave the existing devices untouched.

| Device      |         Base |       Size |
| :---------- | -----------: | ---------: |
| PLIC        | `0x0C000000` | `0x400000` |
| virtio-mmio | `0x10001000` |   `0x1000` |

The UART stays at `0x10000000` with size `0x100`, and the virtio-mmio range starts at `0x10001000`.

## virtio-mmio legacy v1

The device implements the version 1 register layout. The kernel writes the feature selector registers; a legacy device ignores them and returns the same 32-bit feature word.

| Offset  | Register           | Access | Notes                               |
| :------ | :----------------- | :----- | :---------------------------------- |
| `0x000` | `MagicValue`       | R      | `0x74726976` (`virt`).              |
| `0x004` | `Version`          | R      | `1`.                                |
| `0x008` | `DeviceID`         | R      | `2` (block).                        |
| `0x00C` | `VendorID`         | R      | Board value.                        |
| `0x010` | `HostFeatures`     | R      | 32-bit feature word.                |
| `0x014` | `HostFeaturesSel`  | W      | Ignored.                            |
| `0x020` | `GuestFeatures`    | W      | 32-bit driver feature word.         |
| `0x024` | `GuestFeaturesSel` | W      | Ignored.                            |
| `0x028` | `GuestPageSize`    | W      | `4096`.                             |
| `0x030` | `QueueSel`         | W      | Queue index.                        |
| `0x034` | `QueueNumMax`      | R      | Maximum queue size.                 |
| `0x038` | `QueueNum`         | W      | Queue size.                         |
| `0x03C` | `QueueAlign`       | W      | Used-ring alignment.                |
| `0x040` | `QueuePFN`         | RW     | Descriptor table page frame number. |
| `0x050` | `QueueNotify`      | W      | Queue index.                        |
| `0x060` | `InterruptStatus`  | R      | Bit 0 is the used-ring interrupt.   |
| `0x064` | `InterruptACK`     | W      | Clears the written bits.            |
| `0x070` | `Status`           | RW     | Device status.                      |
| `0x100` | Config space       | R      | `virtio_blk_config`.                |

The descriptor table starts at `QueuePFN << 12`. The available ring follows the descriptor table. The used ring starts at the next `QueueAlign` boundary following the available ring.

## virtio-blk

The device advertises one request queue. The config space reports the disk capacity in 512-byte sectors and a block size of 512 bytes. The remaining geometry fields read as zero.

A request is a descriptor chain with a header, one or more data buffers and a status byte. The header holds the request type, a reserved word and the sector number. The device reads the header, performs the host I/O, writes the data and the status, adds the chain head to the used ring and sets `InterruptStatus` bit 0.

| Request type          | Value | Action                      |
| :-------------------- | ----: | :-------------------------- |
| `VIRTIO_BLK_T_IN`     |   `0` | Read from the host file.    |
| `VIRTIO_BLK_T_OUT`    |   `1` | Write to the host file.     |
| `VIRTIO_BLK_T_FLUSH`  |   `4` | Flush the host file.        |
| `VIRTIO_BLK_T_GET_ID` |   `8` | Return a device identifier. |

The status byte is `0` for success, `1` for an I/O error and `2` for an unsupported request.

## PLIC

The PLIC model has one context for the M-mode hart. The kernel's PLIC driver uses context 0 for that hart.

| Range                                         | Register       | Notes                                                      |
| :-------------------------------------------- | :------------- | :--------------------------------------------------------- |
| `0x0000 + 4 * source`                         | Priority       | One word per source.                                       |
| `0x1000 + 4 * (source / 32)`                  | Pending        | One bit per source.                                        |
| `0x2000 + 0x80 * context + 4 * (source / 32)` | Enable         | One bit per source.                                        |
| `0x200000 + 0x1000 * context`                 | Threshold      | Priority threshold.                                        |
| `0x200004 + 0x1000 * context`                 | Claim/Complete | Claim returns the source; complete clears the pending bit. |

The virtio-mmio interrupt line connects to source 1. The board sets the pending bit while the virtio device reports an interrupt. `plic_irq_pending` returns true when an enabled pending source has a priority above the threshold. The board bridges that result into `mip.MEIP` (bit 11) in `RiscvDevicesTick`.

## Host hooks

The board defines the hooks ahead of the umbrella header include. The device model uses the host hooks for all file I/O.

```c
#define RVE_DEV_VIRTIO_BLK_READ(sector, buffer, count)  board_disk_read(sector, buffer, count)
#define RVE_DEV_VIRTIO_BLK_WRITE(sector, buffer, count) board_disk_write(sector, buffer, count)
#define RVE_DEV_VIRTIO_BLK_FLUSH()                      board_disk_flush()
```

The board implements the hooks with `pread`, `pwrite` and `fsync` on the disk file descriptor. The device model holds a pointer to guest RAM and the RAM base and size, so it can read the descriptor table and the data buffers.

## Device model API

The models follow the existing device style: a plain instance, `static inline` functions, static storage and direct calls.

```c
typedef struct {
    uint32_t base;
    uint32_t size;
    uint8_t *memory;
    uint32_t memory_base;
    uint32_t memory_size;
    uint64_t capacity;
    /* Queue and interrupt state. */
} virtio_blk_t;

void virtio_blk_init(virtio_blk_t *dev, uint32_t base, uint32_t size,
                     uint8_t *memory, uint32_t memory_base,
                     uint32_t memory_size, uint64_t capacity);
bool virtio_blk_read(virtio_blk_t *dev, uint32_t address, void *destination, uint8_t length);
bool virtio_blk_write(virtio_blk_t *dev, uint32_t address, const void *source, uint8_t length);
bool virtio_blk_irq_pending(const virtio_blk_t *dev);
```

```c
typedef struct {
    uint32_t base;
    uint32_t size;
    uint32_t pending;
    uint32_t enable;
    uint32_t threshold;
    /* Claim state. */
} plic_t;

void plic_init(plic_t *plic, uint32_t base, uint32_t size);
bool plic_read(plic_t *plic, uint32_t address, void *destination, uint8_t length);
bool plic_write(plic_t *plic, uint32_t address, const void *source, uint8_t length);
void plic_set_pending(plic_t *plic, uint8_t source, bool pending);
bool plic_irq_pending(const plic_t *plic);
```

Each model has a switch (`RVE_DEV_VIRTIO_BLK`, `RVE_DEV_PLIC`) so a board can leave it out.

## Board integration

The native board adds the two ranges to `RiscvDevicesLoad` and `RiscvDevicesStore`, initializes the instances, and bridges the PLIC line into `mip.MEIP` in `RiscvDevicesTick`. The board opens the disk file, checks that its size is a multiple of 512 bytes, and passes the sector count to `virtio_blk_init`.

The emulator command line gains a disk option, for example `-D <file>`, next to the existing `-b <dtb>` option. The option opens the file read/write and attaches it to the virtio-blk device.

## Kernel configuration

The kernel configuration adds the block layer, the virtio block driver and the ext4 filesystem. The virtio-mmio and PLIC drivers are already enabled.

| Option                     | Value |
| :------------------------- | :---- |
| `CONFIG_BLOCK`             | `y`   |
| `CONFIG_VIRTIO_BLK`        | `y`   |
| `CONFIG_EXT4_FS`           | `y`   |
| `CONFIG_EXT4_USE_FOR_EXT2` | `y`   |
| `CONFIG_JBD2`              | `y`   |
| `CONFIG_CRC16`             | `y`   |
| `CONFIG_CRC32`             | `y`   |
| `CONFIG_FS_IOMAP`          | `y`   |
| `CONFIG_BUFFER_HEAD`       | `y`   |

## Device tree

The device tree adds a PLIC node and a virtio-mmio node. The PLIC node connects to the CPU-INTC external interrupt. The virtio-mmio node uses PLIC source 1.

```dts
plic@c000000 {
    compatible = "sifive,plic-1.0.0";
    reg = <0x00 0x0c000000 0x00 0x400000>;
    #interrupt-cells = <0x01>;
    interrupt-controller;
    riscv,ndev = <0x1f>;
    interrupts-extended = <0x02 0x0b>;
    phandle = <0x05>;
};

virtio_mmio@10001000 {
    compatible = "virtio,mmio";
    reg = <0x00 0x10001000 0x00 0x1000>;
    interrupts = <0x01>;
    interrupt-parent = <0x05>;
};
```

The `0x02` phandle is the existing CPU-INTC node. The `0x0b` value is the machine external interrupt.

## Buildroot

The root filesystem comes from the Buildroot `rootfs.tar`. The disk image is assembled on the host: a GPT partition table with one ext4 partition, populated from the rootfs tarball.

The kernel command line is `root=/dev/vda1 rw rootfstype=ext4 rdinit=/nonexistent`. The `rdinit=/nonexistent` value makes the kernel skip the initramfs `/init` and mount the disk as the root.

## Test plan

- Unity unit tests for the PLIC: priority, pending, enable, threshold, claim and complete.
- Unity unit tests for virtio-blk: magic, version, device ID, feature negotiation, queue setup, a read request, a write request and the interrupt status.
- Boot the initramfs image with a blank data disk, create an ext4 filesystem on `/dev/vda`, mount it, write a file and verify the host file.
- Boot with `root=/dev/vda1`, write a file in `/`, reboot and verify that the file survives.

## Acceptance criteria

- Linux boots from the initramfs, `/dev/vda` exists, `dd` writes to the disk, the host file changes and a remount shows the data.
- Linux boots from `/dev/vda1`, the rootfs is writable, a file created in `/` survives a reboot and the host file contains the change.

## Risks and open points

- The kernel supports virtio-mmio version 1; a future kernel can drop version 1.
- The virtio-blk config space must stay consistent with the host file size.
- The host file size must be a multiple of 512 bytes.
- The PLIC bridge runs on every instruction, so it must stay cheap.
- The PLIC and virtio-blk models are large for the 8-bit targets. The per-device switches keep them out of those builds.
