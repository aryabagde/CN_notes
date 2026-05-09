/*
 * gbn_server_commented.c
 * Go-Back-N (GBN) Protocol — Server Side
 *
 * In Go-Back-N, the receiver only accepts packets IN ORDER.
 * If packet 3 arrives before packet 2, packet 3 is discarded and
 * no ACK is sent for it. The server sends a CUMULATIVE ACK for the
 * highest in-order packet it has received so far.
 *
 * Network latency is simulated by sleeping a random number of seconds
 * before sending each ACK.
 */

/* Standard I/O — printf(), perror() */
#include <stdio.h>

/* Standard library — exit(), rand(), srand() */
#include <stdlib.h>

/* Internet address structures */
#include <netinet/in.h>

/* POSIX type definitions */
#include <sys/types.h>

/* Socket API */
#include <sys/socket.h>

/* IP address conversion */
#include <arpa/inet.h>

/* bzero() and similar memory utilities */
#include <strings.h>

/* read(), write(), close(), sleep() */
#include <unistd.h>

/* Port the server listens on */
#define PORTNO 54321

/* Buffer size (defined for consistency, not directly used in GBN logic) */
#define SIZE 256

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTION: create_TCP_listener_socket()
 * Creates a TCP socket, binds it to PORTNO, and starts listening.
 * Returns the listening socket fd.
 * ─────────────────────────────────────────────────────────────────────────────
 */
int create_TCP_listener_socket() {

    /* Create a TCP (SOCK_STREAM) IPv4 (AF_INET) socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /* Address structure to configure the listening socket */
    struct sockaddr_in serv_addr;

    /* Zero out the structure to clear any uninitialized memory */
    bzero((char *) &serv_addr, sizeof(serv_addr));

    /* Use IPv4 */
    serv_addr.sin_family = AF_INET;

    /* Accept connections on all interfaces */
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    /* Set port, converted to network byte order */
    serv_addr.sin_port = htons(PORTNO);

    /*
     * bind() associates the socket with the configured address and port.
     * Fails if the port is already in use.
     */
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        perror("ERROR on binding\n");
        exit(1);
    }

    /*
     * listen() switches the socket to passive mode — ready to accept connections.
     * '5' is the connection backlog queue size.
     */
    if (listen(sockfd, 5) < 0) {
        perror("Listen error");
        exit(1);
    }
    printf("Server started listening on port %d\n", PORTNO);

    return sockfd;
}

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTION: accept_client()
 * Blocks until a client connects. Returns the per-client socket fd.
 * ─────────────────────────────────────────────────────────────────────────────
 */
int accept_client(int sockfd) {

    /* cli_addr will be filled with the connecting client's address info */
    struct sockaddr_in cli_addr;

    /*
     * socklen_t is the correct type for the address length parameter.
     * Must be initialized to sizeof(cli_addr) before calling accept().
     */
    socklen_t cli_addr_len = sizeof(cli_addr);

    /*
     * accept() blocks until a client connects.
     * It creates a NEW socket (accepted_sockfd) dedicated to this client.
     * The original sockfd continues listening for other clients.
     */
    int accepted_sockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &cli_addr_len);
    printf("Connection established to a client\n");

    return accepted_sockfd;
}

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTION: my_write()
 * Sends data with a simulated random network delay.
 *
 * Parameters:
 *   fd        → socket to write to
 *   buf       → data to send
 *   n         → bytes to send
 *   max_delay → maximum random delay in seconds (actual delay = 1 to max_delay)
 * ─────────────────────────────────────────────────────────────────────────────
 */
ssize_t my_write(int fd, void *buf, size_t n, unsigned int max_delay) {

    /*
     * rand() % max_delay gives a random number in [0, max_delay-1].
     * Adding 1 ensures the delay is at least 1 second (never 0).
     * This slows the simulation enough to observe window behavior.
     */
    unsigned int delay = rand() % max_delay;
    delay = delay + 1;

    printf("Network Delay: %u seconds\n", delay);

    /* Pause for 'delay' seconds to simulate network latency */
    sleep(delay);

    /* After the delay, actually transmit the data */
    return write(fd, buf, n);
}

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * MAIN FUNCTION
 * ─────────────────────────────────────────────────────────────────────────────
 */
int main(int argc, char *argv[]) {

    /*
     * Maximum delay in seconds for the ACK simulation.
     * Kept ≤ client timeout to avoid constant client timeouts.
     * Increase this to stress-test retransmissions.
     */
    int max_delay = 5;

    /* Set up the listening socket */
    int socket_fd = create_TCP_listener_socket();

    /* Wait for a client and get the communication socket */
    int accepted_socket_fd = accept_client(socket_fd);

    /*
     * num          → raw bytes read from the socket (network byte order)
     * converted_num → the sequence number after converting to host byte order
     * expected_num  → the next sequence number GBN expects (starts at 1)
     *
     * In GBN, the receiver is STRICT: it only accepts packets in order.
     * If packet 5 arrives before packet 4, packet 5 is discarded.
     */
    uint32_t num, converted_num;
    uint32_t expected_num = 1;

    /* Main receive loop — runs until the client closes the connection */
    while (1) {

        /*
         * read() blocks until the client sends a packet.
         * We read exactly sizeof(uint32_t) = 4 bytes (the sequence number).
         */
        int n = read(accepted_socket_fd, &num, sizeof(uint32_t));

        if (n < 0){
            perror("ERROR in reading from socket");
            exit(1);
        }

        if (n == 0) {
            /*
             * The client closed the connection (sent TCP FIN).
             * n == 0 means end-of-stream.
             */
            printf("Client closed connection.\n");
            break;
        }

        /*
         * Convert from network byte order (big-endian) to host byte order.
         * This gives us the actual sequence number as a normal integer.
         */
        converted_num = ntohl(num);

        /*
         * ── GBN RECEIVER LOGIC ──
         * Only accept the packet if its sequence number matches what we expect.
         */
        if (converted_num == expected_num) {
            /*
             * Packet arrived in order! Accept it and advance the expected number.
             */
            printf("Received IN-ORDER packet %u\n", converted_num);
            expected_num++; /* Next time, we expect the following sequence number */
        } else {
            /*
             * Out-of-order packet — discard it (don't buffer it).
             * GBN does NOT buffer out-of-order packets. The sender will need to
             * retransmit from the missing packet onward (the "go back N" part).
             */
            printf("Received OUT-OF-ORDER packet %u (Expected %u). Discarding.\n",
                   converted_num, expected_num);
        }

        /*
         * ── CUMULATIVE ACK ──
         * Regardless of whether the packet was in-order or not, the server
         * sends a cumulative ACK. The ACK number is (expected_num - 1), meaning:
         * "I have correctly received all packets UP TO this number."
         *
         * If expected_num is still 1 (no packet received yet), ack_to_send = 0,
         * and we skip sending an ACK (ack_to_send > 0 guard below).
         */
        uint32_t ack_to_send = expected_num - 1;
        if (ack_to_send > 0) {
            /*
             * Convert ACK number to network byte order before sending.
             */
            uint32_t ack_converted = htonl(ack_to_send);

            /*
             * my_write() sends the ACK after a random delay.
             * This simulates variable network latency for ACKs.
             */
            n = my_write(accepted_socket_fd, &ack_converted, sizeof(uint32_t), max_delay);
            printf("ACK %u sent\n", ack_to_send);
            if (n < 0){
                perror("ERROR in writing to socket");
                exit(1);
            }
        }
    }

    /* Close the client socket and the listening socket */
    close(accepted_socket_fd);
    close(socket_fd);

    return 0;
}
