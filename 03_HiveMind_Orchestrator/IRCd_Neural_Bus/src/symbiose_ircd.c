/*++
 * symbiose_ircd.c — SymbioseOS IRCv3 Neural Bus Server
 *
 * HIVE-IRC-001: Core IRC server on TCP 127.0.0.1:6667
 *
 * Reference: Interactive_Plan.md §VII·1 (lines 2531-2574)
 *
 * Handles: NICK, USER, JOIN, PART, PRIVMSG, TAGMSG, QUIT, PING, PONG, CAP
 * Creates 7 default channels per §VII·1 topology.
 *--*/

#include "symbiose_ircd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// ── Default channel names ───────────────────────────────────────────────────
static const char* g_DefaultChannels[IRCD_NUM_DEFAULT_CHANS] = {
    CHAN_ORACLE, CHAN_RECON, CHAN_HIVE_MIND,
    CHAN_CLUSTER_ANNOUNCE, CHAN_TELEMETRY,
    CHAN_CHECKPOINT, CHAN_NEURAL_JAM
};

// ── Send raw formatted string to a client ───────────────────────────────────
void ircd_send_raw(IRC_CLIENT* client, const char* fmt, ...)
{
    if (!client || client->Socket == INVALID_SOCKET) return;
    char buf[IRCD_MSG_MAXLEN + 64];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) send(client->Socket, buf, len, 0);
}

// ── Send numeric reply ──────────────────────────────────────────────────────
void ircd_send_numeric(IRCD_SERVER* s, int ci, const char* num, const char* txt)
{
    const char* nick = s->Clients[ci].HasNick ? s->Clients[ci].Nick : "*";
    ircd_send_raw(&s->Clients[ci], ":%s %s %s %s\r\n",
                  IRCD_SERVER_NAME, num, nick, txt);
}

// ── Broadcast to channel (exclude one client) ──────────────────────────────
void ircd_broadcast_channel(IRCD_SERVER* s, int ch, int excl,
                             const char* fmt, ...)
{
    char buf[IRCD_MSG_MAXLEN + 64];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len <= 0) return;

    uint16_t mask = (uint16_t)(1 << ch);
    for (int i = 0; i < IRCD_MAX_CLIENTS; i++) {
        if (i == excl) continue;
        if (s->Clients[i].State >= CLIENT_REGISTERED &&
            (s->Clients[i].ChannelMask & mask)) {
            send(s->Clients[i].Socket, buf, len, 0);
        }
    }
}

// ── Channel helpers ─────────────────────────────────────────────────────────
int ircd_find_channel(IRCD_SERVER* s, const char* name)
{
    for (int i = 0; i < s->ChannelCount; i++) {
        if (s->Channels[i].Active &&
            _stricmp(s->Channels[i].Name, name) == 0)
            return i;
    }
    return -1;
}

int ircd_create_channel(IRCD_SERVER* s, const char* name)
{
    if (s->ChannelCount >= IRCD_MAX_CHANNELS) return -1;
    int idx = s->ChannelCount++;
    strncpy_s(s->Channels[idx].Name, IRCD_CHAN_MAXLEN, name, _TRUNCATE);
    s->Channels[idx].Topic[0] = '\0';
    s->Channels[idx].Active = true;
    s->Channels[idx].MemberCount = 0;
    return idx;
}

void ircd_join_channel(IRCD_SERVER* s, int ci, int ch)
{
    uint16_t mask = (uint16_t)(1 << ch);
    if (s->Clients[ci].ChannelMask & mask) return;  // Already joined
    s->Clients[ci].ChannelMask |= mask;
    s->Channels[ch].MemberCount++;

    // Notify all members
    ircd_broadcast_channel(s, ch, -1,
        ":%s!%s@%s JOIN %s\r\n",
        s->Clients[ci].Nick, s->Clients[ci].User,
        IRCD_SERVER_NAME, s->Channels[ch].Name);

    // Send topic
    if (s->Channels[ch].Topic[0]) {
        ircd_send_numeric(s, ci, RPL_TOPIC,
            s->Channels[ch].Name);
    }

    // Send names list
    char names[2048] = {0};
    int off = 0;
    for (int i = 0; i < IRCD_MAX_CLIENTS; i++) {
        if (s->Clients[i].State >= CLIENT_REGISTERED &&
            (s->Clients[i].ChannelMask & mask)) {
            off += snprintf(names + off, sizeof(names) - off,
                            "%s ", s->Clients[i].Nick);
        }
    }
    char buf[2200];
    snprintf(buf, sizeof(buf), "= %s :%s",
             s->Channels[ch].Name, names);
    ircd_send_numeric(s, ci, RPL_NAMREPLY, buf);
    snprintf(buf, sizeof(buf), "%s :End of /NAMES list",
             s->Channels[ch].Name);
    ircd_send_numeric(s, ci, RPL_ENDOFNAMES, buf);
}

void ircd_part_channel(IRCD_SERVER* s, int ci, int ch)
{
    uint16_t mask = (uint16_t)(1 << ch);
    if (!(s->Clients[ci].ChannelMask & mask)) return;
    ircd_broadcast_channel(s, ch, -1,
        ":%s!%s@%s PART %s\r\n",
        s->Clients[ci].Nick, s->Clients[ci].User,
        IRCD_SERVER_NAME, s->Channels[ch].Name);
    s->Clients[ci].ChannelMask &= ~mask;
    s->Channels[ch].MemberCount--;
}

// ── Parse IRC message ───────────────────────────────────────────────────────
void ircd_parse_message(const char* raw, IRC_MESSAGE* msg)
{
    memset(msg, 0, sizeof(*msg));
    const char* p = raw;

    // IRCv3 tags: @tag1=val;tag2=val
    if (*p == '@') {
        p++;
        const char* end = strchr(p, ' ');
        if (end) {
            size_t len = end - p;
            if (len >= sizeof(msg->Tags)) len = sizeof(msg->Tags) - 1;
            memcpy(msg->Tags, p, len);
            p = end + 1;
        }
    }

    // Prefix: :nick!user@host
    if (*p == ':') {
        p++;
        const char* end = strchr(p, ' ');
        if (end) {
            size_t len = end - p;
            if (len >= sizeof(msg->Prefix)) len = sizeof(msg->Prefix) - 1;
            memcpy(msg->Prefix, p, len);
            p = end + 1;
        }
    }

    // Command
    const char* end = strchr(p, ' ');
    if (end) {
        size_t len = end - p;
        if (len >= sizeof(msg->Command)) len = sizeof(msg->Command) - 1;
        memcpy(msg->Command, p, len);
        p = end + 1;
    } else {
        // Command only, no params
        strncpy_s(msg->Command, sizeof(msg->Command), p, _TRUNCATE);
        // Strip \r\n
        char* cr = strchr(msg->Command, '\r');
        if (cr) *cr = '\0';
        return;
    }

    // Params + trailing
    while (*p && msg->ParamCount < 16) {
        if (*p == ':') {
            p++;
            strncpy_s(msg->Trailing, sizeof(msg->Trailing), p, _TRUNCATE);
            char* cr = strchr(msg->Trailing, '\r');
            if (cr) *cr = '\0';
            char* lf = strchr(msg->Trailing, '\n');
            if (lf) *lf = '\0';
            msg->HasTrailing = true;
            break;
        }
        end = strchr(p, ' ');
        if (!end) {
            strncpy_s(msg->Params[msg->ParamCount], IRCD_MSG_MAXLEN,
                       p, _TRUNCATE);
            char* cr = strchr(msg->Params[msg->ParamCount], '\r');
            if (cr) *cr = '\0';
            char* lf = strchr(msg->Params[msg->ParamCount], '\n');
            if (lf) *lf = '\0';
            msg->ParamCount++;
            break;
        }
        size_t len = end - p;
        if (len >= IRCD_MSG_MAXLEN) len = IRCD_MSG_MAXLEN - 1;
        memcpy(msg->Params[msg->ParamCount], p, len);
        msg->ParamCount++;
        p = end + 1;
    }
}

// ── Try to complete registration ────────────────────────────────────────────
static void try_register(IRCD_SERVER* s, int ci)
{
    IRC_CLIENT* c = &s->Clients[ci];
    if (c->HasNick && c->HasUser && c->State == CLIENT_CONNECTED) {
        c->State = CLIENT_REGISTERED;
        ircd_send_numeric(s, ci, RPL_WELCOME,
            ":Welcome to the SymbioseOS Neural Bus");
        ircd_send_numeric(s, ci, RPL_YOURHOST,
            ":Your host is " IRCD_SERVER_NAME
            ", running " IRCD_VERSION);
        ircd_send_numeric(s, ci, RPL_CREATED,
            ":This server was created for SymbioseOS V3");
        ircd_send_numeric(s, ci, RPL_MYINFO,
            IRCD_SERVER_NAME " " IRCD_VERSION " o o");
        printf("[IRCD] Client %d registered as %s\n", ci, c->Nick);
    }
}

// ── Handle individual commands ──────────────────────────────────────────────
void ircd_handle_message(IRCD_SERVER* s, int ci, IRC_MESSAGE* msg)
{
    IRC_CLIENT* c = &s->Clients[ci];

    // NICK
    if (_stricmp(msg->Command, "NICK") == 0) {
        const char* nick = msg->ParamCount > 0 ? msg->Params[0] : "";
        if (!nick[0]) {
            ircd_send_numeric(s, ci, ERR_NONICKNAMEGIVEN,
                              ":No nickname given");
            return;
        }
        // Check for duplicate
        for (int i = 0; i < IRCD_MAX_CLIENTS; i++) {
            if (i != ci && s->Clients[i].State >= CLIENT_CONNECTED &&
                _stricmp(s->Clients[i].Nick, nick) == 0) {
                ircd_send_numeric(s, ci, ERR_NICKNAMEINUSE,
                    ":Nickname is already in use");
                return;
            }
        }
        strncpy_s(c->Nick, IRCD_NICK_MAXLEN, nick, _TRUNCATE);
        c->HasNick = true;
        try_register(s, ci);
        return;
    }

    // USER
    if (_stricmp(msg->Command, "USER") == 0) {
        if (msg->ParamCount > 0) {
            strncpy_s(c->User, IRCD_NICK_MAXLEN,
                       msg->Params[0], _TRUNCATE);
        }
        if (msg->HasTrailing) {
            strncpy_s(c->Realname, sizeof(c->Realname),
                       msg->Trailing, _TRUNCATE);
        }
        c->HasUser = true;
        try_register(s, ci);
        return;
    }

    // PING → PONG
    if (_stricmp(msg->Command, "PING") == 0) {
        const char* tok = msg->ParamCount > 0 ? msg->Params[0] :
                          (msg->HasTrailing ? msg->Trailing : IRCD_SERVER_NAME);
        ircd_send_raw(c, ":%s PONG %s :%s\r\n",
                      IRCD_SERVER_NAME, IRCD_SERVER_NAME, tok);
        return;
    }

    // CAP (IRCv3 capability negotiation — HIVE-IRC-002)
    if (_stricmp(msg->Command, "CAP") == 0) {
        if (msg->ParamCount > 0 && _stricmp(msg->Params[0], "LS") == 0) {
            c->State = CLIENT_CAP_NEGOTIATING;
            ircd_send_raw(c, ":%s CAP * LS :%s %s %s\r\n",
                IRCD_SERVER_NAME,
                CAP_MESSAGE_TAGS, CAP_LABELED_RESPONSE, CAP_BATCH);
        } else if (msg->ParamCount > 0 &&
                   _stricmp(msg->Params[0], "REQ") == 0) {
            const char* caps = msg->HasTrailing ? msg->Trailing :
                               (msg->ParamCount > 1 ? msg->Params[1] : "");
            if (strstr(caps, CAP_MESSAGE_TAGS))
                c->CapMessageTags = true;
            if (strstr(caps, CAP_LABELED_RESPONSE))
                c->CapLabeledResponse = true;
            if (strstr(caps, CAP_BATCH))
                c->CapBatch = true;
            ircd_send_raw(c, ":%s CAP * ACK :%s\r\n",
                          IRCD_SERVER_NAME, caps);
        } else if (msg->ParamCount > 0 &&
                   _stricmp(msg->Params[0], "END") == 0) {
            if (c->State == CLIENT_CAP_NEGOTIATING)
                c->State = CLIENT_CONNECTED;
            try_register(s, ci);
        }
        return;
    }

    // Everything below requires registration
    if (c->State < CLIENT_REGISTERED) return;

    // JOIN
    if (_stricmp(msg->Command, "JOIN") == 0) {
        const char* chans = msg->ParamCount > 0 ? msg->Params[0] : "";
        char copy[512];
        strncpy_s(copy, sizeof(copy), chans, _TRUNCATE);
        char* ctx = NULL;
        char* tok = strtok_s(copy, ",", &ctx);
        while (tok) {
            int ch = ircd_find_channel(s, tok);
            if (ch < 0) ch = ircd_create_channel(s, tok);
            if (ch >= 0) ircd_join_channel(s, ci, ch);
            tok = strtok_s(NULL, ",", &ctx);
        }
        return;
    }

    // PART
    if (_stricmp(msg->Command, "PART") == 0 && msg->ParamCount > 0) {
        int ch = ircd_find_channel(s, msg->Params[0]);
        if (ch >= 0) ircd_part_channel(s, ci, ch);
        return;
    }

    // PRIVMSG
    if (_stricmp(msg->Command, "PRIVMSG") == 0 && msg->ParamCount > 0) {
        const char* target = msg->Params[0];
        const char* text = msg->HasTrailing ? msg->Trailing : "";

        // CTCP detection (HIVE-IRC-006) — PRIVMSG bodies starting with
        // \x01 are CTCP queries per ircdocs spec
        if (text[0] == '\x01') {
            ctcp_handle_query(s, ci, text + 1);
            return;
        }

        // XDCC command detection (HIVE-IRC-007) — private messages
        // starting with "XDCC " are dispatched to the tensor bot
        if (_strnicmp(text, "XDCC ", 5) == 0) {
            xdcc_handle_command(s, ci, text + 5);
            return;
        }

        if (target[0] == '#') {
            int ch = ircd_find_channel(s, target);
            if (ch >= 0) {
                ircd_broadcast_channel(s, ch, ci,
                    ":%s!%s@%s PRIVMSG %s :%s\r\n",
                    c->Nick, c->User, IRCD_SERVER_NAME,
                    target, text);
            }
        }
        // Death Rattle detection (HIVE-IRC-004)
        if (strstr(text, "ACK_READY_TO_DIE")) {
            ircd_handle_ack_ready_to_die(s, ci);
        }
        return;
    }

    // TAGMSG (IRCv3 — HIVE-IRC-002)
    if (_stricmp(msg->Command, "TAGMSG") == 0 && msg->ParamCount > 0) {
        const char* target = msg->Params[0];
        if (target[0] == '#') {
            int ch = ircd_find_channel(s, target);
            if (ch >= 0) {
                ircd_broadcast_channel(s, ch, ci,
                    "@%s :%s!%s@%s TAGMSG %s\r\n",
                    msg->Tags, c->Nick, c->User,
                    IRCD_SERVER_NAME, target);
            }
        }
        return;
    }

    // TOPIC
    if (_stricmp(msg->Command, "TOPIC") == 0 && msg->ParamCount > 0) {
        int ch = ircd_find_channel(s, msg->Params[0]);
        if (ch >= 0 && msg->HasTrailing) {
            strncpy_s(s->Channels[ch].Topic, IRCD_MSG_MAXLEN,
                       msg->Trailing, _TRUNCATE);
            ircd_broadcast_channel(s, ch, -1,
                ":%s!%s@%s TOPIC %s :%s\r\n",
                c->Nick, c->User, IRCD_SERVER_NAME,
                s->Channels[ch].Name, msg->Trailing);
        }
        return;
    }

    // QUIT
    if (_stricmp(msg->Command, "QUIT") == 0) {
        printf("[IRCD] Client %d (%s) quit\n", ci, c->Nick);
        closesocket(c->Socket);
        c->Socket = INVALID_SOCKET;
        c->State = CLIENT_DISCONNECTED;
        return;
    }
}

// ── Death Rattle protocol ───────────────────────────────────────────────────
void ircd_signal_shutdown(IRCD_SERVER* s)
{
    s->ShutdownImminent = true;
    int ch = ircd_find_channel(s, CHAN_ORACLE);
    if (ch >= 0) {
        ircd_broadcast_channel(s, ch, -1,
            ":%s PRIVMSG %s :SHUTDOWN_IMMINENT timeout=30\r\n",
            IRCD_SERVER_NAME, CHAN_ORACLE);
    }
    printf("[IRCD] Death Rattle: SHUTDOWN_IMMINENT broadcast\n");
}

void ircd_handle_ack_ready_to_die(IRCD_SERVER* s, int ci)
{
    printf("[IRCD] Death Rattle: ACK_READY_TO_DIE from %s\n",
           s->Clients[ci].Nick);
    s->Running = false;
}

// ── Server init ─────────────────────────────────────────────────────────────
int ircd_init(IRCD_SERVER* s)
{
    memset(s, 0, sizeof(*s));
    s->ListenSocket = INVALID_SOCKET;
    s->Running = true;

    for (int i = 0; i < IRCD_MAX_CLIENTS; i++) {
        s->Clients[i].Socket = INVALID_SOCKET;
        s->Clients[i].State = CLIENT_DISCONNECTED;
    }

    // Create default channels
    for (int i = 0; i < IRCD_NUM_DEFAULT_CHANS; i++) {
        ircd_create_channel(s, g_DefaultChannels[i]);
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[IRCD] WSAStartup failed\n");
        return -1;
    }
#endif

    s->ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s->ListenSocket == INVALID_SOCKET) {
        fprintf(stderr, "[IRCD] socket() failed\n");
        return -1;
    }

    // Allow port reuse
    int opt = 1;
    setsockopt(s->ListenSocket, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(IRCD_PORT);
    inet_pton(AF_INET, IRCD_BIND_ADDR, &addr.sin_addr);

    if (bind(s->ListenSocket, (struct sockaddr*)&addr, sizeof(addr))
        == SOCKET_ERROR) {
        fprintf(stderr, "[IRCD] bind() failed on %s:%d\n",
                IRCD_BIND_ADDR, IRCD_PORT);
        return -1;
    }

    if (listen(s->ListenSocket, 8) == SOCKET_ERROR) {
        fprintf(stderr, "[IRCD] listen() failed\n");
        return -1;
    }

    printf("[IRCD] Listening on %s:%d (%d channels)\n",
           IRCD_BIND_ADDR, IRCD_PORT, s->ChannelCount);
    return 0;
}

// ── Process recv buffer for complete lines ──────────────────────────────────
static void process_client_data(IRCD_SERVER* s, int ci)
{
    IRC_CLIENT* c = &s->Clients[ci];
    char* buf = c->RecvBuf;
    int len = c->RecvBufLen;

    while (len > 0) {
        char* crlf = strstr(buf, "\r\n");
        if (!crlf) break;

        *crlf = '\0';
        int lineLen = (int)(crlf - buf);

        if (lineLen > 0) {
            IRC_MESSAGE msg;
            ircd_parse_message(buf, &msg);
            ircd_handle_message(s, ci, &msg);
        }

        int consumed = lineLen + 2;
        buf += consumed;
        len -= consumed;
    }

    // Move remaining data to front
    if (len > 0 && buf != c->RecvBuf) {
        memmove(c->RecvBuf, buf, len);
    }
    c->RecvBufLen = len;
}

// ── Main server loop (select-based) ─────────────────────────────────────────
int ircd_run(IRCD_SERVER* s)
{
    printf("[IRCD] Entering main loop\n");

    while (s->Running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(s->ListenSocket, &readfds);

        SOCKET maxfd = s->ListenSocket;
        for (int i = 0; i < IRCD_MAX_CLIENTS; i++) {
            if (s->Clients[i].Socket != INVALID_SOCKET) {
                FD_SET(s->Clients[i].Socket, &readfds);
                if (s->Clients[i].Socket > maxfd)
                    maxfd = s->Clients[i].Socket;
            }
        }

        struct timeval tv = { 1, 0 };  // 1 second timeout
        int ready = select((int)(maxfd + 1), &readfds, NULL, NULL, &tv);
        if (ready <= 0) continue;

        // Accept new connections
        if (FD_ISSET(s->ListenSocket, &readfds)) {
            SOCKET newSock = accept(s->ListenSocket, NULL, NULL);
            if (newSock != INVALID_SOCKET) {
                int slot = -1;
                for (int i = 0; i < IRCD_MAX_CLIENTS; i++) {
                    if (s->Clients[i].State == CLIENT_DISCONNECTED) {
                        slot = i;
                        break;
                    }
                }
                if (slot >= 0) {
                    memset(&s->Clients[slot], 0, sizeof(IRC_CLIENT));
                    s->Clients[slot].Socket = newSock;
                    s->Clients[slot].State = CLIENT_CONNECTED;
                    s->ClientCount++;
                    printf("[IRCD] Client %d connected\n", slot);
                } else {
                    closesocket(newSock);
                }
            }
        }

        // Read from clients
        for (int i = 0; i < IRCD_MAX_CLIENTS; i++) {
            if (s->Clients[i].Socket == INVALID_SOCKET) continue;
            if (!FD_ISSET(s->Clients[i].Socket, &readfds)) continue;

            IRC_CLIENT* c = &s->Clients[i];
            int space = IRCD_RECV_BUF_SIZE - c->RecvBufLen - 1;
            if (space <= 0) { c->RecvBufLen = 0; continue; }

            int n = recv(c->Socket, c->RecvBuf + c->RecvBufLen, space, 0);
            if (n <= 0) {
                printf("[IRCD] Client %d disconnected\n", i);
                closesocket(c->Socket);
                c->Socket = INVALID_SOCKET;
                c->State = CLIENT_DISCONNECTED;
                s->ClientCount--;
                continue;
            }
            c->RecvBufLen += n;
            c->RecvBuf[c->RecvBufLen] = '\0';
            process_client_data(s, i);
        }
    }

    printf("[IRCD] Main loop exited\n");
    return 0;
}

// ── Shutdown ────────────────────────────────────────────────────────────────
void ircd_shutdown(IRCD_SERVER* s)
{
    s->Running = false;
    for (int i = 0; i < IRCD_MAX_CLIENTS; i++) {
        if (s->Clients[i].Socket != INVALID_SOCKET) {
            ircd_send_raw(&s->Clients[i],
                ":%s QUIT :Server shutting down\r\n", IRCD_SERVER_NAME);
            closesocket(s->Clients[i].Socket);
        }
    }
    if (s->ListenSocket != INVALID_SOCKET)
        closesocket(s->ListenSocket);
#ifdef _WIN32
    WSACleanup();
#endif
    printf("[IRCD] Shutdown complete\n");
}

// ── Entry point ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    (void)argc; (void)argv;
    printf("=== SymbioseOS IRCd Neural Bus v3.0 ===\n");

    IRCD_SERVER server;
    if (ircd_init(&server) != 0) {
        fprintf(stderr, "[FATAL] IRCd init failed\n");
        return 1;
    }

    ircd_run(&server);
    ircd_shutdown(&server);
    return 0;
}
