/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Host-testable core of the B2L XNU 8796 loader.
 *
 * This file does no EFI calls and allocates nothing.  It parses only the
 * kernel Mach-O and the B2LXNU03 BOOT trailer, then derives the physical
 * entry point and the 736-byte boot_args record.  The EFI application will
 * supply the memory arena, framebuffer and device tree pointers.
 *
 * Mach-O validation is adapted from beyond2lte-ios uefi/XnuLoader/MachO.c.
 */
#ifndef B2L_BOOT_CORE_H
#define B2L_BOOT_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "boot_args_8796.h"

#define B2L_MAX_SEGMENTS 32

struct b2l_segment {
	uint64_t va;
	uint64_t size;
	uint64_t file_offset;
	uint64_t file_size;
	uint32_t protection;
};

struct b2l_macho {
	uint64_t virtual_base;
	uint64_t virtual_end;
	uint64_t entry;
	uint32_t segment_count;
	struct b2l_segment segments[B2L_MAX_SEGMENTS];
};

struct b2l_payload {
	uint64_t kernel_offset;
	uint64_t kernel_size;
	uint64_t adt_offset;
	uint64_t adt_size;
	const uint8_t *seed;
};

/* Returns 0 or a negative parser error.  Accepts only a fully linked, plain
 * ARM64 MH_EXECUTE with 4 KiB-aligned, non-overlapping segments, an
 * executable entry and an identity-mapped header. */
int b2l_parse_macho(const void *data, size_t length, struct b2l_macho *out);

/* Validates the B2LXNU03 header and both payload ranges. */
int b2l_parse_payload(const void *data, size_t length, struct b2l_payload *out);

/* Fills the XNU 8796 boot_args for a kernel copied into an identity-mapped
 * EFI arena.  dt_phys must be inside [phys_base, phys_base + arena_size) and
 * is published as a kernel virtual address, as iBoot does. */
int b2l_fill_boot_args(const struct b2l_macho *macho,
    uint64_t phys_base, uint64_t arena_size,
    uint64_t dt_phys, uint32_t dt_length,
    const struct b2l_boot_video *video,
    uint64_t args_phys, const char *command_line,
    struct b2l_boot_args_8796 *args, uint64_t *entry_phys);

#endif /* B2L_BOOT_CORE_H */
