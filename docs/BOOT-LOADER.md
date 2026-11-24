# B2L loader and BOOT payload

## Why a new loader is required

The verified beyond2lte UEFI XNU loader targets XNU 12377. Its `boot_args`
structure is 1152 bytes with `BOOT_LINE_LENGTH 1024`. XNU 8796
(`pexpert/pexpert/arm64/boot.h`) uses `BOOT_LINE_LENGTH 608` and a 736-byte
structure. The offsets through `CommandLine` are identical, but
`bootFlags` moves from 1136 to 720 and `memSizeActual` from 1144 to 728, so a
12377 loader silently writes the wrong fields for this kernel.

`boot/loader/boot_args_8796.h` is the checked layout. The board-contract CI
job compiles it with `-Werror`, so any accidental offset change fails before a
device is involved.

## Boot chain

```
S-Boot -> BootShim -> beyond2lte UEFI (external) -> B2L XnuLoader -> XNU 8796
```

The UEFI firmware remains an external dependency, as in beyond2lte-ios. This
repo will supply the loader application and the BOOT payload; it does not
duplicate the Mu-Silicium platform port.

## BOOT payload trailer

`tools/b2l/mkb2lpayload.py` writes the trailer that follows the Android kernel
inside the BOOT image:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | `B2LXNU03` |
| 8 | 4 | version 3 |
| 12 | 4 | header size 304 |
| 16 | 8 | kernel size |
| 24 | 8 | ADT size |
| 32 | 8 | kernel offset 304 |
| 40 | 8 | ADT offset, 4 KiB aligned after the kernel |
| 48 | 256 | `/chosen` random seed |
| 304 | ... | thin arm64 XNU Mach-O |
| ... | ... | page padding |
| ... | ... | Apple device tree from `tools/b2l/mkadt.py` |

Version 3 carries the ADT in the image instead of rebuilding it in the loader.
`mkadt.py` stays the single source of truth, and the loader only has to copy
the blob and publish its virtual address in `boot_args.deviceTreeP`.

`tools/b2l/package-boot.py` wraps that payload in an Android BOOT v1 image:

```
Android kernel area = BootShim + 4 MiB UEFI FD + reference DTB
trailer             = B2LXNU03 payload
```

The trailer starts at `PAGE + align(kernel_size)`, matching the existing
firmware convention. Both tools have `--check`/`--verify` and a self-test that
runs in the board-contract CI job.

## Loader responsibilities

1. Find the Android BOOT partition and parse the v1 header and trailer.
2. Validate the Mach-O (`0xfeedfacf`, `CPU_TYPE_ARM64`, `MH_EXECUTE`) and every
   segment before copying; reject anything else.
3. Allocate owned RAM through EFI, copy the kernel, ADT and seed, and keep the
   firmware/EL2/framebuffer reservations out of the arena.
4. Fill the 736-byte `boot_args`: `virtBase`, `physBase`, `memSize`,
   `topOfKernelData`, `Video`, `machineType`, `deviceTreeP` (kernel virtual),
   `deviceTreeLength`, `CommandLine`, `bootFlags`, `memSizeActual`.
5. `ExitBootServices` with a fresh map key, cache maintenance, then branch to
   the Mach-O entry with `x0` = physical `boot_args`.

## Implemented core

`boot/loader/b2l_boot_core.c` holds the decisions that can be tested without
firmware. It does no EFI calls, no allocation and keeps no global state:

- `b2l_parse_macho()` — adapted from beyond2lte-ios, accepts only a linked
  plain arm64 `MH_EXECUTE` with page-aligned, non-overlapping segments, an
  executable entry and an identity-mapped header. Rejects dynamic-linker
  dependencies and nonzero rebase/import state.
- `b2l_parse_payload()` — validates the `B2LXNU03` header, the page-aligned
  ADT offset, exact payload length and the 256-byte seed location.
- `b2l_fill_boot_args()` — checks the arena, ADT and `boot_args` ranges,
  derives the physical entry and fills the 736-byte record:
  `Revision`/`Version` 2, `virtBase`, `physBase`, `memSize`,
  `topOfKernelData` (page-aligned after the record), the framebuffer,
  `deviceTreeP` as a kernel virtual address, the 608-byte command line and
  `memSizeActual`.

`boot/loader/test_boot_core.c` builds a synthetic Mach-O and payload,
checks the accepted cases and every rejection, and runs in the
board-contract CI job. It has no dependency on the macOS kernel build.

The remaining UEFI application work is mechanical but hardware-facing:
GOP video, EFI arena allocation outside firmware/EL2/framebuffer
reservations, copying the segments, `ExitBootServices` with a fresh map key,
cache maintenance and the handoff branch.

## Acceptance for this milestone

- Loader and payload build in CI.
- On device: the early `B2L` framebuffer markers appear, followed by the first
  timer tick. No boot claim before a recovered console line.

## Open items

- 8796's `arm_vm_prot_init` does not require the 12377-only
  `/chosen/memory-map` and `/defaults` nodes, but the ADT already provides
  both; keep them.
- `deviceTreeP` must be a kernel virtual address. The loader must place the ADT
  where the kernel's TTBR1 mapping covers it.
- The 16K-page bootstrap path is still unvalidated on Exynos 9820.
