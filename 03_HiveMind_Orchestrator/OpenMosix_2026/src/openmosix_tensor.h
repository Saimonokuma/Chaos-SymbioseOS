// // 03_HiveMind_Orchestrator/OpenMosix_2026/src/openmosix_tensor.h
// Crucible: PATTERN-008 (Race conditions in memory locking)

#ifndef OPENMOSIX_TENSOR_H
#define OPENMOSIX_TENSOR_H

#include <stdbool.h>
#include <stdint.h>

// FIX 24/25: Added platform guard to prevent WDK compilation failures on
// Windows
#ifdef __linux__
#include <sys/mman.h>
#endif

#define OMOSIX_PORT 723
#define MAX_TENSOR_DIM 8192

typedef enum _MOSIX_NODE_ROLE {
  NODE_ORACLE = 0, // Central 110B+ model
  NODE_SCOUT = 1   // 12B/24B dynamic models
} MOSIX_NODE_ROLE;

typedef struct _TENSOR_STATE_PAYLOAD {
  uint64_t model_id;
  size_t kv_cache_size;
  void *kv_cache_ptr; // Physical address mapped via VFS
  bool is_locked_in_ram;
} TENSOR_STATE_PAYLOAD;

// Lock Memory Descriptor Lists before migrating LLM weights across cluster
#ifdef __linux__
static inline int mosix_lock_and_migrate_tensor(pid_t scout_pid,
                                                uint32_t target_ip) {
  mlockall(MCL_CURRENT | MCL_FUTURE); // Prevent page fault during TCP transfer
  // return perform_mosix_transfer(scout_pid, target_ip);
  return 0;
}
#else
static inline int mosix_lock_and_migrate_tensor(int scout_pid,
                                                uint32_t target_ip) {
  // Stub for Windows compilation
  (void)scout_pid;
  (void)target_ip;
  return 0;
}
#endif

#endif // OPENMOSIX_TENSOR_H
