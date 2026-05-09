/*++
 * modality_hotswap.c — Zero-Downtime Modality Upgrade
 *
 * HIVE-MM-006: Hot-swap modality processors without dropping active
 *              inference. Fork new → health-check → atomic swap → kill old.
 *
 * Reference:
 *   - Interactive_Plan.md §XVII·5c (lines 7271-7318) — Hot-Swap
 *
 * Architecture:
 *   When a scout finds a better model (higher accuracy, lower latency),
 *   the hive mind hot-swaps it without downtime:
 *
 *     1. Fork new processor on Port+100 (temporary port)
 *     2. Health-check new instance (500ms grace period)
 *     3. Atomic swap: redirect WorkerPid + Port in registry
 *     4. SIGTERM old processor + waitpid() for clean exit
 *
 *   If the new processor fails health-check, the old one continues
 *   uninterrupted — zero risk of service disruption.
 *
 * Acceptance criteria:
 *   New processor starts on temporary port;
 *   Health-check validates readiness;
 *   Atomic swap completes without dropped frames;
 *   Old processor terminated cleanly
 *--*/

#include "multimodal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* This entire file uses POSIX APIs (fork, exec, socket, waitpid, kill)
 * and runs only inside the Linux guest. */
#ifdef _WIN32
/* Stub: Linux-guest-only module */
#else

#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ═══════════════════════════════════════════════════════════════════════════
// proc_binary_path — Map modality type to executable path
//
// Each modality processor has a corresponding binary in /sbin/ on the
// guest initrd.
// ═══════════════════════════════════════════════════════════════════════════

const char* proc_binary_path(MODALITY_TYPE type)
{
    switch (type) {
    case MOD_IMAGE:
    case MOD_VIDEO:
    case MOD_SCREEN:
    case MOD_DOCUMENT:
        return "/sbin/llama-server";  // Vision via mmproj
    case MOD_AUDIO_IN:
        return "/sbin/whisper-server";
    case MOD_AUDIO_OUT:
        return "/sbin/piper-server";
    default:
        return NULL;  // Inline processors (text, moviola, dibit)
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// health_check — Verify processor is responding on its HTTP port
//
// §XVII·5c line 7295: "Wait for new processor to be ready (health check)"
//
// Attempts a TCP connection to 127.0.0.1:<port>. Returns 1 if
// the connection succeeds, 0 otherwise.
// ═══════════════════════════════════════════════════════════════════════════

int health_check(uint16_t port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Set a 2-second timeout for the connection attempt
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);

    return (result == 0) ? 1 : 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// modality_hotswap — Zero-downtime modality upgrade
//
// §XVII·5c lines 7277-7313
//
// Steps:
//   1. Fork new processor with new model on Port+100
//   2. 500ms grace period + health-check
//   3. Atomic swap: update WorkerPid and Port in registry
//   4. SIGTERM old processor + waitpid()
//
// Returns 0 on success, -1 on failure (old processor kept alive)
// ═══════════════════════════════════════════════════════════════════════════

int modality_hotswap(MODALITY_TYPE type, const char* new_model_path)
{
    if (type >= MOD_MAX || !new_model_path) return -1;

    MODALITY_PROCESSOR* proc = &g_Processors[type];
    const char* binary = proc_binary_path(type);

    if (!binary) {
        fprintf(stderr, "[HOTSWAP] Modality '%s' is an inline processor — "
                "cannot hot-swap\n", proc->Name);
        return -1;
    }

    fprintf(stderr, "[HOTSWAP] Starting hot-swap for '%s' → %s\n",
            proc->Name, new_model_path);

    // 1. Fork new processor on temporary port (Port+100)
    // §XVII·5c lines 7282-7291
    uint16_t temp_port = proc->Port + 100;

    pid_t new_pid = fork();
    if (new_pid == 0) {
        // Child: start new processor on temporary port
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%u", temp_port);
        execl(binary, proc->Name,
              "--model", new_model_path,
              "--host", "127.0.0.1", "--port", port_str, NULL);
        _exit(1);  // execl failed
    }

    if (new_pid < 0) {
        fprintf(stderr, "[HOTSWAP] ERROR: fork() failed\n");
        return -1;
    }

    // 2. Wait for new processor to be ready — §XVII·5c lines 7293-7298
    usleep(500000);  // 500ms grace period

    if (!health_check(temp_port)) {
        fprintf(stderr, "[HOTSWAP] Health-check FAILED on port %u — "
                "killing new process, keeping old\n", temp_port);
        kill(new_pid, SIGTERM);
        waitpid(new_pid, NULL, 0);
        return -1;
    }

    fprintf(stderr, "[HOTSWAP] Health-check PASSED on port %u\n", temp_port);

    // 3. Atomic swap — §XVII·5c lines 7300-7303
    pid_t old_pid = proc->WorkerPid;
    uint16_t old_port = proc->Port;

    proc->WorkerPid = new_pid;
    proc->Port = temp_port;

    fprintf(stderr, "[HOTSWAP] Swapped '%s': pid %d→%d, port %u→%u\n",
            proc->Name, old_pid, new_pid, old_port, temp_port);

    // 4. Gracefully terminate old processor — §XVII·5c lines 7305-7307
    if (old_pid > 0) {
        kill(old_pid, SIGTERM);
        waitpid(old_pid, NULL, 0);
        fprintf(stderr, "[HOTSWAP] Old processor (pid=%d) terminated\n",
                old_pid);
    }

    // 5. Log success — §XVII·5c lines 7310-7311
    fprintf(stderr, "[HOTSWAP] Modality '%s' hot-swapped to %s (pid=%d)\n",
            proc->Name, new_model_path, new_pid);

    return 0;
}

#endif /* !_WIN32 */
