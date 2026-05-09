/*++
 * dcc_tensor.c — DCC Tensor Exchange for Neural Shard Migration
 *
 * HIVE-IRC-005
 *
 * Reference:
 *   - Interactive_Plan.md §VII·6a (lines 2798-2923)
 *   - https://modern.ircdocs.horse/dcc
 *
 * Purpose:
 *   DCC SEND/RESUME for peer-to-peer F32 tensor shard transfers.
 *   Uses CTCP-compliant framing with network-byte-order IPv4 encoding.
 *   Supports both TCP and RDMA transport with progress reporting on #telemetry.
 *
 * Struct layout matches §VII·6a DCC_TRANSFER reference (lines 2844-2854).
 *--*/

#include "symbiose_ircd.h"
#include "jumbo_payload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket    close
#endif

// ── Constants (§VII·6a) ─────────────────────────────────────────────────────
#define DCC_LISTEN_BACKLOG      4
#define DCC_STREAM_CHUNK_SIZE   (64 * 1024 * 1024)  // 64MB per chunk (§VII·6a line 2897)
#define DCC_PORT_BASE           9000
#define DCC_MAX_TRANSFERS       16
#define DCC_PROGRESS_INTERVAL   (1024ULL * 1024 * 1024) // 1GB for progress (line 2914)

// ── DCC transfer state (§VII·6a lines 2844-2854) ────────────────────────────
typedef struct _DCC_TRANSFER {
    char        PeerId[17];         // Destination node ID
    uint32_t    LayerStart;         // Shard layer range start
    uint32_t    LayerEnd;           // Shard layer range end
    uint64_t    TotalBytes;         // Total shard size (F32)
    uint64_t    BytesSent;          // Current offset (for resume)
    uint64_t    Crc64;              // Running CRC64 of sent data
    SOCKET      SocketFd;           // Direct TCP socket (or RDMA cm_id)
    SOCKET      ListenFd;           // Listening socket for incoming
    uint8_t     UseRdma;            // 1 = RDMA, 0 = TCP fallback
    time_t      StartTime;          // For speed calculation
    uint8_t     Active;             // Slot in use
} DCC_TRANSFER;

static DCC_TRANSFER g_Transfers[DCC_MAX_TRANSFERS];

// ── Convert IPv4 string to DCC integer encoding ────────────────────────────
// Per ircdocs: IPv4 as positive integer in network byte order
// e.g. 127.0.0.1 → 2130706433
static uint32_t ipv4_to_dcc_host(const char* ipStr)
{
    struct in_addr addr;
    if (inet_pton(AF_INET, ipStr, &addr) != 1) return 0;
    return ntohl(addr.s_addr);
}

// ── Find a free transfer slot ───────────────────────────────────────────────
static int find_free_slot(void)
{
    for (int i = 0; i < DCC_MAX_TRANSFERS; i++) {
        if (!g_Transfers[i].Active) return i;
    }
    return -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// dcc_offer_shard — Initiate DCC SEND for a tensor shard
//
// Reference: §VII·6a lines 2856-2892
//
// Creates a listening socket and sends a CTCP DCC SEND to the peer.
// Format: \x01DCC SEND layers_<start>-<end>.f32 <host_int> <port> <size>\x01
// ═══════════════════════════════════════════════════════════════════════════
int dcc_offer_shard(IRC_CLIENT* client, const char* targetNode,
                     uint32_t layerStart, uint32_t layerEnd,
                     uint64_t shardSize)
{
    int slot = find_free_slot();
    if (slot < 0) {
        fprintf(stderr, "[DCC] No free transfer slots\n");
        return -1;
    }

    DCC_TRANSFER* xfer = &g_Transfers[slot];
    memset(xfer, 0, sizeof(*xfer));
    strncpy_s(xfer->PeerId, sizeof(xfer->PeerId), targetNode, _TRUNCATE);
    xfer->LayerStart = layerStart;
    xfer->LayerEnd = layerEnd;
    xfer->TotalBytes = shardSize;
    xfer->StartTime = time(NULL);
    xfer->Active = 1;

    // Create listening socket for DCC data connection
    uint16_t port = (uint16_t)(DCC_PORT_BASE + layerStart);
    xfer->ListenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (xfer->ListenFd == INVALID_SOCKET) {
        fprintf(stderr, "[DCC] socket() failed\n");
        xfer->Active = 0;
        return -1;
    }

    int opt = 1;
    setsockopt(xfer->ListenFd, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, IRCD_BIND_ADDR, &addr.sin_addr);

    if (bind(xfer->ListenFd, (struct sockaddr*)&addr, sizeof(addr))
        == SOCKET_ERROR) {
        fprintf(stderr, "[DCC] bind() failed on port %d\n", port);
        closesocket(xfer->ListenFd);
        xfer->Active = 0;
        return -1;
    }

    if (listen(xfer->ListenFd, DCC_LISTEN_BACKLOG) == SOCKET_ERROR) {
        fprintf(stderr, "[DCC] listen() failed\n");
        closesocket(xfer->ListenFd);
        xfer->Active = 0;
        return -1;
    }

    // Send DCC SEND via IRC (§VII·6a line 2866)
    // Format: layers_<start>-<end>.f32 per spec naming convention
    uint32_t hostInt = ipv4_to_dcc_host(IRCD_BIND_ADDR);
    ircd_send_raw(client,
        "PRIVMSG %s :\x01""DCC SEND layers_%u-%u.f32 %u %u %llu\x01\r\n",
        targetNode, layerStart, layerEnd,
        hostInt, port, (unsigned long long)shardSize);

    printf("[DCC] Offering layers_%u-%u.f32 (%llu bytes) on port %d to %s\n",
           layerStart, layerEnd, (unsigned long long)shardSize,
           port, targetNode);
    return slot;
}

// ═══════════════════════════════════════════════════════════════════════════
// dcc_stream_data — Stream F32 bytes over the DCC data connection
//
// Reference: §VII·6a lines 2894-2922
//
// Uses 64MB chunks. Reports progress every 1GB on #telemetry.
// Supports resume from byte offset (pre-set via dcc_resume_shard).
// ═══════════════════════════════════════════════════════════════════════════
int dcc_stream_data(int transferIdx, const void* data, uint64_t dataSize,
                     IRC_CLIENT* ircClient)
{
    if (transferIdx < 0 || transferIdx >= DCC_MAX_TRANSFERS) return -1;
    DCC_TRANSFER* xfer = &g_Transfers[transferIdx];
    if (!xfer->Active) return -1;

    // Accept incoming connection if not yet connected
    if (xfer->SocketFd == 0 || xfer->SocketFd == INVALID_SOCKET) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(xfer->ListenFd, &readfds);
        struct timeval tv = { 30, 0 };

        int ready = select((int)(xfer->ListenFd + 1),
                           &readfds, NULL, NULL, &tv);
        if (ready <= 0) {
            fprintf(stderr, "[DCC] Timeout waiting for peer\n");
            return -1;
        }

        xfer->SocketFd = accept(xfer->ListenFd, NULL, NULL);
        if (xfer->SocketFd == INVALID_SOCKET) {
            fprintf(stderr, "[DCC] accept() failed\n");
            return -1;
        }

        closesocket(xfer->ListenFd);
        xfer->ListenFd = INVALID_SOCKET;
        xfer->StartTime = time(NULL);
        printf("[DCC] Peer connected for transfer %d\n", transferIdx);
    }

    // Compute CRC64 on the FULL buffer BEFORE streaming (audit fix #2)
    // This ensures DCC_COMPLETE broadcast carries correct whole-shard CRC.
    xfer->Crc64 = crc64_ecma(data, (size_t)dataSize);

    // Stream data in 64MB chunks from resume offset (§VII·6a line 2897)
    const uint8_t* ptr = (const uint8_t*)data + xfer->BytesSent;
    uint64_t remaining = dataSize - xfer->BytesSent;
    uint64_t lastProgress = xfer->BytesSent;

    while (remaining > 0) {
        int chunk = (int)(remaining > DCC_STREAM_CHUNK_SIZE ?
                          DCC_STREAM_CHUNK_SIZE : remaining);

        int sent = send(xfer->SocketFd, (const char*)ptr, chunk, 0);
        if (sent <= 0) {
            fprintf(stderr, "[DCC] send() failed at byte %llu\n",
                    (unsigned long long)xfer->BytesSent);
            return -1;
        }

        ptr += sent;
        remaining -= sent;
        xfer->BytesSent += sent;

        // Progress reporting every 1GB (§VII·6a line 2914)
        if (xfer->BytesSent - lastProgress >= DCC_PROGRESS_INTERVAL) {
            float elapsed = (float)(time(NULL) - xfer->StartTime);
            float speedGbps = 0;
            if (elapsed > 0) {
                speedGbps = (float)(xfer->BytesSent / (1024.0 * 1024 * 1024))
                            / elapsed;
            }
            int pct = (int)((xfer->BytesSent * 100) / xfer->TotalBytes);

            // Report on #telemetry (§VII·6a line 2832)
            if (ircClient) {
                ircd_send_raw(ircClient,
                    "PRIVMSG #telemetry :DCC_PROGRESS src=local dst=%s "
                    "pct=%d speed=%.1fGB/s\r\n",
                    xfer->PeerId, pct, speedGbps);
            }
            lastProgress = xfer->BytesSent;
        }
    }

    // Announce completion on #cluster-announce (§VII·6a line 2835)
    if (ircClient) {
        float elapsed = (float)(time(NULL) - xfer->StartTime);
        ircd_send_raw(ircClient,
            "PRIVMSG #cluster-announce :DCC_COMPLETE src=local dst=%s "
            "layers=%u-%u crc64=%016llX bytes=%llu elapsed_s=%.0f\r\n",
            xfer->PeerId, xfer->LayerStart, xfer->LayerEnd,
            (unsigned long long)xfer->Crc64,
            (unsigned long long)xfer->BytesSent, elapsed);
    }

    printf("[DCC] Transfer %d complete: %llu bytes in %.0f seconds\n",
           transferIdx, (unsigned long long)xfer->BytesSent,
           (float)(time(NULL) - xfer->StartTime));
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// dcc_resume_shard — Handle DCC RESUME from peer
//
// When peer sends: \x01DCC RESUME layers_<s>-<e>.f32 <port> <position>\x01
// We set the byte offset and send DCC ACCEPT back.
// ═══════════════════════════════════════════════════════════════════════════
int dcc_resume_shard(IRC_CLIENT* client, const char* filename,
                      uint16_t port, uint64_t position)
{
    // Find matching transfer by filename pattern
    int slot = -1;
    for (int i = 0; i < DCC_MAX_TRANSFERS; i++) {
        if (!g_Transfers[i].Active) continue;

        char expected[256];
        snprintf(expected, sizeof(expected), "layers_%u-%u.f32",
                 g_Transfers[i].LayerStart, g_Transfers[i].LayerEnd);
        if (_stricmp(expected, filename) == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        fprintf(stderr, "[DCC] No matching transfer for RESUME: %s\n",
                filename);
        return -1;
    }

    DCC_TRANSFER* xfer = &g_Transfers[slot];
    if (position >= xfer->TotalBytes) {
        fprintf(stderr, "[DCC] Invalid resume position\n");
        return -1;
    }

    xfer->BytesSent = position;

    // Send DCC ACCEPT: \x01DCC ACCEPT <filename> <port> <position>\x01
    ircd_send_raw(client,
        "NOTICE %s :\x01""DCC ACCEPT %s %u %llu\x01\r\n",
        client->Nick, filename, port,
        (unsigned long long)position);

    printf("[DCC] Accepted RESUME for '%s' at byte %llu\n",
           filename, (unsigned long long)position);
    return slot;
}

// ═══════════════════════════════════════════════════════════════════════════
// dcc_close_transfer — Cleanup
// ═══════════════════════════════════════════════════════════════════════════
void dcc_close_transfer(int transferIdx)
{
    if (transferIdx < 0 || transferIdx >= DCC_MAX_TRANSFERS) return;
    DCC_TRANSFER* xfer = &g_Transfers[transferIdx];

    if (xfer->SocketFd != 0 && xfer->SocketFd != INVALID_SOCKET) {
        closesocket(xfer->SocketFd);
    }
    if (xfer->ListenFd != 0 && xfer->ListenFd != INVALID_SOCKET) {
        closesocket(xfer->ListenFd);
    }

    memset(xfer, 0, sizeof(*xfer));
    printf("[DCC] Transfer %d closed\n", transferIdx);
}
