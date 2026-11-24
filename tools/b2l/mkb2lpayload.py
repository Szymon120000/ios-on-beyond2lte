#!/usr/bin/env python3
"""Build and check the B2L XNU-8796 BOOT payload trailer.

The payload follows the Android BOOT kernel inside the packaged image.  The
firmware loader reads this trailer, verifies the thin arm64 Mach-O kernel and
the Apple device tree, and then builds the 736-byte XNU 8796 boot_args.

Layout (little endian, unchanged from the 12377 trailer except the ADT):

    offset  size  field
    0       8     magic "B2LXNU03"
    8       4     version (3)
    12      4     header size (304)
    16      8     kernel size
    24      8     ADT size
    32      8     kernel offset (304)
    40      8     ADT offset (page aligned after the kernel)
    48      256   random seed for /chosen/random-seed
    304     ...   kernel bytes
    ...     ...   padding
    ...     ...   ADT bytes

No phone I/O is performed.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt

MAGIC = b"B2LXNU03"
VERSION = 3
HEADER_SIZE = 304
SEED_SIZE = 256
PAGE = 4096
MACHO_MAGIC = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C


def align(value, alignment=PAGE):
    return (value + alignment - 1) & -alignment


def check_kernel(kernel):
    if len(kernel) < 32:
        raise ValueError("kernel is shorter than a Mach-O header")
    magic, cputype, _cpusubtype, filetype = struct.unpack_from("<IIII", kernel, 0)
    if magic != MACHO_MAGIC:
        raise ValueError("kernel is not a thin 64-bit Mach-O")
    if cputype != CPU_TYPE_ARM64:
        raise ValueError("kernel is not arm64")
    if filetype != 2:
        raise ValueError("kernel Mach-O filetype is not MH_EXECUTE")
    return len(kernel)


def check_adt(blob):
    if len(blob) < 8:
        raise ValueError("ADT is too short")
    try:
        root, end = adt.parse(blob)
    except (IndexError, struct.error) as e:
        raise ValueError("ADT does not parse: %s" % e)
    if end != len(blob):
        raise ValueError("ADT has trailing bytes after the root node")
    if root.name != "device-tree":
        raise ValueError("ADT root node is not device-tree")
    return len(blob)


def build(kernel, adt, seed=None):
    check_kernel(kernel)
    check_adt(adt)
    kernel_offset = HEADER_SIZE
    adt_offset = align(kernel_offset + len(kernel))
    seed = seed if seed is not None else os.urandom(SEED_SIZE)
    if len(seed) != SEED_SIZE:
        raise ValueError("seed must be exactly 256 bytes")
    header = struct.pack("<8sIIQQQQ", MAGIC, VERSION, HEADER_SIZE,
                         len(kernel), len(adt), kernel_offset, adt_offset)
    header += seed
    if len(header) != HEADER_SIZE:
        raise ValueError("internal header size mismatch")
    payload = bytearray(header)
    payload += kernel
    payload += bytes(adt_offset - len(payload))
    payload += adt
    return bytes(payload)


def check(payload):
    if len(payload) < HEADER_SIZE:
        raise ValueError("payload is shorter than its header")
    magic, version, header_size, kernel_size, adt_size, kernel_offset, adt_offset = \
        struct.unpack_from("<8sIIQQQQ", payload, 0)
    if magic != MAGIC or version != VERSION or header_size != HEADER_SIZE:
        raise ValueError("invalid B2L payload header")
    if kernel_offset != HEADER_SIZE:
        raise ValueError("kernel does not start after the header")
    if kernel_size < 4096 or adt_size < 8:
        raise ValueError("implausible kernel or ADT size")
    if adt_offset != align(kernel_offset + kernel_size):
        raise ValueError("ADT offset is not page aligned after the kernel")
    if adt_offset + adt_size != len(payload):
        raise ValueError("payload has trailing data or is truncated")
    check_kernel(payload[kernel_offset:kernel_offset + kernel_size])
    check_adt(payload[adt_offset:adt_offset + adt_size])
    return {
        "kernel_bytes": kernel_size,
        "adt_bytes": adt_size,
        "header_bytes": HEADER_SIZE,
        "payload_bytes": len(payload),
    }


def self_test():
    kernel = struct.pack("<IIII", MACHO_MAGIC, CPU_TYPE_ARM64, 0, 2) + bytes(4096 - 16)
    root = adt.Node()
    root.props = [("name", 0, b"device-tree\0")]
    blob = adt.serialize(root)
    payload = build(kernel, blob, seed=b"\x5a" * SEED_SIZE)
    info = check(payload)
    assert info["kernel_bytes"] == len(kernel)
    assert info["adt_bytes"] == len(blob)
    for broken in (payload[:-1], payload + b"\0", payload[:100]):
        try:
            check(broken)
        except ValueError:
            pass
        else:
            raise AssertionError("check accepted a corrupted payload")
    try:
        build(kernel, b"nope")
    except ValueError:
        pass
    else:
        raise AssertionError("check accepted an invalid ADT")
    print("B2L payload self-test OK: %d-byte header, %d-byte payload"
          % (HEADER_SIZE, len(payload)))


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--kernel", help="thin arm64 XNU Mach-O")
    p.add_argument("--adt", help="Apple device tree blob")
    p.add_argument("--seed-file", help="exactly 256 seed bytes (default: random)")
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--check", metavar="PAYLOAD", help="validate an existing payload")
    p.add_argument("output", nargs="?", help="payload file to write")
    a = p.parse_args()

    if a.self_test:
        self_test()
        return
    if a.check:
        print(check(open(a.check, "rb").read()))
        return
    if not (a.kernel and a.adt and a.output):
        p.error("--kernel, --adt and an output path are required")
    seed = open(a.seed_file, "rb").read() if a.seed_file else None
    payload = build(open(a.kernel, "rb").read(), open(a.adt, "rb").read(), seed)
    with open(a.output, "wb") as f:
        f.write(payload)
    print(check(payload))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as e:
        print("error: %s" % e, file=sys.stderr)
        sys.exit(1)
