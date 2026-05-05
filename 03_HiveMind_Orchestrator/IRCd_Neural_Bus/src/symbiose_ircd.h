// // 03_HiveMind_Orchestrator/IRCd_Neural_Bus/src/symbiose_ircd.h
// Crucible: PATTERN-004 (no unquoted vars), PATTERN-010 (no shell injection)

#ifndef SYMBIOSE_IRCD_H
#define SYMBIOSE_IRCD_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// ============================================================
// Configuration
// ============================================================

// Fortification IV: Jumbo Frame Support
// RFC 1459 limits messages to 512 bytes.
// We extend this to support infinite token streams.
#define SYMBIOSE_IRC_MAX_LINE (1 << 20)  // 1 MiB per message
#define SYMBIOSE_IRC_MAX_CHUNK (1 << 16) // 64 KiB per chunk
#define SYMBIOSE_IRC_MAX_CHANNEL_NAME 256
#define SYMBIOSE_IRC_MAX_NICK 128
#define SYMBIOSE_IRC_MAX_TOPIC 1024

// Neural Bus Channels
#define SYMBIOSE_CHANNEL_ORACLE "#oracle"      // Central LLM
#define SYMBIOSE_CHANNEL_RECON "#recon"        // Scout reconnaissance
#define SYMBIOSE_CHANNEL_HIVEMIND "#hive-mind" // MoE coordination
#define SYMBIOSE_CHANNEL_CONTROL "#control"    // Human operator

// ============================================================
// MoE Protocol Message Types
// ============================================================

typedef enum _SYMBIOSE_MSG_TYPE {
  SymbioseMsgTaskDispatch = 0x01,   // Oracle -> Scout: new task
  SymbioseMsgTaskResult = 0x02,     // Scout -> Oracle: task result
  SymbioseMsgHeartbeat = 0x03,      // All: keep-alive
  SymbioseMsgMigration = 0x04,      // OpenMosix: process migration signal
  SymbioseMsgContextPage = 0x05,    // VFS: context page request/response
  SymbioseMsgNeuralSnapshot = 0x06, // CCD: neural state snapshot
  SymbioseMsgShutdownAck = 0x07,    // Death Rattle: ACK_READY_TO_DIE
} SYMBIOSE_MSG_TYPE;

// ============================================================
// Jumbo Frame Header
// ============================================================

#pragma pack(push, 1)
typedef struct _SYMBIOSE_JUMBO_HEADER {
  uint32_t magic;        // 0x53594D42 ("SYMB")
  uint16_t version;      // Protocol version (1)
  uint8_t msg_type;      // SYMBIOSE_MSG_TYPE
  uint8_t flags;         // Bit 0: compressed, Bit 1: encrypted
  uint64_t payload_len;  // Total payload length
  uint32_t chunk_seq;    // Chunk sequence number (0-indexed)
  uint32_t chunk_count;  // Total number of chunks
  uint32_t chunk_offset; // Offset of this chunk in payload
  uint16_t chunk_len;    // Length of this chunk's data
  uint16_t reserved;     // Reserved for future use
  uint8_t checksum[32];  // SHA-256 of full payload (only in chunk 0)
} SYMBIOSE_JUMBO_HEADER, *PSYMBIOSE_JUMBO_HEADER;
#pragma pack(pop)

_Static_assert(sizeof(SYMBIOSE_JUMBO_HEADER) == 64,
               "Jumbo header must be 64 bytes");

// ============================================================
// Client Connection
// ============================================================

typedef enum _SYMBIOSE_CLIENT_TYPE {
  SymbioseClientOracle = 0,   // Central LLM (channel admin @)
  SymbioseClientScout = 1,    // Scout model
  SymbioseClientOperator = 2, // Human operator
  SymbioseClientUnknown = 255
} SYMBIOSE_CLIENT_TYPE;

typedef struct _SYMBIOSE_CLIENT {
  int fd;
  SYMBIOSE_CLIENT_TYPE type;
  char nick[SYMBIOSE_IRC_MAX_NICK];
  char channel[SYMBIOSE_IRC_MAX_CHANNEL_NAME];
  time_t last_heartbeat;
  time_t connected_since;

  // Jumbo frame reassembly buffer
  uint8_t *jumbo_buffer;
  size_t jumbo_buffer_size;
  uint32_t jumbo_chunks_received;
  uint32_t jumbo_chunks_expected;
} SYMBIOSE_CLIENT, *PSYMBIOSE_CLIENT;

// ============================================================
// Function Declarations
// ============================================================

// Server lifecycle
int SymbioseIrcd_Init(uint16_t port);
int SymbioseIrcd_Run(void);
void SymbioseIrcd_Shutdown(void);

// Client management
PSYMBIOSE_CLIENT SymbioseIrcd_AcceptClient(int server_fd);
void SymbioseIrcd_DisconnectClient(PSYMBIOSE_CLIENT client);

// Jumbo frame handling
int SymbioseIrcd_SendJumbo(PSYMBIOSE_CLIENT client, SYMBIOSE_MSG_TYPE type,
                           const uint8_t *payload, size_t payload_len);
int SymbioseIrcd_RecvJumbo(PSYMBIOSE_CLIENT client, SYMBIOSE_MSG_TYPE *type,
                           uint8_t **payload, size_t *payload_len);

// MoE Protocol
int SymbioseIrcd_DispatchTask(PSYMBIOSE_CLIENT oracle, PSYMBIOSE_CLIENT scout,
                              const char *task_description, size_t desc_len);
int SymbioseIrcd_ReportResult(PSYMBIOSE_CLIENT scout, PSYMBIOSE_CLIENT oracle,
                              const char *result, size_t result_len);

#endif // SYMBIOSE_IRCD_H
