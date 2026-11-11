# Documentation Index

The general coding rules are shared with the ISA core and live in [RISC-V-emulator/docs/](../../RISC-V-emulator/docs/):

- [decode-once.md](../../RISC-V-emulator/docs/decode-once.md) — Decode-once principle: every instruction decoder field is read exactly once per executed instruction
- [funnel-strategy.md](../../RISC-V-emulator/docs/funnel-strategy.md) — Funnel strategy: merging semantically identical instruction variants into one shared handler
- [no-bit-magic.md](../../RISC-V-emulator/docs/no-bit-magic.md) — No bit-magic rule: express operations in natural C form, signedness through casts
- [no-lookup-tables.md](../../RISC-V-emulator/docs/no-lookup-tables.md) — No lookup tables on AVR: `static const` costs RAM, use a `switch()` or `PROGMEM`
- [measure-once-cut-twice.md](../../RISC-V-emulator/docs/measure-once-cut-twice.md) — Measure once, cut twice: size changes are judged on a per-environment delta table, never a single build
- [noinline-cold-path.md](../../RISC-V-emulator/docs/noinline-cold-path.md) — Noinline on cold paths: when a function may be marked `__attribute__((noinline))`

## Device models

- [clint.md](clint.md) - CLINT register map, time source, latch behaviour and the `mip` bridge.
- [uart16550.md](uart16550.md) - 16550 register map, FIFO, interrupt priority and host hooks.
- [diskio.md](diskio.md) - Disk I/O design: virtio-mmio legacy v1, virtio-blk, SiFive PLIC, host hooks and ext4.
