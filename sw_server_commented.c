/*
 * sw_server_commented.c
 * Stop-and-Wait Protocol — Server Side
 *
 * In Stop-and-Wait, the sender sends ONE packet and waits for an ACK before
 * sending the next. This server receives numbered packets, and sends back
 * an ACK for each one after a random delay (simulating network latency).
 * If a packet arrives out of sequence, it's silently dropped (no ACK sent).
 */

/* Standard I/O — printf(), perror() */
#include <stdio.h>

/* Standard library — exit(), rand(), srand() */
#include <stdlib.h>

/* Internet address structures */
#include <netinet/in.h>

/* POSIX type definitions */
#include <sys/types.h>

/* Socket API — socket(), bind(), listen(), accept(), read(), write(), close() */
#include <sys/socket.h>

/* IP address conversion — inet_addr(), inet_ntoa() */
#include <arpa/inet.h>

/* bzero(), bcmp() etc. — NOTE: <strings.h> (with an 's') vs <string.h> */
#include <strings.h>

/* read(), write(), close(), sleep() */
#include <unistd.h>

/* Port on which this server listens */
#define PORTNO 54321

/* Buffer size (not actively used here but defined for consistency) */
#define SIZE 256

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTION: create_TCP_listener_socket()
 * Creates, binds, and starts listening on a TCP socket.
 * Returns the listening socket file descriptor.
 * ─────────────────────────────────────────────────────────────────────────────
 */
int create_TCP_listener_socket() {

    /* Create a TCP socket. Returns a file descriptor. */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /* Address structure to configure the server's address */
    struct sockaddr_in serv_addr;

    /* Zero out the structure to clear any garbage values */
    bzero((char *) &serv_addr, sizeof(serv_addr));

    /* IPv4 */
    serv_addr.sin_family = AF_INET;

    /* Accept connections on ANY network interface (WiFi, Ethernet, loopback...) */
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    /* Set port, converting to network byte order */
    serv_addr.sin_port = htons(PORTNO);

    /*
     * bind() attaches the socket to the address/port configured above.
     * Without binding, the OS doesn't know which port to associate with this socket.
     */
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        perror("ERROR on binding\n");
        exit(1);
    }

    /*
     * listen() marks the socket as a passive socket — ready to accept connections.
     * The '5' is the backlog: how many pending connections can queue up.
     */
    if (listen(sockfd, 5) < 0) {
        perror("Listen error");
        exit(1);
    }
    printf("server started listening in port %d\n", PORTNO);

    /* Return the listening socket descriptor for use in main() */
    return sockfd;
}

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTION: accept_client()
 * Blocks until a client connects, then returns the connected socket fd.
 * ─────────────────────────────────────────────────────────────────────────────
 */
int accept_client(int sockfd) {

    /*
     * cli_addr will be filled by accept() with the client's IP and port.
     * cli_addr_len must be pre-set to sizeof(cli_addr).
     */
    struct sockaddr_in cli_addr;
    socklen_t cli_addr_len = sizeof(cli_addr);

    /*
     * accept() BLOCKS until a client connects.
     * It then creates a new socket specifically for communication with that client.
     * The original 'sockfd' continues listening for future clients.
     */
    int accepted_sockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &cli_addr_len);
    printf("connection established to a client\n");

    return accepted_sockfd;
}

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTION: my_write()
 * Sends data after sleeping for a random delay to simulate network latency.
 *
 * Parameters:
 *   fd        → socket file descriptor to write to
 *   buf       → pointer to the data to send
 *   n         → number of bytes to send
 *   max_delay → upper bound for random delay in seconds
 * ─────────────────────────────────────────────────────────────────────────────
 */
ssize_t my_write(int fd, void *buf, size_t n, unsigned int max_delay) {

    /*
     * rand() % max_delay produces a random number in [0, max_delay-1].
     * This simulates unpredictable network delay — sometimes fast, sometimes slow.
     * If delay is 0 here, the ACK goes out immediately (no sleep).
     */
    unsigned int delay = rand() % max_delay;

    printf("waiting for %u seconds\n", delay);

    /*
     * sleep(delay) pauses the program for 'delay' seconds.
     * This is what makes the simulation interesting — the client's timeout
     * may fire if this delay is too long.
     */
    sleep(delay);
    printf("now sending\n");

    /*
     * After the simulated delay, actually send the data.
     * write() returns the number of bytes written, or -1 on error.
     */
    return write(fd, buf, n);
}

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * MAIN FUNCTION
 * ─────────────────────────────────────────────────────────────────────────────
 */
int main(int argc, char *argv[]) {

    /*
     * Maximum delay in seconds for ACK transmission simulation.
     * If this is larger than the client's timeout, it will cause retransmissions.
     */
    int max_delay = 5;

    /* Create the server socket and start listening */
    int socket_fd = create_TCP_listener_socket();

    /* Block until a client connects, get the per-client communication socket */
    int accepted_socket_fd = accept_client(socket_fd);

    /*
     * num         → raw bytes received from the client (network byte order)
     * converted_num → packet sequence number after converting to host byte order
     * last_num    → the sequence number of the last correctly received packet
     *               (starts at 0, so we expect packet 1 first)
     */
    uint32_t num, converted_num, last_num = 0;

    /* Keep receiving packets until the client disconnects */
    while (1) {

        /*
         * read() blocks waiting for the client to send a packet.
         * sizeof(uint32_t) = 4 bytes — we're reading a 32-bit integer (the seq number).
         * Returns > 0 on success, 0 if client closed, -1 on error.
         */
        int n = read(accepted_socket_fd, &num, sizeof(uint32_t));

        if (n < 0){
            perror("ERROR in reading from socket");
            exit(1);
        }

        if (n == 0) {
            /*
             * Client closed the connection — end of stream.
             * In TCP, reading 0 bytes means the other side sent FIN.
             */
            printf("client closed\n");
            break;
        }

        /*
         * ntohl() = "Network TO Host Long" — converts the 32-bit integer
         * from network byte order (big-endian) to the host's byte order.
         * We must do this before comparing or printing the number.
         */
        converted_num = ntohl(num);

        /*
         * STOP-AND-WAIT SEQUENCE CHECK:
         * We expect packets in order: 1, 2, 3, 4, ...
         * last_num + 1 is the next expected sequence number.
         * If the received packet is NOT the next expected one, discard it silently.
         * (In a real network, a NAK or no-ACK tells the sender to retry.)
         */
        if(last_num + 1 != converted_num) {
            /*
             * Out-of-order packet. In stop-and-wait this is unusual since only
             * one packet is in flight at a time, but could happen if the client
             * retransmits and the original arrives late.
             * We just skip it — no ACK is sent, so the client will timeout and retry.
             */
            continue;
        }

        printf("received packet %u\n", converted_num);

        /* Update last_num to reflect the correctly received packet */
        last_num = converted_num;

        /*
         * Wait 1 second before sending the ACK.
         * This models the server "processing" the packet.
         * On top of this, my_write() will add a random delay too.
         */
        sleep(1);

        /*
         * Send ACK back to client.
         * We echo 'num' (still in network byte order — no need to convert back).
         * The client will use ntohl() on its end to read the ACK number.
         * my_write() introduces an additional random delay before actually sending.
         */
        n = my_write(accepted_socket_fd, &num, sizeof(uint32_t), max_delay);
        printf("ACK sent\n");
        if (n < 0){
            perror("ERROR in writing to socket");
            exit(1);
        }
    }

    /* Clean up both the per-client socket and the listening socket */
    close(accepted_socket_fd);
    close(socket_fd);

    return 0;
}
