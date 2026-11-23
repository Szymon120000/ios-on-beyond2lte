# beyond2lte board layer

Target: Samsung Galaxy S10+ `SM-G975F` (`beyond2lte`), Exynos 9820.

The XNU machine config is still named `SWEET` because the upstream ios-sweet
series gates its reusable providers (built-in libpthread, HFS, BearSSL,
AMFI/sandbox, IOSurface) on that name. `B2L_BOARD=1` selects the Exynos
board paths without renaming the machine config.

## Patch series

`patches/0082-b2l-board-and-gicv2.patch` is applied after `xnu/patches/`:

- `SWEET.h`: `B2L_BOARD` constants (GICv2 bases, timer PPI 27, 26 MHz,
  0xCA000000 framebuffer), no `HAS_GIC_V3`, no 16K-page-specific timer path.
- `board_config.h`: three CPU clusters (A55/A75/M4) for B2L.
- `pexpert/arm/pe_fiq.c`: GICv2 distributor and CPU-interface setup, Group 1
  so the timer and SGIs arrive as IRQs.
- `pexpert/arm/b2l_gicv2.h`: acknowledge/EOI interface shared with `sleh_irq`.
- `osfmk/arm64/sleh.c`: GICv2 IRQ dispatch (timer, SGI, device IRQs).
- `start.s`, `sweet_early.c`, `arm_vm_init.c`: 0xCA000000 / 1440x3040 early
  framebuffer instead of sweet's 0x9c000000 / 1080x2400.

## Known gaps in this first slice

- 16K pages are still selected through the SWEET machine config. Exynos
  hardware supports the granule, but this must be validated; a 4K switch
  needs the matching bootstrap-table changes in `start.s`/`arm_vm_init.c`.
- The early framebuffer assumes the firmware leaves a 1440x3040 BGRA surface
  at `0xCA000000`. Confirm against the live beyond2lte GOP before booting.
- Qualcomm-only sweet drivers (UFS, GENI touch, SPMI keys, RPMh, USB) still
  probe their addresses unless individually disabled. Do **not** boot this
  kernel on the phone until they are guarded by `B2L_BOARD`.
- The Apple device-tree generator (`tools/b2l/mkadt.py`) is a contract, not
  yet consumed by a loader.

## Hardware contract

| Item | Value |
| --- | --- |
| RAM arena | `0x900000000` + `0xFD800000` (panic log carved from top) |
| GIC distributor | `0x10101000`, 4 KiB |
| GIC CPU interface | `0x10102000`, 8 KiB |
| Timer | virtual timer PPI 27, 26 MHz counter |
| CPU clusters | A55 `Aff1=0`, A75 `Aff1=1`, M4 `Aff1=2` |
| Framebuffer reservation | `0xCA000000`, 20 MiB |
| Native panel | 1440x3040, 5760-byte stride |
| Presented iOS panel | 1125x2436 (iPhone X, D22AP) |
