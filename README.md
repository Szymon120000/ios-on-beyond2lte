# ios-on-beyond2lte

Bringing the `ios-sweet` Apple XNU / iOS 16 bring-up to the Samsung Galaxy S10+
(`beyond2lte`, Exynos 9820).

This is a **new port**, not a continuation of the earlier
[`beyond2lte-ios`](https://github.com/Szymon120000/beyond2lte-ios) XNU 12377
tree. The kernel and userspace base is
[`dipa-studio/ios-sweet`](https://github.com/dipa-studio/ios-sweet): XNU
8796 (the iOS 16 generation) plus its iPhone X (D22AP) iOS 16.7.16 userland.
The repository keeps ios-sweet's full git history under `upstream`; the B2L
work lives in new, reviewable board patches and CI.

> Status: **kernel bring-up scaffold**. The B2L board layer, GICv2 interrupt
> path and Apple device-tree contract are in place and build in CI, but no
> device boot of this new tree is claimed yet. The working reference for
> Exynos hardware is kept in `beyond2lte-ios`; code is ported deliberately,
> not merged wholesale.

## Why a new tree

`ios-sweet` already reaches the iOS 16 home screen on SM7150. Its XNU patch
series is hardware-light: the reusable parts are the non-Apple ARM64 machine
config, the built-in libpthread/HFS/BearSSL providers, AMFI/sandbox
compatibility, IOSurface/display services and userspace bring-up. The Exynos
side needs a new platform layer:

| Area | sweet (SM7150) | beyond2lte (Exynos 9820) |
| --- | --- | --- |
| Interrupts | GICv3, Group 1 IRQ, timer INTID 19 | GICv2 (GIC-400), Group 1 IRQ, timer INTID 27 |
| Timebase | 19.2 MHz | 26 MHz |
| CPUs | 6x A55 + 2x A76, PSCI | 4x A55 + 2x A75 + 2x M4, PSCI |
| RAM | upper bank at 1 GiB | bank at `0x900000000` (4056 MiB usable) |
| Boot chain | Android `fastboot boot` + shim | Samsung S-Boot + UEFI firmware |
| Panel | 1080x2400, 4320-byte stride | 1440x3040 native, iPhone X 1125x2436 presented |
| Touch / power | Goodix GT9896, PM6150 | sec_ts, MAX77705 |
| Storage | Qualcomm UFS | Exynos UFS |

The first board layer (`board/b2l/patches/0082-...`) adds `B2L_BOARD`, the
Exynos GICv2 distributor/CPU-interface path, timer PPI 27, the 1440x3040
early framebuffer and three CPU clusters. The XNU machine config keeps its
upstream name `SWEET` so the ios-sweet provider patches apply unchanged; a
B2L build is selected with `B2L_BOARD=1`.

## Build

The kernel builds on GitHub's macOS runners through
[`blacktop/darwin-xnu-build`](https://github.com/blacktop/darwin-xnu-build).
`.github/workflows/b2l.yml` applies `xnu/patches/` and then
`board/b2l/patches/`, and builds with `B2L_BOARD=1`:

```sh
gh workflow run b2l.yml -R Szymon120000/ios-on-beyond2lte
gh run download RUN_ID -R Szymon120000/ios-on-beyond2lte -n b2l-kernel -D xnu/out
```

The Apple device-tree contract for the future B2L XNU-8796 loader can be
generated and checked locally:

```sh
python3 tools/b2l/mkadt.py --check
python3 tools/b2l/mkadt.py out/b2l.adt --dump
```

## Roadmap

1. **Kernel build** — B2L board define, GICv2, timer, early console, CI. *(in progress)*
2. **Boot handoff** — a B2L XNU-8796 loader: boot-args ABI, ADT, RAM profile,
   4 KiB/16 KiB page decision, framebuffer handoff.
3. **Storage + userspace** — Exynos UFS driver, HFS root, launchd, dyld cache.
4. **Display + input** — IOSurface/AppleCLCD presentation, sec_ts touch,
   MAX77705 power/buttons.
5. **SMP** — PSCI CPU_ON for A75/M4 clusters; three-cluster scheduler.
6. **UI stability** — SpringBoard/backboardd, CoreAnimation software rendering.

See [docs/PORT-PLAN.md](docs/PORT-PLAN.md) and
[board/b2l/README.md](board/b2l/README.md) for the detailed contracts and the
open hardware questions.

## Upstream relationship

- `origin` — this repository.
- `upstream` — `dipa-studio/ios-sweet` (MIT).
- `beyond2lte-ios` — kept as the working Exynos reference; not merged.

Nothing Apple-proprietary is distributed here: the IPSW, shared cache,
DeviceTree (D22AP) and partition backups stay out of git.

## Credits

Apple's open-source XNU, libpthread, HFS and IOHIDFamily releases; BearSSL;
[blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build);
[ios-sweet](https://github.com/dipa-studio/ios-sweet) for the working iOS 16
bring-up; [Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium) and
the beyond2lte UEFI port for the Samsung side; Qualcomm/Samsung published
kernel sources for hardware facts. iOS itself is Apple's.
