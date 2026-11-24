/* SPDX-License-Identifier: BSD-2-Clause */
/* Read the B2LXNU03 payload from the Android BOOT partition through the
 * firmware's block I/O.  No partition is written and no other OS is touched. */
#include "BootPayload.h"

#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/PartitionInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "../loader/b2l_boot_core.h"

#define B2L_PAYLOAD_LIMIT (160U * 1024U * 1024U)

STATIC UINT32
Le32(CONST UINT8 *p)
{
  return (UINT32)p[0] | (UINT32)p[1] << 8 | (UINT32)p[2] << 16 |
         (UINT32)p[3] << 24;
}

STATIC UINT64
Le64(CONST UINT8 *p)
{
  return (UINT64)Le32(p) | (UINT64)Le32(p + 4) << 32;
}

STATIC BOOLEAN
IsBootName(CONST CHAR16 *Name)
{
  return CompareMem(Name, L"BOOT", sizeof(L"BOOT")) == 0 ||
         CompareMem(Name, L"boot", sizeof(L"boot")) == 0;
}

EFI_STATUS
B2lLoadBootPayload(VOID **PayloadOut, UINTN *SizeOut)
{
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0;
  EFI_STATUS Status;
  BOOLEAN SeenMagic = FALSE;

  if (PayloadOut == NULL || SizeOut == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *PayloadOut = NULL;
  *SizeOut = 0;

  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid,
                                   NULL, &Count, &Handles);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  Status = EFI_NOT_FOUND;

  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL *Block;
    EFI_DISK_IO_PROTOCOL *Disk;
    EFI_PARTITION_INFO_PROTOCOL *Partition;
    UINT8 Android[48];
    UINT8 Header[B2L_PAYLOAD_HEADER_SIZE];
    UINT32 Page, MediaId;
    UINT64 Trailer, Capacity, Total, KernelSize, AdtSize, KernelOffset, AdtOffset;
    struct b2l_payload Parsed;
    VOID *Buffer;

    if (EFI_ERROR(gBS->HandleProtocol(Handles[Index], &gEfiBlockIoProtocolGuid,
                                      (VOID **)&Block)) ||
        Block->Media == NULL || !Block->Media->MediaPresent ||
        !Block->Media->LogicalPartition ||
        EFI_ERROR(gBS->HandleProtocol(Handles[Index], &gEfiDiskIoProtocolGuid,
                                      (VOID **)&Disk)) ||
        EFI_ERROR(gBS->HandleProtocol(Handles[Index],
                                      &gEfiPartitionInfoProtocolGuid,
                                      (VOID **)&Partition)) ||
        Partition->Type != PARTITION_TYPE_GPT ||
        !IsBootName(Partition->Info.Gpt.PartitionName)) {
      continue;
    }

    if (Block->Media->BlockSize == 0 ||
        Block->Media->LastBlock == MAX_UINT64 ||
        Block->Media->LastBlock + 1 > MAX_UINT64 / Block->Media->BlockSize) {
      continue;
    }
    Capacity = (Block->Media->LastBlock + 1) * Block->Media->BlockSize;
    MediaId = Block->Media->MediaId;

    if (EFI_ERROR(Disk->ReadDisk(Disk, MediaId, 0, sizeof(Android), Android)) ||
        CompareMem(Android, "ANDROID!", 8) != 0) {
      continue;
    }
    Page = Le32(Android + 36);
    if (Le32(Android + 40) != 1 || Page < 2048 || Page > 65536 ||
        (Page & (Page - 1)) != 0 || Le32(Android + 16) != 0 ||
        Le32(Android + 24) != 0) {
      continue;
    }
    Trailer = Page + (((UINT64)Le32(Android + 8) + Page - 1) & ~((UINT64)Page - 1));
    if (Trailer > Capacity ||
        B2L_PAYLOAD_HEADER_SIZE > Capacity - Trailer) {
      continue;
    }
    if (EFI_ERROR(Disk->ReadDisk(Disk, MediaId, Trailer,
                                 B2L_PAYLOAD_HEADER_SIZE, Header))) {
      continue;
    }
    if (CompareMem(Header, B2L_PAYLOAD_MAGIC, 8) != 0) {
      if (SeenMagic) {
        Status = EFI_COMPROMISED_DATA;
        break;
      }
      continue;
    }
    /* Once the magic is present, corruption must not fall through to another
     * boot source. */
    SeenMagic = TRUE;
    Status = EFI_COMPROMISED_DATA;

    KernelSize = Le64(Header + 16);
    AdtSize = Le64(Header + 24);
    KernelOffset = Le64(Header + 32);
    AdtOffset = Le64(Header + 40);
    if (KernelOffset != B2L_PAYLOAD_HEADER_SIZE || KernelSize < 4096 ||
        AdtSize < 8 || AdtOffset > B2L_PAYLOAD_LIMIT ||
        AdtSize > B2L_PAYLOAD_LIMIT - AdtOffset) {
      break;
    }
    Total = AdtOffset + AdtSize;
    if (Total > Capacity - Trailer) {
      break;
    }
    Buffer = AllocatePool((UINTN)Total);
    if (Buffer == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }
    if (EFI_ERROR(Disk->ReadDisk(Disk, MediaId, Trailer, (UINTN)Total,
                                 Buffer))) {
      FreePool(Buffer);
      break;
    }
    if (b2l_parse_payload(Buffer, (size_t)Total, &Parsed) != 0) {
      FreePool(Buffer);
      break;
    }
    *PayloadOut = Buffer;
    *SizeOut = (UINTN)Total;
    Print(L"B2L: read %lu-byte kernel and %lu-byte ADT from BOOT\r\n",
          KernelSize, AdtSize);
    Status = EFI_SUCCESS;
    break;
  }

  FreePool(Handles);
  return Status;
}
