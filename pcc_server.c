#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>

#define VALID_NUM_ARGS 2
#define MIN_PRINTABLE_CHAR 32
#define MAX_PRINTABLE_CHAR 126
#define LISTEN_QUEUE_SIZE 10
#define BUFF_SIZE 1048576   // 2^20B = 1 MB
#define TCP_ERROR (errno == ETIMEDOUT || errno ==  ECONNRESET || errno == EPIPE)

static uint32_t pcc_total[MAX_PRINTABLE_CHAR - MIN_PRINTABLE_CHAR + 1];
static int connfd , sigint_sent;

void exit_server() {
    for (int i = 0; i < MAX_PRINTABLE_CHAR - MIN_PRINTABLE_CHAR + 1; i++) {
        printf("char '%c' : %u times\n", i + MIN_PRINTABLE_CHAR, pcc_total[i]);
    }
    exit(EXIT_SUCCESS);
}

void sigint_handler() {
    if (connfd < 0) {
        exit_server();
    } else {
        sigint_sent = 1;
    }
};
const struct sigaction sigint = {.sa_handler = &sigint_handler, .sa_flags = SA_RESTART};

int main(int argc, char *argv[])
{
    in_port_t server_port;
    ssize_t bytes_cnt, bytes_tot, i;
    __off_t N;
    uint32_t C;
    int tcp_err, listenfd = -1;
    connfd = -1, sigint_sent = 0;

    if (argc != VALID_NUM_ARGS) {
        fprintf(stderr, "The program should get %u arguments: %s.\n", VALID_NUM_ARGS - 1, strerror(EINVAL));
        exit(EXIT_FAILURE);
    }
    server_port = (in_port_t) atoi(argv[1]);

    char buff[BUFF_SIZE];
    uint32_t pcc_counter[MAX_PRINTABLE_CHAR - MIN_PRINTABLE_CHAR + 1];

    if (sigaction(SIGINT, &sigint, 0) != 0) {
        fprintf(stderr, "Error setting SIGINT handler: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((listenfd = socket( AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Socket Failed: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0) {
        fprintf(stderr, "Set socket option Failed: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    socklen_t addrsize = sizeof(struct sockaddr_in);
    memset(&serv_addr, 0, addrsize);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY = any local machine address
    serv_addr.sin_port = htons(server_port);

    if(bind(listenfd,(struct sockaddr*) &serv_addr,addrsize) != 0)
    {
        fprintf(stderr, "Bind Failed: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(listen(listenfd, LISTEN_QUEUE_SIZE) != 0)
    {
        fprintf(stderr,"Listen Failed: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    while (!sigint_sent) {
        if ((connfd = accept(listenfd, NULL, NULL)) < 0) {
            fprintf(stderr,"Accept Failed: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        C = tcp_err = 0;
        memset(&pcc_counter, 0, sizeof(pcc_counter));

        bytes_tot = 0;
        while (bytes_tot < sizeof(N)) {
            if((bytes_cnt = read(connfd, buff + bytes_tot, sizeof(N) - bytes_tot)) <= 0) {
                if ((bytes_cnt == 0 && bytes_tot < sizeof(N)) || TCP_ERROR) {
                    fprintf(stderr, "TCP error (N): %s.\nAccepting new connections.", strerror(errno));
                    close(connfd);
                    connfd = -1;
                    tcp_err = 1;
                    break;
                } else if (bytes_cnt < 0) {
                    fprintf(stderr, "Receive N over TCP failed: %s.\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }
            bytes_tot += bytes_cnt;
        }
        if (tcp_err) continue;
        N = ntohl(((uint32_t *)buff)[0]);

        bytes_tot = 0;
        while(bytes_tot < N)
        {
            if((bytes_cnt = read(connfd, buff, N - bytes_tot)) <= 0) {
                if ((bytes_cnt == 0 && bytes_tot < N) || TCP_ERROR) {
                    fprintf(stderr, "TCP error (data): %s.\nAccepting new connections.", strerror(errno));
                    close(connfd);
                    connfd = -1;
                    tcp_err = 1;
                    break;
                } else if (bytes_cnt < 0) {
                    fprintf(stderr, "Receive data over TCP failed: %s.\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }

            for (i = 0; i < bytes_cnt; i++) {
                if (buff[i] < MIN_PRINTABLE_CHAR || buff[i] > MAX_PRINTABLE_CHAR) continue;
                pcc_counter[buff[i] - MIN_PRINTABLE_CHAR]++;
                C++;
            }
            bytes_tot += bytes_cnt;
        }
        if (tcp_err) continue;

        ((uint32_t *)buff)[0] = htonl(C);
        bytes_tot = 0;
        while (bytes_tot < sizeof(C)) {
            if((bytes_cnt = write(connfd, buff + bytes_tot, sizeof(C) - bytes_tot)) <= 0) {
                if ((bytes_cnt == 0 && bytes_tot < sizeof(C)) || TCP_ERROR) {
                    fprintf(stderr, "TCP error (C): %s.\nAccepting new connections.", strerror(errno));
                    close(connfd);
                    connfd = -1;
                    tcp_err = 1;
                    break;
                } else if (bytes_cnt < 0) {
                    fprintf(stderr, "Send C over TCP failed: %s.\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }
            bytes_tot += bytes_cnt;
        }
        if (tcp_err) continue;

        for (i = 0; i < MAX_PRINTABLE_CHAR - MIN_PRINTABLE_CHAR + 1; i++) {
            pcc_total[i] += pcc_counter[i];
        }
        close(connfd);
        connfd = -1;
    }
    exit_server();
}
