/*
 * sw_client_commented.c
 * Stop-and-Wait Protocol — Client Side
 *
 * In Stop-and-Wait, the sender transmits one numbered packet, then waits for
 * an ACK before sending the next. If a timeout occurs (no ACK received within
 * the time limit), the same packet is retransmitted. This continues until all
 * 10 packets are acknowledged.
 */

/* Standard I/O */
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

/* read(), write(), close() */
#include <unistd.h>

/* time() — used to seed the random number generator */
#include <time.h>

/* select() — used to implement the read-with-timeout feature */
#include <sys/select.h>

/* Server's IP address — loopback means "same machine" */
#define IP "127.0.0.1"

/* Must match the port the server is listening on */
#define PORTNO 54321

/* Buffer size (not directly used here, defined for consistency) */
#define SIZE 256

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTION: connect_to_server()
 * Creates a socket and connects to the server.
 * Returns the connected socket file descriptor.
 * ─────────────────────────────────────────────────────────────────────────────
 */
int connect_to_server() {

    /* Create a TCP socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /* Address structure for the server */
    struct sockaddr_in serv_addr;

    /* Clear all fields of the structure */
    bzero((char *) &serv_addr, sizeof(serv_addr));

    /* IPv4 addressing */
    serv_addr.sin_family = AF_INET;

    /* Convert the server's IP string to binary form */
    serv_addr.sin_addr.s_addr = inet_addr(IP);

    /* Set the server port in network byte order */
    serv_addr.sin_port = htons(PORTNO);

    /*
     * connect() performs the TCP three-way handshake with the server.
     * It BLOCKS until the connection succeeds or fails.
     * Returns 0 on success, -1 on failure.
     */
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR while connecting");
        exit(1);
    }

    return sockfd;
}

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTION: my_read()
 * A wrapper around read() that adds a TIMEOUT feature.
 * Uses select() to watch the socket for incoming data.
 * If data arrives within 'timeout' seconds, it reads and returns it.
 * If timeout expires before data arrives, it returns 0 (like a timeout signal).
 *
 * Parameters:
 *   fd      → socket to watch
 *   buf     → buffer to store received data
 *   n       → max bytes to read
 *   timeout → seconds to wait before giving up (0 = check once and return)
 *
 * Returns:
 *   > 0 → bytes read (data received)
 *   = 0 → timeout (no data arrived in time)
 *   < 0 → error
 * ─────────────────────────────────────────────────────────────────────────────
 */
ssize_t my_read(int fd, void *buf, size_t n, time_t timeout) {

    /*
     * fd_set is a set of file descriptors that select() will monitor.
     * We'll add our socket to this set so select() watches it for readability.
     */
    fd_set read_fds;

    /* FD_ZERO() clears all file descriptors from the set */
    FD_ZERO(&read_fds);

    /* FD_SET() adds our socket file descriptor 'fd' to the watch set */
    FD_SET(fd, &read_fds);

    /*
     * 'struct timeval' represents a time duration with seconds and microseconds.
     * This defines how long select() will wait before declaring a timeout.
     */
    struct timeval timer;
    timer.tv_sec = timeout;  /* Full seconds to wait */
    timer.tv_usec = 0;       /* Additional microseconds (0 here) */

    /*
     * select() monitors the file descriptors in 'read_fds' and waits up to
     * 'timer' duration for any of them to become readable (i.e., data arrives).
     *
     * Arguments:
     *   fd+1       → one more than the highest fd in the set (required by select)
     *   &read_fds  → set of fds to watch for readability
     *   NULL       → don't watch any fds for writability
     *   NULL       → don't watch any fds for exceptions
     *   &timer     → timeout duration; modified by select() to reflect remaining time
     *
     * Returns:
     *   > 0  → at least one fd is ready (data available on our socket)
     *   = 0  → timeout expired, no data arrived
     *   < 0  → error
     */
    int retval = select(fd+1, &read_fds, NULL, NULL, &timer);

    if (retval < 1) {
        /*
         * Either timeout (retval == 0) or error (retval == -1).
         * Return this value directly so the caller can distinguish.
         */
        return retval;
    } else {
        /*
         * Data is available on 'fd'. Now actually read it.
         * FD_ISSET(fd, &read_fds) would be true here (data is ready).
         */
        return read(fd, buf, n);
    }
}

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * MAIN FUNCTION
 * ─────────────────────────────────────────────────────────────────────────────
 */
int main(int argc, char *argv[]) {

    /*
     * Seed the random number generator with the current time.
     * Although rand() isn't used in the client here, it's good practice
     * when the program may use randomness later.
     */
    srand( time(NULL) );

    /*
     * How long (in seconds) to wait for an ACK from the server.
     * If the server doesn't respond in this time, we assume the packet or
     * ACK was lost and retransmit the same packet.
     * Try a smaller value like 2 to see more frequent retransmissions.
     */
    int timeout = 10;

    /* Connect to the server and get the socket fd */
    int connected_socket_fd = connect_to_server();

    /*
     * 'num' will hold the packet sequence number in network byte order.
     * uint32_t is a 32-bit unsigned integer — ensures consistent size across platforms.
     */
    uint32_t num;

    /*
     * 'i' is the current packet sequence number we're trying to send.
     * We only advance 'i' after receiving a successful ACK.
     * We send 10 packets total (i = 1 to 10).
     */
    int i = 1;

    while (i <= 10) {

        printf("sending packet %u\n", i);

        /*
         * htonl() = "Host TO Network Long" — converts the 32-bit integer 'i'
         * from host byte order to network byte order (big-endian).
         * This is mandatory before sending integers over the network.
         */
        num = htonl(i);

        /*
         * write() sends 4 bytes (the sequence number) to the server.
         * sizeof(uint32_t) = 4, so we always send exactly 4 bytes.
         */
        int n = write(connected_socket_fd, &num, sizeof(uint32_t));
        if (n < 0){
            perror("ERROR while writing to socket");
            exit(1);
        }

        /*
         * my_read() waits up to 'timeout' seconds for the server's ACK.
         * If the server is slow (due to its simulated delay), this may timeout.
         * Returns:
         *   = 0 → timeout (no ACK received in time)
         *   > 0 → ACK received successfully
         *   < 0 → error
         */
        n = my_read(connected_socket_fd, &num, sizeof(uint32_t), timeout);

        if (n == 0) {
            /*
             * TIMEOUT CASE: No ACK arrived within the timeout window.
             * The packet or ACK may have been delayed or lost.
             * In Stop-and-Wait, we simply retransmit the same packet.
             * We do NOT increment 'i' — continue will restart the while loop
             * and resend packet 'i' again.
             */
            printf("timeout occurred, resending\n");
            continue;
        }

        if (n < 0){
            /* An actual socket error — not a timeout */
            perror("ERROR while reading from socket");
            exit(1);
        }

        /*
         * ACK received!
         * ntohl() converts the ACK number back from network byte order to host order.
         * We print it to confirm which packet was acknowledged.
         */
        printf("ACK received for packet %u \n", ntohl(num));

        /*
         * Only now do we advance to the next packet.
         * This is the defining characteristic of Stop-and-Wait:
         * one packet at a time, no new send until current one is ACK'd.
         */
        i++;
    }

    /* All 10 packets sent and acknowledged — close the connection */
    close(connected_socket_fd);

    return 0;
}
