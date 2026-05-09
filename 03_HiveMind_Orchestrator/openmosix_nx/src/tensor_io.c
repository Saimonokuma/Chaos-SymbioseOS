/*++
 * tensor_io.c — io_uring Async Tensor I/O
 *
 * HIVE-MOSIX-006: IORING_SETUP_SQPOLL, 256-deep queue, O_DIRECT aligned
 *                 reads from NVMe TensorStore
 *
 * Reference:
 *   - Interactive_Plan.md §VIII·5a (lines 3718-3784) — io_uring Tensor I/O
 *
 * Architecture:
 *   All NVMe reads (F32 model shards from TensorStore) use io_uring
 *   instead of blocking read()/write() syscalls. SQPOLL mode eliminates
 *   context-switch overhead — critical when loading 492GB of F32 weights
 *   at 7GB/s from NVMe.
 *
 *   Key design:
 *     - IORING_SETUP_SQPOLL: kernel-side polling, zero syscalls
 *     - 256-deep queue: prefetch next shard while current runs inference
 *     - O_DIRECT + 4KB alignment: bypass page cache, straight to GPU memory
 *     - user_data field: correlate CQE completions with shard IDs
 *
 * Acceptance criteria:
 *   F32 shard loads at full NVMe bandwidth (~7GB/s);
 *   zero syscall overhead via SQPOLL
 *--*/

#include "openmosix_tensor.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <liburing.h>

// ── Module state ────────────────────────────────────────────────────────────
static struct io_uring g_TensorRing;
static int             g_TensorRingInitialized = 0;

// ═══════════════════════════════════════════════════════════════════════════
// tensor_io_init — Initialize io_uring at hive_mind startup
//
// Reference: §VIII·5a lines 3731-3744
//
// Called once from PID 1 init. Sets up SQPOLL mode for zero-syscall I/O.
// Falls back to pread() if io_uring is unavailable (old kernels).
// ═══════════════════════════════════════════════════════════════════════════

int tensor_io_init(void)
{
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    // SQPOLL: kernel polls the submission queue — no io_uring_enter() needed
    // sq_thread_idle: keep polling thread alive for 2s after last submission
    params.flags = IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 2000;

    int ret = io_uring_queue_init_params(URING_DEPTH, &g_TensorRing, &params);
    if (ret < 0) {
        fprintf(stderr, "[TENSOR_IO] io_uring init failed: %d (%s) — "
                "falling back to pread\n", ret, strerror(-ret));
        return ret;
    }

    g_TensorRingInitialized = 1;

    fprintf(stderr, "[TENSOR_IO] io_uring initialized: depth=%d SQPOLL=%s "
            "sq_thread_idle=%ums\n",
            URING_DEPTH,
            (params.flags & IORING_SETUP_SQPOLL) ? "yes" : "no",
            params.sq_thread_idle);

    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// tensor_async_load — Async read of F32 shard from NVMe TensorStore
//
// Reference: §VIII·5a lines 3746-3757
//
// Returns immediately — completion delivered via io_uring CQE.
// The aligned_buf MUST be 4096-byte aligned for O_DIRECT (§VIII·5a line 3727).
// user_data is returned in the CQE for shard ID correlation.
// ═══════════════════════════════════════════════════════════════════════════

int tensor_async_load(int fd, void* aligned_buf, size_t size, off_t offset,
                       uint64_t user_data)
{
    if (!g_TensorRingInitialized) {
        fprintf(stderr, "[TENSOR_IO] ERROR: io_uring not initialized\n");
        return -EINVAL;
    }

    // Validate O_DIRECT alignment (§VIII·5a line 3727)
    if ((uintptr_t)aligned_buf & (SHARD_ALIGN - 1)) {
        fprintf(stderr, "[TENSOR_IO] ERROR: buffer not %d-byte aligned "
                "(required for O_DIRECT)\n", SHARD_ALIGN);
        return -EINVAL;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&g_TensorRing);
    if (!sqe) {
        // Ring full — back-pressure
        fprintf(stderr, "[TENSOR_IO] WARNING: SQ full (%d depth) — "
                "back-pressure active\n", URING_DEPTH);
        return -EBUSY;
    }

    io_uring_prep_read(sqe, fd, aligned_buf, size, offset);
    io_uring_sqe_set_data64(sqe, user_data);  // Correlate with shard ID
    io_uring_submit(&g_TensorRing);

    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// tensor_reap_completions — Batch completion reaper
//
// Reference: §VIII·5a lines 3760-3776
//
// Called from hive_mind event loop. Non-blocking peek — returns immediately
// if no completions available. Invokes callback for each completed I/O
// with the shard_id (from user_data) and result (bytes read or -errno).
// ═══════════════════════════════════════════════════════════════════════════

int tensor_reap_completions(TensorCallback callback, int max_batch)
{
    if (!g_TensorRingInitialized || !callback) return 0;

    struct io_uring_cqe* cqe;
    int reaped = 0;

    while (reaped < max_batch) {
        int ret = io_uring_peek_cqe(&g_TensorRing, &cqe);
        if (ret == -EAGAIN) break;  // No more completions

        uint64_t shard_id = io_uring_cqe_get_data64(cqe);
        callback(shard_id, cqe->res);  // res = bytes read or -errno

        io_uring_cqe_seen(&g_TensorRing, cqe);
        reaped++;
    }

    return reaped;
}

// ═══════════════════════════════════════════════════════════════════════════
// tensor_io_destroy — Cleanup io_uring resources
// ═══════════════════════════════════════════════════════════════════════════

void tensor_io_destroy(void)
{
    if (g_TensorRingInitialized) {
        io_uring_queue_exit(&g_TensorRing);
        g_TensorRingInitialized = 0;
        fprintf(stderr, "[TENSOR_IO] io_uring destroyed\n");
    }
}
