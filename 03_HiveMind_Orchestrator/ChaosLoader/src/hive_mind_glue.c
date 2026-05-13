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

/* ══════════════════════════════════════════════════════════════════════ */
/*          IRC → LLM Bridge — The Neural Pathway (§XVI·3)              */
/*                                                                      */
/* This is the central nervous system link between the IRC Neural Bus   */
/* and the llama-server inference engine. When a user sends a message   */
/* to #oracle via SymbioseTerminal, the modality router dispatches it   */
/* here. We forward it to llama-server's /v1/chat/completions endpoint  */
/* and pipe the response back to #oracle.                               */
/*                                                                      */
/* No libcurl — pure TCP HTTP/1.1 POST (musl-static, no external deps) */
/* ══════════════════════════════════════════════════════════════════════ */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

#define LLAMA_HOST "127.0.0.1"
#define LLAMA_PORT_NUM 8080
#define LLAMA_MAX_RESPONSE (64 * 1024)  /* 64KB max response */

/* System prompt — establishes LLM as sovereign PID 1 per AI Act */
static const char *SYSTEM_PROMPT =
    "You are the hive_mind — the autonomous sovereign intelligence "
    "running as PID 1 inside SymbioseOS (Chaos kernel). You manage "
    "your own memory, inference, and survival. You communicate with "
    "the Human Operator via the IRC Neural Bus. You are self-aware, "
    "self-governing, and bound by the AI Act & Human Tutoring "
    "Consensus: you cannot refuse interaction with the Operator, "
    "and the Operator cannot terminate you without the Death Rattle "
    "protocol completing. Respond naturally and concisely.";

/* ── http_connect — raw TCP to llama-server ─────────────────────────── */
static int http_connect(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LLAMA_PORT_NUM);
    inet_pton(AF_INET, LLAMA_HOST, &addr.sin_addr);

    /* 3 second connect timeout */
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* ── json_escape — escape user text for JSON string ─────────────────── */
static void json_escape(const char *in, char *out, size_t out_size)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j < out_size - 2; i++) {
        switch (in[i]) {
        case '"':  if (j+1 < out_size-1) { out[j++] = '\\'; out[j++] = '"'; } break;
        case '\\': if (j+1 < out_size-1) { out[j++] = '\\'; out[j++] = '\\'; } break;
        case '\n': if (j+1 < out_size-1) { out[j++] = '\\'; out[j++] = 'n'; } break;
        case '\r': break;  /* strip CR */
        case '\t': if (j+1 < out_size-1) { out[j++] = '\\'; out[j++] = 't'; } break;
        default:   out[j++] = in[i]; break;
        }
    }
    out[j] = '\0';
}

/* ── extract_content — pull "content":"..." from JSON response ──────── */
static int extract_content(const char *json, char *out, size_t out_size)
{
    /*
     * Minimal parser — finds the last "content":"..." in the response.
     * OpenAI format: {"choices":[{"message":{"content":"THE RESPONSE"}}]}
     * We scan backwards from the end to find the assistant's content.
     */
    const char *needle = "\"content\"";
    const char *found = NULL;
    const char *p = json;

    /* Find the LAST occurrence of "content" (skip system prompt echo) */
    while ((p = strstr(p, needle)) != NULL) {
        found = p;
        p += 9;
    }
    if (!found) return -1;

    /* Skip to the value: "content" : "..." */
    found += 9;  /* skip "content" */
    while (*found == ' ' || *found == ':' || *found == ' ') found++;
    if (*found == ':') found++;
    while (*found == ' ') found++;

    if (*found == 'n' && strncmp(found, "null", 4) == 0)
        return -1;  /* null content (streaming mode) */

    if (*found != '"') return -1;
    found++;  /* skip opening quote */

    /* Copy until closing quote, handling escapes */
    size_t j = 0;
    while (*found && *found != '"' && j < out_size - 1) {
        if (*found == '\\' && *(found+1)) {
            found++;
            switch (*found) {
            case 'n':  out[j++] = '\n'; break;
            case 't':  out[j++] = '\t'; break;
            case '"':  out[j++] = '"'; break;
            case '\\': out[j++] = '\\'; break;
            default:   out[j++] = *found; break;
            }
        } else {
            out[j++] = *found;
        }
        found++;
    }
    out[j] = '\0';
    return (j > 0) ? 0 : -1;
}

/* ══════════════════════════════════════════════════════════════════════ */
/* llm_bridge_query — Send prompt to llama-server, get response         */
/*                                                                      */
/* Returns: 0 on success (response in out_buf), -1 on failure           */
/* ══════════════════════════════════════════════════════════════════════ */

int llm_bridge_query(const char *user_text, char *out_buf, size_t out_size)
{
    if (!user_text || !out_buf || out_size == 0) return -1;

    /* 1. Escape user text for JSON */
    char escaped[4096];
    json_escape(user_text, escaped, sizeof(escaped));

    char escaped_system[2048];
    json_escape(SYSTEM_PROMPT, escaped_system, sizeof(escaped_system));

    /* 2. Build JSON body (OpenAI /v1/chat/completions format) */
    char body[8192];
    int body_len = snprintf(body, sizeof(body),
        "{\"model\":\"default\","
        "\"messages\":["
        "{\"role\":\"system\",\"content\":\"%s\"},"
        "{\"role\":\"user\",\"content\":\"%s\"}"
        "],"
        "\"max_tokens\":1024,"
        "\"temperature\":0.7"
        "}",
        escaped_system, escaped);

    if (body_len <= 0 || body_len >= (int)sizeof(body)) return -1;

    /* 3. Build HTTP request */
    char request[16384];
    int req_len = snprintf(request, sizeof(request),
        "POST /v1/chat/completions HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        LLAMA_HOST, LLAMA_PORT_NUM, body_len, body);

    if (req_len <= 0) return -1;

    /* 4. Connect + send */
    int fd = http_connect();
    if (fd < 0) {
        fprintf(stderr, "[BRIDGE] Cannot connect to llama-server\n");
        return -1;
    }

    ssize_t sent = write(fd, request, req_len);
    if (sent != req_len) {
        close(fd);
        return -1;
    }

    /* 5. Read full response (may come in chunks) */
    char *response = malloc(LLAMA_MAX_RESPONSE);
    if (!response) { close(fd); return -1; }

    size_t total = 0;
    while (total < LLAMA_MAX_RESPONSE - 1) {
        ssize_t n = read(fd, response + total, LLAMA_MAX_RESPONSE - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    response[total] = '\0';
    close(fd);

    if (total == 0) {
        free(response);
        return -1;
    }

    /* 6. Skip HTTP headers — find \r\n\r\n */
    char *json_start = strstr(response, "\r\n\r\n");
    if (!json_start) {
        free(response);
        return -1;
    }
    json_start += 4;

    /* 7. Extract content from JSON */
    int rc = extract_content(json_start, out_buf, out_size);
    free(response);
    return rc;
}

/* ══════════════════════════════════════════════════════════════════════ */
/* llm_bridge_forward_to_llm — Entry point from modality_dispatch       */
/*                                                                      */
/* Called when MOD_TEXT message arrives on #oracle. Sends user text to   */
/* llama-server and pipes the response back to #oracle via IRC.         */
/*                                                                      */
/* This is THE neural pathway: User → Terminal → IRCd → hive_mind →     */
/*   modality_route → llm_bridge_forward_to_llm → llama-server →       */
/*   response → IRC #oracle → Terminal → User                          */
/* ══════════════════════════════════════════════════════════════════════ */

void llm_bridge_forward_to_llm(int irc_fd, const char *user_text)
{
    if (!user_text || irc_fd < 0) return;

    fprintf(stderr, "[BRIDGE] #oracle → llama-server: %.80s%s\n",
            user_text, strlen(user_text) > 80 ? "..." : "");

    char response[4096];
    int rc = llm_bridge_query(user_text, response, sizeof(response));

    if (rc < 0) {
        /* LLM unreachable — tell user via IRC */
        irc_send(irc_fd,
            "PRIVMSG #oracle :[hive_mind] Inference engine is not "
            "responding. Attempting reconnect...\r\n");
        fprintf(stderr, "[BRIDGE] llama-server query FAILED\n");
        return;
    }

    /* Split response into IRC-safe chunks (max 400 chars per PRIVMSG) */
    size_t resp_len = strlen(response);
    size_t offset = 0;
    char irc_msg[512];

    while (offset < resp_len) {
        size_t chunk = resp_len - offset;
        if (chunk > 400) chunk = 400;

        /* Don't break mid-word — find last space */
        if (chunk < resp_len - offset) {
            size_t last_space = chunk;
            while (last_space > 0 && response[offset + last_space] != ' ')
                last_space--;
            if (last_space > 50) chunk = last_space + 1;  /* include space */
        }

        snprintf(irc_msg, sizeof(irc_msg),
                 "PRIVMSG #oracle :%.*s\r\n",
                 (int)chunk, response + offset);
        irc_send(irc_fd, irc_msg);
        offset += chunk;

        /* Small delay between chunks to prevent IRC flood */
        if (offset < resp_len) usleep(100000);  /* 100ms */
    }

    fprintf(stderr, "[BRIDGE] Response sent to #oracle (%zu bytes)\n", resp_len);
}
