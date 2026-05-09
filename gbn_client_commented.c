/*
 * gbn_client_commented.c
 * Go-Back-N (GBN) Protocol — Client Side
 *
 * In Go-Back-N, the sender maintains a SLIDING WINDOW of unacknowledged packets.
 * It can send up to 'window_size' packets without waiting for ACKs.
 * If a timeout occurs (no ACK in time), ALL unacknowledged packets are retransmitted
 * starting from 'base' (the oldest unACKed packet). This is the "go back N" part.
 *
 * Key concepts:
 *   base         → sequence number of the oldest unACKed packet (window start)
 *   next_seq_num → next packet to send (window end + 1)
 *   window_size  → max number of unACKed packets allowed in flight at once
 *
 * ACKs are CUMULATIVE: ACK(n) means "all packets up to n received correctly."
 */

/* Standard I/O */
#include <stdio.h>

/* Standard library — exit(), srand() */
#include <stdlib.h>

/* Internet address structures */
#include <netinet/in.h>

/* POSIX type definitions */
#include <sys/types.h>

/* Socket API */
#include <sys/socket.h>

/* IP address conversion */
#include <arpa/inet.h>

/* bzero() */
#include <strings.h>

/* read(), write(), close() */
#include <unistd.h>

/* time() — used to seed the random number generator */
#include <time.h>

/* select() — used for implementing timeout on reads */
#include <sys/select.h>

/* Server's IP address */
#define IP "127.0.0.1"

/* Must match the server's listening port */
#define PORTNO 54321

/* Buffer size */
#define SIZE 256

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTION: connect_to_server()
 * Creates a socket and establishes a TCP connection to the server.
 * Returns the connected socket file descriptor.
 * ─────────────────────────────────────────────────────────────────────────────
 */
int connect_to_server() {

    /* Create a TCP socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /* Fill in the server's address info */
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(IP);   /* Convert "127.0.0.1" to binary */
    serv_addr.sin_port = htons(PORTNO);           /* Port in network byte order */

    /*
     * connect() performs the TCP handshake.
     * Blocks until connected or fails.
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
 * A read() wrapper with a configurable timeout using select().
 *
 * Parameters:
 *   fd      → socket to watch
 *   buf     → buffer to read into
 *   n       → max bytes to read
 *   timeout → seconds to wait (0 = non-blocking poll — check once and return)
 *
 * Returns:
 *   > 0 → bytes read (data arrived in time)
 *   = 0 → timeout expired (no data arrived)
 *   < 0 → error
 * ─────────────────────────────────────────────────────────────────────────────
 */
ssize_t my_read(int fd, void *buf, size_t n, time_t timeout) {

    /*
     * fd_set is a bitmask of file descriptors for select() to monitor.
     * We add our socket to it so select() watches it for incoming data.
     */
    fd_set read_fds;
    FD_ZERO(&read_fds);    /* Clear all bits in the set */
    FD_SET(fd, &read_fds); /* Set the bit for our socket fd */

    /*
     * struct timeval holds the timeout duration.
     * tv_sec  = whole seconds
     * tv_usec = microseconds (millionths of a second)
     */
    struct timeval timer;
    timer.tv_sec = timeout;
    timer.tv_usec = 0;

    /*
     * select() watches read_fds and blocks until:
     *   - One of the fds has data ready to read, OR
     *   - The timer expires.
     *
     * When timeout == 0, it checks immediately and returns without blocking.
     * This is used as a "poll" — to drain extra ACKs from the buffer quickly.
     */
    int retval = select(fd+1, &read_fds, NULL, NULL, &timer);

    if (retval < 1) {
        return retval; /* 0 = timeout, -1 = error */
    } else {
        return read(fd, buf, n); /* Data is ready, read it now */
    }
}

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * MAIN FUNCTION
 * ─────────────────────────────────────────────────────────────────────────────
 */
int main(int argc, char *argv[]) {

    /* Seed random number generator (not used in this file, but good practice) */
    srand( time(NULL) );

    /*
     * Timeout in seconds: how long to wait for any ACK after sending a window.
     * If no ACK arrives in this time, assume loss and retransmit from 'base'.
     */
    int timeout = 5;

    /*
     * GBN WINDOW PARAMETERS:
     *
     * window_size    → max number of packets that can be "in flight" (sent but not yet ACKed)
     *                  At any moment, next_seq_num - base <= window_size
     *
     * total_packets  → total number of packets to successfully deliver (1 through 10)
     *
     * total_tries    → tracks total transmission attempts (including retransmissions)
     *
     * base           → the OLDEST unacknowledged packet's sequence number.
     *                  When ACK(n) is received, base slides to n+1.
     *
     * next_seq_num   → the sequence number of the NEXT packet to send.
     *                  Advances as new packets are sent (within the window).
     */
    int window_size = 4;
    int total_packets = 10;
    int total_tries = 0;
    int base = 1;
    int next_seq_num = 1;

    /* Connect to the server */
    int connected_socket_fd = connect_to_server();

    /* 'num' holds the sequence/ACK number in network byte order for send/receive */
    uint32_t num;

    /*
     * Main GBN loop: keeps running until all packets are ACKed.
     * Loop exits when base > total_packets, meaning every packet was acknowledged.
     */
    while (base <= total_packets) {

        /*
         * ── STEP 1: FILL THE WINDOW ──
         * Send packets as long as:
         *   a) next_seq_num is within the window: next_seq_num < base + window_size
         *   b) We haven't sent all packets yet: next_seq_num <= total_packets
         *
         * The window at any time is [base, base + window_size - 1].
         * 'next_seq_num' advances as we send, but 'base' only moves when ACKs arrive.
         */
        while (next_seq_num < base + window_size && next_seq_num <= total_packets) {

            printf("Sending packet %d\n", next_seq_num);

            /*
             * Convert the sequence number to network byte order before sending.
             * htonl() = Host TO Network Long (32-bit).
             */
            num = htonl(next_seq_num);

            /* Send the 4-byte sequence number to the server */
            int n = write(connected_socket_fd, &num, sizeof(uint32_t));
            if (n < 0){
                perror("ERROR while writing to socket");
                exit(1);
            }

            next_seq_num++; /* Advance to the next packet sequence number */
            total_tries++;  /* Count this as one transmission attempt */
        }

        /*
         * ── STEP 2: WAIT FOR AN ACK ──
         * After filling the window, wait up to 'timeout' seconds for an ACK.
         * This is a BLOCKING wait — the client sleeps here until an ACK arrives
         * or the timer runs out.
         */
        int n = my_read(connected_socket_fd, &num, sizeof(uint32_t), timeout);

        if (n == 0) {
            /*
             * ── TIMEOUT CASE ──
             * No ACK received within the timeout window.
             * This means one or more packets (or their ACKs) were lost/delayed.
             *
             * Go-Back-N response: RETRANSMIT the entire window from 'base'.
             * We reset next_seq_num to base — the inner while loop above
             * will re-send all packets from base to base+window_size-1.
             */
            printf("\n--- TIMEOUT on base packet %d! ---\n", base);
            printf("Go-Back-N: Resending window from packet %d...\n", base);
            next_seq_num = base; /* Go back to the start of the window */

        } else if (n > 0) {
            /*
             * ── ACK RECEIVED ──
             * Convert the received ACK number from network to host byte order.
             */
            uint32_t ack_num = ntohl(num);
            printf("ACK received for packet %u \n", ack_num);

            /*
             * ── CUMULATIVE ACK PROCESSING ──
             * ACK(n) means all packets up to and including n were received correctly.
             * So we slide the window forward: base = ack_num + 1.
             *
             * We only update base if the ACK is for something we're still waiting on
             * (ack_num >= base). A duplicate ACK for something already acknowledged
             * (ack_num < base) is silently ignored.
             */
            if (ack_num >= base) {
                base = ack_num + 1;
            }

            /*
             * ── POLLING LOOP: DRAIN EXTRA ACKs ──
             * After processing one ACK, immediately check if MORE ACKs are
             * already waiting in the TCP receive buffer.
             *
             * my_read(..., 0) uses timeout=0, making it NON-BLOCKING:
             *   - If data is available immediately → reads and processes it.
             *   - If no data available → returns 0 right away (no waiting).
             *
             * This loop keeps sliding the window forward for any ACKs
             * that piled up while we were sending packets.
             */
            while (my_read(connected_socket_fd, &num, sizeof(uint32_t), 0) > 0) {
                ack_num = ntohl(num);
                printf("ACK received for packet %u \n", ack_num);
                if (ack_num >= base) {
                    base = ack_num + 1;
                }
            }
            /* After the polling loop, any pending ACKs have been processed */

        } else {
            /* n < 0 → a real socket error (not a timeout) */
            perror("ERROR while reading from socket");
            exit(1);
        }
    }
    /* Loop exits when base > total_packets → all 10 packets are ACKed */

    printf("\nAll %d packets sent and acknowledged successfully with %d attempts.\n",
           total_packets, total_tries);

    /* Close the connection */
    close(connected_socket_fd);

    return 0;
}
