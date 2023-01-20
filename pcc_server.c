#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>

//************************* Const Macros *************************************

#define VALID_NUM_ARGS 2
#define MIN_PRINTABLE_CHAR 32
#define MAX_PRINTABLE_CHAR 126
#define LISTEN_QUEUE_SIZE 10
#define BUFF_SIZE 1048576   // 2^20B = 1 MB
#define TCP_ERROR (errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE)
#define FAILURE (-1)
#define SUCCESS 0
#define CONTINUE (-2)

static uint32_t pcc_total[MAX_PRINTABLE_CHAR - MIN_PRINTABLE_CHAR + 1]; // Count all the printable chars the server processed
static int connfd, sigint_sent; // connfd: connection socket, -1 means no connection, sigint_sent: is SIGINT sent to the process

//************************* SIGINT & Server Exit *****************************

// Safe exit the server process, printing all printable chars the server processed
void exit_server() {
    for (int i = 0; i < MAX_PRINTABLE_CHAR - MIN_PRINTABLE_CHAR + 1; i++) {
        printf("char '%c' : %u times\n", i + MIN_PRINTABLE_CHAR, pcc_total[i]);
    }
    exit(EXIT_SUCCESS);
}

// SIGINT costume handler
void sigint_handler() {
    if (connfd < 0) { // If no active connection, exit the server
        exit_server();
    } else {
        sigint_sent = 1; // Active connection, finish processing that connection
    }
}

const struct sigaction sigint = {.sa_handler = &sigint_handler, .sa_flags = SA_RESTART};

//************************* Helper Functions *****************************

// Parse user input into server_port
// return 0 on success, -1 on failure
int parse_user_input(int argc, char *argv[], in_port_t *server_port) {
    if (argc != VALID_NUM_ARGS) {
        fprintf(stderr, "The program should get %u arguments: %s.\n", VALID_NUM_ARGS - 1, strerror(EINVAL));
        return FAILURE;
    }
    *server_port = (in_port_t) atoi(argv[1]);
    return SUCCESS;
}

// Open new listen socket with queue size == LISTEN_QUEUE_SIZE
// return new listen socket fd on success, -1 on failure
int open_listen_socket(in_port_t server_port) {
    int listenfd;

    if ((listenfd = socket( AF_INET, SOCK_STREAM, 0)) < 0) { // New IPv4/TCP socket
        fprintf(stderr, "Socket Failed: %s.\n", strerror(errno));
        return FAILURE;
    }
    // Set listen socket to be 'SO_REUSEADDR', so the address could be reuse
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0) {
        fprintf(stderr, "Set socket option Failed: %s.\n", strerror(errno));
        return FAILURE;
    }

    // Server Attributes. Using htonl/s to get network byte order
    struct sockaddr_in serv_addr;
    socklen_t addrsize = sizeof(struct sockaddr_in);
    memset(&serv_addr, 0, addrsize);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY = any local machine address
    serv_addr.sin_port = htons(server_port);

    if(bind(listenfd,(struct sockaddr*) &serv_addr,addrsize) != 0) // Bind socket to port
    {
        fprintf(stderr, "Bind Failed: %s.\n", strerror(errno));
        return FAILURE;
    }
    if(listen(listenfd, LISTEN_QUEUE_SIZE) != 0) // Start listen on server port
    {
        fprintf(stderr,"Listen Failed: %s.\n", strerror(errno));
        return FAILURE;
    }

    return listenfd;
}

// Accept and process new connections
// return 0 on success, -1 on failure other than TCP, -2 on TCP errors
int accept_new_connection(int listenfd, char *buff, uint32_t *pcc_counter) {
    ssize_t bytes_cnt, bytes_tot, i;
    uint32_t N, C = 0;

    if ((connfd = accept(listenfd, NULL, NULL)) < 0) { // Wait for new connection
        fprintf(stderr, "Accept Failed: %s.\n", strerror(errno));
        return FAILURE;
    }

    bytes_tot = 0; // Count the total bytes sent
    while (bytes_tot < sizeof(N)) { // Read N from the client - # of bytes to be sent
        if ((bytes_cnt = read(connfd, buff + bytes_tot, sizeof(N) - bytes_tot)) <= 0) {
            if ((bytes_cnt == 0 && bytes_tot < sizeof(N)) || TCP_ERROR) { // 0 means client stopped unexpectedly, TCP errors
                fprintf(stderr, "TCP error (N): %s.\nAccepting new connections.\n", strerror(errno));
                return CONTINUE;
            } else if (bytes_cnt < 0) { // Any other error
                fprintf(stderr, "Receive N over TCP failed: %s.\n", strerror(errno));
                return FAILURE;
            }
        }
        bytes_tot += bytes_cnt;
    }
    N = ntohl(((uint32_t *) buff)[0]); // Parse N from the buffer in host byte order

    bytes_tot = 0;
    while (bytes_tot < N) { // Read N bytes from the client
        if ((bytes_cnt = read(connfd, buff, BUFF_SIZE)) <= 0) {
            if ((bytes_cnt == 0 && bytes_tot < N) || TCP_ERROR) {
                fprintf(stderr, "TCP error (data): %s.\nAccepting new connections.\n", strerror(errno));
                return CONTINUE;
            } else if (bytes_cnt < 0) {
                fprintf(stderr, "Receive data over TCP failed: %s.\n", strerror(errno));
                return FAILURE;
            }
        }

        for (i = 0; i < bytes_cnt; i++) { // Count the printable chars sent
            if (buff[i] < MIN_PRINTABLE_CHAR || buff[i] > MAX_PRINTABLE_CHAR) continue;
            pcc_counter[buff[i] - MIN_PRINTABLE_CHAR]++; // Count per char
            C++; // Count all printable chars
        }
        bytes_tot += bytes_cnt;
    }

    ((uint32_t *) buff)[0] = htonl(C); // Put C (# of printable chars) in buffer (network byte order)
    bytes_tot = 0;
    while (bytes_tot < sizeof(C)) { // Send C to the client
        if ((bytes_cnt = write(connfd, buff + bytes_tot, sizeof(C) - bytes_tot)) <= 0) {
            if ((bytes_cnt == 0 && bytes_tot < sizeof(C)) || TCP_ERROR) {
                fprintf(stderr, "TCP error (C): %s.\nAccepting new connections.", strerror(errno));
                return CONTINUE;
            } else if (bytes_cnt < 0) {
                fprintf(stderr, "Send C over TCP failed: %s.\n", strerror(errno));
                return FAILURE;
            }
        }
        bytes_tot += bytes_cnt;
    }

    return SUCCESS;
}

//********************************* Main *************************************

int main(int argc, char *argv[])
{
    in_port_t server_port; // Server port to be used
    int ret, listenfd;
    connfd = -1, sigint_sent = 0;

    if (parse_user_input(argc, argv, &server_port) == FAILURE) // Parse user input
        exit(EXIT_FAILURE);

    if (sigaction(SIGINT, &sigint, 0) != 0) { // Set SIGINT new handler
        fprintf(stderr, "Error setting SIGINT handler: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    listenfd = open_listen_socket(server_port); // Open listen socket
    if (listenfd == FAILURE) exit(EXIT_FAILURE);

    char buff[BUFF_SIZE];
    uint32_t pcc_counter[MAX_PRINTABLE_CHAR - MIN_PRINTABLE_CHAR + 1]; // Local counter
    while (!sigint_sent) { // Accept connections to the server until SIGINT
        connfd = -1; // No active connection
        memset(&pcc_counter, 0, sizeof(pcc_counter)); // Clear the counter
        ret = accept_new_connection(listenfd, buff, pcc_counter);
        close(connfd);

        if (ret == FAILURE) exit(EXIT_FAILURE); // Unexpected error
        if (ret == CONTINUE) continue; // TCP error, no need to update pcc_total

        // Update the global counter according to the local one
        for (int i = 0; i < MAX_PRINTABLE_CHAR - MIN_PRINTABLE_CHAR + 1; i++) {
            pcc_total[i] += pcc_counter[i];
        }
    }
    exit_server();
}
