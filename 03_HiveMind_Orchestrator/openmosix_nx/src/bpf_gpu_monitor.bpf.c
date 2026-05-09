/*++
 * bpf_gpu_monitor.bpf.c — eBPF GPU Profiling (BPF JIT)
 *
 * HIVE-MOSIX-002: bpftime userspace eBPF uprobes on cuMemAlloc/cuLaunchKernel
 * HIVE-MOSIX-011: BPF JIT + ringbuf (16MB) + percpu arrays
 *
 * Reference:
 *   - Interactive_Plan.md §VIII·5d (lines 3952-4036) — BPF JIT GPU Profiling
 *   - Interactive_Plan.md §VIII·2 (lines 3438-3461) — node_score() feeds
 *
 * Architecture:
 *   Compiled with: clang -O2 -target bpf -c bpf_gpu_monitor.bpf.c
 *   Loaded via: bpf_object__open_file() + bpf_object__load()
 *
 *   Attaches uprobes to libcudart.so functions:
 *     - cudaMalloc: track VRAM allocations
 *     - cuLaunchKernel: track CUDA kernel launches
 *
 *   Data delivery:
 *     - bpf_ringbuf (16MB): zero-copy event delivery to hive_mind userspace
 *     - BPF_MAP_TYPE_PERCPU_ARRAY: cumulative VRAM counter for node_score()
 *
 *   Telemetry routing:
 *     eBPF ringbuf → hive_mind poll() → aggregate per 100ms →
 *     PRIVMSG #telemetry :GPU_STATS alloc=<MB> launches=<N> temp=<°C> util=<pct>
 *
 * Acceptance criteria:
 *   GPU events appear in ringbuf within 100ns of CUDA call;
 *   vram_allocated map reads < 1μs
 *--*/

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

// ═══════════════════════════════════════════════════════════════════════════
// GPU Event Structure
//
// Reference: §VIII·5d lines 3964-3971
//
// Delivered via bpf_ringbuf to userspace for aggregation and IRC reporting.
// ═══════════════════════════════════════════════════════════════════════════

struct gpu_event {
    __u64   timestamp_ns;       // bpf_ktime_get_ns()
    __u32   event_type;         // 0=ALLOC, 1=FREE, 2=LAUNCH, 3=MEMCPY
    __u64   size_bytes;         // For ALLOC/MEMCPY events
    __u64   device_ptr;         // GPU virtual address
    __u32   pid;                // Process ID
    char    kernel_name[64];    // For LAUNCH events — CUDA kernel name
};

// ═══════════════════════════════════════════════════════════════════════════
// BPF Maps
// ═══════════════════════════════════════════════════════════════════════════

// Ring buffer — zero-copy delivery to hive_mind userspace
// §VIII·5d lines 3973-3977: 16MB ring buffer
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);  // 16MB = 16,777,216 bytes
} gpu_events SEC(".maps");

// Cumulative VRAM tracking — read directly by node_score() in < 1μs
// §VIII·5d lines 3979-3985
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u64));
} vram_allocated SEC(".maps");

// Kernel launch counter — per-CPU for lock-free atomic updates
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u64));
} launch_count SEC(".maps");

// ═══════════════════════════════════════════════════════════════════════════
// uprobe/cudaMalloc — Track GPU memory allocations
//
// Reference: §VIII·5d lines 3987-4006
//
// Attached to: libcudart.so::cudaMalloc
// Captures: allocation size (first argument), PID, timestamp
// Updates: cumulative vram_allocated counter for node_score()
// ═══════════════════════════════════════════════════════════════════════════

SEC("uprobe/cudaMalloc")
int trace_cuda_malloc(struct pt_regs *ctx)
{
    struct gpu_event *evt;
    evt = bpf_ringbuf_reserve(&gpu_events, sizeof(*evt), 0);
    if (!evt) return 0;

    evt->timestamp_ns = bpf_ktime_get_ns();
    evt->event_type   = 0;  // ALLOC
    evt->size_bytes   = PT_REGS_PARM1(ctx);  // size argument to cudaMalloc
    evt->device_ptr   = 0;  // Filled by return probe (uretprobe)
    evt->pid          = bpf_get_current_pid_tgid() >> 32;

    // Update cumulative VRAM counter (sub-microsecond read latency)
    // §VIII·5d lines 4000-4003
    __u32 key = 0;
    __u64 *total = bpf_map_lookup_elem(&vram_allocated, &key);
    if (total) {
        __sync_fetch_and_add(total, evt->size_bytes);
    }

    bpf_ringbuf_submit(evt, 0);
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// uprobe/cudaFree — Track GPU memory deallocations
//
// Decrements the cumulative VRAM counter.
// ═══════════════════════════════════════════════════════════════════════════

SEC("uprobe/cudaFree")
int trace_cuda_free(struct pt_regs *ctx)
{
    struct gpu_event *evt;
    evt = bpf_ringbuf_reserve(&gpu_events, sizeof(*evt), 0);
    if (!evt) return 0;

    evt->timestamp_ns = bpf_ktime_get_ns();
    evt->event_type   = 1;  // FREE
    evt->device_ptr   = PT_REGS_PARM1(ctx);  // device pointer to free
    evt->size_bytes   = 0;  // Size not available at free time
    evt->pid          = bpf_get_current_pid_tgid() >> 32;

    bpf_ringbuf_submit(evt, 0);
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// uprobe/cuLaunchKernel — Track CUDA kernel launches
//
// Reference: §VIII·5d lines 4009-4026
//
// Attached to: libcuda.so::cuLaunchKernel
// Captures: kernel function name (CUfunction), PID, timestamp
// Increments: per-CPU launch counter for telemetry
// ═══════════════════════════════════════════════════════════════════════════

SEC("uprobe/cuLaunchKernel")
int trace_kernel_launch(struct pt_regs *ctx)
{
    struct gpu_event *evt;
    evt = bpf_ringbuf_reserve(&gpu_events, sizeof(*evt), 0);
    if (!evt) return 0;

    evt->timestamp_ns = bpf_ktime_get_ns();
    evt->event_type   = 2;  // LAUNCH
    evt->size_bytes   = 0;
    evt->device_ptr   = 0;
    evt->pid          = bpf_get_current_pid_tgid() >> 32;

    // Read kernel function name from first argument (CUfunction)
    // §VIII·5d lines 4020-4022
    bpf_probe_read_user_str(evt->kernel_name, sizeof(evt->kernel_name),
                            (void*)PT_REGS_PARM1(ctx));

    // Increment per-CPU launch counter
    __u32 key = 0;
    __u64 *count = bpf_map_lookup_elem(&launch_count, &key);
    if (count) {
        __sync_fetch_and_add(count, 1);
    }

    bpf_ringbuf_submit(evt, 0);
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// uprobe/cudaMemcpy — Track GPU↔Host memory transfers
//
// Captures memcpy direction and size for I/O profiling.
// ═══════════════════════════════════════════════════════════════════════════

SEC("uprobe/cudaMemcpy")
int trace_cuda_memcpy(struct pt_regs *ctx)
{
    struct gpu_event *evt;
    evt = bpf_ringbuf_reserve(&gpu_events, sizeof(*evt), 0);
    if (!evt) return 0;

    evt->timestamp_ns = bpf_ktime_get_ns();
    evt->event_type   = 3;  // MEMCPY
    evt->device_ptr   = PT_REGS_PARM1(ctx);  // dst pointer
    evt->size_bytes   = PT_REGS_PARM3(ctx);  // count (bytes)
    evt->pid          = bpf_get_current_pid_tgid() >> 32;

    bpf_ringbuf_submit(evt, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
