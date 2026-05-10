/*
 * hive_mind_init.c — PID 1 for SymbioseOS Guest (§XVII·3)
 *
 * This is the first process the Chaos kernel executes.
 * It orchestrates the full SymbioseOS boot sequence:
 *
 *   Step 1:  Mount essential filesystems
 *   Step 2:  Set hostname from node config
 *   Step 3:  Bring up loopback network
 *   Step 4:  Fork + exec symbiose_ircd
 *   Step 5:  Read /etc/symbiose/model.conf
 *   Step 6:  Fork + exec llama-server
 *   Step 7:  Map SHM GPA + wait for Ready
 *   Step 8:  Connect to IRC + JOIN 7 channels
 *   Step 9:  Enter hive_mind_event_loop()
 *   Step 10: SIGTERM death rattle handler (§III·5)
 *
 * Build: gcc -static -Os -o hive_mind hive_mind_init.c [modules...] -lm -lpthread
 * Source: 03_HiveMind_Orchestrator/ChaosLoader/src/hive_mind_init.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sched.h>

/* ── Configuration ────────────────────────────────────────────────────── */
#define IRC_HOST       "127.0.0.1"
#define IRC_PORT       6667
#define LLAMA_PORT     8080
#define SHM_SIZE_BYTES (4ULL * 1024 * 1024 * 1024)  /* 4GB */
#define MODEL_CONF     "/etc/symbiose/model.conf"
#define NODE_CONF      "/etc/symbiose/node.conf"

/* ── IRC channel list (§XVI·3) ────────────────────────────────────────── */
static const char *IRC_CHANNELS[] = {
    "#oracle",
    "#recon",
    "#hive-mind",
    "#cluster-announce",
    "#telemetry",
    "#checkpoint",
    "#neural-jam",
    NULL
};

/* ── Global state ─────────────────────────────────────────────────────── */
static volatile int g_running = 1;
static pid_t g_ircd_pid  = -1;
static pid_t g_llama_pid = -1;

/* ── Model config (parsed from /etc/symbiose/model.conf) ──────────────── */
typedef struct {
    char model_path[512];
    char format[64];
    char mmproj_path[512];
    int  multimodal;
    int  port;
} model_config_t;

static model_config_t g_model_cfg;

/* ── Forward declarations ─────────────────────────────────────────────── */
static int  irc_connect(const char *host, int port);
static void irc_send_raw(int fd, const char *msg);
static void irc_join_channels(int fd);
static void parse_model_conf(model_config_t *cfg);
static void death_rattle(int sig);
static void reap_children(int sig);

/* ══════════════════════════════════════════════════════════════════════ */
/*                           MAIN — PID 1                               */
/* ══════════════════════════════════════════════════════════════════════ */

int main(void)
{
    printf("\n");
    printf("════════════════════════════════════════════\n");
    printf("  SymbioseOS hive_mind PID 1\n");
    printf("  F32 Neural Cluster — Boot Sequence\n");
    printf("════════════════════════════════════════════\n");
    printf("\n");

    /* ── Step 1: Mount essential filesystems ───────────────────────── */
    printf("[hive_mind] Step 1: Mounting filesystems...\n");
    mount("proc",     "/proc", "proc",     0, NULL);
    mount("sysfs",    "/sys",  "sysfs",    0, NULL);
    mount("devtmpfs", "/dev",  "devtmpfs", 0, NULL);
    mount("tmpfs",    "/tmp",  "tmpfs",    0, NULL);
    printf("[hive_mind] Filesystems mounted.\n");

    /* ── Step 2: Set hostname ─────────────────────────────────────── */
    printf("[hive_mind] Step 2: Setting hostname...\n");
    sethostname("symbiose-hive", 13);

    /* ── Step 3: Bring up loopback ────────────────────────────────── */
    printf("[hive_mind] Step 3: Bringing up loopback...\n");
    system("ip link set lo up 2>/dev/null");
    system("ip addr add 127.0.0.1/8 dev lo 2>/dev/null");

    /* ── Install signal handlers ──────────────────────────────────── */
    signal(SIGTERM, death_rattle);
    signal(SIGCHLD, reap_children);

    /* ── Step 4: Start symbiose_ircd ──────────────────────────────── */
    printf("[hive_mind] Step 4: Starting symbiose_ircd...\n");
    g_ircd_pid = fork();
    if (g_ircd_pid == 0) {
        execl("/sbin/symbiose_ircd", "symbiose_ircd",
              "--bind", IRC_HOST, "--port", "6667", NULL);
        /* If exec fails, try alternate path */
        execl("/usr/bin/symbiose_ircd", "symbiose_ircd",
              "--bind", IRC_HOST, "--port", "6667", NULL);
        perror("[hive_mind] execl symbiose_ircd failed");
        _exit(1);
    }
    if (g_ircd_pid < 0) {
        perror("[hive_mind] fork symbiose_ircd failed");
    } else {
        printf("[hive_mind] symbiose_ircd started (PID %d)\n", g_ircd_pid);
    }
    usleep(200000);  /* 200ms grace for IRCd to bind */

    /* ── Step 5: Read model config ────────────────────────────────── */
    printf("[hive_mind] Step 5: Reading model config...\n");
    memset(&g_model_cfg, 0, sizeof(g_model_cfg));
    g_model_cfg.port = LLAMA_PORT;
    strncpy(g_model_cfg.format, "SafeTensors", sizeof(g_model_cfg.format) - 1);
    parse_model_conf(&g_model_cfg);
    printf("[hive_mind] Model: %s (format: %s, multimodal: %s)\n",
           g_model_cfg.model_path[0] ? g_model_cfg.model_path : "(none)",
           g_model_cfg.format,
           g_model_cfg.multimodal ? "yes" : "no");

    /* ── Step 6: Start llama-server ───────────────────────────────── */
    printf("[hive_mind] Step 6: Starting llama-server...\n");
    if (g_model_cfg.model_path[0]) {
        g_llama_pid = fork();
        if (g_llama_pid == 0) {
            if (g_model_cfg.multimodal && g_model_cfg.mmproj_path[0]) {
                execl("/sbin/llama-server", "llama-server",
                      "--model", g_model_cfg.model_path,
                      "--mmproj", g_model_cfg.mmproj_path,
                      "--port", "8080",
                      "--host", "127.0.0.1",
                      NULL);
            } else {
                execl("/sbin/llama-server", "llama-server",
                      "--model", g_model_cfg.model_path,
                      "--port", "8080",
                      "--host", "127.0.0.1",
                      NULL);
            }
            perror("[hive_mind] execl llama-server failed");
            _exit(1);
        }
        if (g_llama_pid > 0) {
            printf("[hive_mind] llama-server started (PID %d)\n", g_llama_pid);
        }
    } else {
        printf("[hive_mind] No model configured — llama-server skipped.\n");
    }
    usleep(500000);  /* 500ms for llama-server to init */

    /* ── Step 7: Map SHM (deferred until hypervisor Ready) ────────── */
    printf("[hive_mind] Step 7: SHM mapping deferred (awaiting hypervisor).\n");
    /* In production: read GPA from GUEST_RAX on first VM-Exit
     * uint64_t shm_gpa = read_gpa_from_register();
     * void* shm = mmap(...);
     * volatile SHM_CONTROL_HEADER* hdr = (SHM_CONTROL_HEADER*)shm;
     * while (hdr->Ready != 1) sched_yield();
     */

    /* ── Step 8: Connect to IRC + JOIN channels ───────────────────── */
    printf("[hive_mind] Step 8: Connecting to IRC Neural Bus...\n");
    int irc_fd = -1;
    int retries = 0;
    while (irc_fd < 0 && retries < 10 && g_running) {
        irc_fd = irc_connect(IRC_HOST, IRC_PORT);
        if (irc_fd < 0) {
            retries++;
            printf("[hive_mind] IRC connect retry %d/10...\n", retries);
            sleep(1);
        }
    }

    if (irc_fd >= 0) {
        printf("[hive_mind] IRC connected on fd %d\n", irc_fd);
        irc_send_raw(irc_fd, "NICK hive_mind\r\n");
        irc_send_raw(irc_fd, "USER hive_mind 0 * :Hive Mind PID 1\r\n");
        usleep(100000);
        irc_join_channels(irc_fd);
        irc_send_raw(irc_fd, "PRIVMSG #cluster-announce :HIVE_ONLINE node=hive_mind params=0\r\n");
        printf("[hive_mind] HIVE_ONLINE announced.\n");
    } else {
        printf("[hive_mind] WARNING: Could not connect to IRC after 10 retries.\n");
    }

    /* ── Step 9: Event loop — never returns ───────────────────────── */
    printf("\n");
    printf("════════════════════════════════════════════\n");
    printf("  SymbioseOS hive_mind — ONLINE\n");
    printf("  IRC: %s    llama: %s\n",
           irc_fd >= 0 ? "connected" : "OFFLINE",
           g_llama_pid > 0 ? "running" : "skipped");
    printf("════════════════════════════════════════════\n");
    printf("\n");

    /* Main event loop: poll IRC, check children, yield */
    char buf[4096];
    while (g_running) {
        if (irc_fd >= 0) {
            fd_set rfds;
            struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
            FD_ZERO(&rfds);
            FD_SET(irc_fd, &rfds);

            int ret = select(irc_fd + 1, &rfds, NULL, NULL, &tv);
            if (ret > 0 && FD_ISSET(irc_fd, &rfds)) {
                int n = read(irc_fd, buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    /* Handle PING to keep connection alive */
                    if (strncmp(buf, "PING", 4) == 0) {
                        buf[1] = 'O';  /* PING → PONG */
                        irc_send_raw(irc_fd, buf);
                    }
                    /* Route IRC messages to modules here */
                } else if (n == 0) {
                    printf("[hive_mind] IRC connection closed, reconnecting...\n");
                    close(irc_fd);
                    irc_fd = irc_connect(IRC_HOST, IRC_PORT);
                }
            }
        } else {
            sleep(5);
        }

        /* Check if children are alive */
        if (g_ircd_pid > 0 && waitpid(g_ircd_pid, NULL, WNOHANG) != 0) {
            printf("[hive_mind] symbiose_ircd exited — restarting...\n");
            g_ircd_pid = fork();
            if (g_ircd_pid == 0) {
                execl("/sbin/symbiose_ircd", "symbiose_ircd",
                      "--bind", IRC_HOST, "--port", "6667", NULL);
                _exit(1);
            }
        }
    }

    /* ── Step 10: Death Rattle — handled by signal handler ────────── */
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════ */
/*                        Helper Functions                              */
/* ══════════════════════════════════════════════════════════════════════ */

/* Step 10: SIGTERM handler — Death Rattle (§III·5) */
static void death_rattle(int sig)
{
    (void)sig;
    g_running = 0;

    printf("[hive_mind] SIGTERM received — initiating Death Rattle...\n");

    /* Write ACK to serial console so KMDF unblocks EvtDeviceD0Exit */
    int tty = open("/dev/ttyS0", O_WRONLY);
    if (tty >= 0) {
        const char *ack = "ACK_READY_TO_DIE\n";
        write(tty, ack, strlen(ack));
        close(tty);
    }

    /* Graceful shutdown: children first */
    if (g_llama_pid > 0) {
        kill(g_llama_pid, SIGTERM);
        waitpid(g_llama_pid, NULL, 0);
    }
    if (g_ircd_pid > 0) {
        kill(g_ircd_pid, SIGTERM);
        waitpid(g_ircd_pid, NULL, 0);
    }

    sync();
    reboot(RB_POWER_OFF);
}

/* SIGCHLD handler — reap zombies */
static void reap_children(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

/* IRC connect helper */
static int irc_connect(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* IRC send helper (local, renamed to avoid conflict with shared irc_send in glue) */
static void irc_send_raw(int fd, const char *msg)
{
    if (fd >= 0 && msg) {
        write(fd, msg, strlen(msg));
    }
}

/* IRC join all 7 channels */
static void irc_join_channels(int fd)
{
    for (int i = 0; IRC_CHANNELS[i] != NULL; i++) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "JOIN %s\r\n", IRC_CHANNELS[i]);
        irc_send_raw(fd, cmd);
    }
}

/* Parse model.conf — simple key extraction (no JSON lib in musl static) */
static void parse_model_conf(model_config_t *cfg)
{
    FILE *f = fopen(MODEL_CONF, "r");
    if (!f) {
        printf("[hive_mind] No model.conf found — using defaults.\n");
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Extract "model_path": "..." */
        char *key, *val;
        if ((key = strstr(line, "\"model_path\"")) && (val = strchr(key + 12, '"'))) {
            char *end = strchr(val + 1, '"');
            if (end) { memcpy(cfg->model_path, val + 1, end - val - 1); }
        }
        if ((key = strstr(line, "\"format\"")) && (val = strchr(key + 8, '"'))) {
            char *end = strchr(val + 1, '"');
            if (end) { memcpy(cfg->format, val + 1, end - val - 1); }
        }
        if ((key = strstr(line, "\"mmproj_path\"")) && (val = strchr(key + 13, '"'))) {
            char *end = strchr(val + 1, '"');
            if (end) { memcpy(cfg->mmproj_path, val + 1, end - val - 1); }
        }
        if (strstr(line, "\"multimodal\"") && strstr(line, "true")) {
            cfg->multimodal = 1;
        }
    }
    fclose(f);
}
