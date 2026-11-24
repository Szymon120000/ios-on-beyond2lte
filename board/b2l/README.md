# beyond2lte board layer

Target: Samsung Galaxy S10+ `SM-G975F` (`beyond2lte`), Exynos 9820.

The XNU machine config is still named `SWEET` because the upstream ios-sweet
series gates its reusable providers (built-in libpthread, HFS, BearSSL,
AMFI/sandbox, IOSurface) on that name. `B2L_BOARD=1` selects the Exynos
board paths without renaming the machine config.

## Patch series

The board series is applied after `xnu/patches/`:

`patches/0082-b2l-board-and-gicv2.patch`:

- `SWEET.h`: `B2L_BOARD` constants (GICv2 bases, timer PPI 27, 26 MHz,
  0xCA000000 framebuffer), no `HAS_GIC_V3`, and 16K pages retained so the
  proven ios-sweet bootstrap-table code stays unchanged.
- `board_config.h`: three CPU clusters (A55/A75/M4) for B2L.
- `pexpert/arm/pe_fiq.c`: GICv2 distributor and CPU-interface setup, Group 1
  so the timer and SGIs arrive as IRQs.
- `pexpert/arm/b2l_gicv2.h`: acknowledge/EOI interface shared with `sleh_irq`.
- `osfmk/arm64/sleh.c`: GICv2 IRQ dispatch (timer, SGI, device IRQs).
- `start.s`, `sweet_early.c`, `arm_vm_init.c`: 0xCA000000 / 1440x3040 early
  framebuffer instead of sweet's 0x9c000000 / 1080x2400.

`patches/0083-xcode16-iig-iorpc-kernelcontent.patch`:

- Adds the `kernelContent` member to `IORPC` under `KERNEL`, matching what
  Xcode 16's `iig` emits. The GitHub runners have no Xcode 15.

`patches/0084-b2l-platform-personalities.patch`:

- `KernelConfigTables.cpp`: B2L matches `SweetPlatformExpert` on `D22AP` and
  `SweetCPU` on `samsung,exynos9820`, the strings in the generated ADT.
- `IOKitBSDInit.cpp`: `ufsroot0` stays a sweet-only boot argument, so a B2L
  boot cannot enter the Qualcomm UFS/GENI/RPMh/USB probe path.

## Known gaps in this first slice

- 16K pages are retained from the SWEET machine config. The Exynos cores
  advertise the granule, but it must be confirmed on device before the
  bootstrap tables are trusted. A later 4K switch needs matching
  `start.s`/`arm_vm_init.c` changes.
- The early framebuffer assumes the firmware leaves a 1440x3040 BGRA surface
  at `0xCA000000`. Confirm against the live beyond2lte GOP before booting.
- Qualcomm-only sweet drivers compile in, but their entry point
  (`sweet_ufs_rootdev`) is no longer reachable under `B2L_BOARD`. The
  IOResources services that do start (KeyStore, Watchdog, Surface, HID
  personalities) stay in RAM and only touch hardware behind boot arguments.
- The Apple device-tree generator (`tools/b2l/mkadt.py`) is a contract, not
  yet consumed by a loader.
- The new loader writes the 8796 `boot_args` layout and is built by CI, but
  it has not run on the phone. The older beyond2lte XNU 12377 image is known
  to reach SpringBoard with charging; that is a different kernel tree and
  does not validate the 8796 loader.

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
