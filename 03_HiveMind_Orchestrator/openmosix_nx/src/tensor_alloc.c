/*++
 * tensor_alloc.c — Huge Page Tensor Allocator
 *
 * HIVE-MOSIX-007: 1GB → 2MB → 4KB fallback cascade for F32 tensor memory
 *
 * Reference:
 *   - Interactive_Plan.md §VIII·5b (lines 3788-3847) — Huge Pages
 *   - Interactive_Plan.md §XIV·5 defconfig — CONFIG_HUGETLBFS
 *
 * Architecture:
 *   F32 model weights (492GB for 123B params) require huge pages to avoid
 *   TLB thrashing. With 4KB pages, 492GB = 130 million TLB entries.
 *   2MB huge pages reduce this by 512×. 1GB huge pages by 262144×.
 *
 *   Allocation cascade:
 *     1. Try 1GB huge pages (MAP_HUGE_1GB) — fewest TLB entries
 *     2. Fall back to 2MB huge pages (MAP_HUGETLB) — still excellent
 *     3. Last resort: regular 4KB pages — WARNING logged
 *
 *   Boot cmdline: hugepagesz=1G hugepages=64 hugepagesz=2M hugepages=4096
 *
 * Acceptance criteria: TLB miss rate < 0.1% under inference
 *--*/

#include "openmosix_tensor.h"

#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

// ── MAP_HUGE_1GB may not be defined on all systems ──────────────────────────
#ifndef MAP_HUGE_1GB
#define MAP_HUGE_1GB    (30 << MAP_HUGE_SHIFT)
#endif

#ifndef MAP_HUGE_SHIFT
#define MAP_HUGE_SHIFT  26
#endif

// ═══════════════════════════════════════════════════════════════════════════
// tensor_alloc_huge — Allocate tensor buffer on huge pages
//
// Reference: §VIII·5b lines 3798-3828
//
// Cascade: 1GB → 2MB → 4KB regular pages.
// All allocations are page-aligned for O_DIRECT NVMe compatibility.
// ═══════════════════════════════════════════════════════════════════════════

void* tensor_alloc_huge(size_t size_bytes)
{
    if (size_bytes == 0) return NULL;

    void* ptr;

    // ── Attempt 1: 1GB huge pages ──────────────────────────────────────
    // Fewest TLB entries. Requires hugepagesz=1G hugepages=64 on cmdline.
    size_t aligned_1g = (size_bytes + HUGEPAGE_1G - 1) & ~(HUGEPAGE_1G - 1);

    ptr = mmap(NULL, aligned_1g,
               PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB,
               -1, 0);

    if (ptr != MAP_FAILED) {
        fprintf(stderr, "[TENSOR_ALLOC] Allocated %zu bytes on 1GB huge pages "
                "(%zu pages)\n", aligned_1g, aligned_1g / HUGEPAGE_1G);
        return ptr;
    }

    // ── Attempt 2: 2MB huge pages ──────────────────────────────────────
    // Still reduces TLB by 512×. More likely to be available.
    size_t aligned_2m = (size_bytes + HUGEPAGE_2M - 1) & ~(HUGEPAGE_2M - 1);

    ptr = mmap(NULL, aligned_2m,
               PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
               -1, 0);

    if (ptr != MAP_FAILED) {
        fprintf(stderr, "[TENSOR_ALLOC] Allocated %zu bytes on 2MB huge pages "
                "(%zu pages)\n", aligned_2m, aligned_2m / HUGEPAGE_2M);
        return ptr;
    }

    // ── Attempt 3: Regular 4KB pages (last resort) ─────────────────────
    // Performance warning: TLB thrashing expected for large models
    size_t aligned_4k = (size_bytes + 4095) & ~4095ULL;

    fprintf(stderr, "[TENSOR_ALLOC] WARNING: huge pages unavailable — "
            "TLB thrashing expected for %zu bytes\n", aligned_4k);

    ptr = mmap(NULL, aligned_4k,
               PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS,
               -1, 0);

    if (ptr == MAP_FAILED) {
        fprintf(stderr, "[TENSOR_ALLOC] FATAL: mmap failed for %zu bytes: %s\n",
                aligned_4k, strerror(errno));
        return NULL;
    }

    return ptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// tensor_pin_memory — Pin pages to prevent swapping during inference
//
// Reference: §VIII·5b line 3831-3834
//
// Uses mlock() to prevent the OOM killer from evicting F32 weights.
// This is critical for inference stability — a page fault during
// matrix multiply would add milliseconds of latency.
// ═══════════════════════════════════════════════════════════════════════════

int tensor_pin_memory(void* ptr, size_t size)
{
    if (!ptr || size == 0) return -1;

    int ret = mlock(ptr, size);
    if (ret != 0) {
        fprintf(stderr, "[TENSOR_ALLOC] WARNING: mlock failed for %zu bytes: %s "
                "(check ulimit -l)\n", size, strerror(errno));
        return -1;
    }

    fprintf(stderr, "[TENSOR_ALLOC] Pinned %zu MB (%zu bytes)\n",
            size / (1024 * 1024), size);
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// tensor_free — Release tensor memory (unpin + unmap)
// ═══════════════════════════════════════════════════════════════════════════

void tensor_free(void* ptr, size_t size)
{
    if (!ptr || size == 0) return;

    // Unpin first (ignore error — may not have been pinned)
    munlock(ptr, size);

    // Unmap
    if (munmap(ptr, size) != 0) {
        fprintf(stderr, "[TENSOR_ALLOC] WARNING: munmap failed: %s\n",
                strerror(errno));
    }
}
