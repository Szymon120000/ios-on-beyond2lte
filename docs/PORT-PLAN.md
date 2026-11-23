# Port plan: ios-sweet (XNU 8796 / iOS 16) on beyond2lte

## Ground rules

- Start from ios-sweet, not from the earlier beyond2lte-ios tree. The old tree
  is a hardware reference only; code arrives as reviewed patches with a test
  or a documented hardware source.
- Keep the ios-sweet upstream patches intact. Board changes go to
  `board/b2l/patches/` and are selected with `B2L_BOARD=1`.
- No device boot claim until a recovered console log or a screen shows the
  expected stage.

## Milestone 1: compilable B2L kernel (current)

- [x] Repository seeded from ios-sweet history; upstream remote recorded.
- [x] `B2L_BOARD` machine define and Exynos constants.
- [x] GICv2 distributor/CPU interface and Group 1 IRQ dispatch.
- [x] Timer PPI 27, 26 MHz counter, three-cluster CPU topology.
- [x] Early framebuffer moved to the beyond2lte reservation.
- [x] Apple device-tree contract generator with self-checks.
- [ ] CI build green on macOS 14 with `B2L_BOARD=1`.
- [ ] Guard Qualcomm-only sweet drivers behind `!B2L_BOARD`.

## Milestone 2: loader and first console

- Decide 16K vs 4K pages. 16K keeps ios-sweet's bootstrap math; 4K matches
  the existing Exynos port and needs new table code.
- Write a B2L loader for the XNU 8796 `boot_args` ABI (the old UEFI loader is
  12377-only). It must publish `/chosen`, `/chosen/memory-map`, `/defaults`,
  `/cpus`, `/arm-io`, the panic-log region and the GICv2 node.
- Package through Samsung S-Boot (BOOT partition) and recover the kernel
  console over the persistent log or debug UART.
- Acceptance: first `B2L` line and timer ticks on device.

## Milestone 3: storage and userspace

- Port the Exynos UFS host driver (GPT discovery, read/write, flush).
- HFS+ root in USERDATA with journal replay; keep ios-sweet's HFS provider.
- launchd + dyld shared cache from the iPhone X 16.7.16 image.
- Acceptance: launchd and the system daemons start; log ring readable.

## Milestone 4: display and input

- IOSurface + IOMobileFramebuffer presentation at 1125x2436.
- sec_ts touch over HSI2C; MAX77705 charger/fuel gauge and buttons.
- Acceptance: lock screen and touch.

## Milestone 5: SMP and UI

- PSCI CPU_ON for A75/M4; per-cluster placement.
- SpringBoard/backboardd stability, software CoreAnimation performance.
- Acceptance: home screen, Settings, Control Center usable.

## Open questions

1. Does S-Boot hand over a usable GOP at 0xCA000000 on every panel/firmware?
2. Does the Exynos 9820 EL1 support 16K translation granules in practice?
3. Can the 8796 boot-args ABI be satisfied by a small loader without pulling
   in the 12377 UEFI implementation wholesale?
4. Which secure-world carve-outs must remain reserved in the ADT memory map?
