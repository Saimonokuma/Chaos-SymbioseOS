// 03_HiveMind_Orchestrator/VFS_Storage_Manager/src/vfs_manager.h
// Crucible: PATTERN-008 (TOCTOU prevention), PATTERN-015 (cleanup on error)

#ifndef VFS_MANAGER_H
#define VFS_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================
// Configuration
// ============================================================

#define VFS_MAGIC				0x56465331	// "VFS1"
#define VFS_VERSION				1
#define VFS_PAGE_SIZE			4096
#define VFS_VECTOR_DIMENSION	768			// LLM embedding dimension
#define VFS_MAX_DEVICES			4
#define VFS_MAX_COLLECTIONS		256

// ============================================================
// Data Structures
// ============================================================

typedef enum _VFS_STATUS {
	VfsStatusOk = 0,
	VfsStatusError,
	VfsStatusNotFound,
	VfsStatusCorrupted,
	VfsStatusDeviceBusy,
	VfsStatusOutOfSpace,
	VfsStatusInvalidParameter
} VFS_STATUS;

typedef struct _VFS_DEVICE_INFO {
	uint32_t device_id;
	uint32_t block_size;
	uint64_t total_blocks;
	uint64_t used_blocks;
	uint64_t free_blocks;
	bool is_isolated;			// TRUE if Windows NTFS driver is detached
} VFS_DEVICE_INFO, *PVFS_DEVICE_INFO;

typedef struct _VFS_VECTOR_ID {
	uint64_t collection_id;
	uint64_t vector_id;
} VFS_VECTOR_ID, *PVFS_VECTOR_ID;

typedef struct _VFS_VECTOR_ENTRY {
	VFS_VECTOR_ID id;
	float vector[VFS_VECTOR_DIMENSION];
	uint64_t timestamp;
	uint32_t metadata_len;
	uint8_t metadata[];			// Flexible array member
} VFS_VECTOR_ENTRY, *PVFS_VECTOR_ENTRY;

typedef struct _VFS_COLLECTION {
	uint64_t id;
	char name[256];
	uint64_t vector_count;
	uint64_t created_at;
	uint64_t updated_at;
	VFS_DEVICE_INFO *device;	// Owning device
} VFS_COLLECTION, *PVFS_COLLECTION;

// ============================================================
// SPDK Integration (Ring-0 NVMe Access)
// ============================================================

typedef struct _VFS_SPDK_CONTEXT {
	void *nvme_ctrlr;			// struct spdk_nvme_ctrlr *
	void *nvme_ns;				// struct spdk_nvme_ns *
	uint32_t io_queue_size;
	bool is_initialized;
} VFS_SPDK_CONTEXT, *PVFS_SPDK_CONTEXT;

// ============================================================
// Function Declarations
// ============================================================

// Lifecycle
VFS_STATUS Vfs_Init(PVFS_SPDK_CONTEXT spdk_ctx, uint32_t nvme_pci_id);
void Vfs_Shutdown(PVFS_SPDK_CONTEXT spdk_ctx);

// Device management
VFS_STATUS Vfs_GetDeviceInfo(uint32_t device_index, PVFS_DEVICE_INFO info);
VFS_STATUS Vfs_IsolateDevice(uint32_t nvme_pci_id);
VFS_STATUS Vfs_RestoreDevice(uint32_t device_index);

// Vector operations
VFS_STATUS Vfs_InsertVector(PVFS_COLLECTION collection,
							 const float *vector,
							 uint32_t vector_dim,
							 const uint8_t *metadata,
							 uint32_t metadata_len,
							 PVFS_VECTOR_ID out_id);

VFS_STATUS Vfs_QueryVector(PVFS_COLLECTION collection,
							const float *query_vector,
							uint32_t vector_dim,
							uint32_t top_k,
							PVFS_VECTOR_ENTRY *results,
							uint32_t *result_count);

VFS_STATUS Vfs_DeleteVector(PVFS_COLLECTION collection, VFS_VECTOR_ID id);

// Context paging (CCD)
VFS_STATUS Vfs_PageOutContext(uint64_t session_id,
							  const uint8_t *kv_cache,
							  size_t kv_cache_len,
							  uint64_t *out_page_id);

VFS_STATUS Vfs_PageInContext(uint64_t session_id,
							 uint64_t page_id,
							 uint8_t *kv_cache,
							 size_t kv_cache_len,
							 size_t *bytes_read);

// Neural snapshot (CoW backup)
VFS_STATUS Vfs_CreateNeuralSnapshot(uint64_t session_id,
									 uint64_t *out_snapshot_id);

VFS_STATUS Vfs_RestoreNeuralSnapshot(uint64_t snapshot_id,
									  uint64_t *out_session_id);

#endif // VFS_MANAGER_H