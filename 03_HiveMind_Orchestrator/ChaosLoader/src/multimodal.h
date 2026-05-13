/*++
 * multimodal.h — HIVE-MM Multimodal Sensory Pipeline Shared Types
 *
 * Master header for all §XVII·4–5 multimodal subsystems.
 * Defines modality types, processor registry, vision structures,
 * delta-motion (Moviola), Di-Bit tokens, RDI telemetry, and
 * MIDI grammar types.
 *
 * Reference:
 *   - Interactive_Plan.md §XVII·4a (lines 6919-6971) — Modality Router
 *   - Interactive_Plan.md §XVII·4b (lines 6973-7041) — Vision Pipeline
 *   - Interactive_Plan.md §XVII·4e (lines 7099-7165) — Audio Pipeline
 *   - Interactive_Plan.md §XVII·4f (lines 7167-7217) — Video Temporal
 *   - Interactive_Plan.md §XVII·4g (lines 7319-7431) — Moviola Delta
 *   - Interactive_Plan.md §XVII·4h (lines 7432-7512) — Di-Bit Injection
 *   - Interactive_Plan.md §XVII·4i (lines 7513-7563) — DVS Hardware
 *   - Interactive_Plan.md §XVII·5a (lines 7223-7261) — Scout Discovery
 *   - Interactive_Plan.md §XVII·5c (lines 7271-7318) — Hot-Swap
 *   - Interactive_Plan.md §XVII·5e (lines 7621-7715) — RDI Telemetry
 *   - Interactive_Plan.md §XVII·5f (lines 7716-7803) — MIDI Grammar
 *
 * Architecture:
 *   This header keeps the multimodal sensory layer cleanly separated
 *   from the HIVE-MOSIX clustering layer (openmosix_tensor.h).
 *   Both headers are independent — MM files include this; MOSIX files
 *   include openmosix_tensor.h.
 *--*/

#ifndef MULTIMODAL_H
#define MULTIMODAL_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>

// ═══════════════════════════════════════════════════════════════════════════
// §XVII·4a — Modality Type Enumeration
//
// 9 modality types covering the full sensory spectrum.
// MOD_DIBIT_NATIVE is the Optical Singularity — direct LLM embedding
// injection bypassing mmproj entirely.
// ═══════════════════════════════════════════════════════════════════════════

typedef enum {
    MOD_TEXT         = 0,   // Plain text chat
    MOD_IMAGE        = 1,   // Single image (JPEG/PNG/WebP)
    MOD_VIDEO        = 2,   // Video frame sequence
    MOD_AUDIO_IN     = 3,   // Inbound audio (STT via Whisper)
    MOD_AUDIO_OUT    = 4,   // Outbound audio (TTS via Piper)
    MOD_SCREEN       = 5,   // Desktop/screen capture
    MOD_DOCUMENT     = 6,   // PDF/document OCR
    MOD_MOVIOLA      = 7,   // Neuromorphic delta-motion vision (§XVII·4g)
    MOD_DIBIT_NATIVE = 8,   // Di-Bit packed tokens for direct LLM injection (§XVII·4h)
    MOD_MAX                 // = 9
} MODALITY_TYPE;

// ═══════════════════════════════════════════════════════════════════════════
// §XVII·4a — Modality Processor Registry Entry
//
// Each processor runs as a child process (forked) with an HTTP API.
// The router dispatches incoming IRC messages to the correct processor.
// ═══════════════════════════════════════════════════════════════════════════

typedef struct _MODALITY_PROCESSOR {
    MODALITY_TYPE  Type;
    const char*    Name;
    uint8_t        Enabled;          // Toggled by model.conf capabilities
    pid_t          WorkerPid;        // Child process (if forked)
    uint16_t       Port;             // HTTP port for the processor
    uint64_t       FramesProcessed;  // Lifetime counter
    float          AvgLatencyMs;     // Rolling average processing time
} MODALITY_PROCESSOR;

// Global processor registry (defined in modality_router.c)
extern MODALITY_PROCESSOR g_Processors[MOD_MAX];

// ═══════════════════════════════════════════════════════════════════════════
// §XVII·4b — Vision Frame (CLIP-normalized F32 tiles)
//
// Raw JPEG → F32 normalized pixels → 336×336 tiles for ViT-L/14 encoder
// CLIP normalization: mean=[0.48145466, 0.4578275, 0.40821073]
//                      std=[0.26862954, 0.26130258, 0.27577711]
// ═══════════════════════════════════════════════════════════════════════════

#define TILE_SIZE   336     // Standard CLIP ViT-L/14 input resolution
#define MAX_TILES   12      // Max tiles per image (4032×3024 → 12 tiles)

typedef struct _VISION_FRAME {
    uint32_t  Width;
    uint32_t  Height;
    uint32_t  Channels;          // 3 = RGB
    float*    PixelsF32;         // Normalized F32 pixel data
    uint32_t  TileCount;         // Number of 336×336 tiles
    float**   Tiles;             // Array of tile pointers (each 336×336×3 F32)
    uint64_t  Crc64;
    uint64_t  TimestampNs;       // Nanosecond capture timestamp
} VISION_FRAME;

// ═══════════════════════════════════════════════════════════════════════════
// §XVII·4f — Video Temporal Context (keyframe sliding window)
// ═══════════════════════════════════════════════════════════════════════════

#define VIDEO_KEYFRAME_WINDOW  16   // Last 16 keyframes retained
#define KEYFRAME_INTERVAL       5   // Extract 1 keyframe every 5 frames

typedef struct _VIDEO_CONTEXT {
    VISION_FRAME*  Keyframes[VIDEO_KEYFRAME_WINDOW];
    uint32_t       KeyframeCount;
    uint32_t       TotalFramesSeen;
    uint64_t       FirstFrameTs;     // Nanosecond timestamp of first frame
    uint64_t       LastFrameTs;      // Nanosecond timestamp of last frame
    float          Fps;              // Estimated FPS from timestamps
} VIDEO_CONTEXT;

// ═══════════════════════════════════════════════════════════════════════════
// §XVII·4g — Delta-Motion Frame (Moviola Protocol)
//
// 1-bit change-map: 0=static, 1=motion event per pixel
// Software path: |frame[n] - frame[n-1]| > threshold
// Hardware path: DVS natively emits per-pixel events (§XVII·4i)
// ═══════════════════════════════════════════════════════════════════════════

#define DELTA_THRESHOLD     15      // Pixel change threshold (0-255)
#define MOVIOLA_MICROGRID   10      // 10×10 micro-grid for Di-Bit packing

typedef struct _DELTA_FRAME {
    uint32_t  Width;
    uint32_t  Height;
    uint8_t*  ChangeMap;         // 1-bit per pixel: 0=static, 1=motion
    uint32_t  ActivePixels;      // Count of non-zero pixels (sparse nnz)
    float     Sparsity;          // Ratio of active vs total pixels
    uint64_t  TimestampNs;
} DELTA_FRAME;

// ═══════════════════════════════════════════════════════════════════════════
// §XVII·4h — Di-Bit Token (Optical Singularity encoding)
//
// 10×10 micro-grid → 100 cells × 2 bits = 200 bits = 25 bytes per token
// Di-Bit values: 00=static, 01=onset, 10=offset, 11=sustained
// ═══════════════════════════════════════════════════════════════════════════

#define DIBIT_STATIC    0x00    // No change between frames
#define DIBIT_ONSET     0x01    // New motion detected
#define DIBIT_OFFSET    0x02    // Motion ceased
#define DIBIT_SUSTAINED 0x03    // Continuous motion

#define DIBIT_GRID_BYTES  25    // 10×10 × 2-bit = 200 bits = 25 bytes
#define MAX_GRIDS        4096   // Max micro-grids per frame (64×64 @ 640×640)

typedef struct _DIBIT_TOKEN {
    uint8_t     grid[DIBIT_GRID_BYTES]; // 10×10 × 2-bit = 200 bits = 25 bytes
    uint16_t    grid_x;                 // Micro-grid column position in frame
    uint16_t    grid_y;                 // Micro-grid row position in frame
    uint64_t    timestamp_ns;           // Frame timestamp
    float       sparsity;              // Fraction of zero cells in this grid
} DIBIT_TOKEN;

// ═══════════════════════════════════════════════════════════════════════════
// §XVII·5e — RDI State (Resonance Deviation Index)
//
// The single most important metric for D.E.M.H.X. alignment.
// Convergence: |RDI - π/9| < 0.01 for 3 consecutive reports.
// ═══════════════════════════════════════════════════════════════════════════

#define MARK1_CONSTANT          0.349066f   // π/9 — Universal Harmonic Constant
#define RDI_CONVERGE_THRESHOLD  0.01f       // |RDI - π/9| < 0.01 = converged
#define RDI_CONVERGE_COUNT      3           // Must converge for 3 consecutive reports

typedef struct _RDI_STATE {
    float   last_rdi;
    int     converge_streak;    // Consecutive reports within threshold
    bool    converged;
} RDI_STATE;

// ═══════════════════════════════════════════════════════════════════════════
// Model Configuration (multimodal extension)
// ═══════════════════════════════════════════════════════════════════════════

typedef struct _MM_MODEL_CONFIG {
    char        model_path[256];         // Base LLM weights
    char        mmproj_path[256];        // Vision projection weights (optional)
    char        whisper_model_path[256]; // STT model (optional)
    char        tts_model_path[256];     // TTS model (optional)
    uint8_t     multimodal;              // 1 if multimodal enabled
    uint8_t     supports_dibit_native;   // 1 if LLM supports native Di-Bit injection
    uint8_t     dvs_mode;               // 1 if DVS hardware present
    int         moviola_delta_threshold; // Pixel change threshold (model.conf, default: 15)
    int         total_layers;
    uint64_t    model_size_bytes;
} MM_MODEL_CONFIG;

// Global model configuration (defined in modality_router.c)
extern MM_MODEL_CONFIG g_MmConfig;

// ═══════════════════════════════════════════════════════════════════════════
// HTTP Response (minimal structure for Piper/Whisper communication)
// ═══════════════════════════════════════════════════════════════════════════

typedef struct _HTTP_RESPONSE {
    uint8_t*  data;
    size_t    size;
    int       status_code;
} HTTP_RESPONSE;

// ═══════════════════════════════════════════════════════════════════════════
// SHM Ring Buffer Integration (references Tier 2b §VII·7)
//
// These are forward declarations for the SHM ring functions defined
// in the IRCd_Neural_Bus tier. Linked at build time.
// ═══════════════════════════════════════════════════════════════════════════

#define SHM_SLOT_SIZE       (512ULL * 1024 * 1024)  // 512MB per slot
#define SHM_SLOT_COUNT      8                        // 8 slots total

typedef struct _SHM_SLOT_META {
    uint32_t    PayloadType;     // MODALITY_TYPE enum value
    uint64_t    PayloadSize;     // Actual data size in slot
    uint64_t    Crc64;           // CRC64 of payload data
    uint64_t    TimestampNs;     // Write timestamp
    uint8_t     State;           // 0=free, 1=writing, 2=ready, 3=reading
} SHM_SLOT_META;

typedef struct _SHM_RING_CONTROL {
    uint32_t        WriteHead;
    uint32_t        ReadHead;
    SHM_SLOT_META   SlotMeta[SHM_SLOT_COUNT];
} SHM_RING_CONTROL;

#define SHM_SLOT_DATA_OFFSET(slot) \
    (sizeof(SHM_RING_CONTROL) + (size_t)(slot) * SHM_SLOT_SIZE)

// SHM ring operations (shm_ring.c — Tier 2b HIVE-IRC-008)
// NOTE: shm_ring.c uses a module-global g_RingControl, so these take no
// SHM_RING_CONTROL* parameter. The struct above is for guest-side direct
// access via EPT-mapped memory (zero-copy consumer path).
int   shm_ring_init(void);
int   shm_ring_acquire_write(void);
void  shm_ring_commit(int slot);
int   shm_ring_acquire_read(void);
void  shm_ring_release(int slot);
void* shm_ring_get_slot_ptr(int slot);
void  shm_ring_set_meta(int slot, uint64_t payloadId, uint64_t payloadSize,
                         uint64_t crc64, uint32_t payloadType,
                         uint32_t sourceChannel);
void  shm_ring_destroy(void);

// ═══════════════════════════════════════════════════════════════════════════
// IRC Integration (references Tier 2b)
// ═══════════════════════════════════════════════════════════════════════════

extern int g_IrcFd;     // Global IRC socket (symbiose_ircd.c)
int  irc_send(int fd, const char* msg);

// ═══════════════════════════════════════════════════════════════════════════
// CRC64 utility (shared across tiers)
// ═══════════════════════════════════════════════════════════════════════════

uint64_t crc64_compute(const void* data, size_t size);

// ═══════════════════════════════════════════════════════════════════════════
// Nanosecond timestamp utility
// ═══════════════════════════════════════════════════════════════════════════

static inline uint64_t clock_gettime_ns(clockid_t clk)
{
    struct timespec ts;
    clock_gettime(clk, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// ═══════════════════════════════════════════════════════════════════════════
// Forward declarations — HIVE-MM modules
// ═══════════════════════════════════════════════════════════════════════════

// modality_router.c (HIVE-MM-001)
void modality_route(int irc_fd, const char* msg, void* shm);
void modality_dispatch(MODALITY_TYPE type, const char* msg, void* shm);
void modality_init(const MM_MODEL_CONFIG* cfg);

// vision_pipeline.c (HIVE-MM-002)
VISION_FRAME* vision_preprocess(const uint8_t* jpeg_data, size_t jpeg_size);
void vision_extract_tile(const float* pixels, int w, int h, int tile_idx,
                          int cols, float* out_tile);
void vision_frame_free(VISION_FRAME* frame);

// tts_pipeline.c (HIVE-MM-003)
void tts_synthesize(int irc_fd, void* shm, const char* text,
                     const char* voice_id);
HTTP_RESPONSE http_post(const char* url, const char* body, size_t body_len);
void tts_fork_whisper(const char* model_path);
void tts_fork_piper(const char* model_path);

// video_temporal.c (HIVE-MM-004)
void video_ingest_frame(const uint8_t* jpeg, size_t size, uint32_t frame_num);
const VIDEO_CONTEXT* video_get_context(void);
uint32_t video_get_active_keyframes(void);
void video_reset(void);

// moviola_delta.c (HIVE-MM-005)
DELTA_FRAME* moviola_compute_delta(const uint8_t* gray_frame,
                                     uint32_t w, uint32_t h);
uint32_t moviola_pack_dibit(const DELTA_FRAME* df, uint8_t* token_buf,
                             size_t buf_size);
uint8_t moviola_extract_grid(const uint8_t* change_map, uint32_t width,
                              uint32_t gx, uint32_t gy);
void delta_frame_free(DELTA_FRAME* df);
void moviola_delta_reset(void);

// modality_hotswap.c (HIVE-MM-006)
int  modality_hotswap(MODALITY_TYPE type, const char* new_model_path);
int  health_check(uint16_t port);
const char* proc_binary_path(MODALITY_TYPE type);

// scout_modality.c (HIVE-MM-007)
void scout_dispatch_modality(int irc_fd, MODALITY_TYPE type,
                              const char* model_name);
void scout_handle_result(int irc_fd, const char* msg);
void scout_trigger_evolution(int irc_fd, MODALITY_TYPE type);

// demhx_rdi.c (HIVE-MM-008)
float compute_rdi(const float* embeddings, int dim);
void  rdi_report(RDI_STATE* state, float rdi, int irc_fd, const char* source);

// moviola_dibit.c (HIVE-MM-009)
int moviola_dibit_route(const DELTA_FRAME* delta, int irc_fd, void* shm);
void pack_dibit_grid(const DELTA_FRAME* delta, int gx, int gy,
                      DIBIT_TOKEN* tok);
void moviola_fallback_to_vision(const DIBIT_TOKEN* tokens, int count,
                                 int irc_fd, void* shm);

// demhx_midi_grammar.c (HIVE-MM-010)
int encode_midi_grammar(const float* embeddings, int dim,
                         uint8_t* midi_buf, int* midi_len);
int decode_midi_grammar(const uint8_t* midi_buf, int midi_len,
                         float* local_weights, int dim, RDI_STATE* rdi_state);

// moviola_dvs.c (HIVE-MM-011)
#ifdef SYMBIOSE_DVS_SUPPORT
int dvs_to_delta_frame(void* events, DELTA_FRAME* out);
void set_dibit(uint8_t* change_map, uint16_t x, uint16_t y, uint8_t value);
#endif

// hive_mind_glue.c — IRC→LLM Bridge (The Neural Pathway)
void llm_bridge_forward_to_llm(int irc_fd, const char* user_text);
int  llm_bridge_query(const char* user_text, char* out_buf, size_t out_size);

#endif /* MULTIMODAL_H */
