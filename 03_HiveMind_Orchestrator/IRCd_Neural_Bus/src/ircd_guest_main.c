/*
 * ircd_guest_main.c — Guest-side IRCd structural build
 *
 * In production, this binary runs inside the VM and connects
 * to the host's IRCd via VirtIO serial or TCP bridge.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define IRCD_PORT 6667

int main(void) {
    printf("[symbiose_ircd-guest] Starting on port %d\n", IRCD_PORT);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[symbiose_ircd-guest] socket");
        return 1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(IRCD_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[symbiose_ircd-guest] bind");
        return 1;
    }

    if (listen(sockfd, 4) < 0) {
        perror("[symbiose_ircd-guest] listen");
        return 1;
    }

    printf("[symbiose_ircd-guest] Listening on 0.0.0.0:%d\n", IRCD_PORT);
    printf("[symbiose_ircd-guest] Ready for Neural Bus connections\n");

    for (;;) {
        sleep(3600);
    }

    return 0;
}
