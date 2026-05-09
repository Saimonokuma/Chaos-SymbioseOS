/*++
 * irc_qos.c — YeAH! TCP + CAKE QoS Shaping for the Neural Bus
 *
 * HIVE-IRC-010  (P1 — Critical Path)
 *
 * Reference:
 *   - Interactive_Plan.md §XIV·7
 *   - yeah_tcp_cake_analysis.md
 *   - yeah_cake_cross_reference.md
 *   - cake_pdf_audit.md
 *   - tc-cake(8) man page: https://man7.org/linux/man-pages/man8/tc-cake.8.html
 *   - YeAH TCP paper: Baiocchi, Castellani, Vacirca (Univ. Rome)
 *
 * Purpose:
 *   Implements DSCP marking, CAKE qdisc setup (egress + IFB ingress),
 *   YeAH! TCP activation, fwmark-based tin classification, and
 *   observability helpers for the SymbioseOS Neural Bus.
 *
 * Architecture:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                    hive_mind (PID 1)                    │
 *   │  irc_qos_init()  ─── sysctl + tc commands at boot      │
 *   │  irc_set_dscp()  ─── per-socket DSCP marking            │
 *   │  irc_qos_stats() ─── observability via #telemetry       │
 *   └──────────────┬─────────────────────────┬────────────────┘
 *        EGRESS    │                         │    INGRESS
 *   ┌──────────────▼──────┐    ┌─────────────▼────────────────┐
 *   │ eth0 root: CAKE     │    │ eth0 ingress → mirred → IFB  │
 *   │  diffserv4           │    │ ifb4eth0 root: CAKE          │
 *   │  triple-isolate      │    │  diffserv4 wash ingress      │
 *   │  fwmark 0x0F         │    │  triple-isolate              │
 *   │  no-split-gso (10G+) │    │  ack-filter                  │
 *   └─────────────────────┘    └──────────────────────────────┘
 *
 * DiffServ4 Tin Mapping (§XIV·7 + tc-cake(8)):
 *   Voice (CS7,CS6,EF)     → NODE_HEARTBEAT, RECALL_ALL, MOD_STATS
 *   Video (AF4x,AF3x)      → TTS_AUDIO, MOVIOLA_DELTA, DIBIT_NATIVE
 *   Best Effort (CS0)      → General IRC PRIVMSG, SCOUT_DISPATCH
 *   Bulk  (CS1,LE)         → DCC SEND tensors, XDCC, DCC SSEND
 *
 * Constraint X·1: NO WHPX — all networking is real TCP via virtio-net
 *                 in the guest, not paravirtualised.
 *--*/

#include "symbiose_ircd.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <stdlib.h>
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * DSCP Constants — DiffServ Code Points mapped to CAKE tins
 *
 * tc-cake(8) diffserv4 tin assignment:
 *   Voice:       CS7, CS6, EF (0xB8), VA, CS5, CS4
 *   Video:       AF4x, AF3x, CS3, AF2x, CS2, TOS4, TOS1
 *   Best Effort: CS0 (0x00) — default
 *   Bulk:        CS1 (0x20), LE (kernel ≥5.9)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* TOS byte values (DSCP << 2, per RFC 2474) */
#define DSCP_EF       0xB8    /* Expedited Forwarding — Voice tin        */
#define DSCP_CS6      0xC0    /* CS6 — Voice tin                         */
#define DSCP_AF41     0x88    /* AF41 — Video tin                        */
#define DSCP_AF31     0x68    /* AF31 — Video tin                        */
#define DSCP_CS0      0x00    /* Best Effort — default tin               */
#define DSCP_CS1      0x20    /* CS1 — Bulk tin                          */

/* ═══════════════════════════════════════════════════════════════════════════
 * Modality-to-DSCP Mapping
 *
 * These map SymbioseOS modality types to CAKE DiffServ4 tins.
 * The mapping is used by irc_set_dscp() to mark outgoing packets.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Modality type constants (matching SYMBIOSE_JUMBO_HEADER.PayloadType) */
/* Guard against Windows imm.h which also defines MOD_CONTROL, MOD_ALT etc. */
#ifdef _WIN32
#undef MOD_CONTROL
#endif
#define MOD_CONTROL      0x01   /* IRC control, heartbeats  → Voice     */
#define MOD_STATS        0x02   /* Telemetry, RDI reports   → Voice     */
#define MOD_AUDIO        0x10   /* TTS PCM audio            → Video     */
#define MOD_VIDEO        0x11   /* Moviola delta frames     → Video     */
#define MOD_DIBIT        0x12   /* DVS dibit-native events  → Video     */
#define MOD_TEXT         0x20   /* General text, PRIVMSG    → Best Effort*/
#define MOD_SCOUT        0x21   /* Scout dispatch/results   → Best Effort*/
#define MOD_TENSOR       0x30   /* DCC tensor shards        → Bulk      */
#define MOD_XDCC         0x31   /* XDCC bulk transfers      → Bulk      */
#define MOD_CHECKPOINT   0x32   /* Checkpoint WAL data      → Bulk      */

/* ═══════════════════════════════════════════════════════════════════════════
 * irc_dscp_for_modality — Return TOS byte for a given modality type
 * ═══════════════════════════════════════════════════════════════════════════ */
static uint8_t irc_dscp_for_modality(uint32_t modalityType)
{
    switch (modalityType) {
    /* Voice tin — latency-critical control plane */
    case MOD_CONTROL:
    case MOD_STATS:
        return DSCP_EF;

    /* Video tin — real-time media */
    case MOD_AUDIO:
    case MOD_VIDEO:
    case MOD_DIBIT:
        return DSCP_AF41;

    /* Bulk tin — large data transfers */
    case MOD_TENSOR:
    case MOD_XDCC:
    case MOD_CHECKPOINT:
        return DSCP_CS1;

    /* Best Effort — everything else */
    case MOD_TEXT:
    case MOD_SCOUT:
    default:
        return DSCP_CS0;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * irc_set_dscp — Mark a socket with the correct DSCP for its modality
 *
 * Sets IP_TOS on the socket so CAKE's diffserv4 classifier routes
 * packets into the correct priority tin.
 *
 * Also sets SO_SNDBUF to 128MB for bulk tensor transfers (YeAH! TCP
 * high-BDP optimisation — paper §III, "efficient capacity exploitation").
 *
 * Called from:
 *   - dcc_tensor.c   → dcc_stream_data()  after accept()
 *   - ctcp_dcc.c     → ctcp_dcc_ssend()   after TLS handshake
 *   - xdcc_bot.c     → xdcc_handle_send() after DCC offer
 *   - modality_router.c → modality_dispatch() central dispatch
 * ═══════════════════════════════════════════════════════════════════════════ */
void irc_set_dscp(int fd, uint32_t modalityType)
{
    if (fd < 0) return;

    uint8_t tos = irc_dscp_for_modality(modalityType);

#ifdef _WIN32
    DWORD tosVal = (DWORD)tos;
    setsockopt((SOCKET)fd, IPPROTO_IP, IP_TOS,
               (const char*)&tosVal, sizeof(tosVal));
#else
    int tosVal = (int)tos;
    setsockopt(fd, IPPROTO_IP, IP_TOS, &tosVal, sizeof(tosVal));
#endif

    /* For bulk transfers: set 128MB SO_SNDBUF for YeAH! TCP high-BDP.
     * YeAH's STCP fast mode (paper Eq.1) needs large buffers to fully
     * exploit high-bandwidth links without triggering slow mode. */
    if (modalityType == MOD_TENSOR || modalityType == MOD_XDCC ||
        modalityType == MOD_CHECKPOINT) {
        int sndbuf = 134217728;  /* 128MB */
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                   (const char*)&sndbuf, sizeof(sndbuf));

#ifndef _WIN32
        /* Disable Nagle for bulk streaming — data is already chunked
         * at 64MB boundaries by dcc_stream_data() (§VII·6a line 2897) */
        int nodelay = 0;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                   &nodelay, sizeof(nodelay));
#endif
    }

    /* For control messages: enable TCP_NODELAY to minimise latency */
    if (modalityType == MOD_CONTROL || modalityType == MOD_STATS) {
#ifndef _WIN32
        int nodelay = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                   &nodelay, sizeof(nodelay));
#endif
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * irc_qos_run_cmd — Execute a shell command (Linux guest only)
 *
 * Used internally by irc_qos_init() to run tc/sysctl/ip commands.
 * On Windows host, these are no-ops (QoS runs in the Linux guest).
 * ═══════════════════════════════════════════════════════════════════════════ */
static int irc_qos_run_cmd(const char* cmd)
{
#ifdef _WIN32
    /* QoS commands are Linux guest-side only */
    (void)cmd;
    return 0;
#else
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[QOS] Command failed (%d): %s\n", ret, cmd);
    }
    return ret;
#endif
}

/* ═══════════════════════════════════════════════════════════════════════════
 * irc_qos_init — Full QoS initialisation at hive_mind PID 1 boot
 *
 * Sequence (cross-ref: cake_pdf_audit.md + yeah_cake_cross_reference.md):
 *   1. Activate YeAH! TCP congestion control
 *   2. Apply all sysctls (TCP buffers, CAKE defaults, BDP tuning)
 *   3. Set up CAKE egress qdisc on primary interface
 *   4. Set up IFB + CAKE ingress shaping (download traffic)
 *   5. Apply iptables fwmark rules for tin classification
 *   6. (Optional) Set up WAN scout interface
 *
 * Parameters:
 *   dcIface  — datacenter interface name (e.g. "eth0")
 *   dcBwMbit — datacenter bandwidth in Mbit (e.g. 10000 for 10G)
 *   wanIface — WAN scout interface name (NULL to skip)
 *   wanBwMbit— WAN bandwidth in Mbit (e.g. 1000 for 1G)
 * ═══════════════════════════════════════════════════════════════════════════ */
int irc_qos_init(const char* dcIface, int dcBwMbit,
                  const char* wanIface, int wanBwMbit)
{
    char cmd[512];
    int  errors = 0;

    printf("[QOS] ═══ SymbioseOS QoS Init — YeAH! TCP + CAKE ═══\n");

    /* ── Step 1: Activate YeAH! TCP ──────────────────────────────────────
     * YeAH! TCP (paper: Baiocchi et al.) provides:
     *   - Zero packet loss via precautionary decongestion (Table I)
     *   - Hybrid fast/slow mode switching (Eq.1: Q < Q_max → STCP)
     *   - 1/8 loss decrease factor (vs CUBIC's 0.2)
     *   - RTT fairness: Jain's index ≥0.95 at 4:1 ratio (Fig.7) */
    printf("[QOS] Step 1: Activating YeAH! TCP congestion control\n");
    errors += irc_qos_run_cmd(
        "sysctl -w net.ipv4.tcp_congestion_control=yeah");

    /* ── Step 2: Apply sysctls ───────────────────────────────────────────
     * Sources: §XIV·7a + cake_pdf_audit.md Finding #3 */
    printf("[QOS] Step 2: Applying sysctl tunings\n");

    /* 128MB TCP buffer for YeAH high-BDP optimisation */
    errors += irc_qos_run_cmd(
        "sysctl -w net.core.rmem_max=134217728");
    errors += irc_qos_run_cmd(
        "sysctl -w net.core.wmem_max=134217728");
    errors += irc_qos_run_cmd(
        "sysctl -w net.ipv4.tcp_rmem='4096 1048576 134217728'");
    errors += irc_qos_run_cmd(
        "sysctl -w net.ipv4.tcp_wmem='4096 1048576 134217728'");

    /* TCP features required for YeAH */
    errors += irc_qos_run_cmd(
        "sysctl -w net.ipv4.tcp_window_scaling=1");
    errors += irc_qos_run_cmd(
        "sysctl -w net.ipv4.tcp_sack=1");
    errors += irc_qos_run_cmd(
        "sysctl -w net.ipv4.tcp_timestamps=1");

    /* CAKE as default qdisc for all new interfaces
     * (cake_pdf_audit.md Finding #3) */
    errors += irc_qos_run_cmd(
        "sysctl -w net.core.default_qdisc=cake");

    /* Prevent packet drops at high ingress rates (default 1000 too low
     * for 10G — cake_pdf_audit.md Finding #3) */
    errors += irc_qos_run_cmd(
        "sysctl -w net.core.netdev_max_backlog=16384");

    /* CRITICAL: Prevent cwnd reset on idle DCC connections.
     * Without this, every pause between tensor shards triggers TCP
     * slow start again — devastating for intermittent DCC transfers.
     * (cake_pdf_audit.md Finding #3) */
    errors += irc_qos_run_cmd(
        "sysctl -w net.ipv4.tcp_slow_start_after_idle=0");

    /* ── Step 3: CAKE egress qdisc (upload/outbound) ─────────────────────
     * tc-cake(8): diffserv4 = 4 tins (Bulk/BE/Video/Voice)
     *             triple-isolate = per-src + per-dst + per-flow
     *             fwmark 0x0F = programmatic tin override via iptables
     *             no-split-gso = 10G+ throughput (cake_pdf_audit #4)
     *             ack-filter = reduce redundant ACKs */
    printf("[QOS] Step 3: Setting up CAKE egress on %s (%dMbit)\n",
           dcIface, dcBwMbit);

    /* Remove existing qdisc */
    snprintf(cmd, sizeof(cmd),
        "tc qdisc del dev %s root 2>/dev/null; true", dcIface);
    irc_qos_run_cmd(cmd);

    /* Apply CAKE egress — note: no-split-gso for 10G+ datacenter
     * (cake_pdf_audit.md Finding #4: trades ~1ms latency for up to
     * 40% more throughput on bulk DCC transfers) */
    snprintf(cmd, sizeof(cmd),
        "tc qdisc replace dev %s root cake "
        "bandwidth %dmbit "
        "rtt datacentre "
        "diffserv4 "
        "triple-isolate "
        "fwmark 0x0F "
        "ack-filter "
        "%s",
        dcIface, dcBwMbit,
        dcBwMbit >= 10000 ? "no-split-gso" : "split-gso");
    errors += irc_qos_run_cmd(cmd);

    /* ── Step 4: IFB ingress shaping (download/inbound) ──────────────────
     * Without this, inbound DCC RECV tensor downloads are unshaped.
     * Classic SQM pattern from cake_pdf_audit.md Finding #2:
     *   eth0 ingress → mirred redirect → ifb4<iface> → CAKE
     *
     * Required kernel configs (cake_pdf_audit.md Finding #1):
     *   CONFIG_IFB, CONFIG_NET_SCH_INGRESS, CONFIG_NET_ACT_MIRRED,
     *   CONFIG_NET_CLS_ACT, CONFIG_NET_CLS_U32 */
    printf("[QOS] Step 4: Setting up IFB ingress shaping on %s\n",
           dcIface);

    /* Create IFB device for ingress redirect */
    snprintf(cmd, sizeof(cmd),
        "ip link add name ifb4%s type ifb 2>/dev/null; true", dcIface);
    irc_qos_run_cmd(cmd);

    snprintf(cmd, sizeof(cmd), "ip link set ifb4%s up", dcIface);
    errors += irc_qos_run_cmd(cmd);

    /* Attach ingress qdisc and redirect all traffic to IFB */
    snprintf(cmd, sizeof(cmd),
        "tc qdisc del dev %s ingress 2>/dev/null; true", dcIface);
    irc_qos_run_cmd(cmd);

    snprintf(cmd, sizeof(cmd),
        "tc qdisc add dev %s ingress", dcIface);
    errors += irc_qos_run_cmd(cmd);

    snprintf(cmd, sizeof(cmd),
        "tc filter add dev %s parent ffff: protocol all "
        "u32 match u32 0 0 "
        "action mirred egress redirect dev ifb4%s",
        dcIface, dcIface);
    errors += irc_qos_run_cmd(cmd);

    /* CAKE on IFB for ingress shaping
     * Note: wash on ingress clears untrusted DSCP from external sources
     * (cake_pdf_audit.md Finding #5 + Finding #7) */
    snprintf(cmd, sizeof(cmd),
        "tc qdisc add dev ifb4%s root cake "
        "bandwidth %dmbit "
        "rtt datacentre "
        "diffserv4 "
        "triple-isolate "
        "ack-filter "
        "ingress",
        dcIface, dcBwMbit);
    errors += irc_qos_run_cmd(cmd);

    /* ── Step 5: iptables fwmark rules for tin classification ────────────
     * fwmark values map to CAKE tins (fwmark 0x0F mask):
     *   0x01 = Bulk tin       (DCC tensor ports 9000-9255)
     *   0x02 = Best Effort    (default)
     *   0x03 = Video tin      (Piper TTS port 8083)
     *   0x04 = Voice tin      (IRC control port 6667)
     *
     * yeah_cake_cross_reference.md §fwmark MASK */
    printf("[QOS] Step 5: Applying iptables fwmark classification\n");

    /* IRC control → Voice tin (highest priority) */
    errors += irc_qos_run_cmd(
        "iptables -t mangle -A OUTPUT -p tcp --dport 6667 "
        "-j MARK --set-mark 0x04");

    /* Piper TTS → Video tin (real-time audio) */
    errors += irc_qos_run_cmd(
        "iptables -t mangle -A OUTPUT -p tcp --dport 8083 "
        "-j MARK --set-mark 0x03");

    /* DCC tensor ports → Bulk tin (low priority, high throughput) */
    errors += irc_qos_run_cmd(
        "iptables -t mangle -A OUTPUT -p tcp --dport 9000:9255 "
        "-j MARK --set-mark 0x01");

    /* ── Step 6: WAN scout interface (optional) ─────────────────────────
     * For mobile scouts with variable bandwidth:
     *   - autorate-ingress for dynamic BW estimation
     *   - nat for NAT-aware flow isolation
     *   - wash for untrusted DSCP from ISPs
     *   - split-gso for latency (WAN, not datacenter) */
    if (wanIface && wanBwMbit > 0) {
        printf("[QOS] Step 6: Setting up WAN CAKE on %s (%dMbit)\n",
               wanIface, wanBwMbit);

        /* WAN egress */
        snprintf(cmd, sizeof(cmd),
            "tc qdisc del dev %s root 2>/dev/null; true", wanIface);
        irc_qos_run_cmd(cmd);

        snprintf(cmd, sizeof(cmd),
            "tc qdisc replace dev %s root cake "
            "bandwidth %dmbit "
            "rtt internet "
            "diffserv4 "
            "triple-isolate "
            "nat "
            "ack-filter "
            "split-gso",
            wanIface, wanBwMbit);
        errors += irc_qos_run_cmd(cmd);

        /* WAN ingress via IFB — with wash for untrusted DSCP */
        snprintf(cmd, sizeof(cmd),
            "ip link add name ifb4%s type ifb 2>/dev/null; true",
            wanIface);
        irc_qos_run_cmd(cmd);

        snprintf(cmd, sizeof(cmd),
            "ip link set ifb4%s up", wanIface);
        errors += irc_qos_run_cmd(cmd);

        snprintf(cmd, sizeof(cmd),
            "tc qdisc del dev %s ingress 2>/dev/null; true", wanIface);
        irc_qos_run_cmd(cmd);

        snprintf(cmd, sizeof(cmd),
            "tc qdisc add dev %s ingress", wanIface);
        errors += irc_qos_run_cmd(cmd);

        snprintf(cmd, sizeof(cmd),
            "tc filter add dev %s parent ffff: protocol all "
            "u32 match u32 0 0 "
            "action mirred egress redirect dev ifb4%s",
            wanIface, wanIface);
        errors += irc_qos_run_cmd(cmd);

        /* wash + ingress on WAN (cake_pdf_audit.md Finding #7) */
        snprintf(cmd, sizeof(cmd),
            "tc qdisc add dev ifb4%s root cake "
            "bandwidth %dmbit "
            "rtt internet "
            "diffserv4 "
            "triple-isolate "
            "nat "
            "wash "
            "ack-filter "
            "ingress",
            wanIface, wanBwMbit);
        errors += irc_qos_run_cmd(cmd);
    }

    printf("[QOS] ═══ QoS Init complete — %d errors ═══\n", errors);
    return errors == 0 ? 0 : -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * irc_qos_stats — Query CAKE statistics and report on #telemetry
 *
 * Runs `tc -s -d qdisc show` and parses tin stats.
 * Called periodically by the telemetry loop.
 * (cake_pdf_audit.md Finding #6: observability commands)
 * ═══════════════════════════════════════════════════════════════════════════ */
void irc_qos_stats(IRC_CLIENT* client, const char* iface)
{
#ifdef _WIN32
    (void)client; (void)iface;
#else
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "tc -s -d qdisc show dev %s 2>/dev/null", iface);

    FILE* fp = popen(cmd, "r");
    if (!fp) return;

    /* Parse first 5 lines of CAKE stats for telemetry report */
    char line[512];
    int lineNum = 0;
    char statsBuf[2048] = {0};
    int offset = 0;

    while (fgets(line, sizeof(line), fp) && lineNum < 5) {
        /* Strip newline */
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        int written = snprintf(statsBuf + offset,
                               sizeof(statsBuf) - offset,
                               "%s | ", line);
        if (written > 0) offset += written;
        lineNum++;
    }
    pclose(fp);

    /* Report on #telemetry */
    if (client && offset > 0) {
        ircd_send_raw(client,
            "PRIVMSG #telemetry :QOS_STATS dev=%s %s\r\n",
            iface, statsBuf);
    }
#endif
}

/* ═══════════════════════════════════════════════════════════════════════════
 * irc_qos_destroy — Tear down QoS qdiscs and IFB devices
 *
 * Called during ircd_shutdown() or Death Rattle protocol.
 * ═══════════════════════════════════════════════════════════════════════════ */
void irc_qos_destroy(const char* dcIface, const char* wanIface)
{
    char cmd[256];

    printf("[QOS] Tearing down QoS configuration\n");

    /* Remove egress CAKE */
    snprintf(cmd, sizeof(cmd),
        "tc qdisc del dev %s root 2>/dev/null; true", dcIface);
    irc_qos_run_cmd(cmd);

    /* Remove ingress qdisc */
    snprintf(cmd, sizeof(cmd),
        "tc qdisc del dev %s ingress 2>/dev/null; true", dcIface);
    irc_qos_run_cmd(cmd);

    /* Remove IFB device */
    snprintf(cmd, sizeof(cmd),
        "tc qdisc del dev ifb4%s root 2>/dev/null; true", dcIface);
    irc_qos_run_cmd(cmd);
    snprintf(cmd, sizeof(cmd),
        "ip link del ifb4%s 2>/dev/null; true", dcIface);
    irc_qos_run_cmd(cmd);

    /* Remove fwmark rules */
    irc_qos_run_cmd(
        "iptables -t mangle -F OUTPUT 2>/dev/null; true");

    /* WAN cleanup */
    if (wanIface) {
        snprintf(cmd, sizeof(cmd),
            "tc qdisc del dev %s root 2>/dev/null; true", wanIface);
        irc_qos_run_cmd(cmd);
        snprintf(cmd, sizeof(cmd),
            "tc qdisc del dev %s ingress 2>/dev/null; true", wanIface);
        irc_qos_run_cmd(cmd);
        snprintf(cmd, sizeof(cmd),
            "ip link del ifb4%s 2>/dev/null; true", wanIface);
        irc_qos_run_cmd(cmd);
    }

    printf("[QOS] QoS teardown complete\n");
}
