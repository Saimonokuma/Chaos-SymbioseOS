/*++
 * symbiose_ircd.h — SymbioseOS IRCv3 Neural Bus — Header
 *
 * HIVE-IRC-001 / HIVE-IRC-004
 *
 * Reference: Interactive_Plan.md §VII·1 (lines 2531-2574)
 *
 * Purpose:
 *   Data structures and constants for the custom IRCv3 daemon.
 *   This server runs on TCP 127.0.0.1:6667 and provides the
 *   Neural Bus communication layer between ChaosLoader (host)
 *   and hive_mind PID 1 (guest).
 *
 * Channel topology (7 channels):
 *   #oracle           — Host ↔ Guest: LLM orchestration
 *   #recon            — Guest → Host: Scout intelligence
 *   #hive-mind        — Guest ↔ Guest: Inter-scout coordination
 *   #cluster-announce — Broadcast: Node discovery
 *   #telemetry        — Guest → Host: Real-time metrics
 *   #checkpoint       — Guest → Host: Death Rattle serialization
 *   #neural-jam       — Guest ↔ Guest: D.E.M.H.X. phase alignment
 *--*/

#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
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

#include <stdint.h>
#include <stdbool.h>

// ── POSIX portability (for guest-side inclusion) ────────────────────────────
#ifndef _WIN32
#include <strings.h>
#define _stricmp      strcasecmp
#define _strnicmp     strncasecmp
#define strncpy_s(d,n,s,t)  strncpy((d),(s),(n)-1)
#define sscanf_s      sscanf
#define snprintf_s    snprintf
static inline int fopen_s(FILE** f, const char* p, const char* m)
{
    *f = fopen(p, m);
    return *f ? 0 : -1;
}
static inline void gmtime_s(struct tm* out, const time_t* t)
{
    gmtime_r(t, out);
}
#endif

// ── Server constants ────────────────────────────────────────────────────────
#define IRCD_BIND_ADDR       "127.0.0.1"
#define IRCD_PORT            6667
#define IRCD_SERVER_NAME     "symbiose.local"
#define IRCD_VERSION         "SymbioseIRCd-3.0"
#define IRCD_MAX_CLIENTS     32
#define IRCD_MAX_CHANNELS    16
#define IRCD_NICK_MAXLEN     32
#define IRCD_MSG_MAXLEN      512     // RFC 2812 limit
#define IRCD_RECV_BUF_SIZE   4096
#define IRCD_CHAN_MAXLEN      64

// ── Channel names (§VII·1 topology) ─────────────────────────────────────────
#define CHAN_ORACLE           "#oracle"
#define CHAN_RECON            "#recon"
#define CHAN_HIVE_MIND        "#hive-mind"
#define CHAN_CLUSTER_ANNOUNCE "#cluster-announce"
#define CHAN_TELEMETRY        "#telemetry"
#define CHAN_CHECKPOINT       "#checkpoint"
#define CHAN_NEURAL_JAM       "#neural-jam"
#define IRCD_NUM_DEFAULT_CHANS  7

// ── IRCv3 CAP names ─────────────────────────────────────────────────────────
#define CAP_MESSAGE_TAGS     "message-tags"
#define CAP_LABELED_RESPONSE "labeled-response"
#define CAP_BATCH            "batch"

// ── IRC numeric replies (RFC 2812 subset) ───────────────────────────────────
#define RPL_WELCOME          "001"
#define RPL_YOURHOST         "002"
#define RPL_CREATED          "003"
#define RPL_MYINFO           "004"
#define RPL_NAMREPLY         "353"
#define RPL_ENDOFNAMES       "366"
#define RPL_TOPIC            "332"
#define RPL_NOTOPIC          "331"
#define RPL_CAP              "CAP"
#define ERR_UNKNOWNCOMMAND   "421"
#define ERR_NONICKNAMEGIVEN  "431"
#define ERR_NICKNAMEINUSE    "433"
#define ERR_NOTONCHANNEL     "442"
#define ERR_NOSUCHCHANNEL    "403"

// ── Client state ────────────────────────────────────────────────────────────
typedef enum _IRC_CLIENT_STATE {
    CLIENT_DISCONNECTED = 0,
    CLIENT_CONNECTED,           // TCP connected, no NICK/USER yet
    CLIENT_REGISTERED,          // NICK + USER received
    CLIENT_CAP_NEGOTIATING      // CAP LS in progress
} IRC_CLIENT_STATE;

// ── Client structure ────────────────────────────────────────────────────────
typedef struct _IRC_CLIENT {
    SOCKET              Socket;
    IRC_CLIENT_STATE    State;
    char                Nick[IRCD_NICK_MAXLEN];
    char                User[IRCD_NICK_MAXLEN];
    char                Realname[128];
    bool                HasNick;
    bool                HasUser;

    // IRCv3 CAP state
    bool                CapMessageTags;
    bool                CapLabeledResponse;
    bool                CapBatch;

    // Receive buffer (partial line accumulation)
    char                RecvBuf[IRCD_RECV_BUF_SIZE];
    int                 RecvBufLen;

    // Channel membership (bitmask — up to 16 channels)
    uint16_t            ChannelMask;
} IRC_CLIENT;

// ── Channel structure ───────────────────────────────────────────────────────
typedef struct _IRC_CHANNEL {
    char                Name[IRCD_CHAN_MAXLEN];
    char                Topic[IRCD_MSG_MAXLEN];
    bool                Active;
    int                 MemberCount;
} IRC_CHANNEL;

// ── Server state ────────────────────────────────────────────────────────────
typedef struct _IRCD_SERVER {
    SOCKET              ListenSocket;
    IRC_CLIENT          Clients[IRCD_MAX_CLIENTS];
    IRC_CHANNEL         Channels[IRCD_MAX_CHANNELS];
    int                 ClientCount;
    int                 ChannelCount;
    bool                Running;

    // Death Rattle state (HIVE-IRC-004)
    bool                ShutdownImminent;
    uint64_t            ShutdownDeadlineMs;
} IRCD_SERVER;

// ── Parsed IRC message ──────────────────────────────────────────────────────
typedef struct _IRC_MESSAGE {
    char                Tags[IRCD_MSG_MAXLEN];      // IRCv3 message tags
    char                Prefix[IRCD_NICK_MAXLEN];   // :nick!user@host
    char                Command[32];                 // NICK, USER, JOIN, etc.
    char                Params[16][IRCD_MSG_MAXLEN]; // Space-separated params
    int                 ParamCount;
    char                Trailing[IRCD_MSG_MAXLEN];   // :trailing text
    bool                HasTrailing;
} IRC_MESSAGE;

// ── Public API ──────────────────────────────────────────────────────────────

// Core server lifecycle
int  ircd_init(IRCD_SERVER* server);
int  ircd_run(IRCD_SERVER* server);
void ircd_shutdown(IRCD_SERVER* server);

// Message handling
void ircd_parse_message(const char* raw, IRC_MESSAGE* msg);
void ircd_handle_message(IRCD_SERVER* server, int clientIdx, IRC_MESSAGE* msg);

// Channel operations
int  ircd_find_channel(IRCD_SERVER* server, const char* name);
int  ircd_create_channel(IRCD_SERVER* server, const char* name);
void ircd_join_channel(IRCD_SERVER* server, int clientIdx, int chanIdx);
void ircd_part_channel(IRCD_SERVER* server, int clientIdx, int chanIdx);

// Sending
void ircd_send_raw(IRC_CLIENT* client, const char* fmt, ...);
void ircd_send_numeric(IRCD_SERVER* server, int clientIdx,
                        const char* numeric, const char* text);
void ircd_broadcast_channel(IRCD_SERVER* server, int chanIdx,
                             int excludeClient, const char* fmt, ...);

// Death Rattle (HIVE-IRC-004)
void ircd_signal_shutdown(IRCD_SERVER* server);
void ircd_handle_ack_ready_to_die(IRCD_SERVER* server, int clientIdx);

// ── dcc_tensor.c (HIVE-IRC-005) ─────────────────────────────────────────────
int  dcc_offer_shard(IRC_CLIENT* client, const char* targetNode,
                      uint32_t layerStart, uint32_t layerEnd,
                      uint64_t shardSize);
int  dcc_stream_data(int transferIdx, const void* data, uint64_t dataSize,
                      IRC_CLIENT* ircClient);
int  dcc_resume_shard(IRC_CLIENT* client, const char* filename,
                       uint16_t port, uint64_t position);
void dcc_close_transfer(int transferIdx);

// ── ctcp_dcc.c (HIVE-IRC-006) ───────────────────────────────────────────────
void ctcp_handle_query(IRCD_SERVER* server, int clientIdx,
                        const char* ctcpMsg);
void ctcp_dcc_send(IRC_CLIENT* client, const char* target,
                    const char* filename, const char* localIp,
                    uint16_t port, uint64_t fileSize);
void ctcp_dcc_ssend(IRC_CLIENT* client, const char* target,
                     const char* filename, const char* localIp,
                     uint16_t port, uint64_t fileSize);
void ctcp_dcc_send_reverse(IRC_CLIENT* client, const char* target,
                            const char* filename, uint64_t fileSize);

// ── xdcc_bot.c (HIVE-IRC-007) ───────────────────────────────────────────────
void     xdcc_bot_init(const char* nodeId);
int      xdcc_bot_add_shard(uint32_t layerStart, uint32_t layerEnd,
                             uint64_t sizeBytes, uint64_t crc64,
                             const char* modelName, uint8_t pinned);
void     xdcc_handle_command(IRCD_SERVER* server, int clientIdx,
                              const char* cmdLine);
void     update_tensor_registry_topic(IRC_CLIENT* client);
uint64_t xdcc_total_size(void);
void     xdcc_transfer_complete(uint64_t bytesTransferred);

// ── shm_ring.c (HIVE-IRC-008) ───────────────────────────────────────────────
// NOTE: Also declared in multimodal.h for cross-tier inclusion.
// shm_ring.c uses a module-global g_RingControl (no pointer params).
int   shm_ring_init(void);
int   shm_ring_acquire_write(void);
void  shm_ring_commit(int slot);
int   shm_ring_acquire_read(void);
void  shm_ring_release(int slot);
void* shm_ring_get_slot_ptr(int slot);
void  shm_ring_set_meta(int slot, uint64_t payloadId, uint64_t payloadSize,
                         uint64_t crc64, uint32_t payloadType,
                         uint32_t sourceChannel);
int   shm_ring_write_jumbo(IRC_CLIENT* client, const char* channel,
                            void* data, uint64_t size, uint32_t payloadType);
void  shm_ring_destroy(void);

// ── tensor_index.c (HIVE-IRC-009) ───────────────────────────────────────────
// NOTE: TENSOR_BLOCK is defined in tensor_index.c (opaque from header).
// Checkpoint writers use forward-declared IRC_CLIENT*.
void     tensor_index_register(uint64_t crc64, uint32_t layerStart,
                                uint32_t layerEnd, uint64_t size,
                                const char* holderNode);
void     tensor_index_announce(IRC_CLIENT* client, uint64_t crc64,
                                uint32_t layerStart, uint32_t layerEnd,
                                const char* holderNode, uint64_t sizeBytes);
void     checkpoint_write_kv(IRC_CLIENT* client, const char* nodeId,
                              uint32_t layerStart, uint32_t layerEnd,
                              uint32_t tokenCount, uint64_t vramAddr);
void     checkpoint_write_node(IRC_CLIENT* client, const char* nodeId,
                                const char* ip, float vramGb,
                                uint8_t rdmaCapable,
                                uint32_t layerStart, uint32_t layerEnd);
void     checkpoint_write_model(IRC_CLIENT* client, const char* modelName,
                                 uint32_t paramsBillions, uint32_t shardCount);
void     checkpoint_write_full(IRC_CLIENT* client);
void     tensor_index_restore_from_log(const char* line);
void     hive_mind_recover_from_checkpoint(const char* logPath);
uint32_t tensor_index_get_count(void);

// ── irc_qos.c (HIVE-IRC-010) ────────────────────────────────────────────────
void irc_set_dscp(int fd, uint32_t modalityType);
int  irc_qos_init(const char* dcIface, int dcBwMbit,
                   const char* wanIface, int wanBwMbit);
void irc_qos_stats(IRC_CLIENT* client, const char* iface);
void irc_qos_destroy(const char* dcIface, const char* wanIface);


