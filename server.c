#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "mldsa_helper.h"
#include <sys/select.h>

#define BUF_SIZE 1024
#define SIG_MAX_LEN 5000  // 足夠放 ML-DSA-44 的 signature

// 把 len bytes 全部收完
ssize_t recv_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    uint8_t *p = buf;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n <= 0) {
            return n; // 0 = 對方關閉, <0 = error
        }
        total += n;
    }
    return (ssize_t)total;
}

// 把 len bytes 全部送完
ssize_t send_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const uint8_t *p = buf;
    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, 0);
        if (n <= 0) {
            return n;
        }
        total += n;
    }
    return (ssize_t)total;
}

// 送一個「簽過章的訊息」： header(8 bytes) + sig + msg
int send_signed_message(int sockfd, MLDSA_Ctx *ctx,
                        const char *msg, size_t msg_len) {
    uint8_t sig[SIG_MAX_LEN];
    size_t sig_len = 0;

    if (mldsa_sign(ctx, (const uint8_t *)msg, msg_len, sig, &sig_len) != 0) {
        fprintf(stderr, "sign failed\n");
        return -1;
    }

    uint32_t header[2];
    header[0] = htonl((uint32_t)msg_len);
    header[1] = htonl((uint32_t)sig_len);

    if (send_all(sockfd, header, sizeof(header)) != (ssize_t)sizeof(header)) {
        fprintf(stderr, "send header failed\n");
        return -1;
    }
    if (send_all(sockfd, sig, sig_len) != (ssize_t)sig_len) {
        fprintf(stderr, "send sig failed\n");
        return -1;
    }
    if (send_all(sockfd, msg, msg_len) != (ssize_t)msg_len) {
        fprintf(stderr, "send msg failed\n");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int port = 9000;
    if (argc >= 2) {
        port = atoi(argv[1]);
    }

    int listenfd = -1, connfd = -1;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t cli_len;
    char buf[BUF_SIZE];

    // === 1. 建 socket + bind + listen ===
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listenfd);
        exit(1);
    }

    if (listen(listenfd, 5) < 0) {
        perror("listen");
        close(listenfd);
        exit(1);
    }

    printf("Server listening on port %d...\n", port);

    // === 2. 等 client 連進來 ===
    cli_len = sizeof(cli_addr);
    connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &cli_len);
    if (connfd < 0) {
        perror("accept");
        close(listenfd);
        exit(1);
    }

    printf("Client connected: %s:%d\n",
           inet_ntoa(cli_addr.sin_addr),
           ntohs(cli_addr.sin_port));

    // === 3. ML-DSA 初始化 + 公鑰交換 ===
    MLDSA_Ctx dctx;
    if (mldsa_init(&dctx) != 0) {
        fprintf(stderr, "mldsa_init failed\n");
        close(connfd);
        close(listenfd);
        exit(1);
    }

    size_t my_pk_len = 0;
    const uint8_t *my_pk = mldsa_get_pk(&dctx, &my_pk_len);

    uint8_t *peer_pk = malloc(my_pk_len);
    if (!peer_pk) {
        perror("malloc peer_pk");
        mldsa_free(&dctx);
        close(connfd);
        close(listenfd);
        exit(1);
    }

    // 先收 client 的 pk
    printf("Waiting for client public key (%zu bytes)...\n", my_pk_len);
    if (recv_all(connfd, peer_pk, my_pk_len) != (ssize_t)my_pk_len) {
        fprintf(stderr, "recv peer_pk failed\n");
        free(peer_pk);
        mldsa_free(&dctx);
        close(connfd);
        close(listenfd);
        exit(1);
    }
    printf("Client public key received.\n");

    // 再送自己的 pk 給 client
    if (send_all(connfd, my_pk, my_pk_len) != (ssize_t)my_pk_len) {
        fprintf(stderr, "send my_pk failed\n");
        free(peer_pk);
        mldsa_free(&dctx);
        close(connfd);
        close(listenfd);
        exit(1);
    }
    printf("My public key sent to client.\n");

    printf("輸入訊息聊天，輸入 /quit 離開。\n");

    // === 4. 進入聊天迴圈：select 監聽 socket + stdin ===
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(connfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int maxfd = (connfd > STDIN_FILENO) ? connfd : STDIN_FILENO;
        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }

        // 4-1. 有「對方傳來的簽章訊息」
        if (FD_ISSET(connfd, &readfds)) {
            uint32_t header[2];
            ssize_t n = recv_all(connfd, header, sizeof(header));
            if (n == 0) {
                printf("Client 關閉連線。\n");
                break;
            }
            if (n != (ssize_t)sizeof(header)) {
                fprintf(stderr, "recv header failed\n");
                break;
            }

            uint32_t msg_len = ntohl(header[0]);
            uint32_t sig_len = ntohl(header[1]);

            if (msg_len > 100000 || sig_len > SIG_MAX_LEN) {
                fprintf(stderr, "invalid length: msg=%u sig=%u\n", msg_len, sig_len);
                break;
            }

            uint8_t *sig = malloc(sig_len);
            char *msg_buf = malloc(msg_len + 1);
            if (!sig || !msg_buf) {
                fprintf(stderr, "malloc failed (recv)\n");
                free(sig);
                free(msg_buf);
                break;
            }

            if (recv_all(connfd, sig, sig_len) != (ssize_t)sig_len) {
                fprintf(stderr, "recv sig failed\n");
                free(sig);
                free(msg_buf);
                break;
            }

            if (recv_all(connfd, msg_buf, msg_len) != (ssize_t)msg_len) {
                fprintf(stderr, "recv msg failed\n");
                free(sig);
                free(msg_buf);
                break;
            }
            msg_buf[msg_len] = '\0';

            // 驗章
            if (mldsa_verify(&dctx, peer_pk,
                             (uint8_t *)msg_buf, msg_len,
                             sig, sig_len) == 0) {
                printf("[Client] %s\n", msg_buf);
            } else {
                printf("[警告] 收到驗章失敗的訊息，被丟棄。\n");
            }

            free(sig);
            free(msg_buf);
        }

        // 4-2. 有「自己鍵盤輸入」要送出去
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (!fgets(buf, BUF_SIZE, stdin)) {
                printf("stdin EOF，結束。\n");
                break;
            }
            buf[strcspn(buf, "\n")] = '\0';

            if (strcmp(buf, "/quit") == 0) {
                printf("離線中...\n");
                break;
            }

            size_t msg_len = strlen(buf);
            if (msg_len > 0) {
                if (send_signed_message(connfd, &dctx, buf, msg_len) != 0) {
                    fprintf(stderr, "send_signed_message failed\n");
                    break;
                }
            }
        }
    }

    free(peer_pk);
    mldsa_free(&dctx);
    close(connfd);
    close(listenfd);
    return 0;
}
