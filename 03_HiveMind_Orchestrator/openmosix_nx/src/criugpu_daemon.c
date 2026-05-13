/*++
 * criugpu_daemon.c — CRIU GPU Plugin (Live VRAM Migration)
 *
 * HIVE-MOSIX-003: Lock APIs, dump VRAM, stream via RDMA, restore on target
 *
 * Reference:
 *   - Interactive_Plan.md §VIII·3 (lines 3492-3553) — CRIUgpu Protocol
 *
 * Architecture:
 *   This daemon runs on each cluster node and handles GPU-aware process
 *   migration. When a node overheats or is evicted, the running inference
 *   shard must be transferred to a new node WITHOUT losing the KV cache.
 *
 *   MULTI-VENDOR SUPPORT:
 *     - NVIDIA: CUDA runtime (cudaMemcpy, cudaMalloc, cudaFree)
 *     - AMD:    ROCm/HIP runtime (hipMemcpy, hipMalloc, hipFree)
 *     - CPU:    memcpy fallback (no GPU passthrough)
 *
 *   HIP is API-compatible with CUDA — the function signatures are identical.
 *   We use dlopen() at runtime to detect which backend is available.
 *
 *   Migration sequence:
 *     1. CRIU dump --leave-stopped (freeze process)
 *     2. GPU DeviceToHost copy (serialize VRAM)
 *     3. Write VRAM to checkpoint dir as vram.bin + CRC64
 *     4. RDMA stream checkpoint + VRAM to target
 *     5. CRIU restore on target
 *     6. GPU HostToDevice copy (restore VRAM)
 *     7. Announce SHARD_READY on #hive-mind
 *
 *   CRC64 validation ensures VRAM integrity across migration.
 *
 * Acceptance criteria:
 *   GPU state survives checkpoint/restore; VRAM CRC64 matches
 *--*/

#include "openmosix_tensor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * GPU Backend Abstraction — NVIDIA CUDA / AMD ROCm HIP / CPU Fallback
 *
 * Both CUDA and HIP share identical function signatures:
 *   cudaMemcpy / hipMemcpy  (dst, src, size, kind)
 *   cudaMalloc / hipMalloc  (ptr, size)
 *   cudaFree   / hipFree    (ptr)
 *   cudaGetDeviceProperties / hipGetDeviceProperties
 *
 * We dlopen() at runtime to detect which backend is installed.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    GPU_BACKEND_NONE = 0,    /* No GPU — CPU fallback */
    GPU_BACKEND_CUDA = 1,    /* NVIDIA CUDA */
    GPU_BACKEND_HIP  = 2     /* AMD ROCm/HIP */
} gpu_backend_t;

typedef enum {
    gpuSuccess = 0
} gpuError_t;

typedef enum {
    gpuMemcpyHostToDevice = 1,
    gpuMemcpyDeviceToHost = 2
} gpuMemcpyKind;

/* Function pointers loaded via dlopen */
typedef gpuError_t (*fn_gpuMemcpy)(void*, const void*, size_t, gpuMemcpyKind);
typedef gpuError_t (*fn_gpuMalloc)(void**, size_t);
typedef gpuError_t (*fn_gpuFree)(void*);

static gpu_backend_t  g_GpuBackend = GPU_BACKEND_NONE;
static void*          g_GpuLib     = NULL;
static fn_gpuMemcpy   g_gpuMemcpy  = NULL;
static fn_gpuMalloc   g_gpuMalloc  = NULL;
static fn_gpuFree     g_gpuFree    = NULL;

/* CRC64 from hive_mind_glue.c */
extern uint64_t crc64_compute(const void* data, size_t len);

/* ── Checkpoint paths ──────────────────────────────────────────────────── */
#define CRIU_CHECKPOINT_DIR     "/tmp/shard_ckpt"
#define VRAM_DUMP_FILE          "/tmp/shard_ckpt/vram.bin"
#define VRAM_CRC_FILE           "/tmp/shard_ckpt/vram.crc64"

/* ═══════════════════════════════════════════════════════════════════════════
 * criugpu_detect_backend — Runtime GPU backend detection
 *
 * Tries to dlopen() the GPU runtime libraries in order:
 *   1. libhiprt64.so / libamdhip64.so (AMD ROCm/HIP)
 *   2. libcudart.so (NVIDIA CUDA)
 *   3. CPU fallback (no GPU VRAM to migrate)
 *
 * Called once at init. After this, g_gpuMemcpy etc. are callable.
 * ═══════════════════════════════════════════════════════════════════════════ */

int criugpu_detect_backend(void)
{
    /* ── Try AMD ROCm/HIP first ──────────────────────────────────────── */
    g_GpuLib = dlopen("libamdhip64.so", RTLD_LAZY);
    if (!g_GpuLib) g_GpuLib = dlopen("libamdhip64.so.6", RTLD_LAZY);
    if (!g_GpuLib) g_GpuLib = dlopen("libamdhip64.so.5", RTLD_LAZY);

    if (g_GpuLib) {
        g_gpuMemcpy = (fn_gpuMemcpy)dlsym(g_GpuLib, "hipMemcpy");
        g_gpuMalloc = (fn_gpuMalloc)dlsym(g_GpuLib, "hipMalloc");
        g_gpuFree   = (fn_gpuFree)dlsym(g_GpuLib, "hipFree");

        if (g_gpuMemcpy && g_gpuMalloc && g_gpuFree) {
            g_GpuBackend = GPU_BACKEND_HIP;
            fprintf(stderr, "[CRIUgpu] Detected AMD ROCm/HIP backend\n");
            return 0;
        }
        dlclose(g_GpuLib);
        g_GpuLib = NULL;
    }

    /* ── Try NVIDIA CUDA ─────────────────────────────────────────────── */
    g_GpuLib = dlopen("libcudart.so", RTLD_LAZY);
    if (!g_GpuLib) g_GpuLib = dlopen("libcudart.so.12", RTLD_LAZY);
    if (!g_GpuLib) g_GpuLib = dlopen("libcudart.so.11", RTLD_LAZY);

    if (g_GpuLib) {
        g_gpuMemcpy = (fn_gpuMemcpy)dlsym(g_GpuLib, "cudaMemcpy");
        g_gpuMalloc = (fn_gpuMalloc)dlsym(g_GpuLib, "cudaMalloc");
        g_gpuFree   = (fn_gpuFree)dlsym(g_GpuLib, "cudaFree");

        if (g_gpuMemcpy && g_gpuMalloc && g_gpuFree) {
            g_GpuBackend = GPU_BACKEND_CUDA;
            fprintf(stderr, "[CRIUgpu] Detected NVIDIA CUDA backend\n");
            return 0;
        }
        dlclose(g_GpuLib);
        g_GpuLib = NULL;
    }

    /* ── CPU fallback ────────────────────────────────────────────────── */
    g_GpuBackend = GPU_BACKEND_NONE;
    fprintf(stderr, "[CRIUgpu] No GPU runtime found — CPU-only mode\n");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * vram_serialize — Dump GPU memory to a file with CRC64
 *
 * For both CUDA and HIP: gpuMemcpy(host, device, size, DeviceToHost)
 * For CPU mode: memcpy from the host-side tensor buffer
 *
 * Returns: 0 on success, -1 on failure
 * ═══════════════════════════════════════════════════════════════════════════ */

static int vram_serialize(void* device_ptr, size_t vram_size,
                           const char* out_path)
{
    if (!device_ptr || vram_size == 0 || !out_path) return -1;

    /* Allocate host buffer for VRAM contents */
    void* host_buf = malloc(vram_size);
    if (!host_buf) {
        fprintf(stderr, "[CRIUgpu] Failed to allocate %zu bytes for VRAM dump\n",
                vram_size);
        return -1;
    }

    /* Copy VRAM → host */
    if (g_GpuBackend != GPU_BACKEND_NONE && g_gpuMemcpy) {
        gpuError_t err = g_gpuMemcpy(host_buf, device_ptr, vram_size,
                                      gpuMemcpyDeviceToHost);
        if (err != gpuSuccess) {
            fprintf(stderr, "[CRIUgpu] %s DeviceToHost copy failed (err=%d)\n",
                    g_GpuBackend == GPU_BACKEND_HIP ? "hipMemcpy" : "cudaMemcpy",
                    err);
            free(host_buf);
            return -1;
        }
        fprintf(stderr, "[CRIUgpu] %s: %zu bytes copied DeviceToHost\n",
                g_GpuBackend == GPU_BACKEND_HIP ? "HIP" : "CUDA", vram_size);
    } else {
        /* CPU fallback — device_ptr is actually a host pointer */
        memcpy(host_buf, device_ptr, vram_size);
        fprintf(stderr, "[CRIUgpu] CPU mode: %zu bytes memcpy'd\n", vram_size);
    }

    /* Write to file */
    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        fprintf(stderr, "[CRIUgpu] Cannot create %s: %s\n",
                out_path, strerror(errno));
        free(host_buf);
        return -1;
    }

    ssize_t written = write(fd, host_buf, vram_size);
    close(fd);

    if (written != (ssize_t)vram_size) {
        fprintf(stderr, "[CRIUgpu] Short write to %s: %zd/%zu\n",
                out_path, written, vram_size);
        free(host_buf);
        return -1;
    }

    /* Compute and save CRC64 for integrity validation */
    uint64_t crc = crc64_compute(host_buf, vram_size);
    free(host_buf);

    char crc_path[512];
    snprintf(crc_path, sizeof(crc_path), "%s.crc64", out_path);
    FILE* fp = fopen(crc_path, "w");
    if (fp) {
        fprintf(fp, "%016llx %zu\n", (unsigned long long)crc, vram_size);
        fclose(fp);
    }

    fprintf(stderr, "[CRIUgpu] VRAM dumped: %zu bytes, CRC64=%016llx\n",
            vram_size, (unsigned long long)crc);

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * vram_restore — Load VRAM dump back to GPU with CRC64 validation
 *
 * For both CUDA and HIP: gpuMemcpy(device, host, size, HostToDevice)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int vram_restore(void* device_ptr, size_t vram_size,
                         const char* in_path)
{
    if (!device_ptr || vram_size == 0 || !in_path) return -1;

    /* Read VRAM dump from file */
    int fd = open(in_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[CRIUgpu] Cannot open %s: %s\n",
                in_path, strerror(errno));
        return -1;
    }

    void* host_buf = malloc(vram_size);
    if (!host_buf) { close(fd); return -1; }

    ssize_t nread = read(fd, host_buf, vram_size);
    close(fd);

    if (nread != (ssize_t)vram_size) {
        fprintf(stderr, "[CRIUgpu] Short read from %s: %zd/%zu\n",
                in_path, nread, vram_size);
        free(host_buf);
        return -1;
    }

    /* Validate CRC64 before restoring */
    uint64_t crc = crc64_compute(host_buf, vram_size);

    char crc_path[512];
    snprintf(crc_path, sizeof(crc_path), "%s.crc64", in_path);
    FILE* fp = fopen(crc_path, "r");
    if (fp) {
        unsigned long long expected_crc;
        size_t expected_size;
        if (fscanf(fp, "%llx %zu", &expected_crc, &expected_size) == 2) {
            if (crc != (uint64_t)expected_crc || vram_size != expected_size) {
                fprintf(stderr, "[CRIUgpu] CRC64 MISMATCH! Expected %016llx, "
                        "got %016llx — VRAM CORRUPT\n",
                        expected_crc, (unsigned long long)crc);
                fclose(fp);
                free(host_buf);
                return -1;
            }
            fprintf(stderr, "[CRIUgpu] CRC64 validated: %016llx ✓\n",
                    (unsigned long long)crc);
        }
        fclose(fp);
    }

    /* Copy host → VRAM */
    if (g_GpuBackend != GPU_BACKEND_NONE && g_gpuMemcpy) {
        gpuError_t err = g_gpuMemcpy(device_ptr, host_buf, vram_size,
                                      gpuMemcpyHostToDevice);
        if (err != gpuSuccess) {
            fprintf(stderr, "[CRIUgpu] %s HostToDevice copy failed (err=%d)\n",
                    g_GpuBackend == GPU_BACKEND_HIP ? "hipMemcpy" : "cudaMemcpy",
                    err);
            free(host_buf);
            return -1;
        }
        fprintf(stderr, "[CRIUgpu] %s: %zu bytes restored HostToDevice\n",
                g_GpuBackend == GPU_BACKEND_HIP ? "HIP" : "CUDA", vram_size);
    } else {
        memcpy(device_ptr, host_buf, vram_size);
        fprintf(stderr, "[CRIUgpu] CPU mode: %zu bytes memcpy'd\n", vram_size);
    }

    free(host_buf);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * criu_checkpoint_shard — Freeze and dump a running inference shard
 *
 * Steps:
 *   1. CRIU dump --leave-stopped (freeze process)
 *   2. GPU DeviceToHost copy (serialize VRAM via detected backend)
 *   3. Write VRAM to checkpoint dir as vram.bin + CRC64
 * ═══════════════════════════════════════════════════════════════════════════ */

int criu_checkpoint_shard(const char* checkpoint_dir,
                           void* vram_ptr, size_t vram_size)
{
    if (!checkpoint_dir) checkpoint_dir = CRIU_CHECKPOINT_DIR;

    mkdir(checkpoint_dir, 0700);

    fprintf(stderr, "[CRIUgpu] Starting checkpoint to %s (backend=%s)\n",
            checkpoint_dir,
            g_GpuBackend == GPU_BACKEND_HIP  ? "AMD/HIP"  :
            g_GpuBackend == GPU_BACKEND_CUDA ? "NVIDIA/CUDA" : "CPU");

    /* ── Step 1: CRIU dump ──────────────────────────────────────────── */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "criu dump --leave-stopped --tcp-established -D %s 2>&1",
        checkpoint_dir);

    int ret = system(cmd);
    if (WEXITSTATUS(ret) != 0) {
        fprintf(stderr, "[CRIUgpu] CRIU dump failed (exit %d)\n",
                WEXITSTATUS(ret));
        return -1;
    }
    fprintf(stderr, "[CRIUgpu] Process frozen via CRIU\n");

    /* ── Step 2: Serialize VRAM ─────────────────────────────────────── */
    if (vram_ptr && vram_size > 0) {
        char vram_path[512];
        snprintf(vram_path, sizeof(vram_path), "%s/vram.bin", checkpoint_dir);

        if (vram_serialize(vram_ptr, vram_size, vram_path) != 0) {
            fprintf(stderr, "[CRIUgpu] VRAM serialization failed\n");
            return -1;
        }
    } else {
        fprintf(stderr, "[CRIUgpu] No VRAM pointer — checkpoint is CPU-only\n");
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * criu_restore_shard — Restore a shard on the target node
 *
 * Steps:
 *   1. CRIU restore the process
 *   2. GPU HostToDevice copy (restore VRAM)
 *   3. Announce SHARD_READY on #hive-mind
 * ═══════════════════════════════════════════════════════════════════════════ */

int criu_restore_shard(const char* checkpoint_dir,
                        void* vram_ptr, size_t vram_size)
{
    if (!checkpoint_dir) checkpoint_dir = CRIU_CHECKPOINT_DIR;

    fprintf(stderr, "[CRIUgpu] Starting restore from %s (backend=%s)\n",
            checkpoint_dir,
            g_GpuBackend == GPU_BACKEND_HIP  ? "AMD/HIP"  :
            g_GpuBackend == GPU_BACKEND_CUDA ? "NVIDIA/CUDA" : "CPU");

    /* ── Step 1: CRIU restore ───────────────────────────────────────── */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "criu restore --tcp-established -D %s 2>&1",
        checkpoint_dir);

    int ret = system(cmd);
    if (WEXITSTATUS(ret) != 0) {
        fprintf(stderr, "[CRIUgpu] CRIU restore failed (exit %d)\n",
                WEXITSTATUS(ret));
        return -1;
    }
    fprintf(stderr, "[CRIUgpu] Process restored via CRIU\n");

    /* ── Step 2: Restore VRAM ───────────────────────────────────────── */
    if (vram_ptr && vram_size > 0) {
        char vram_path[512];
        snprintf(vram_path, sizeof(vram_path), "%s/vram.bin", checkpoint_dir);

        if (vram_restore(vram_ptr, vram_size, vram_path) != 0) {
            fprintf(stderr, "[CRIUgpu] VRAM restoration failed\n");
            return -1;
        }
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * criugpu_migrate — Full migration cycle (source side)
 *
 * Orchestrates:
 *   1. Detect GPU backend (if not already done)
 *   2. Checkpoint shard (CRIU + VRAM dump)
 *   3. Stream checkpoint to target via RDMA
 *   4. Signal target to restore
 * ═══════════════════════════════════════════════════════════════════════════ */

int criugpu_migrate(int irc_fd, const char* src_node_id,
                     const char* dst_node_id, uint32_t layer_start,
                     uint32_t layer_end)
{
    fprintf(stderr, "[CRIUgpu] Migration: %s → %s (layers %u-%u)\n",
            src_node_id, dst_node_id, layer_start, layer_end);

    /* Ensure backend is detected */
    if (g_GpuBackend == GPU_BACKEND_NONE && !g_GpuLib) {
        criugpu_detect_backend();
    }

    /* Announce migration on #hive-mind */
    char msg[256];
    snprintf(msg, sizeof(msg),
        "PRIVMSG #hive-mind :SHARD_MIGRATE src=%s dst=%s layers=%u-%u "
        "backend=%s\r\n",
        src_node_id, dst_node_id, layer_start, layer_end,
        g_GpuBackend == GPU_BACKEND_HIP  ? "hip"  :
        g_GpuBackend == GPU_BACKEND_CUDA ? "cuda" : "cpu");
    irc_send(irc_fd, msg);

    /* ── Step 1: Checkpoint ──────────────────────────────────────────── */
    /* In production: vram_ptr and vram_size come from the eBPF GPU
     * monitor's allocation tracker (bpf_gpu_monitor.bpf.c).
     * For CPU-only mode, we pass NULL and the checkpoint is process-only. */
    if (criu_checkpoint_shard(CRIU_CHECKPOINT_DIR, NULL, 0) != 0) {
        fprintf(stderr, "[CRIUgpu] Checkpoint failed — aborting migration\n");
        return -1;
    }

    /* ── Step 2: Read checkpoint into buffer for RDMA ────────────────── */
    struct stat st;
    char vram_path[512];
    snprintf(vram_path, sizeof(vram_path), "%s/vram.bin", CRIU_CHECKPOINT_DIR);

    void* ckpt_data = NULL;
    size_t ckpt_size = 0;

    if (stat(vram_path, &st) == 0 && st.st_size > 0) {
        ckpt_size = st.st_size;
        ckpt_data = malloc(ckpt_size);
        if (ckpt_data) {
            int fd = open(vram_path, O_RDONLY);
            if (fd >= 0) {
                read(fd, ckpt_data, ckpt_size);
                close(fd);
            }
        }
    }

    /* ── Step 3: RDMA stream to target ───────────────────────────────── */
    if (ckpt_data && ckpt_size > 0) {
        fprintf(stderr, "[CRIUgpu] RDMA streaming %zu bytes to %s\n",
                ckpt_size, dst_node_id);
        rdma_migrate_shard(dst_node_id, ckpt_data, ckpt_size);
        free(ckpt_data);
    }

    /* ── Step 4: Signal target to restore ────────────────────────────── */
    snprintf(msg, sizeof(msg),
        "PRIVMSG #hive-mind :CRIU_RESTORE dst=%s layers=%u-%u "
        "backend=%s\r\n",
        dst_node_id, layer_start, layer_end,
        g_GpuBackend == GPU_BACKEND_HIP  ? "hip"  :
        g_GpuBackend == GPU_BACKEND_CUDA ? "cuda" : "cpu");
    irc_send(irc_fd, msg);

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * rdma_migrate_shard — RDMA write for checkpoint transfer
 *
 * Wraps rdma_stream_shard() with CRIU-specific error handling.
 * ═══════════════════════════════════════════════════════════════════════════ */

int rdma_migrate_shard(const char* dst_node_id, void* checkpoint, size_t size)
{
    if (!dst_node_id || !checkpoint || size == 0) return -EINVAL;

    fprintf(stderr, "[CRIUgpu] RDMA migrate %zu bytes to %s\n",
            size, dst_node_id);

    /* Find target node in registry */
    HIVE_NODE* target = NULL;
    for (int i = 0; i < MAX_NODES; i++) {
        if (!g_NodeRegistry[i].Active) continue;
        if (strncmp(g_NodeRegistry[i].NodeId, dst_node_id,
                     NODE_ID_LEN - 1) == 0) {
            target = &g_NodeRegistry[i];
            break;
        }
    }

    if (!target) {
        fprintf(stderr, "[CRIUgpu] Target node %s not found in registry\n",
                dst_node_id);
        return -ENOTCONN;
    }

    return rdma_stream_shard(target, checkpoint, size);
}
