# Virtio network device model

## Purpose

The virtio network device gives the Linux guest an Ethernet interface over virtio-mmio. The guest driver batches packets in virtqueues, so the model needs fewer MMIO traps than a register-level NIC. The board connects the device to a host TAP interface.

The model is header-only, `static inline`, and has no dynamic allocation. It shares the virtio-mmio transport with the virtio-blk and virtio-console models.

## Architecture

The transport is device-independent. The blk, console and net models are thin wrappers around it.

```
Linux guest
  virtio-net driver
    virtio-mmio legacy v1 (MMIO)
      PLIC source 3
        PLIC context 0
          mip.MEIP
            hart
```

The transport lives in `RiscvEmulatorDeviceVirtioMmio.h`. The net model in `RiscvEmulatorDeviceVirtioNet.h` embeds a `virtio_mmio_t` and adds the config space, the RX push and the TX gather.

## Memory map

The net device follows the QEMU `virt` machine and sits next to the blk and console devices.

| Device         |         Base |     Size | PLIC source |
| :------------- | -----------: | -------: | ----------: |
| virtio-blk     | `0x10001000` | `0x1000` |           1 |
| virtio-console | `0x10002000` | `0x1000` |           2 |
| virtio-net     | `0x10003000` | `0x1000` |           3 |

The device models are address-independent. The board chooses the base, the size and the PLIC source.

## Shared transport

The transport implements the virtio-mmio legacy version 1 register layout. The register map is the same as the blk transport in [diskio.md](diskio.md).

The transport supports two queues. That is exactly the receiveq and the transmitq of virtio-net. The model does not advertise a control queue or multiqueue.

## Network device

The net device reports device ID 1. It advertises two feature bits:

| Feature              | Bit | Purpose                          |
| :------------------- | --: | :------------------------------- |
| `VIRTIO_NET_F_MAC`   |   5 | The config space holds the MAC.  |
| `VIRTIO_NET_F_STATUS`|  16 | The config space holds the link. |

The model does not advertise checksum offloads, segmentation offloads, mergeable buffers, a control queue or multiqueue. The guest computes checksums in software and uses one buffer per packet.

### Config space

The config space is 8 bytes.

| Offset | Field    | Value                    |
| :----- | :------- | :----------------------- |
| `0x00` | `mac[6]` | The MAC from the board.  |
| `0x06` | `status` | `VIRTIO_NET_S_LINK_UP`.  |

### Queues

| Queue | Direction       | Purpose    |
| :---- | :-------------- | :--------- |
| 0     | device to guest | receiveq   |
| 1     | guest to device | transmitq  |

### Packet header

Every packet starts with the legacy virtio-net header. The header is 10 bytes and reads as zero because the model offers no offloads.

| Offset | Field         |
| :----- | :------------ |
| `0`    | `flags`       |
| `1`    | `gso_type`    |
| `2`    | `hdr_len`     |
| `4`    | `gso_size`    |
| `6`    | `csum_start`  |
| `8`    | `csum_offset` |

### Transmit path

The driver adds a descriptor chain. The first descriptor is device-readable and holds the 10-byte header followed by the start of the Ethernet frame. Later descriptors continue the frame.

The device skips the header, gathers the frame into a fixed buffer, calls `RVE_DEV_VIRTIO_NET_TX(buffer, length)`, adds the chain head to the used ring with length 0 and sets the interrupt status. A frame larger than `VIRTIO_NET_BUFFER_SIZE` is dropped.

### Receive path

The driver pre-fills queue 0 with device-writable buffers. `virtio_net_rx_push` takes the next available buffer, writes the 10-byte header and the frame, adds the used entry with the number of bytes written and sets the interrupt status.

A packet without a buffer is dropped. A buffer smaller than the header plus the frame is returned to the used ring with length 0. The driver re-fills the queue after it reads the data.

The board polls the TAP file descriptor and calls `virtio_net_rx_push` for each frame. The device does not read the TAP file descriptor.

## Host hook

The board defines the hook before including the umbrella header.

```c
#define RVE_DEV_VIRTIO_NET_TX(buffer, length) board_net_write(buffer, length)
```

The default hook discards the frame. The hook receives a pointer to a device buffer, not guest RAM.

## API

| Function                | Purpose                                                    |
| :---------------------- | :--------------------------------------------------------- |
| `virtio_net_init`       | Initializes the net device and the shared transport.       |
| `virtio_net_read`       | Reads a transport register or the config space.            |
| `virtio_net_write`      | Writes a transport register and processes a TX notify.     |
| `virtio_net_rx_push`    | Delivers one frame to the next receiveq buffer.            |
| `virtio_net_irq_pending`| Returns true when the used-ring interrupt is pending.      |

## Board integration

The board owns the TAP file descriptor, the MAC address and the PLIC source.

- Open the TAP interface by name. The native board uses `-N <ifname>`.
- Poll the TAP file descriptor in the main loop.
- Call `virtio_net_rx_push` for each frame.
- Define the TX hook to write the frame to the TAP file descriptor.
- Set PLIC source 3 pending from `virtio_net_irq_pending`.
- Pass the MAC address to `virtio_net_init`.

The user creates the TAP interface before the emulator starts.

```sh
sudo ip tuntap add dev tap0 mode tap user "$USER"
sudo ip link set tap0 up
sudo ip addr add 192.168.100.1/24 dev tap0
sudo ip -6 addr add fd00:100::1/64 dev tap0
```

The guest configures the interface by hand. No init script is needed.

```sh
ip link set eth0 up
ip addr add 192.168.100.2/24 dev eth0
ip -6 addr add fd00:100::2/64 dev eth0
ping 192.168.100.1
ping -6 fd00:100::1
```

## Kernel integration

The guest needs these kernel options:

| Option              | Value |
| :------------------ | :---- |
| `CONFIG_NET`        | `y`   |
| `CONFIG_INET`       | `y`   |
| `CONFIG_IPV6`       | `y`   |
| `CONFIG_NETDEVICES` | `y`   |
| `CONFIG_VIRTIO_NET` | `y`   |
| `CONFIG_VIRTIO_MMIO` | `y`   |

The guest needs these busybox applets:

| Option                       | Value |
| :--------------------------- | :---- |
| `CONFIG_IP`                  | `y`   |
| `CONFIG_IPADDR`              | `y`   |
| `CONFIG_IPLINK`              | `y`   |
| `CONFIG_FEATURE_IP_ADDRESS` | `y`   |
| `CONFIG_FEATURE_IP_LINK`     | `y`   |
| `CONFIG_FEATURE_IPV6`        | `y`   |
| `CONFIG_PING`                | `y`   |
| `CONFIG_PING6`               | `y`   |

The device tree adds a `virtio,mmio` node at `0x10003000` with PLIC source 3.

```dts
virtio_mmio@10003000 {
    compatible = "virtio,mmio";
    reg = <0x00 0x10003000 0x00 0x1000>;
    interrupts = <0x03>;
    interrupt-parent = <0x05>;
};
```

## Test plan

- Unity unit tests for the net device: identity, features, config space, queue setup, a single-descriptor transmit, a multi-descriptor transmit, a receive push, a receive drop without a buffer, a receive drop with a small buffer, the interrupt status and the address range.
- Boot the guest, bring `eth0` up, assign a static IPv4 and IPv6 ULA address and ping the host TAP address over both families.

## Acceptance criteria

- The guest reports `eth0` with the configured MAC.
- A ping from the guest to the host TAP IPv4 address receives a reply.
- A ping from the guest to the host TAP IPv6 ULA address receives a reply.
- A ping from the host to the guest IPv4 and IPv6 ULA address receives a reply.

## Constraints

- No `malloc`.
- No function pointers.
- No 64-bit division.
- The device library does not know the core type. The board owns the `mip` bridge.
- The board owns the PLIC source assignment, the TAP file descriptor and the MAC address.
- The model is disabled on 8-bit targets with `RVE_DEV_VIRTIO_NET=0`.
