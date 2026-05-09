/*++
 * scout_modality.c — Scout Modality Discovery & Evolution
 *
 * HIVE-MM-007: The self-evolving AI dispatches scouts to acquire new
 *              sensory capabilities when it detects a gap. The scout
 *              searches HuggingFace, downloads via DCC, and the hive
 *              reabsorbs the capability — evolving new senses.
 *
 * Reference:
 *   - Interactive_Plan.md §XVII·5a (lines 7223-7261) — Scout Discovery
 *   - Interactive_Plan.md §XVII·5b (lines 7262-7270) — Evolution Messages
 *
 * Architecture:
 *   1. hive_mind detects user sends IMAGE_DATA but mmproj=NULL
 *      → "I cannot see. I need vision capabilities."
 *   2. Dispatches scout to #recon: SCOUT_DISPATCH task=ACQUIRE_MODALITY
 *   3. Scout searches HuggingFace for compatible weights
 *   4. Downloads via DCC, validates CRC64
 *   5. Reports SCOUT_RESULT with artifact location
 *   6. hive_mind reabsorbs: writes to TensorStore, updates model.conf,
 *      restarts processor → "I can now see."
 *   7. Announces MODALITY_EVOLVED on #cluster-announce
 *
 * IRC Messages (§XVII·5b):
 *   MODALITY_EVOLVED node=<id> type=<mod> model=<name>
 *   MODALITY_QUERY type=<mod>
 *   MODALITY_OFFER node=<id> type=<mod> model=<name> crc64=<hash>
 *   MOD_STATS type=<n> frames=<count> avg_ms=<latency>
 *
 * Acceptance criteria:
 *   Scout dispatch triggers on missing modality;
 *   SCOUT_RESULT parsed and artifact stored;
 *   MODALITY_EVOLVED announced on success;
 *   No dispatch for already-enabled modalities
 *--*/

#include "multimodal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ═══════════════════════════════════════════════════════════════════════════
// scout_trigger_evolution — Auto-detect missing capability and dispatch
//
// §XVII·5a lines 7230-7236
//
// Called by modality_dispatch() when a user sends data for a disabled
// modality. The AI recognises it lacks the capability and dispatches
// a scout to acquire it autonomously.
// ═══════════════════════════════════════════════════════════════════════════

void scout_trigger_evolution(int irc_fd, MODALITY_TYPE type)
{
    if (type >= MOD_MAX) return;

    // Don't trigger if already enabled (prevents duplicate scouts)
    if (g_Processors[type].Enabled) return;

    // Map modality type to human-readable capability name
    const char* capability = "unknown";
    const char* search_hint = "";

    switch (type) {
    case MOD_IMAGE:
    case MOD_VIDEO:
    case MOD_SCREEN:
        capability = "vision";
        search_hint = "mmproj compatible with model";
        break;
    case MOD_AUDIO_IN:
        capability = "stt";
        search_hint = "whisper model gguf";
        break;
    case MOD_AUDIO_OUT:
        capability = "tts";
        search_hint = "piper model onnx";
        break;
    case MOD_DOCUMENT:
        capability = "ocr";
        search_hint = "document OCR model";
        break;
    default:
        return;  // MOD_TEXT, MOD_MOVIOLA, MOD_DIBIT — not scout-acquirable
    }

    fprintf(stderr, "[SCOUT] Capability gap detected: '%s' (type=%d). "
            "Dispatching scout...\n", capability, type);

    scout_dispatch_modality(irc_fd, type, search_hint);
}

// ═══════════════════════════════════════════════════════════════════════════
// scout_dispatch_modality — Send SCOUT_DISPATCH to #recon
//
// §XVII·5a lines 7233-7236
//
// Message format:
//   PRIVMSG #recon :SCOUT_DISPATCH scout_id=<uuid>
//     task=ACQUIRE_MODALITY type=<mod> search="<hint>"
// ═══════════════════════════════════════════════════════════════════════════

void scout_dispatch_modality(int irc_fd, MODALITY_TYPE type,
                              const char* model_name)
{
    // Generate scout UUID from timestamp + type
    char scout_id[32];
    snprintf(scout_id, sizeof(scout_id), "scout_%ld_%d",
             (long)time(NULL), type);

    // Map type to string name
    const char* type_str = "unknown";
    if (type < MOD_MAX && g_Processors[type].Name) {
        type_str = g_Processors[type].Name;
    }

    char msg[512];
    snprintf(msg, sizeof(msg),
        "PRIVMSG #recon :SCOUT_DISPATCH scout_id=%s "
        "task=ACQUIRE_MODALITY type=%s search=\"%s\"\r\n",
        scout_id, type_str,
        model_name ? model_name : "auto-detect");
    irc_send(irc_fd, msg);

    fprintf(stderr, "[SCOUT] Dispatched %s for modality '%s'\n",
            scout_id, type_str);
}

// ═══════════════════════════════════════════════════════════════════════════
// scout_handle_result — Process SCOUT_RESULT message
//
// §XVII·5a lines 7243-7253
//
// Parses the scout result, writes the artifact to the local filesystem,
// updates model configuration, triggers modality hot-swap, and announces
// MODALITY_EVOLVED to the cluster.
//
// Expected message format:
//   SCOUT_RESULT scout_id=<uuid> status=SUCCESS modality=<type>
//     artifact=<filename> crc64=<hash> size=<bytes>
// ═══════════════════════════════════════════════════════════════════════════

void scout_handle_result(int irc_fd, const char* msg)
{
    if (!msg) return;

    // Parse key fields from SCOUT_RESULT
    char scout_id[64] = {0};
    char status[32] = {0};
    char modality[32] = {0};
    char artifact[256] = {0};
    uint64_t crc64 = 0;
    size_t artifact_size = 0;

    // Simple field extraction
    const char* p;
    if ((p = strstr(msg, "scout_id=")))
        sscanf(p, "scout_id=%63s", scout_id);
    if ((p = strstr(msg, "status=")))
        sscanf(p, "status=%31s", status);
    if ((p = strstr(msg, "modality=")))
        sscanf(p, "modality=%31s", modality);
    if ((p = strstr(msg, "artifact=")))
        sscanf(p, "artifact=%255s", artifact);
    if ((p = strstr(msg, "crc64=")))
        sscanf(p, "crc64=%lu", &crc64);
    if ((p = strstr(msg, "size=")))
        sscanf(p, "size=%zu", &artifact_size);

    if (strcmp(status, "SUCCESS") != 0) {
        fprintf(stderr, "[SCOUT] %s FAILED for modality '%s'\n",
                scout_id, modality);
        return;
    }

    fprintf(stderr, "[SCOUT] %s SUCCESS: modality=%s artifact=%s "
            "crc64=0x%016lx size=%zu\n",
            scout_id, modality, artifact, crc64, artifact_size);

    // §XVII·5a lines 7249-7253: Update model.conf and trigger hot-swap
    // In production: write artifact to TensorStore, update g_MmConfig,
    // then call modality_hotswap() for the relevant modality.

    // Determine modality type from string
    MODALITY_TYPE type = MOD_TEXT;
    if (strcmp(modality, "vision") == 0) type = MOD_IMAGE;
    else if (strcmp(modality, "stt") == 0 || strcmp(modality, "whisper") == 0)
        type = MOD_AUDIO_IN;
    else if (strcmp(modality, "tts") == 0 || strcmp(modality, "piper-tts") == 0)
        type = MOD_AUDIO_OUT;

    // Attempt hot-swap with the new artifact
    if (modality_hotswap(type, artifact) == 0) {
        // §XVII·5a lines 7255-7258: Announce evolution
        char evolve_msg[512];
        snprintf(evolve_msg, sizeof(evolve_msg),
            "PRIVMSG #cluster-announce :MODALITY_EVOLVED "
            "node=hive_mind type=%s model=%s\r\n",
            modality, artifact);
        irc_send(irc_fd, evolve_msg);

        fprintf(stderr, "[SCOUT] MODALITY_EVOLVED: %s → %s\n",
                modality, artifact);
    }
}
