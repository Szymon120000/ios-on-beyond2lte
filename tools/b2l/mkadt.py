#!/usr/bin/env python3
"""Generate the Apple Device Tree contract for the beyond2lte XNU port.

This is the *contract* used by the future B2L XNU-8796 loader.  It is not
consumed by the current beyond2lte UEFI firmware (which targets XNU 12377),
and no device boot is claimed until that loader exists.

Source of the hardware values: the live Exynos 9820 memory map, the Exynos
GICv2 register block, and the iPhone X (D22AP) identity whose iOS 16 userland
the project intends to run.

Usage:
    python3 tools/b2l/mkadt.py out/b2l.adt [--dump]
    python3 tools/b2l/mkadt.py --check
"""
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402

# Third DDR bank, above the firmware and secure-world carve-outs.  The live
# beyond2lte profile exposes 0x900000000..0x9FD800000 (4056 MiB) to the OS.
PHYS_BASE = 0x900000000
MEM_SIZE = 0xFD800000
PANIC_LOG_SIZE = 0x100000

GICD_PA, GICD_SIZE = 0x10101000, 0x1000
GICC_PA, GICC_SIZE = 0x10102000, 0x2000
TIMEBASE = 26000000
VTIMER_INTID = 27

# Firmware display reservation (20 MiB), native S10+ panel.
FB_PA, FB_SIZE = 0xCA000000, 0x1400000
FB_W, FB_H = 1440, 3040
FB_STRIDE = FB_W * 4

# iOS 16.7 iPhone X userland identity.
ECID = 0x42324C0000000001  # synthetic 'B2L' + 1; never the phone's ECID
CHIP_ID = 0x8015           # A11, matches the iPhone10,3 userland

BOOT_MANIFEST_HASH = hashlib.sha384(b'b2l boot manifest').digest()

u32 = lambda *v: struct.pack('<%dI' % len(v), *v)
u64 = lambda *v: struct.pack('<%dQ' % len(v), *v)
cstr = lambda s: s.encode() + b'\0'


def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, cstr(name))] + [(k, 0, v) for k, v in props]
    n.children = list(children)
    return n


def build(seed=None):
    panic_base = PHYS_BASE + MEM_SIZE - PANIC_LOG_SIZE
    mem_size = MEM_SIZE - PANIC_LOG_SIZE
    seed = seed if seed is not None else os.urandom(64)
    phandle = iter(range(1, 1000))
    ph = lambda: ('AAPL,phandle', u32(next(phandle)))

    chosen = node('chosen', [
        ph(),
        ('dram-base', u64(PHYS_BASE)),
        ('dram-size', u64(mem_size)),
        ('random-seed', seed),
        ('debug-enabled', u32(1)),
        ('embedded-panic-log-size', u32(PANIC_LOG_SIZE)),
        ('firmware-version', cstr('b2l-boot')),
        ('system-firmware-version', cstr('b2l-boot')),
        ('boot-manifest-hash', BOOT_MANIFEST_HASH),
        ('crypto-hash-method', cstr('sha2-384')),
        ('display-scale', u32(3)),
        ('unique-chip-id', struct.pack('<Q', ECID)),
        ('chip-id', u32(CHIP_ID)),
        # Development-fused, undemoted: activates without Apple servers.
        ('certificate-production-status', u32(0)),
        ('effective-production-status-ap', u32(0)),
        ('certificate-security-mode', u32(1)),
        ('effective-security-mode-sep', u32(1)),
    ], [node('memory-map', [ph()]),
        node('asmb', [ph(), ('lp-sip0', u64(0x6f)), ('lp-sip1', u32(1))])])

    # Exynos 9820: 4x Cortex-A55, 2x Cortex-A75, 2x Cortex-M4.
    clusters = {0: (0, 4, 1804800000), 1: (4, 2, 2704000000), 2: (6, 2, 2704000000)}
    cpu_nodes = []
    for cluster, (first, count, clock) in clusters.items():
        for index in range(count):
            core = first + index
            cpu_nodes.append(node(f'cpu{core}', [
                ph(),
                ('device_type', cstr('cpu')),
                ('compatible', cstr('samsung,exynos9820') + cstr('ARM,v8')),
                ('reg', u32((cluster << 8) | index)),
                ('cpu-id', u32(core)),
                ('cluster-id', u32(cluster)),
                ('cluster-type', u32(0)),
                ('die-cluster-id', u32(cluster)),
                ('cluster-core-id', u32(index)),
                ('l2-cache-id', u32(core)),
                ('l3-cache-id', u32(0)),
                ('state', cstr('running' if core == 0 else 'stopped')),
                ('timebase-frequency', u32(TIMEBASE)),
                ('clock-frequency', u32(clock)),
                ('bus-frequency', u32(100000000)),
                ('memory-frequency', u32(2093000000)),
                ('peripheral-frequency', u32(TIMEBASE)),
                ('fixed-frequency', u32(TIMEBASE)),
            ]))

    cpus = node('cpus', [ph(), ('#address-cells', u32(1)), ('#size-cells', u32(0))],
                cpu_nodes)

    gic = node('gic', [
        ph(),
        ('compatible', cstr('arm,gic-400')),
        ('interrupt-controller', b''),
        ('virtual-timer-intid', u32(VTIMER_INTID)),
        ('reg', u64(GICD_PA, GICD_SIZE, GICC_PA, GICC_SIZE)),
    ])

    arm_io = node('arm-io', [
        ph(),
        ('device_type', cstr('b2l-io')),
        ('compatible', cstr('b2l-io') + cstr('arm-io')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
        ('ranges', u64(0, 0, 0x100000000)),
    ], [gic,
        node('wdt', [ph(), ('no-pmu', u32(1))]),
        node('disp0', [
            ph(),
            ('device_type', cstr('display-subsystem')),
            ('compatible', cstr('b2l,display')),
            ('canvas-width', u32(1125)),
            ('canvas-height', u32(2436)),
        ])])

    fbcon = node('b2l-fbcon', [
        ph(),
        ('reg', u64(FB_PA, FB_SIZE)),
        ('width', u32(FB_W)),
        ('height', u32(FB_H)),
        ('stride', u32(FB_STRIDE)),
    ])

    defaults = node('defaults', [ph(), ('no-effaceable-storage', u32(1))])

    buttons = node('buttons', [
        ph(),
        ('button-names', cstr('volup') + cstr('voldown') + cstr('hold')),
        ('home-button-type', u32(2)),
        ('opposed-power-vol-buttons', u32(1)),
    ])

    pram = node('pram', [ph(), ('reg', u64(panic_base, PANIC_LOG_SIZE))])

    # The /product node from the real iPhone X tree is an Apple artifact and
    # stays out of git.  Without it MobileGestalt falls back to an iPod
    # identity, so a production image must be generated with the extracted
    # D22AP DeviceTree available at B2L_D22AP_ADT.
    product = []
    product_path = os.environ.get(
        'B2L_D22AP_ADT',
        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     '../../research/ipsw-x/d22ap.adt'))
    if os.path.exists(product_path):
        root, _ = adt.parse(open(product_path, 'rb').read())
        for child in root.children:
            props = dict((k, v) for k, f, v in child.props)
            if props.get('name') == b'product\0':
                product = [node('product', [ph()] + [
                    (k, v) for k, f, v in child.props
                    if not f and k not in ('name', 'AAPL,phandle')])]
                break
    else:
        print(f'warning: {product_path} missing; no /product node', file=sys.stderr)

    return node('device-tree', [
        ph(),
        ('compatible', cstr('D22AP') + cstr('iPhone10,3') +
            cstr('samsung,beyond2lte') + cstr('samsung,exynos9820') + cstr('AppleARM')),
        ('model', cstr('iPhone10,3')),
        ('target-type', cstr('D22')),
        ('target-sub-type', cstr('D22AP')),
        ('manufacturer', cstr('Apple Inc.')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
    ], [chosen, defaults, cpus, arm_io, fbcon, buttons, pram] + product)


def check():
    blob = adt.serialize(build(seed=b'\x5a' * 64))
    root, _ = adt.parse(blob)
    props = {}
    children = {}
    for child in root.children:
        name = dict((k, v) for k, f, v in child.props)['name'].rstrip(b'\0')
        children[name.decode()] = child
        props[name.decode()] = dict((k, v) for k, f, v in child.props)

    assert dict((k, v) for k, f, v in root.props)['model'] == b'iPhone10,3\0'
    assert props['chosen']['dram-base'] == u64(PHYS_BASE)
    assert props['chosen']['dram-size'] == u64(MEM_SIZE - PANIC_LOG_SIZE)
    assert props['chosen']['unique-chip-id'] == struct.pack('<Q', ECID)
    assert props['cpus']['#address-cells'] == u32(1)
    cpu_members = [c for c in children['cpus'].children
                   if dict((k, v) for k, f, v in c.props)['name'].startswith(b'cpu')]
    assert len(cpu_members) == 8, len(cpu_members)
    gic = dict((k, v) for k, f, v in children['arm-io'].children[0].props)
    assert gic['compatible'] == b'arm,gic-400\0'
    assert gic['reg'] == u64(GICD_PA, GICD_SIZE, GICC_PA, GICC_SIZE)
    assert gic['virtual-timer-intid'] == u32(VTIMER_INTID)
    fb = props['b2l-fbcon']
    assert fb['reg'] == u64(FB_PA, FB_SIZE)
    assert fb['stride'] == u32(FB_STRIDE)
    assert props['pram']['reg'] == u64(PHYS_BASE + MEM_SIZE - PANIC_LOG_SIZE, PANIC_LOG_SIZE)
    print(f'B2L ADT check OK: {len(blob)} bytes, 8 CPUs, '
          f'GICv2 @ {GICD_PA:#x}/{GICC_PA:#x}, timer PPI {VTIMER_INTID}')


if __name__ == '__main__':
    if '--check' in sys.argv:
        check()
        sys.exit(0)
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    blob = adt.serialize(build())
    with open(sys.argv[1], 'wb') as out:
        out.write(blob)
    if '--dump' in sys.argv:
        adt.dump(adt.parse(blob)[0])
    print(f'{sys.argv[1]}: {len(blob)} bytes', file=sys.stderr)
