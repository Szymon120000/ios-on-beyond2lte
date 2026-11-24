/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * B2L XNU 8796 loader for the beyond2lte UEFI firmware.
 *
 * Reads the B2LXNU03 payload from the Android BOOT partition, places the
 * kernel, Apple device tree and boot_args in a fixed identity-mapped arena,
 * and hands off with the MMU and caches off.  Secondary CPUs are started by
 * the kernel's SweetCPU driver through PSCI; this loader starts only the boot
 * CPU.
 */
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "BootPayload.h"
#include "../loader/b2l_boot_core.h"

#define B2L_ARENA_BASE 0x900000000ULL
#define B2L_ARENA_SIZE 0xFD800000ULL
#define B2L_MAP_SIZE   (1024U * 1024U)
#define B2L_MARK_SIZE  48U
#define B2L_MARK_STEP  72U

VOID EFIAPI B2LXnuHandoff(UINT64 EntryPhysical, UINT64 BootArgsPhysical,
                          UINT64 CleanStart, UINT64 CleanEnd);

STATIC UINT64
ReadCurrentEl(VOID)
{
  UINT64 Value;
  __asm__ volatile("mrs %0, CurrentEL" : "=r"(Value));
  return Value >> 2;
}

STATIC UINT64
ReadSctlr(VOID)
{
  UINT64 Value;
  __asm__ volatile("mrs %0, sctlr_el1" : "=r"(Value));
  return Value;
}

/* The MMU-off trampoline and the kernel's first instructions must run at
 * their physical addresses.  Reject firmware that violates that rule. */
STATIC BOOLEAN
IdentityAddress(UINT64 Address)
{
  UINT64 Par;

  if ((ReadSctlr() & 1) == 0) {
    return TRUE;
  }
  __asm__ volatile("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1"
                   : "=r"(Par) : "r"(Address) : "memory");
  if (Par & 1) {
    return FALSE;
  }
  return (Par & 0x0000fffffffff000ULL) == (Address & 0x0000fffffffff000ULL);
}

STATIC BOOLEAN
IdentityRange(UINT64 Address, UINT64 Length)
{
  UINT64 End;

  if (Length == 0 || Address > MAX_UINT64 - (Length - 1)) {
    return FALSE;
  }
  End = Address + Length - 1;
  for (UINT64 At = Address & ~4095ULL; At <= End; At += 4096) {
    if (!IdentityAddress(At)) {
      return FALSE;
    }
    if (End - At < 4096) {
      break;
    }
  }
  return TRUE;
}

STATIC EFI_STATUS
GetVideo(struct b2l_boot_video *Video)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_STATUS Status;
  UINT64 Required;

  Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL,
                               (VOID **)&Gop);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if (Gop->Mode == NULL || Gop->Mode->Info == NULL ||
      Gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor) {
    return EFI_UNSUPPORTED;
  }
  Video->v_baseAddr = Gop->Mode->FrameBufferBase;
  Video->v_display = 0;
  Video->v_rowBytes = (UINT64)Gop->Mode->Info->PixelsPerScanLine * 4;
  Video->v_width = Gop->Mode->Info->HorizontalResolution;
  Video->v_height = Gop->Mode->Info->VerticalResolution;
  Video->v_depth = 32;
  if (Video->v_height == 0 ||
      Video->v_rowBytes > MAX_UINT64 / Video->v_height) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Required = Video->v_rowBytes * Video->v_height;
  if (Video->v_baseAddr == 0 || (Video->v_baseAddr & 3) ||
      Video->v_width < 1184 || Video->v_height < 864 ||
      Gop->Mode->Info->PixelsPerScanLine < Video->v_width ||
      Required > Gop->Mode->FrameBufferSize) {
    return EFI_BAD_BUFFER_SIZE;
  }
  return EFI_SUCCESS;
}

STATIC VOID
B2lMark(const struct b2l_boot_video *Video, UINTN Index, UINT32 Color)
{
  volatile UINT32 *Fb;
  UINT64 X0, Y0;

  if (Video->v_baseAddr == 0 || Video->v_rowBytes == 0) {
    return;
  }
  Fb = (volatile UINT32 *)(UINTN)Video->v_baseAddr;
  X0 = Index * B2L_MARK_STEP + 8;
  Y0 = 8;
  if (X0 + B2L_MARK_SIZE > Video->v_width ||
      Y0 + B2L_MARK_SIZE > Video->v_height) {
    return;
  }
  for (UINT64 Y = 0; Y < B2L_MARK_SIZE; Y++) {
    for (UINT64 X = 0; X < B2L_MARK_SIZE; X++) {
      Fb[(Y0 + Y) * (Video->v_rowBytes / 4) + X0 + X] = Color;
    }
  }
  __asm__ volatile("dsb sy" ::: "memory");
}

STATIC VOID
B2lFail(const struct b2l_boot_video *Video, EFI_STATUS Status,
        CONST CHAR16 *Where)
{
  Print(L"B2L: FAIL %s: %r\r\n", Where, Status);
  if (Video != NULL) {
    for (UINTN I = 0; I < 16; I++) {
      B2lMark(Video, I, 0x00ff0000);
    }
  }
  CpuDeadLoop();
}

STATIC EFI_STATUS
ValidateArena(VOID *Map, UINTN MapSize, UINTN DescriptorSize,
              UINT64 Base, UINT64 Size)
{
  UINT8 *Cursor = Map;
  UINT8 *End = Cursor + MapSize;

  while (Cursor + DescriptorSize <= End) {
    EFI_MEMORY_DESCRIPTOR *Descriptor = (EFI_MEMORY_DESCRIPTOR *)(VOID *)Cursor;
    UINT64 Start = Descriptor->PhysicalStart;
    UINT64 Length = Descriptor->NumberOfPages * 4096;

    if (Descriptor->Type == EfiLoaderData &&
        Base >= Start && Base + Size <= Start + Length) {
      return EFI_SUCCESS;
    }
    Cursor += DescriptorSize;
  }
  return EFI_COMPROMISED_DATA;
}

STATIC VOID
CopyKernel(const struct b2l_macho *Macho, const struct b2l_payload *Payload,
           const UINT8 *PayloadBase, UINT64 ArenaBase)
{
  for (UINT32 I = 0; I < Macho->segment_count; I++) {
    const struct b2l_segment *Segment = &Macho->segments[I];
    UINT8 *Destination = (UINT8 *)(UINTN)(ArenaBase +
                                          (Segment->va - Macho->virtual_base));

    ZeroMem(Destination, (UINTN)Segment->size);
    CopyMem(Destination,
            PayloadBase + Payload->kernel_offset + Segment->file_offset,
            (UINTN)Segment->file_size);
  }
  __asm__ volatile("dsb sy" ::: "memory");
}

EFI_STATUS EFIAPI
UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  VOID *PayloadBase = NULL;
  UINTN PayloadSize = 0;
  struct b2l_payload Payload;
  struct b2l_macho Macho;
  struct b2l_boot_video Video = { 0 };
  struct b2l_boot_args_8796 Args;
  UINT64 Entry = 0;
  UINT64 KernelSpan;
  UINT64 DtPhysical;
  UINT64 ArgsPhysical;
  UINT64 TopOfKernelData;
  EFI_PHYSICAL_ADDRESS Arena = B2L_ARENA_BASE;
  EFI_LOADED_IMAGE_PROTOCOL *Image;
  UINTN MapSize = B2L_MAP_SIZE;
  UINTN MapKey = 0;
  UINTN DescriptorSize = 0;
  UINT32 DescriptorVersion = 0;
  VOID *Map = NULL;
  UINT64 Stack;
  UINT64 ImageBase;
  UINT64 ImageSize;
  STATIC CONST CHAR8 CommandLine[] =
    "-v debug=0x14e serial=3 cpus=8 keepsyms=1 b2l_native_cache=0";

  (VOID)SystemTable;
  Print(L"B2L: XNU 8796 loader for beyond2lte\r\n");

  if (ReadCurrentEl() != 1) {
    B2lFail(NULL, EFI_UNSUPPORTED, L"CurrentEL is not EL1");
  }

  Status = B2lLoadBootPayload(&PayloadBase, &PayloadSize);
  if (EFI_ERROR(Status)) {
    B2lFail(NULL, Status, L"no usable BOOT payload");
  }
  Status = (b2l_parse_payload(PayloadBase, (size_t)PayloadSize, &Payload) == 0)
           ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
  if (EFI_ERROR(Status)) {
    B2lFail(NULL, Status, L"payload validation");
  }
  Status = (b2l_parse_macho((UINT8 *)PayloadBase + Payload.kernel_offset,
                            (size_t)Payload.kernel_size, &Macho) == 0)
           ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
  if (EFI_ERROR(Status)) {
    B2lFail(NULL, Status, L"kernel Mach-O validation");
  }
  KernelSpan = Macho.virtual_end - Macho.virtual_base;
  if (KernelSpan == 0 || KernelSpan >= B2L_ARENA_SIZE) {
    B2lFail(NULL, EFI_COMPROMISED_DATA, L"kernel size");
  }
  Print(L"B2L: kernel %lu bytes, entry 0x%lx, ADT %lu bytes\r\n",
        KernelSpan, Macho.entry, Payload.adt_size);

  Status = GetVideo(&Video);
  if (EFI_ERROR(Status)) {
    B2lFail(NULL, Status, L"GOP framebuffer");
  }
  B2lMark(&Video, 0, 0x000000ff);

  Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData,
                              (UINTN)(B2L_ARENA_SIZE / 4096), &Arena);
  if (EFI_ERROR(Status) || Arena != B2L_ARENA_BASE) {
    if (!EFI_ERROR(Status)) {
      gBS->FreePages(Arena, (UINTN)(B2L_ARENA_SIZE / 4096));
      Status = EFI_COMPROMISED_DATA;
    }
    B2lFail(&Video, Status, L"fixed arena allocation");
  }
  B2lMark(&Video, 1, 0x0000ffff);

  Map = AllocatePool(B2L_MAP_SIZE);
  if (Map == NULL) {
    B2lFail(&Video, EFI_OUT_OF_RESOURCES, L"memory map buffer");
  }
  Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescriptorSize,
                             &DescriptorVersion);
  if (EFI_ERROR(Status)) {
    B2lFail(&Video, Status, L"GetMemoryMap");
  }
  Status = ValidateArena(Map, MapSize, DescriptorSize, B2L_ARENA_BASE,
                         B2L_ARENA_SIZE);
  if (EFI_ERROR(Status)) {
    B2lFail(&Video, Status, L"arena is not owned loader data");
  }

  CopyKernel(&Macho, &Payload, PayloadBase, B2L_ARENA_BASE);
  B2lMark(&Video, 2, 0x00ff00);

  DtPhysical = (B2L_ARENA_BASE + KernelSpan + 4095) & ~4095ULL;
  ArgsPhysical = (DtPhysical + Payload.adt_size + 7) & ~7ULL;
  if (DtPhysical < B2L_ARENA_BASE ||
      Payload.adt_size > B2L_ARENA_BASE + B2L_ARENA_SIZE - DtPhysical ||
      ArgsPhysical < DtPhysical ||
      sizeof(Args) > B2L_ARENA_BASE + B2L_ARENA_SIZE - ArgsPhysical) {
    B2lFail(&Video, EFI_COMPROMISED_DATA, L"ADT or boot_args placement");
  }
  CopyMem((VOID *)(UINTN)DtPhysical,
          (UINT8 *)PayloadBase + Payload.adt_offset,
          (UINTN)Payload.adt_size);
  __asm__ volatile("dsb sy" ::: "memory");

  Status = (b2l_fill_boot_args(&Macho, B2L_ARENA_BASE, B2L_ARENA_SIZE,
                               DtPhysical, (UINT32)Payload.adt_size, &Video,
                               ArgsPhysical,
                               CommandLine,
                               &Args, &Entry) == 0)
           ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
  if (EFI_ERROR(Status)) {
    B2lFail(&Video, Status, L"boot_args");
  }
  TopOfKernelData = Args.topOfKernelData;
  B2lMark(&Video, 3, 0x00ffff);

  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                               (VOID **)&Image);
  if (EFI_ERROR(Status)) {
    B2lFail(&Video, Status, L"loaded image");
  }
  ImageBase = (UINT64)(UINTN)Image->ImageBase;
  ImageSize = Image->ImageSize;
  __asm__ volatile("mov %0, sp" : "=r"(Stack));
  if (!IdentityRange(B2L_ARENA_BASE, TopOfKernelData - B2L_ARENA_BASE) ||
      !IdentityRange(ImageBase, ImageSize) ||
      !IdentityRange(Stack & ~4095ULL, 4096)) {
    B2lFail(&Video, EFI_UNSUPPORTED, L"firmware is not identity mapped");
  }

  Status = gBS->SetWatchdogTimer(0, 0, 0, NULL);
  if (EFI_ERROR(Status)) {
    B2lFail(&Video, Status, L"disable watchdog");
  }

  Print(L"B2L: entry PA 0x%lx, boot_args PA 0x%lx, ADT PA 0x%lx\r\n",
        Entry, ArgsPhysical, DtPhysical);
  Print(L"B2L: exiting boot services\r\n");

  /* No console, protocol or allocation calls after the first attempt. */
  for (UINTN Attempt = 0; Attempt < 8; Attempt++) {
    MapSize = B2L_MAP_SIZE;
    Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescriptorSize,
                               &DescriptorVersion);
    if (EFI_ERROR(Status)) {
      B2lFail(&Video, Status, L"GetMemoryMap before handoff");
    }
    if (EFI_ERROR(ValidateArena(Map, MapSize, DescriptorSize,
                                B2L_ARENA_BASE, B2L_ARENA_SIZE))) {
      B2lFail(&Video, EFI_COMPROMISED_DATA, L"arena lost before handoff");
    }
    Status = gBS->ExitBootServices(ImageHandle, MapKey);
    if (!EFI_ERROR(Status)) {
      B2lMark(&Video, 4, 0x00ff00);
      B2LXnuHandoff(Entry, ArgsPhysical, B2L_ARENA_BASE, TopOfKernelData);
      CpuDeadLoop();
    }
    if (Status != EFI_INVALID_PARAMETER) {
      B2lFail(&Video, Status, L"ExitBootServices");
    }
  }

  B2lFail(&Video, EFI_ABORTED, L"ExitBootServices retries");
  return EFI_ABORTED;
}
