/*
 * server1_commented.c
 * A basic TCP echo server that accepts one client connection,
 * echoes back whatever the client sends, and stops when "quit" is received.
 */

/* Standard I/O library — provides printf(), perror() etc. */
#include <stdio.h>

/* Standard library — provides exit() for terminating the program */
#include <stdlib.h>

/* Provides Internet address structures like 'struct sockaddr_in'
   which holds IP address and port information */
#include <netinet/in.h>

/* Provides POSIX types like 'size_t', 'ssize_t' used in socket calls */
#include <sys/types.h>

/* Core socket API — provides socket(), bind(), listen(), accept(),
   read(), write(), close() system calls */
#include <sys/socket.h>

/* Provides inet_addr(), inet_ntoa() for converting IP addresses
   between human-readable strings and binary form */
#include <arpa/inet.h>

/* String utilities — provides bzero() to zero-out memory, bcmp() to
   compare memory blocks, strlen() to get string length */
#include <string.h>

/* POSIX API — provides read(), write(), close() for file/socket I/O */
#include <unistd.h>

/* The port number on which this server will listen.
   Ports above 1024 don't need root privileges to use. */
#define PORTNO 54321

/* Maximum number of bytes we can receive or send in one go.
   Our buffer will be this size. */
#define SIZE 256

int main(int argc, char **argv) {

    /*
     * socket() creates a new socket and returns a file descriptor (integer handle).
     *   AF_INET     → Use IPv4 addressing
     *   SOCK_STREAM → Use TCP (reliable, ordered, connection-based)
     *   0           → Protocol auto-selected (TCP for SOCK_STREAM)
     * The returned 'sockfd' is like an open file — you use it to refer to the socket later.
     */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /*
     * 'struct sockaddr_in' is the data structure that holds the server's
     * address information: address family, IP address, and port number.
     */
    struct sockaddr_in serv_addr;

    /*
     * bzero() fills the entire serv_addr structure with zeros.
     * This is important to avoid garbage values in unused fields,
     * which could cause unpredictable behavior.
     *   (char *) &serv_addr → cast the address of serv_addr to a char pointer
     *   sizeof(serv_addr)   → number of bytes to zero out
     */
    bzero((char *) &serv_addr, sizeof(serv_addr));

    /*
     * Set the address family to AF_INET (IPv4).
     * This tells the OS we're working with IPv4 addresses.
     */
    serv_addr.sin_family = AF_INET;

    /*
     * INADDR_ANY means "listen on all available network interfaces".
     * For example, if your machine has both WiFi and Ethernet, it will
     * accept connections on both. Alternatively you could use:
     *   serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
     * to only accept connections on localhost (loopback interface).
     */
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    /*
     * htons() = "Host TO Network Short" — converts the port number from
     * the host's byte order (could be little-endian) to network byte order
     * (always big-endian). Network protocols always use big-endian, so this
     * conversion is mandatory for portability.
     */
    serv_addr.sin_port = htons(PORTNO);

    /*
     * bind() associates the socket with the address and port we configured above.
     * Think of it as "reserving" that port for this server process.
     *   sockfd              → the socket we created
     *   (struct sockaddr *) → cast needed because bind() takes a generic sockaddr pointer
     *   sizeof(serv_addr)   → size of the address structure
     * Returns < 0 on failure (e.g., port already in use).
     */
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        perror("ERROR on binding\n"); /* perror() prints a human-readable error message */
        exit(1);                      /* Exit with error code 1 to signal failure */
    }

    /*
     * listen() puts the socket into "passive" mode — it's now ready to accept
     * incoming connection requests but doesn't accept them yet.
     *   sockfd → the socket to listen on
     *   5      → the backlog: maximum number of pending connections to queue
     *            (connections waiting to be accepted). If the queue is full,
     *            new connection attempts will be refused.
     */
    if (listen(sockfd, 5) < 0) {
        perror("Listen error");
        exit(3); /* Exit code 3 to distinguish this failure from others */
    }
    printf("server started listening in port %d\n", PORTNO);

    /*
     * 'struct sockaddr_in cli_addr' will be filled by accept() with the
     * client's IP address and port number — useful for logging who connected.
     */
    struct sockaddr_in cli_addr;

    /*
     * cli_addr_len must be initialized to the size of cli_addr BEFORE calling accept().
     * accept() will update it with the actual size of the address that was stored.
     */
    unsigned int cli_addr_len = sizeof(cli_addr);

    /*
     * accept() blocks (waits) until a client connects.
     * When a client connects, it:
     *   1. Completes the TCP three-way handshake with the client.
     *   2. Creates a NEW socket specifically for this client connection.
     *   3. Fills cli_addr with the client's address info.
     *   4. Returns the new socket's file descriptor (accepted_sockfd).
     *
     * The original 'sockfd' keeps listening for more clients.
     * 'accepted_sockfd' is used for all communication with THIS client.
     */
    int accepted_sockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &cli_addr_len);
    printf("connection established to a client\n");

    /* You could uncomment these to print the client's IP and port:
     * printf("IP address is: %s\n", inet_ntoa(cli_addr.sin_addr));
     * printf("port is: %d\n", (int) ntohs(cli_addr.sin_port));
     */

    /*
     * 'buffer' is where we'll store the data received from the client.
     * It's SIZE (256) bytes large.
     */
    char buffer[SIZE];

    /*
     * Enter an infinite loop to keep receiving and echoing messages.
     * We'll break out of the loop when the client disconnects or sends "quit".
     */
    while (1) {
        /*
         * Zero out the buffer before each read so there are no leftover
         * bytes from the previous iteration that could confuse us.
         */
        bzero(buffer, SIZE);

        /*
         * read() reads up to (SIZE-1) bytes from the client socket into buffer.
         * We use SIZE-1 (not SIZE) to leave room for a null terminator '\0',
         * so we can treat buffer as a proper C string.
         * Returns:
         *   > 0  → number of bytes actually read
         *   = 0  → the client closed the connection (end of stream)
         *   < 0  → an error occurred
         */
        int n = read(accepted_sockfd, buffer, SIZE-1);

        if (n < 0){
            perror("ERROR in reading from socket");
            exit(1);
        }

        if (n == 0) {
            /*
             * n == 0 means the client closed its end of the connection.
             * TCP signals end-of-stream this way (like EOF on a file).
             */
            printf("client closed\n");
            break; /* Exit the while loop */
        }

        printf("client sent: %s\n", buffer);

        /*
         * Echo the message back to the client using write().
         * strlen(buffer) sends only the actual string length,
         * not the entire 256-byte buffer (which would be wasteful).
         */
        n = write(accepted_sockfd, buffer, strlen(buffer));
        if (n < 0){
            perror("ERROR in writing to socket");
            exit(1);
        }

        /*
         * bcmp() compares the first 4 bytes of 'buffer' with "quit".
         * bcmp() returns 0 if they are equal (like memcmp).
         * !bcmp(...) → true when they ARE equal.
         * So if the client sent "quit", we break out of the loop and close.
         */
        if (!bcmp(buffer, "quit", 4))
            break;
    }

    /*
     * close() releases the socket and its resources.
     * This sends a FIN to the client, signaling the server is done.
     */
    close(accepted_sockfd);

    /* Return 0 to indicate the program finished successfully */
    return 0;
}
