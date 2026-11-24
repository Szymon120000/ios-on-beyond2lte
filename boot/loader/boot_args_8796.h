/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * XNU 8796 (macOS 13.5 / iOS 16 era) ARM64 boot_args ABI.
 *
 * The beyond2lte XNU 12377 loader cannot be reused unchanged: its boot_args
 * has a 1024-byte command line and is 1152 bytes, while 8796 has a
 * BOOT_LINE_LENGTH of 608 and a 736-byte structure.  Every offset up to
 * CommandLine is shared, but bootFlags/memSizeActual move, so the loader
 * must be rebuilt against this layout.
 *
 * Source of truth: xnu-8796.141.3 pexpert/pexpert/arm64/boot.h.
 */
#ifndef B2L_BOOT_ARGS_8796_H
#define B2L_BOOT_ARGS_8796_H

#include <stddef.h>
#include <stdint.h>

#define B2L_BOOT_LINE_LENGTH 608U

struct b2l_boot_video {
	uint64_t v_baseAddr;
	uint64_t v_display;
	uint64_t v_rowBytes;
	uint64_t v_width;
	uint64_t v_height;
	uint64_t v_depth;
};

struct b2l_boot_args_8796 {
	uint16_t Revision;
	uint16_t Version;
	uint64_t virtBase;
	uint64_t physBase;
	uint64_t memSize;
	uint64_t topOfKernelData;
	struct b2l_boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[B2L_BOOT_LINE_LENGTH];
	uint64_t bootFlags;
	uint64_t memSizeActual;
};

_Static_assert(sizeof(struct b2l_boot_video) == 48, "Boot_Video size");
_Static_assert(offsetof(struct b2l_boot_args_8796, Video) == 40, "Boot_Video offset");
_Static_assert(offsetof(struct b2l_boot_args_8796, machineType) == 88, "machineType offset");
_Static_assert(offsetof(struct b2l_boot_args_8796, deviceTreeP) == 96, "deviceTreeP offset");
_Static_assert(offsetof(struct b2l_boot_args_8796, deviceTreeLength) == 104, "deviceTreeLength offset");
_Static_assert(offsetof(struct b2l_boot_args_8796, CommandLine) == 108, "CommandLine offset");
_Static_assert(offsetof(struct b2l_boot_args_8796, bootFlags) == 720, "bootFlags offset");
_Static_assert(offsetof(struct b2l_boot_args_8796, memSizeActual) == 728, "memSizeActual offset");
_Static_assert(sizeof(struct b2l_boot_args_8796) == 736, "boot_args size");

#endif /* B2L_BOOT_ARGS_8796_H */
