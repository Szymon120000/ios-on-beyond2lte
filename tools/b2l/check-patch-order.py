#!/usr/bin/env python3
"""Check that the ios-sweet and B2L patch series are ordered and complete."""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
series = {
    'upstream': sorted(p.name for p in (root / 'xnu/patches').glob('*.patch')),
    'b2l': sorted(p.name for p in (root / 'board/b2l/patches').glob('*.patch')),
}


def number(name):
    match = re.match(r'^(\d{4})-', name)
    if not match:
        raise SystemExit(f'patch without a four-digit number: {name}')
    return int(match.group(1))


for label, names in series.items():
    if not names:
        raise SystemExit(f'empty {label} patch series')
    numbers = [number(n) for n in names]
    if numbers != sorted(numbers):
        raise SystemExit(f'{label} patches are not sorted')
    if len(set(numbers)) != len(numbers):
        raise SystemExit(f'{label} patches reuse a number')

upstream_last = number(series['upstream'][-1])
board_first = number(series['b2l'][0])
if board_first <= upstream_last:
    raise SystemExit(
        f'board patch {board_first:04d} must sort after upstream {upstream_last:04d}')
if not (root / 'board/b2l/patches/0082-b2l-board-and-gicv2.patch').exists():
    raise SystemExit('missing the B2L board/GICv2 patch')

print(f'patch order OK: {len(series["upstream"])} upstream, '
      f'{len(series["b2l"])} board')
