#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#define VALID_NUM_ARGS 4
#define BUFF_SIZE 1048576   // 2^20B = 1 MB

int main(int argc, char *argv[])
{
    in_port_t server_port;
    in_addr_t server_ip;
    uint32_t C;
    int sockfd;
    ssize_t bytes_cnt, bytes_tot, bytes_read_from_file, bytes_sent;
    __off_t N;

    if (argc != VALID_NUM_ARGS) {
        fprintf(stderr, "The program should get %u arguments: %s.\n", VALID_NUM_ARGS - 1, strerror(EINVAL));
        exit(EXIT_FAILURE);
    }
    server_port = (in_port_t) atoi(argv[2]);
    if (inet_pton(AF_INET, argv[1], &server_ip) != 1) {
        fprintf(stderr, "Invalid IP address: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int fd = open(argv[3], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening file %s: %s.\n", argv[3], strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct stat st;
    fstat(fd, &st);
    N = st.st_size;
    char buff[BUFF_SIZE];
    memset(buff, 0 ,sizeof(buff));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port); // Note: htons for endiannes
    serv_addr.sin_addr.s_addr = server_ip;
    socklen_t addrsize = sizeof(struct sockaddr_in );

    if ((sockfd = socket( AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Create socket Failed: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    printf("Client: connecting...\n");

    if(connect(sockfd,(struct sockaddr*) &serv_addr,addrsize) < 0) {
        fprintf(stderr, "Connect failed: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ((uint32_t *)buff)[0] = htonl(N);
    bytes_tot = 0;
    while (bytes_tot < sizeof(N)) {
        if((bytes_cnt = write(sockfd, buff + bytes_tot, sizeof(N) - bytes_tot)) < 0) {
            fprintf(stderr, "Send N failed: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        bytes_tot += bytes_cnt;
    }

    bytes_tot = bytes_read_from_file = 0;
    while(bytes_tot < N)
    {
        if (bytes_read_from_file == 0) {
            if ((bytes_read_from_file = read(fd, buff, BUFF_SIZE)) < 0) {
                fprintf(stderr, "Read from file %s failed: %s.\n", argv[3], strerror(errno));
                exit(EXIT_FAILURE);
            }
            bytes_sent = 0;
        }
        if((bytes_cnt = write(sockfd, buff + bytes_sent, bytes_read_from_file)) < 0) {
            fprintf(stderr, "Send bytes over TCP failed: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        bytes_read_from_file -= bytes_cnt;
        bytes_sent += bytes_cnt;
        bytes_tot += bytes_cnt;
    }
    close(fd);

    bytes_tot = 0;
    while (bytes_tot < sizeof(C)) {
        if((bytes_cnt = read(sockfd, buff + bytes_tot, sizeof(C) - bytes_tot)) < 0) {
            fprintf(stderr, "Receive bytes over TCP failed: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        bytes_tot += bytes_cnt;
    }
    close(sockfd);

    C = ntohl(((uint32_t *)buff)[0]);
    printf("# of printable characters: %u\n", C);
    return 0;
}
