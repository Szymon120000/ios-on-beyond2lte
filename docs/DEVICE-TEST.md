# Device test: flashing the B2L `boot.img`

This page describes how to test the CI artifact on the Galaxy S10+
(`beyond2lte`, Exynos 9820). It does not document a working boot yet.

## Artifact

The CI `boot` job uploads:

- `out/b2l-boot.img` - Android BOOT v1 image containing
  `BootShim + 4 MiB UEFI FD + DTB + B2LXNU03 trailer`
- `out/b2l-boot.img.sha256`
- `out/b2l-payload.bin`

The latest green CI artifact (run `45477190609`, commit `d6dddf6`) is
21,274,344 bytes with sha256
`d0e2c222b60fcf3cbf67b905ceede021242c48740b3e96fc69d2593eb79f015a`.
The `boot` job prints this hash, and `package-boot.py --verify` re-parses the
image; check both before flashing.

Write the image to the **BOOT** partition only. Never flash the bootloader,
the PIT, or USERDATA for this test.

## Flashing

With heimdall in download mode:

```sh
heimdall flash --BOOT b2l-boot.img
```

With Odin/Windows, wrap the image in an AP tar if needed and select only AP;
leave BL, CP, CSC and USERDATA empty. A TWRP zip is not produced by this repo.

Restore the previous boot image from a dump before testing anything else.

## Expected evidence, in order

1. Samsung S-Boot splash, then the firmware splash.
2. The loader paints five progress cells in the top-left corner:
   blue, cyan, green, yellow, green. Cells are 48x48 pixels, stepping 72
   pixels to the right.
3. `B2L: XNU 8796 loader for beyond2lte` and the kernel/ADT size line on the
   firmware console (if a display console is available).
4. A red row of 16 cells means the loader rejected something: no payload, bad
   Mach-O, arena allocation failure, ADT/boot_args placement, non-identity
   mapping, or `ExitBootServices` failure. This is not a kernel fault.
5. If the green handoff marker appears and the screen stays black, the loader
   handed off and XNU either faulted or has no console output yet.

## Capturing the kernel console

Preferred: the phone's debug UART pads at 115200 8N1. Capture the full session
from power-on, not only from the point where the screen changes.

Without UART, the ADT describes a panic-log region carved from the top of the
`0x900000000` arena. Recovering it requires a later boot or an external dump;
until then, framebuffer markers are the only on-device evidence.

Do not treat a splash, a marker, or a black screen as a boot. Only a recovered
`B2L` console line or the first timer tick counts for this port.
