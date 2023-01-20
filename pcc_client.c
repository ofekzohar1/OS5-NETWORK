#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>

//************************* Const Macros *************************************

#define VALID_NUM_ARGS 4
#define BUFF_SIZE 1048576   // 2^20B = 1 MB
#define FAILURE (-1)
#define SUCCESS 0

//************************* Helper Functions *****************************

// Parse user input into server IP/port
// return 0 on success, -1 on failure
int parse_user_input(int argc, char *argv[], in_addr_t *server_ip, in_port_t *server_port) {
    if (argc != VALID_NUM_ARGS) {
        fprintf(stderr, "The program should get %u arguments: %s.\n", VALID_NUM_ARGS - 1, strerror(EINVAL));
        return FAILURE;
    }
    *server_port = (in_port_t) atoi(argv[2]);
    if (inet_pton(AF_INET, argv[1], server_ip) != 1) {
        fprintf(stderr, "Invalid IP address: %s.\n", strerror(errno));
        return FAILURE;
    }
    return SUCCESS;
}

// Open file and determine its size
// return file fd on success, -1 on failure
int open_and_get_file_size(char *filepath, uint32_t *file_size) {
    int filefd = open(filepath, O_RDONLY); // Open for read only
    if (filefd < 0) {
        fprintf(stderr, "Error opening file %s: %s.\n", filepath, strerror(errno));
        return FAILURE;
    }

    struct stat st;
    if (fstat(filefd, &st) != 0) { // Get file's stat
        fprintf(stderr, "Error get file size (%s): %s.\n", filepath, strerror(errno));
        close(filefd);
        return FAILURE;
    }
    *file_size = st.st_size;

    return filefd;
}

// Connect to the server
// return new socket fd on success, -1 on failure
int connect_to_server(in_addr_t server_ip, in_port_t server_port) {
    int sockfd;
    // Server Attributes. Using htonl/s to get network byte order
    struct sockaddr_in serv_addr;
    socklen_t addrsize = sizeof(struct sockaddr_in);
    memset(&serv_addr, 0, addrsize);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr.s_addr = server_ip;

    if ((sockfd = socket( AF_INET, SOCK_STREAM, 0)) < 0) { // New IPv4/TCP socket
        fprintf(stderr, "Create socket Failed: %s.\n", strerror(errno));
        return FAILURE;
    }

    if(connect(sockfd,(struct sockaddr*) &serv_addr,addrsize) < 0) { // Connect socket to server
        fprintf(stderr, "Connect failed: %s.\n", strerror(errno));
        return FAILURE;
    }

    return sockfd;
}

// Send file data to the sever using TCP
// C: # of printable chars sent to the server, calculate at the server side
// return C on success, -1 on failure
ssize_t send_file_to_server(int sockfd, int filefd, uint32_t N, int C_size) {
    ssize_t bytes_cnt, bytes_tot, bytes_read_from_file, bytes_sent;
    char buff[BUFF_SIZE];
    memset(buff, 0 ,sizeof(buff));

    ((uint32_t *)buff)[0] = htonl(N);

    bytes_tot = 0; // Count the total bytes sent
    while (bytes_tot < sizeof(N)) { // Send N to the server - # of bytes to be sent
        if((bytes_cnt = write(sockfd, buff + bytes_tot, sizeof(N) - bytes_tot)) < 0) {
            fprintf(stderr, "Send N failed: %s.\n", strerror(errno));
            return FAILURE;
        }
        bytes_tot += bytes_cnt;
    }

    bytes_tot = bytes_read_from_file = 0;
    while(bytes_tot < N) { // Send N bytes to the server
        if (bytes_read_from_file == 0) { // Read from file to buffer
            if ((bytes_read_from_file = read(filefd, buff, BUFF_SIZE)) < 0) {
                fprintf(stderr, "Read from file failed: %s.\n", strerror(errno));
                return FAILURE;
            }
            bytes_sent = 0;
        }
        // Send buffer to the server
        if((bytes_cnt = write(sockfd, buff + bytes_sent, bytes_read_from_file)) < 0) {
            fprintf(stderr, "Send bytes over TCP failed: %s.\n", strerror(errno));
            return FAILURE;
        }
        bytes_read_from_file -= bytes_cnt;
        bytes_sent += bytes_cnt;
        bytes_tot += bytes_cnt;
    }

    bytes_tot = 0;
    while (bytes_tot < C_size) { // Read C from the server - # of printable chars sent
        if((bytes_cnt = read(sockfd, buff + bytes_tot, C_size - bytes_tot)) < 0) {
            fprintf(stderr, "Receive bytes over TCP failed: %s.\n", strerror(errno));
            return FAILURE;
        }
        bytes_tot += bytes_cnt;
    }

    return ntohl(((uint32_t *)buff)[0]); // Return C in host byte order
}

//********************************* Main *************************************

int main(int argc, char *argv[])
{
    in_port_t server_port; // Server port to be used
    in_addr_t server_ip; // Server IP to be used
    uint32_t C, N;
    int sockfd, filefd;

    if (parse_user_input(argc, argv, &server_ip, &server_port) == FAILURE) // Parse user input
        exit(EXIT_FAILURE);

    filefd = open_and_get_file_size(argv[3], &N); // Open file nad get its size
    if (filefd == FAILURE) exit(EXIT_FAILURE);

    sockfd = connect_to_server(server_ip, server_port); // Open TCP connection to server
    if(sockfd == FAILURE) exit(EXIT_FAILURE);

    C = send_file_to_server(sockfd, filefd, N, sizeof(C)); // Send file to server
    close(filefd);
    close(sockfd);
    if (C == FAILURE) exit(EXIT_FAILURE);

    printf("# of printable characters: %u\n", C);
    return EXIT_SUCCESS;
}
