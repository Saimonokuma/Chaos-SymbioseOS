/*++
 * ctcp_dcc.c — CTCP/DCC Protocol Compliance Layer
 *
 * HIVE-IRC-006
 *
 * Reference:
 *   - Interactive_Plan.md §VII·6d (lines 3011-3110)
 *   - https://modern.ircdocs.horse/ctcp
 *   - https://modern.ircdocs.horse/dcc
 *
 * Purpose:
 *   Spec-compliant CTCP query/response handler and DCC message generators.
 *
 * Mandatory CTCP responses (§VII·6d lines 3089-3096):
 *   VERSION    — MUST  — "SymbioseOS-HiveMind/<version> (F32 Neural Cluster)"
 *   CLIENTINFO — SHOULD — "ACTION DCC PING VERSION CLIENTINFO SOURCE TIME FINGER"
 *   PING       — MUST  — Echo back exact params
 *   SOURCE     — MAY   — repo URL
 *   TIME       — SHOULD — ISO 8601 UTC
 *   FINGER     — MAY   — node info
 *
 * DCC wrappers (§VII·6d lines 3039-3084):
 *   ctcp_dcc_send()         — Standard DCC SEND
 *   ctcp_dcc_ssend()        — Secure DCC / TLS (verb SSEND)
 *   ctcp_dcc_send_reverse() — Port 0 NAT traversal
 *--*/

#include "symbiose_ircd.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

// ── CTCP response strings (§VII·6d lines 3089-3096) ────────────────────────
#define CTCP_VERSION_STRING  \
    "SymbioseOS-HiveMind/" IRCD_VERSION " (F32 Neural Cluster)"
#define CTCP_CLIENTINFO_LIST \
    "ACTION DCC PING VERSION CLIENTINFO SOURCE TIME FINGER"
#define CTCP_SOURCE_URL      "https://github.com/Chaos-SymbioseOS"
#define CTCP_DELIM           '\x01'

// ── IPv4 to DCC host integer (§VII·6d line 3032-3037) ──────────────────────
static uint32_t ipv4_to_dcc_host(const char* dotted_quad)
{
    struct in_addr addr;
    if (inet_pton(AF_INET, dotted_quad, &addr) != 1) return 0;
    return ntohl(addr.s_addr);  // DCC spec: host-order integer as string
}

// ═══════════════════════════════════════════════════════════════════════════
// CTCP query handler
//
// Per ircdocs CTCP spec:
//   Queries sent with PRIVMSG, replies sent with NOTICE.
//   Queries to channels always generate private replies.
//
// Called when a PRIVMSG body starts with \x01 (after stripping delimiters).
// ═══════════════════════════════════════════════════════════════════════════
void ctcp_handle_query(IRCD_SERVER* server, int clientIdx,
                        const char* ctcpMsg)
{
    IRC_CLIENT* c = &server->Clients[clientIdx];

    // Parse command and params from "COMMAND params..."
    char cmd[64] = {0};
    char params[IRCD_MSG_MAXLEN] = {0};

    const char* space = strchr(ctcpMsg, ' ');
    if (space) {
        size_t cmdLen = space - ctcpMsg;
        if (cmdLen >= sizeof(cmd)) cmdLen = sizeof(cmd) - 1;
        memcpy(cmd, ctcpMsg, cmdLen);
        strncpy_s(params, sizeof(params), space + 1, _TRUNCATE);
        // Strip trailing \x01
        char* delim = strchr(params, CTCP_DELIM);
        if (delim) *delim = '\0';
    } else {
        strncpy_s(cmd, sizeof(cmd), ctcpMsg, _TRUNCATE);
        char* delim = strchr(cmd, CTCP_DELIM);
        if (delim) *delim = '\0';
    }

    // ── VERSION — Metadata Query (MUST implement) ───────────────────────
    if (_stricmp(cmd, "VERSION") == 0) {
        ircd_send_raw(c,
            ":%s NOTICE %s :\x01""VERSION %s\x01\r\n",
            IRCD_SERVER_NAME, c->Nick, CTCP_VERSION_STRING);
        printf("[CTCP] VERSION → %s\n", c->Nick);
        return;
    }

    // ── CLIENTINFO — Metadata Query (SHOULD implement) ──────────────────
    if (_stricmp(cmd, "CLIENTINFO") == 0) {
        ircd_send_raw(c,
            ":%s NOTICE %s :\x01""CLIENTINFO %s\x01\r\n",
            IRCD_SERVER_NAME, c->Nick, CTCP_CLIENTINFO_LIST);
        printf("[CTCP] CLIENTINFO → %s\n", c->Nick);
        return;
    }

    // ── PING — Extended Query (MUST implement) ──────────────────────────
    // Reply must contain exactly the same parameters as the query.
    if (_stricmp(cmd, "PING") == 0) {
        ircd_send_raw(c,
            ":%s NOTICE %s :\x01""PING %s\x01\r\n",
            IRCD_SERVER_NAME, c->Nick, params);
        printf("[CTCP] PING → %s\n", c->Nick);
        return;
    }

    // ── SOURCE — Metadata Query (MAY implement) ─────────────────────────
    if (_stricmp(cmd, "SOURCE") == 0) {
        ircd_send_raw(c,
            ":%s NOTICE %s :\x01""SOURCE %s\x01\r\n",
            IRCD_SERVER_NAME, c->Nick, CTCP_SOURCE_URL);
        return;
    }

    // ── TIME — Extended Query (SHOULD implement) ────────────────────────
    // Per spec: new implementations SHOULD default to UTC (ISO 8601)
    if (_stricmp(cmd, "TIME") == 0) {
        time_t now = time(NULL);
        struct tm utc;
        gmtime_s(&utc, &now);
        char timeBuf[64];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &utc);
        ircd_send_raw(c,
            ":%s NOTICE %s :\x01""TIME %s\x01\r\n",
            IRCD_SERVER_NAME, c->Nick, timeBuf);
        return;
    }

    // ── FINGER — Metadata Query (MAY implement) ─────────────────────────
    // §VII·6d line 3096: "FINGER hive_mind PID1 — <node_count> nodes"
    if (_stricmp(cmd, "FINGER") == 0) {
        ircd_send_raw(c,
            ":%s NOTICE %s :\x01""FINGER hive_mind PID1 — "
            "F32 Neural Cluster\x01\r\n",
            IRCD_SERVER_NAME, c->Nick);
        return;
    }

    // ── ACTION — Extended Formatting (no reply, display only) ───────────
    if (_stricmp(cmd, "ACTION") == 0) {
        printf("[CTCP] * %s %s\n", c->Nick, params);
        return;
    }

    printf("[CTCP] Unknown query '%s' from %s (ignored)\n", cmd, c->Nick);
}

// ═══════════════════════════════════════════════════════════════════════════
// DCC convenience wrappers (§VII·6d lines 3039-3084)
// ═══════════════════════════════════════════════════════════════════════════

// ── ctcp_dcc_send — Standard DCC SEND ───────────────────────────────────
// Reference: §VII·6d lines 3039-3052
// Format: PRIVMSG <target> :\x01DCC SEND <filename> <host> <port> [<size>]\x01
void ctcp_dcc_send(IRC_CLIENT* client, const char* target,
                    const char* filename, const char* localIp,
                    uint16_t port, uint64_t fileSize)
{
    uint32_t dccHost = ipv4_to_dcc_host(localIp);
    ircd_send_raw(client,
        "PRIVMSG %s :\x01""DCC SEND %s %u %u %llu\x01\r\n",
        target, filename, dccHost, port,
        (unsigned long long)fileSize);
    printf("[DCC] SEND %s → %s (%s:%d, %llu bytes)\n",
           filename, target, localIp, port,
           (unsigned long long)fileSize);
}

// ── ctcp_dcc_ssend — Secure DCC / TLS ───────────────────────────────────
// Reference: §VII·6d lines 3054-3068
// "The verb SSEND is used instead of SEND. The direct TCP connection uses TLS."
void ctcp_dcc_ssend(IRC_CLIENT* client, const char* target,
                     const char* filename, const char* localIp,
                     uint16_t port, uint64_t fileSize)
{
    uint32_t dccHost = ipv4_to_dcc_host(localIp);
    ircd_send_raw(client,
        "PRIVMSG %s :\x01""DCC SSEND %s %u %u %llu\x01\r\n",
        target, filename, dccHost, port,
        (unsigned long long)fileSize);
    printf("[DCC] SSEND (TLS) %s → %s (%s:%d, %llu bytes)\n",
           filename, target, localIp, port,
           (unsigned long long)fileSize);
}

// ── ctcp_dcc_send_reverse — Port 0 NAT traversal ───────────────────────
// Reference: §VII·6d lines 3070-3084
// "When port 0 is advertised, it signals the sending client wishes to
//  open a connection but cannot explicitly offer a listening port."
void ctcp_dcc_send_reverse(IRC_CLIENT* client, const char* target,
                            const char* filename, uint64_t fileSize)
{
    ircd_send_raw(client,
        "PRIVMSG %s :\x01""DCC SEND %s 0 0 %llu\x01\r\n",
        target, filename, (unsigned long long)fileSize);
    printf("[DCC] Reverse SEND (port 0) %s → %s (%llu bytes)\n",
           filename, target, (unsigned long long)fileSize);
}
