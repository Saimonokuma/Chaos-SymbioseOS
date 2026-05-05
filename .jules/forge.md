## 2024-05-23 - IRC Jumbo Frame Reassembly Buffer Overflow
**Bug:** The `SYMBIOSE_IRC_MAX_LINE` (1 MiB) reassembly buffer lacked bounds-checking against `jumbo_chunks_received * SYMBIOSE_IRC_MAX_CHUNK` in the MoE protocol parsing logic, which allowed a malicious client to trigger a heap overflow.
**Root Cause:** The `SymbioseIrcd_RecvJumbo` function was an empty placeholder returning 0, meaning it lacked proper initialization (via `calloc`) or memory expansion logic (`realloc`) to check that the payload and chunks matched constraints.
**Prevention:** Validate `chunk_offset + chunk_len <= payload_len` prior to copying into a reassembly buffer. Properly allocate memory dynamically while ensuring robust size validations against the payload.
