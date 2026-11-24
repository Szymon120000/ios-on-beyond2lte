#!/usr/bin/env python3
"""Assemble and verify a B2L Android BOOT v1 image.

The image layout mirrors the verified beyond2lte chain:

    Android kernel area = BootShim + UEFI firmware FD + reference DTB
    trailer             = B2LXNU03 payload (kernel + ADT + seed)

The loader finds the trailer at PAGE + align(kernel_size), so the payload is
not part of the Android kernel size.  No phone I/O is performed.

External inputs still come from the beyond2lte UEFI project: the BootShim
binary, the 4 MiB firmware FD built with the B2L loader embedded, and the
reference DTB used by the firmware.
"""
import argparse
import hashlib
import struct
import sys

import mkb2lpayload

PAGE = 2048
HEADER_SIZE = 1648
MAGIC = b"ANDROID!"
FD_SIZE = 0x400000


def align(value, alignment=PAGE):
    return (value + alignment - 1) & -alignment


def build_header(kernel_size, cmdline=b""):
    if len(cmdline) >= 512:
        raise ValueError("cmdline is longer than 511 bytes")
    header = bytearray(HEADER_SIZE)
    struct.pack_into("<8s", header, 0, MAGIC)
    struct.pack_into("<I", header, 8, kernel_size)
    struct.pack_into("<I", header, 36, PAGE)
    struct.pack_into("<I", header, 40, 1)
    header[64:64 + len(cmdline)] = cmdline
    struct.pack_into("<I", header, 1644, HEADER_SIZE)
    return bytes(header)


def pack(shim, firmware, dtb, payload, cmdline=b""):
    if len(firmware) != FD_SIZE:
        raise ValueError("firmware FD must be exactly 4 MiB")
    if len(shim) % 16 or shim[56:60] != b"ARM\x64":
        raise ValueError("BootShim is not an arm64 boot stub")
    mkb2lpayload.check(payload)
    android_kernel = shim + firmware + dtb
    image = build_header(len(android_kernel), cmdline)
    image += bytes(align(len(image)) - len(image))
    image += android_kernel
    image += bytes(align(len(image)) - len(image))
    image += payload
    return bytes(image)


def check(image):
    if len(image) < PAGE + HEADER_SIZE or image[:8] != MAGIC:
        raise ValueError("missing Android BOOT header")
    kernel_size, _, ramdisk_size, _, second_size = struct.unpack_from("<5I", image, 8)
    page, version = struct.unpack_from("<2I", image, 36)
    if page != PAGE or version != 1:
        raise ValueError("expected Android v1 with 2048-byte pages")
    if ramdisk_size or second_size:
        raise ValueError("ramdisk and second stage must be empty")
    if struct.unpack_from("<I", image, 1644)[0] != HEADER_SIZE:
        raise ValueError("incorrect Android v1 header_size")
    trailer = PAGE + align(kernel_size)
    if trailer + mkb2lpayload.HEADER_SIZE > len(image):
        raise ValueError("BOOT trailer is truncated")
    info = mkb2lpayload.check(image[trailer:])
    return {
        "android_kernel_bytes": kernel_size,
        "trailer_offset": trailer,
        "boot_bytes": len(image),
        "payload": info,
        "sha256": hashlib.sha256(image).hexdigest(),
    }


def self_test():
    shim = bytearray(128)
    struct.pack_into("<QQ", shim, 8, 0xC9000000, FD_SIZE)
    shim[56:60] = b"ARM\x64"
    firmware = bytes(FD_SIZE)
    dtb = b"\xd0\x0d\xfe\xed" + struct.pack(">I", 64) + bytes(56)
    kernel = struct.pack("<IIII", mkb2lpayload.MACHO_MAGIC,
                         mkb2lpayload.CPU_TYPE_ARM64, 0, 2) + bytes(4096 - 16)
    root = mkb2lpayload.adt.Node()
    root.props = [("name", 0, b"device-tree\0")]
    payload = mkb2lpayload.build(kernel, mkb2lpayload.adt.serialize(root),
                                 seed=b"\x5a" * mkb2lpayload.SEED_SIZE)
    image = pack(bytes(shim), firmware, dtb, payload)
    info = check(image)
    assert info["payload"]["kernel_bytes"] == len(kernel)
    for broken in (image[:-1], image + b"\0"):
        try:
            check(broken)
        except ValueError:
            pass
        else:
            raise AssertionError("check accepted a corrupted image")
    print("B2L boot image self-test OK: %d bytes" % len(image))


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--shim", help="BootShim arm64 binary")
    p.add_argument("--firmware", help="4 MiB beyond2lte UEFI FD with the B2L loader")
    p.add_argument("--dtb", help="reference beyond2lte DTB")
    p.add_argument("--payload", help="B2LXNU03 trailer from mkb2lpayload.py")
    p.add_argument("--cmdline", default="")
    p.add_argument("--verify", metavar="BOOT.IMG", help="validate an existing image")
    p.add_argument("--self-test", action="store_true")
    p.add_argument("output", nargs="?")
    a = p.parse_args()

    if a.self_test:
        self_test()
        return
    if a.verify:
        print(check(open(a.verify, "rb").read()))
        return
    if not (a.shim and a.firmware and a.dtb and a.payload and a.output):
        p.error("--shim, --firmware, --dtb, --payload and an output path are required")
    image = pack(open(a.shim, "rb").read(), open(a.firmware, "rb").read(),
                 open(a.dtb, "rb").read(), open(a.payload, "rb").read(),
                 a.cmdline.encode())
    with open(a.output, "wb") as f:
        f.write(image)
    print(check(image))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as e:
        print("error: %s" % e, file=sys.stderr)
        sys.exit(1)
