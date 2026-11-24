/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Host tests for the B2L loader core.  Built and run by the board-contract
 * CI job; no EFI, no device access.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "b2l_boot_core.h"

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
		exit(1); \
	} \
} while (0)

#define KERNEL_VA 0xfffffff007004000ULL
#define KERNEL_VSIZE 0x4000ULL
#define KERNEL_CMDS 472U

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void
put64(uint8_t *p, uint64_t v)
{
	put32(p, (uint32_t)v);
	put32(p + 4, (uint32_t)(v >> 32));
}

static void
put_magic(uint8_t *p, const char *magic)
{
	memcpy(p, magic, 8);
}

/* Thin arm64 MH_EXECUTE: __TEXT with one section, LC_UNIXTHREAD entry. */
static void
build_macho(uint8_t *buf, size_t size)
{
	memset(buf, 0, size);
	put32(buf + 0, 0xfeedfacf);
	put32(buf + 4, 0x0100000c);
	put32(buf + 8, 0);
	put32(buf + 12, 2);
	put32(buf + 16, 2);
	put32(buf + 20, KERNEL_CMDS - 32);
	put32(buf + 28, 0);

	put32(buf + 32, 0x19);
	put32(buf + 36, 72 + 80);
	memcpy(buf + 40, "__TEXT", 6);
	put64(buf + 56, KERNEL_VA);
	put64(buf + 64, KERNEL_VSIZE);
	put64(buf + 72, 0);
	put64(buf + 80, KERNEL_CMDS);
	put32(buf + 88, 7);
	put32(buf + 92, 5);
	put32(buf + 96, 1);

	put32(buf + 184, 5);
	put32(buf + 188, 288);
	put32(buf + 192, 6);
	put32(buf + 196, 68);
	put64(buf + 184 + 272, KERNEL_VA + 0x100);
}

static size_t
build_payload(uint8_t *buf, const uint8_t *kernel, size_t kernel_size,
    size_t adt_size)
{
	size_t adt_offset = (304 + kernel_size + 4095) / 4096 * 4096;
	size_t total = adt_offset + adt_size;

	memset(buf, 0, total);
	put_magic(buf, "B2LXNU03");
	put32(buf + 8, 3);
	put32(buf + 12, 304);
	put64(buf + 16, kernel_size);
	put64(buf + 24, adt_size);
	put64(buf + 32, 304);
	put64(buf + 40, adt_offset);
	memset(buf + 48, 0x5a, 256);
	memcpy(buf + 304, kernel, kernel_size);
	return total;
}

int
main(void)
{
	static uint8_t macho[8192], payload[16384];
	struct b2l_macho parsed;
	struct b2l_payload trailer;
	struct b2l_boot_video video = {
		.v_baseAddr = 0xca000000,
		.v_display = 0,
		.v_rowBytes = 1440 * 4,
		.v_width = 1440,
		.v_height = 3040,
		.v_depth = 32,
	};
	struct b2l_boot_args_8796 args;
	uint64_t entry = 0;
	char cmdline[700];
	size_t payload_size;
	unsigned i;

	build_macho(macho, sizeof(macho));
	CHECK(b2l_parse_macho(macho, sizeof(macho), &parsed) == 0);
	CHECK(parsed.segment_count == 1);
	CHECK(parsed.virtual_base == 0xfffffff007000000ULL);
	CHECK(parsed.virtual_end == KERNEL_VA + KERNEL_VSIZE);
	CHECK(parsed.entry == KERNEL_VA + 0x100);

	CHECK(b2l_parse_macho(macho, 16, &parsed) != 0);
	macho[0] = 0x00;
	CHECK(b2l_parse_macho(macho, sizeof(macho), &parsed) != 0);
	macho[0] = 0x00;
	put32(macho + 12, 6); /* MH_DYLIB */
	CHECK(b2l_parse_macho(macho, sizeof(macho), &parsed) != 0);
	build_macho(macho, sizeof(macho));

	payload_size = build_payload(payload, macho, 4096, 8);
	CHECK(b2l_parse_payload(payload, payload_size, &trailer) == 0);
	CHECK(trailer.kernel_offset == 304);
	CHECK(trailer.kernel_size == 4096);
	CHECK(trailer.adt_offset == 8192);
	CHECK(trailer.adt_size == 8);
	CHECK(trailer.seed[0] == 0x5a && trailer.seed[255] == 0x5a);

	CHECK(b2l_parse_payload(payload, payload_size - 1, &trailer) != 0);
	payload[0] = 'X';
	CHECK(b2l_parse_payload(payload, payload_size, &trailer) != 0);
	put_magic(payload, "B2LXNU03");
	put64(payload + 40, 4096);
	CHECK(b2l_parse_payload(payload, payload_size, &trailer) != 0);
	put64(payload + 40, 8192);

	memset(cmdline, 'c', sizeof(cmdline));
	cmdline[sizeof(cmdline) - 1] = '\0';
	CHECK(b2l_fill_boot_args(&parsed, 0x900000000ULL, 0x20000000ULL,
	    0x900100000ULL, 8, &video, 0x900200000ULL, cmdline,
	    &args, &entry) == 0);
	CHECK(args.Revision == 2 && args.Version == 2);
	CHECK(args.virtBase == parsed.virtual_base);
	CHECK(args.physBase == 0x900000000ULL);
	CHECK(args.memSize == 0x20000000ULL);
	CHECK(args.topOfKernelData == 0x900200000ULL + 4096);
	CHECK(args.deviceTreeP == parsed.virtual_base + 0x100000ULL);
	CHECK(args.deviceTreeLength == 8);
	CHECK(args.Video.v_baseAddr == 0xca000000ULL);
	CHECK(args.Video.v_rowBytes == 1440 * 4);
	CHECK(args.CommandLine[sizeof(args.CommandLine) - 2] == 'c');
	CHECK(args.CommandLine[sizeof(args.CommandLine) - 1] == '\0');
	CHECK(entry == 0x900000000ULL + (parsed.entry - parsed.virtual_base));
	CHECK(args.memSizeActual == 0x20000000ULL);

	/* The command line is truncated, never overflowed. */
	memset(cmdline, 'd', sizeof(cmdline));
	cmdline[sizeof(cmdline) - 1] = '\0';
	CHECK(b2l_fill_boot_args(&parsed, 0x900000000ULL, 0x20000000ULL,
	    0x900100000ULL, 8, &video, 0x900200000ULL, cmdline,
	    &args, &entry) == 0);
	CHECK(args.CommandLine[607] == '\0');

	/* Rejections: arena, ADT, alignment, entry, kernel size. */
	CHECK(b2l_fill_boot_args(&parsed, 0x900000001ULL, 0x20000000ULL,
	    0x900100000ULL, 8, &video, 0x900200000ULL, "", &args, &entry) != 0);
	CHECK(b2l_fill_boot_args(&parsed, 0x900000000ULL, 0x20000001ULL,
	    0x900100000ULL, 8, &video, 0x900200000ULL, "", &args, &entry) != 0);
	CHECK(b2l_fill_boot_args(&parsed, 0x900000000ULL, 0x20000000ULL,
	    0x900100001ULL, 8, &video, 0x900200000ULL, "", &args, &entry) != 0);
	CHECK(b2l_fill_boot_args(&parsed, 0x900000000ULL, 0x20000000ULL,
	    0x900100000ULL, 8, &video, 0x900200004ULL, "", &args, &entry) != 0);
	CHECK(b2l_fill_boot_args(&parsed, 0x900000000ULL, 0x20000000ULL,
	    0x900100000ULL, 0, &video, 0x900200000ULL, "", &args, &entry) != 0);
	CHECK(b2l_fill_boot_args(&parsed, 0x900000000ULL, 0x20000000ULL,
	    0xb00000000ULL, 8, &video, 0x900200000ULL, "", &args, &entry) != 0);
	CHECK(b2l_fill_boot_args(&parsed, 0x900000000ULL, 0x3000ULL,
	    0x900100000ULL, 8, &video, 0x900200000ULL, "", &args, &entry) != 0);
	CHECK(b2l_fill_boot_args(NULL, 0x900000000ULL, 0x20000000ULL,
	    0x900100000ULL, 8, &video, 0x900200000ULL, "", &args, &entry) != 0);

	/* Every record byte is deterministic for a fixed arena. */
	for (i = 0; i < sizeof(args.CommandLine); i++) {
		CHECK(args.CommandLine[i] == 0 || args.CommandLine[i] == 'd');
	}

	printf("B2L loader core self-test OK: %zu-byte payload, %zu-byte boot_args\n",
	    payload_size, sizeof(args));
	return 0;
}
