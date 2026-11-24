/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Host-testable core of the B2L XNU 8796 loader.
 *
 * No EFI calls, no allocation and no global state: every function validates
 * its inputs and writes only the caller's structures.  The UEFI application
 * supplies the arena, framebuffer, ADT and boot_args addresses.
 *
 * The Mach-O checks are adapted from beyond2lte-ios uefi/XnuLoader/MachO.c,
 * which was written for XNU 12377; the kernel image format is unchanged.
 */
#include "b2l_boot_core.h"

#define U64_MAX_VALUE ((uint64_t)~0ULL)
#define PAYLOAD_MAGIC "B2LXNU03"
#define PAYLOAD_VERSION 3u
#define PAYLOAD_HEADER_SIZE 304u
#define PAYLOAD_SEED_OFFSET 48u

static uint32_t
read32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t
read64(const uint8_t *p)
{
	return read32(p) | (uint64_t)read32(p + 4) << 32;
}

static int
within(uint64_t off, uint64_t size, uint64_t length)
{
	return off <= length && size <= length - off;
}

static int
magic_is(const uint8_t *p, const char *magic)
{
	size_t i;

	for (i = 0; i < 8; i++) {
		if (p[i] != (uint8_t)magic[i]) {
			return 0;
		}
	}
	return 1;
}

int
b2l_parse_macho(const void *data, size_t length, struct b2l_macho *out)
{
	const uint8_t *p = data;
	uint64_t offset = 32, commands_end, lowest = U64_MAX_VALUE, highest = 0;
	uint64_t entry = 0;
	uint32_t i, j, count, command_size, command, size, n = 0;
	int have_entry = 0, executable_entry = 0, mapped_header = 0;

	if (!p || !out || length < 32 || read32(p) != 0xfeedfacf ||
	    read32(p + 4) != 0x0100000c || (read32(p + 8) & 0x00ffffff) != 0 ||
	    read32(p + 12) != 2) {
		return -1;
	}
	count = read32(p + 16);
	command_size = read32(p + 20);
	if (!count || count > 4096 || !within(32, command_size, length)) {
		return -2;
	}
	commands_end = 32 + (uint64_t)command_size;
	for (i = 0; i < count; i++) {
		if (!within(offset, 8, commands_end)) {
			return -3;
		}
		command = read32(p + offset);
		size = read32(p + offset + 4);
		if (size < 8 || (size & 7) || !within(offset, size, commands_end)) {
			return -4;
		}
		if (command == 0x19) { /* LC_SEGMENT_64 */
			uint64_t va, vsize, file_offset, file_size;
			uint32_t sections;

			if (size < 72) {
				return -5;
			}
			va = read64(p + offset + 24);
			vsize = read64(p + offset + 32);
			file_offset = read64(p + offset + 40);
			file_size = read64(p + offset + 48);
			sections = read32(p + offset + 64);
			if (sections > (size - 72) / 80 ||
			    size != 72 + (uint64_t)sections * 80 ||
			    file_size > vsize ||
			    (read32(p + offset + 68) & 1) ||
			    !within(file_offset, file_size, length) ||
			    va > U64_MAX_VALUE - vsize) {
				return -6;
			}
			if (vsize) {
				if (n == B2L_MAX_SEGMENTS || (va & 4095) ||
				    (vsize & 4095)) {
					return -7;
				}
				for (j = 0; j < n; j++) {
					const struct b2l_segment *s = &out->segments[j];

					if (va < s->va + s->size &&
					    s->va < va + vsize) {
						return -8;
					}
				}
				out->segments[n].va = va;
				out->segments[n].size = vsize;
				out->segments[n].file_offset = file_offset;
				out->segments[n].file_size = file_size;
				out->segments[n].protection =
				    read32(p + offset + 60);
				if (file_offset == 0 && file_size >= commands_end) {
					mapped_header = 1;
				}
				n++;
				if (va < lowest) {
					lowest = va;
				}
				if (va + vsize > highest) {
					highest = va + vsize;
				}
			}
		} else if (command == 5) { /* LC_UNIXTHREAD */
			if (have_entry || size != 288 ||
			    read32(p + offset + 8) != 6 ||
			    read32(p + offset + 12) != 68) {
				return -9;
			}
			entry = read64(p + offset + 16 + 32 * 8);
			have_entry = 1;
		} else if (command == 0x80000034) { /* LC_BUILD_VERSION */
			if (size != 16 || read32(p + offset + 12) != 0) {
				return -10;
			}
		} else if (command == 0x22 || command == 0x80000022) {
			/* Rebase records need no changes at slide zero.
			 * Imported symbols require a dynamic loader. */
			if (size != 48 || read32(p + offset + 20) ||
			    read32(p + offset + 28) || read32(p + offset + 36)) {
				return -11;
			}
		} else if (command == 0xc || command == 0x18 ||
		    command == 0x80000018 || command == 0x8000001f ||
		    command == 0x80000023) {
			return -12;
		}
		offset += size;
	}
	if (offset != commands_end || !n || !have_entry || !mapped_header ||
	    (entry & 3)) {
		return -13;
	}
	for (j = 0; j < n; j++) {
		const struct b2l_segment *s = &out->segments[j];

		if ((s->protection & 4) && entry >= s->va &&
		    within(entry - s->va, 4, s->file_size)) {
			executable_entry = 1;
		}
	}
	if (!executable_entry || highest > U64_MAX_VALUE - 4095 ||
	    lowest < 0xffff000000000000ULL) {
		return -14;
	}
	out->virtual_base = lowest & ~0x1fffffULL;
	out->virtual_end = (highest + 4095) & ~4095ULL;
	out->entry = entry;
	out->segment_count = n;
	return 0;
}

int
b2l_parse_payload(const void *data, size_t length, struct b2l_payload *out)
{
	const uint8_t *p = data;
	uint64_t kernel_offset, kernel_size, adt_offset, adt_size;

	if (!p || !out || length < PAYLOAD_HEADER_SIZE || !magic_is(p, PAYLOAD_MAGIC) ||
	    read32(p + 8) != PAYLOAD_VERSION ||
	    read32(p + 12) != PAYLOAD_HEADER_SIZE) {
		return -1;
	}
	kernel_size = read64(p + 16);
	adt_size = read64(p + 24);
	kernel_offset = read64(p + 32);
	adt_offset = read64(p + 40);
	if (kernel_offset != PAYLOAD_HEADER_SIZE || kernel_size < 4096 ||
	    adt_size < 8) {
		return -2;
	}
	if (!within(kernel_offset, kernel_size, length) ||
	    adt_offset != (kernel_offset + kernel_size + 4095) / 4096 * 4096 ||
	    !within(adt_offset, adt_size, length) ||
	    adt_offset + adt_size != length) {
		return -3;
	}
	out->kernel_offset = kernel_offset;
	out->kernel_size = kernel_size;
	out->adt_offset = adt_offset;
	out->adt_size = adt_size;
	out->seed = p + PAYLOAD_SEED_OFFSET;
	return 0;
}

static int
range_ok(uint64_t base, uint64_t size, uint64_t addr, uint64_t len)
{
	if (!size || addr < base) {
		return 0;
	}
	return within(addr - base, len, size);
}

int
b2l_fill_boot_args(const struct b2l_macho *macho,
    uint64_t phys_base, uint64_t arena_size,
    uint64_t dt_phys, uint32_t dt_length,
    const struct b2l_boot_video *video,
    uint64_t args_phys, const char *command_line,
    struct b2l_boot_args_8796 *args, uint64_t *entry_phys)
{
	uint64_t kernel_size, top_of_kernel_data;
	size_t i;

	if (!macho || !args || !entry_phys || !video || !command_line) {
		return -1;
	}
	if (!arena_size || (phys_base & 4095) || (arena_size & 4095) ||
	    phys_base > U64_MAX_VALUE - arena_size) {
		return -2;
	}
	kernel_size = macho->virtual_end - macho->virtual_base;
	if (!kernel_size || kernel_size > arena_size ||
	    !range_ok(phys_base, arena_size, args_phys,
	    sizeof(*args)) ||
	    (args_phys & 7) ||
	    !range_ok(phys_base, arena_size, dt_phys, dt_length) ||
	    (dt_phys & 3) || dt_length < 8) {
		return -3;
	}
	top_of_kernel_data =
	    (args_phys + (uint64_t)sizeof(*args) + 4095) & ~4095ULL;
	if (top_of_kernel_data < args_phys ||
	    !range_ok(phys_base, arena_size, top_of_kernel_data, 0)) {
		return -4;
	}
	if (macho->entry < macho->virtual_base ||
	    macho->entry >= macho->virtual_end) {
		return -5;
	}

	*entry_phys = phys_base + (macho->entry - macho->virtual_base);
	if (*entry_phys >= top_of_kernel_data) {
		return -6;
	}

	*args = (struct b2l_boot_args_8796){ 0 };
	args->Revision = 2;
	args->Version = 2;
	args->virtBase = macho->virtual_base;
	args->physBase = phys_base;
	args->memSize = arena_size;
	args->topOfKernelData = top_of_kernel_data;
	args->Video = *video;
	args->machineType = 0;
	args->deviceTreeP = macho->virtual_base + (dt_phys - phys_base);
	args->deviceTreeLength = dt_length;
	for (i = 0; i < sizeof(args->CommandLine) - 1 && command_line[i]; i++) {
		args->CommandLine[i] = command_line[i];
	}
	args->bootFlags = 0;
	args->memSizeActual = arena_size;
	return 0;
}
