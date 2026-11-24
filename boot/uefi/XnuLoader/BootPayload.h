/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_BOOT_PAYLOAD_H
#define B2L_BOOT_PAYLOAD_H

#include <Uefi.h>

/* Locate the Android BOOT partition and read the complete B2LXNU03 payload
 * (kernel + Apple device tree + seed) into pool memory.  The caller owns
 * *PayloadOut and releases it with FreePool. */
EFI_STATUS B2lLoadBootPayload(VOID **PayloadOut, UINTN *SizeOut);

#endif
