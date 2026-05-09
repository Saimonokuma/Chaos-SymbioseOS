/*++
 * boot_params.c — Linux Boot Protocol 2.13 Zero-Page Builder
 *
 * HIVE-LOADER-003: Build the boot_params structure for the Linux kernel
 *
 * Reference: Interactive_Plan.md §V·2 (lines 2231-2317)
 *
 * Purpose:
 *   Constructs the boot_params "zero page" in a local buffer.
 *   This struct is sent to the KMDF driver via IOCTL_SYMBIOSE_SET_BOOT_PARAMS,
 *   which writes it into guest RAM at GPA 0x10000.
 *
 * The zero page tells the Linux kernel:
 *   - Where to find the initrd (ramdisk_image/ramdisk_size)
 *   - The kernel command line (cmd_line_ptr)
 *   - The physical memory map (e820_table)
 *   - Boot protocol version and loader type
 *
 * Key field offsets (from arch/x86/include/uapi/asm/bootparam.h):
 *   0x01E8 — e820_entries (UINT8)
 *   0x0202 — header magic "HdrS" (0x53726448)
 *   0x0206 — version (0x020D = protocol 2.13)
 *   0x0210 — type_of_loader (0xFF = undefined)
 *   0x0211 — loadflags (0x01 = LOADED_HIGH)
 *   0x0214 — code32_start (0x100000 = 1MB)
 *   0x0218 — ramdisk_image (GPA of initrd)
 *   0x021C — ramdisk_size
 *   0x0228 — cmd_line_ptr (GPA of command line string)
 *   0x0236 — xloadflags (0x0003 = KERNEL_64 | ABOVE_4G)
 *   0x02D0 — e820_table[] (20 bytes per entry)
 *--*/

#include <windows.h>
#include <stdio.h>
#include <string.h>

// ── Boot Protocol Constants ─────────────────────────────────────────────────
#define BOOT_PARAMS_SIZE    4096        // One 4KB page
#define HDRS_MAGIC          0x53726448  // "HdrS"
#define BOOT_PROTOCOL_VER   0x020D      // Protocol 2.13
#define LOADER_TYPE_UNKNOWN 0xFF
#define LOADFLAGS_HIGH      0x01        // Kernel loaded above 1MB
#define XLOADFLAGS_64BIT    0x0003      // KERNEL_64 | CAN_BE_LOADED_ABOVE_4G
#define CODE32_START        0x100000    // 1MB — protected mode entry

// Guest physical addresses for boot structures
#define GPA_BOOT_PARAMS     0x10000ULL  // 64KB mark
#define GPA_CMDLINE         0x11000ULL  // 68KB mark (just after boot_params)

// e820 memory type constants
#define E820_RAM            1
#define E820_RESERVED       2

// ── Kernel command line ─────────────────────────────────────────────────────
// console=ttyS0 — guest serial output intercepted by VM-Exit handler
// earlyprintk=serial — early boot messages via serial port
// nomodeset — no GPU mode setting (GPU is DDA passthrough)
// quiet — reduce kernel verbosity
// init=/symbiose/hive_mind — PID 1 is the hive mind orchestrator
//
static const char* GUEST_CMDLINE =
    "console=ttyS0 earlyprintk=serial nomodeset quiet "
    "init=/symbiose/hive_mind";

// ── BuildBootParams ─────────────────────────────────────────────────────────
//
// Constructs a boot_params zero page in a caller-provided 4KB buffer.
//
// Parameters:
//   buffer      — 4KB output buffer (will be zeroed)
//   initrdGpa   — Guest physical address of initrd in guest RAM
//   initrdSize  — Size of initrd in bytes
//   guestRamSize — Total guest RAM size in bytes
//   cmdlineGpa  — GPA where cmdline string will be placed
//   cmdlineOut  — Output: pointer to cmdline string (caller writes to guest)
//   cmdlineLen  — Output: length of cmdline including null terminator
//
void BuildBootParams(
    UINT8*  buffer,
    UINT64  initrdGpa,
    UINT64  initrdSize,
    UINT64  guestRamSize,
    UINT64  cmdlineGpa,
    const char** cmdlineOut,
    UINT32* cmdlineLen
    )
{
    // Zero the entire 4KB page
    ZeroMemory(buffer, BOOT_PARAMS_SIZE);

    // ── setup_header fields ─────────────────────────────────────────────
    // Reference: §V·2 (lines 2267-2284)

    // Header magic — kernel checks this to validate boot protocol
    *(UINT32*)(buffer + 0x0202) = HDRS_MAGIC;

    // Protocol version 2.13
    *(UINT16*)(buffer + 0x0206) = BOOT_PROTOCOL_VER;

    // Loader type: unknown (we're a custom hypervisor loader)
    *(UINT8*)(buffer + 0x0210) = LOADER_TYPE_UNKNOWN;

    // Loadflags: kernel is loaded at high memory (above 1MB)
    *(UINT8*)(buffer + 0x0211) = LOADFLAGS_HIGH;

    // Protected mode entry point
    *(UINT32*)(buffer + 0x0214) = CODE32_START;

    // Extended load flags: 64-bit kernel, can load above 4GB
    *(UINT16*)(buffer + 0x0236) = XLOADFLAGS_64BIT;

    // ── Initrd location ─────────────────────────────────────────────────
    *(UINT32*)(buffer + 0x0218) = (UINT32)initrdGpa;
    *(UINT32*)(buffer + 0x021C) = (UINT32)initrdSize;

    // ── Command line pointer ────────────────────────────────────────────
    *(UINT32*)(buffer + 0x0228) = (UINT32)cmdlineGpa;

    // ── e820 memory map ─────────────────────────────────────────────────
    // Reference: §V·2 (lines 2286-2304)
    //
    // Each entry: { UINT64 addr, UINT64 size, UINT32 type } = 20 bytes
    //
    UINT8* e820 = buffer + 0x02D0;
    UINT8  e820Count = 0;

    // Entry 0: Low conventional memory 0x0 → 0x9FFFF (640KB)
    *(UINT64*)(e820 + 0)  = 0x0;
    *(UINT64*)(e820 + 8)  = 0xA0000;
    *(UINT32*)(e820 + 16) = E820_RAM;
    e820 += 20;
    e820Count++;

    // Entry 1: Reserved area 0xA0000 → 0xFFFFF (VGA + ROM, 384KB)
    *(UINT64*)(e820 + 0)  = 0xA0000;
    *(UINT64*)(e820 + 8)  = 0x60000;  // 0x100000 - 0xA0000
    *(UINT32*)(e820 + 16) = E820_RESERVED;
    e820 += 20;
    e820Count++;

    // Entry 2: Main guest RAM 0x100000 → guestRamSize (above 1MB)
    *(UINT64*)(e820 + 0)  = 0x100000;
    *(UINT64*)(e820 + 8)  = guestRamSize - 0x100000;
    *(UINT32*)(e820 + 16) = E820_RAM;
    e820 += 20;
    e820Count++;

    // Write e820 entry count
    *(UINT8*)(buffer + 0x01E8) = e820Count;

    // ── Return cmdline info to caller ───────────────────────────────────
    if (cmdlineOut) *cmdlineOut = GUEST_CMDLINE;
    if (cmdlineLen) *cmdlineLen = (UINT32)(strlen(GUEST_CMDLINE) + 1);

    printf("  boot_params: HdrS=0x%X proto=0x%X initrd=0x%llX(%llu) "
           "cmdline=0x%llX e820=%u entries\n",
           HDRS_MAGIC, BOOT_PROTOCOL_VER,
           initrdGpa, initrdSize,
           cmdlineGpa, e820Count);
}
