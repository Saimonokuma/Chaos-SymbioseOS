/*++
 * tts_pipeline.c — Text-to-Speech via Piper ONNX
 *
 * HIVE-MM-003: TTS synthesis via Piper HTTP server + SHM ring output.
 *              Also manages child process lifecycle for Whisper (STT)
 *              and Piper (TTS) servers.
 *
 * Reference:
 *   - Interactive_Plan.md §XVII·4e (lines 7099-7165) — Audio Pipeline
 *   - Piper TTS documentation (Context7: /rhasspy/piper)
 *
 * Architecture:
 *   1. POST text to piper-server (HTTP localhost:8083)
 *   2. Receive raw PCM audio (16-bit mono 22050Hz)
 *   3. Write PCM to SHM ring slot (PayloadType=4 = MOD_AUDIO_OUT)
 *   4. Announce TTS_AUDIO on #oracle for host UI playback
 *
 *   Piper output format (from Context7 docs):
 *     - 16-bit PCM, mono, 22050 Hz
 *     - Streamed or batch via --output-raw
 *
 * Acceptance criteria:
 *   PCM audio written to SHM ring;
 *   TTS_AUDIO IRC message sent with correct slot/size/sample_rate;
 *   Child processes forked and tracked in g_Processors[]
 *--*/

#include "multimodal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This entire file uses POSIX APIs (fork, exec, socket, waitpid) and
 * runs only inside the Linux guest. Guard everything for MinGW cross-check. */
#ifdef _WIN32
/* Stub: Linux-guest-only module — no Windows implementation */
#else

#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ═══════════════════════════════════════════════════════════════════════════
// http_post — Minimal HTTP POST for local processor communication
//
// Sends body to url, returns response data.
// In production: use libcurl or a lightweight HTTP client.
// For now: socket-based implementation for localhost-only connections.
// ═══════════════════════════════════════════════════════════════════════════

HTTP_RESPONSE http_post(const char* url, const char* body, size_t body_len)
{
    HTTP_RESPONSE resp = {0};

    // Extract host and port from url (expected: http://127.0.0.1:<port>/...)
    int port = 8083;  // Default Piper port
    const char* port_str = strstr(url, "127.0.0.1:");
    if (port_str) {
        port_str += 10; // skip "127.0.0.1:"
        port = atoi(port_str);
    }

    // Extract path
    const char* path = "/synthesize";
    const char* path_start = strchr(port_str ? port_str : url, '/');
    if (path_start) path = path_start;

    // Create TCP connection to localhost
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "[TTS] Socket creation failed\n");
        return resp;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[TTS] Connection to port %d failed\n", port);
        close(sock);
        return resp;
    }

    // Build HTTP POST request
    char header[512];
    int hdr_len = snprintf(header, sizeof(header),
        "POST %s HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, port, body_len);

    send(sock, header, hdr_len, 0);
    send(sock, body, body_len, 0);

    // Read response (skip HTTP headers, capture body)
    size_t buf_cap = 4 * 1024 * 1024;  // 4MB max audio
    uint8_t* buf = malloc(buf_cap);
    size_t total = 0;
    ssize_t n;

    while ((n = recv(sock, buf + total, buf_cap - total, 0)) > 0) {
        total += n;
    }
    close(sock);

    // Find end of HTTP headers (\r\n\r\n)
    uint8_t* body_start = NULL;
    for (size_t i = 0; i + 3 < total; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            body_start = buf + i + 4;
            break;
        }
    }

    if (body_start) {
        resp.size = total - (body_start - buf);
        resp.data = malloc(resp.size);
        if (resp.data) {
            memcpy(resp.data, body_start, resp.size);
        }
        resp.status_code = 200;
    }

    free(buf);
    return resp;
}

// ═══════════════════════════════════════════════════════════════════════════
// tts_synthesize — Text-to-Speech via Piper ONNX
//
// §XVII·4e lines 7107-7135
//
// 1. POST text to piper-server via HTTP localhost:8083
// 2. Receive raw PCM audio (16-bit, mono, 22050Hz)
// 3. Write PCM to SHM ring slot (PayloadType=4 per §XVII·4a enum)
// 4. Announce TTS_AUDIO on #oracle
//
// Piper output format (Context7 /rhasspy/piper):
//   "Stream raw audio bytes for real-time processing"
//   "16-bit PCM, mono, 22050 Hz"
// ═══════════════════════════════════════════════════════════════════════════

void tts_synthesize(int irc_fd, void* shm, const char* text,
                     const char* voice_id)
{
    if (!text || !shm) return;

    // 1. Build URL with voice parameter
    char url[256];
    snprintf(url, sizeof(url),
        "http://127.0.0.1:8083/synthesize?voice=%s",
        voice_id ? voice_id : "default");

    fprintf(stderr, "[TTS] Synthesising %zu chars, voice=%s\n",
            strlen(text), voice_id ? voice_id : "default");

    // 2. POST text, receive raw PCM audio
    HTTP_RESPONSE resp = http_post(url, text, strlen(text));
    if (!resp.data || resp.size == 0) {
        fprintf(stderr, "[TTS] ERROR: empty response from Piper\n");
        return;
    }

    // 3. Write PCM audio to SHM ring slot
    SHM_RING_CONTROL* ring = (SHM_RING_CONTROL*)shm;
    int slot = shm_ring_acquire_write();
    if (slot < 0) {
        fprintf(stderr, "[TTS] WARNING: SHM ring full — dropping audio\n");
        free(resp.data);
        return;
    }

    void* slot_data = (uint8_t*)shm + SHM_SLOT_DATA_OFFSET(slot);
    memcpy(slot_data, resp.data, resp.size);

    ring->SlotMeta[slot].PayloadType = MOD_AUDIO_OUT;  // = 4
    ring->SlotMeta[slot].PayloadSize = resp.size;
    ring->SlotMeta[slot].Crc64 = crc64_compute(resp.data, resp.size);
    ring->SlotMeta[slot].TimestampNs = clock_gettime_ns(CLOCK_MONOTONIC);
    shm_ring_commit(slot);

    // 4. Announce to host UI via IRC (§XVII·4e lines 7130-7134)
    char msg[256];
    snprintf(msg, sizeof(msg),
        "PRIVMSG #oracle :TTS_AUDIO slot=%d size=%lu "
        "sample_rate=22050 channels=1\r\n",
        slot, (unsigned long)resp.size);
    irc_send(irc_fd, msg);

    fprintf(stderr, "[TTS] Audio written to SHM slot %d (%zu bytes, "
            "22050Hz mono)\n", slot, resp.size);

    free(resp.data);
}

// ═══════════════════════════════════════════════════════════════════════════
// Audio child process management
//
// §XVII·4e lines 7138-7164
// Fork whisper-server (STT) and piper-server (TTS) as child processes.
// Called during modality_init() setup phase.
// ═══════════════════════════════════════════════════════════════════════════

void tts_fork_whisper(const char* model_path)
{
    if (!model_path || model_path[0] == '\0') return;

    pid_t whisper_pid = fork();
    if (whisper_pid == 0) {
        // Child: exec whisper-server
        execl("/sbin/whisper-server", "whisper-server",
              "--model", model_path,
              "--host", "127.0.0.1", "--port", "8081", NULL);
        _exit(1);  // execl failed
    }

    if (whisper_pid > 0) {
        g_Processors[MOD_AUDIO_IN].Enabled   = 1;
        g_Processors[MOD_AUDIO_IN].WorkerPid = whisper_pid;
        fprintf(stderr, "[TTS] Whisper STT forked (pid=%d, port=8081, "
                "model=%s)\n", whisper_pid, model_path);
    } else {
        fprintf(stderr, "[TTS] ERROR: fork() failed for whisper-server\n");
    }
}

void tts_fork_piper(const char* model_path)
{
    if (!model_path || model_path[0] == '\0') return;

    pid_t piper_pid = fork();
    if (piper_pid == 0) {
        // Child: exec piper-server
        execl("/sbin/piper-server", "piper-server",
              "--model", model_path,
              "--host", "127.0.0.1", "--port", "8083", NULL);
        _exit(1);  // execl failed
    }

    if (piper_pid > 0) {
        g_Processors[MOD_AUDIO_OUT].Enabled   = 1;
        g_Processors[MOD_AUDIO_OUT].WorkerPid = piper_pid;
        fprintf(stderr, "[TTS] Piper TTS forked (pid=%d, port=8083, "
                "model=%s)\n", piper_pid, model_path);
    } else {
        fprintf(stderr, "[TTS] ERROR: fork() failed for piper-server\n");
    }
}

#endif /* !_WIN32 */
