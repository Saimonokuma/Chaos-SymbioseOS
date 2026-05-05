#include "vfs_manager.h"

VFS_STATUS Vfs_Init(PVFS_SPDK_CONTEXT spdk_ctx, uint32_t nvme_pci_id) {
  return VfsStatusOk;
}
void Vfs_Shutdown(PVFS_SPDK_CONTEXT spdk_ctx) {}
VFS_STATUS Vfs_GetDeviceInfo(uint32_t device_index, PVFS_DEVICE_INFO info) {
  return VfsStatusOk;
}
VFS_STATUS Vfs_IsolateDevice(uint32_t nvme_pci_id) { return VfsStatusOk; }
VFS_STATUS Vfs_RestoreDevice(uint32_t device_index) { return VfsStatusOk; }
VFS_STATUS Vfs_InsertVector(PVFS_COLLECTION collection, const float *vector,
                            uint32_t vector_dim, const uint8_t *metadata,
                            uint32_t metadata_len, PVFS_VECTOR_ID out_id) {
  return VfsStatusOk;
}
VFS_STATUS Vfs_QueryVector(PVFS_COLLECTION collection,
                           const float *query_vector, uint32_t vector_dim,
                           uint32_t top_k, PVFS_VECTOR_ENTRY *results,
                           uint32_t *result_count) {
  return VfsStatusOk;
}
VFS_STATUS Vfs_DeleteVector(PVFS_COLLECTION collection, VFS_VECTOR_ID id) {
  return VfsStatusOk;
}
VFS_STATUS Vfs_PageOutContext(uint64_t session_id, const uint8_t *kv_cache,
                              size_t kv_cache_len, uint64_t *out_page_id) {
  return VfsStatusOk;
}
VFS_STATUS Vfs_PageInContext(uint64_t session_id, uint64_t page_id,
                             uint8_t *kv_cache, size_t kv_cache_len,
                             size_t *bytes_read) {
  return VfsStatusOk;
}
VFS_STATUS Vfs_CreateNeuralSnapshot(uint64_t session_id,
                                    uint64_t *out_snapshot_id) {
  return VfsStatusOk;
}
VFS_STATUS Vfs_RestoreNeuralSnapshot(uint64_t snapshot_id,
                                     uint64_t *out_session_id) {
  return VfsStatusOk;
}

int main() { return 0; }
