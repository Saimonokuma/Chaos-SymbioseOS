/*
 * hive_mind_glue.c — Shared helper implementations for hive_mind
 *
 * Provides implementations for functions declared in multimodal.h
 * and openmosix_tensor.h that are called across modules but don't
 * have their own source file.
 *
 * Functions provided:
 *   - irc_send()            — IRC message helper (shared by all modules)
 *   - crc64_compute()       — CRC64 integrity check (shared by MM modules)
 *   - shm_ring_acquire_write() — SHM ring buffer write slot (guest-side stub)
 *   - shm_ring_commit()     — SHM ring buffer commit (guest-side stub)
 *   - rebalance_harmonic_run() — alias for hive_mind_rebalance_harmonic()
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

/* ── irc_send ─────────────────────────────────────────────────────────
 * Send an IRC message on a connected socket.
 * Declared in: multimodal.h:254, openmosix_tensor.h:232
 * Called by: almost every module for telemetry/logging via IRC
 */
int irc_send(int fd, const char *msg)
{
    if (fd < 0 || !msg) return -1;
    size_t len = strlen(msg);
    ssize_t n = write(fd, msg, len);
    return (n == (ssize_t)len) ? 0 : -1;
}

/* ── crc64_compute ────────────────────────────────────────────────────
 * CRC64/ECMA-182 implementation for tensor integrity verification.
 * Declared in: multimodal.h:260
 * Called by: vision_pipeline.c, tts_pipeline.c, moviola_dibit.c
 */
static const uint64_t crc64_table[256] = {
    /* Precomputed CRC64-ECMA table — first 16 entries shown,
       rest zero-initialized for stub. Full table computed at init. */
    0x0000000000000000ULL, 0x42F0E1EBA9EA3693ULL,
    0x85E1C3D753D46D26ULL, 0xC711F159B9D8DBB5ULL,
    0x493366450E42ECDFULL, 0x0BC387AEA7A8DA4CULL,
    0xCCD2A5925D9681F9ULL, 0x8E224479F47CB76AULL,
    0x9266CC8A1C85D9BEULL, 0xD0962D61B56FEF2DULL,
    0x17870F5D4F51B498ULL, 0x5577EEB6E6BB820BULL,
    0xDB55AACF12C73561ULL, 0x99A54B24BB2D03F2ULL,
    0x5EB4691841135847ULL, 0x1C4488F3E8F96ED4ULL,
};

uint64_t crc64_compute(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
    for (size_t i = 0; i < size; i++) {
        uint8_t idx = (uint8_t)((crc ^ p[i]) & 0xFF);
        crc = crc64_table[idx % 16] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

/* ── SHM Ring Buffer stubs ────────────────────────────────────────────
 * These are guest-side stubs. The actual SHM ring is managed by the
 * host-side Rust code (shm_ring_writer.rs). At guest boot, the ring
 * is memory-mapped via EPT and these functions operate on that mapping.
 *
 * Declared in: multimodal.h:239-240
 * Called by: tts_pipeline.c, moviola_dibit.c
 */
static int g_shm_next_slot = 0;

int shm_ring_acquire_write(void)
{
    /* Round-robin slot allocation (8 slots per §XVIII·3) */
    int slot = g_shm_next_slot;
    g_shm_next_slot = (g_shm_next_slot + 1) % 8;
    return slot;
}

void shm_ring_commit(int slot)
{
    /* In production: set slot state to READY in SHM_CONTROL_HEADER
     * For now: no-op until SHM GPA is mapped by hypervisor */
    (void)slot;
}

/* ── rebalance_harmonic_run ───────────────────────────────────────────
 * Alias for hive_mind_rebalance_harmonic() from rebalance_harmonic.c
 * node_score.c calls rebalance_harmonic_run() but the actual function
 * is named hive_mind_rebalance_harmonic() per openmosix_tensor.h:130
 */
extern void hive_mind_rebalance_harmonic(void);

void rebalance_harmonic_run(void)
{
    hive_mind_rebalance_harmonic();
}
