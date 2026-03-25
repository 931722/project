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
#define SIG_MAX_LEN 5000

ssize_t recv_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    uint8_t *p = buf;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n <= 0) {
            return n;
        }
        total += n;
    }
    return (ssize_t)total;
}

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
    if (argc < 3) {
        fprintf(stderr, "用法: %s <server_ip> <port>\n", argv[0]);
        exit(1);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    int sockfd = -1;
    struct sockaddr_in serv_addr;
    char buf[BUF_SIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        exit(1);
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(1);
    }

    printf("Connected to %s:%d\n", server_ip, port);

    // === ML-DSA 初始化 + 公鑰交換 ===
    MLDSA_Ctx dctx;
    if (mldsa_init(&dctx) != 0) {
        fprintf(stderr, "mldsa_init failed\n");
        close(sockfd);
        exit(1);
    }

    size_t my_pk_len = 0;
    const uint8_t *my_pk = mldsa_get_pk(&dctx, &my_pk_len);

    uint8_t *peer_pk = malloc(my_pk_len);
    if (!peer_pk) {
        perror("malloc peer_pk");
        mldsa_free(&dctx);
        close(sockfd);
        exit(1);
    }

    // client 先送自己的 pk
    if (send_all(sockfd, my_pk, my_pk_len) != (ssize_t)my_pk_len) {
        fprintf(stderr, "send my_pk failed\n");
        free(peer_pk);
        mldsa_free(&dctx);
        close(sockfd);
        exit(1);
    }
    printf("My public key sent to server.\n");

    // 再收 server 的 pk
    printf("Waiting for server public key (%zu bytes)...\n", my_pk_len);
    if (recv_all(sockfd, peer_pk, my_pk_len) != (ssize_t)my_pk_len) {
        fprintf(stderr, "recv peer_pk failed\n");
        free(peer_pk);
        mldsa_free(&dctx);
        close(sockfd);
        exit(1);
    }
    printf("Server public key received.\n");

    printf("輸入訊息聊天，輸入 /quit 離開。\n");

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int maxfd = (sockfd > STDIN_FILENO) ? sockfd : STDIN_FILENO;
        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }

        // 1. server 傳來簽章訊息
        if (FD_ISSET(sockfd, &readfds)) {
            uint32_t header[2];
            ssize_t n = recv_all(sockfd, header, sizeof(header));
            if (n == 0) {
                printf("Server 關閉連線。\n");
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

            if (recv_all(sockfd, sig, sig_len) != (ssize_t)sig_len) {
                fprintf(stderr, "recv sig failed\n");
                free(sig);
                free(msg_buf);
                break;
            }

            if (recv_all(sockfd, msg_buf, msg_len) != (ssize_t)msg_len) {
                fprintf(stderr, "recv msg failed\n");
                free(sig);
                free(msg_buf);
                break;
            }
            msg_buf[msg_len] = '\0';

            if (mldsa_verify(&dctx, peer_pk,
                             (uint8_t *)msg_buf, msg_len,
                             sig, sig_len) == 0) {
                printf("[Server] %s\n", msg_buf);
            } else {
                printf("[警告] 收到驗章失敗的訊息，被丟棄。\n");
            }

            free(sig);
            free(msg_buf);
        }

        // 2. 自己鍵盤輸入 → 簽章送出去
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
                if (send_signed_message(sockfd, &dctx, buf, msg_len) != 0) {
                    fprintf(stderr, "send_signed_message failed\n");
                    break;
                }
            }
        }
    }

    free(peer_pk);
    mldsa_free(&dctx);
    close(sockfd);
    return 0;
}
