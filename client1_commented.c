/*
 * client1_commented.c
 * A basic TCP echo client that connects to a server, sends user-typed strings,
 * and prints whatever the server echoes back. Stops when "quit" is received.
 */

/* Standard I/O — provides printf(), scanf(), perror() */
#include <stdio.h>

/* Standard library — provides exit() */
#include <stdlib.h>

/* Provides Internet address structures like 'struct sockaddr_in' */
#include <netinet/in.h>

/* Provides POSIX type definitions used in socket calls */
#include <sys/types.h>

/* Core socket API — provides socket(), connect(), read(), write(), close() */
#include <sys/socket.h>

/* Provides inet_addr() to convert an IP string like "127.0.0.1" to binary form */
#include <arpa/inet.h>

/* String utilities — bzero(), bcmp(), strlen() */
#include <string.h>

/* POSIX I/O — read(), write(), close() */
#include <unistd.h>

/* The IP address of the server to connect to.
   "127.0.0.1" is the loopback address — it means "this same machine". */
#define IP "127.0.0.1"

/* The port number the server is listening on.
   Client must use the same port as the server. */
#define PORTNO 54321

/* Buffer size for messages — 256 bytes is plenty for short strings */
#define SIZE 256

int main(int argc, char *argv[]) {

    /*
     * socket() creates a TCP socket.
     *   AF_INET     → IPv4
     *   SOCK_STREAM → TCP (reliable, stream-based)
     *   0           → auto-select protocol (TCP)
     * Returns a file descriptor (integer) used to refer to this socket.
     */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /*
     * 'struct sockaddr_in' holds the destination (server) address info:
     * IP address, port, and address family.
     */
    struct sockaddr_in serv_addr;

    /*
     * Zero out serv_addr to make sure no garbage values exist in unused fields.
     */
    bzero((char *) &serv_addr, sizeof(serv_addr));

    /*
     * Tell the OS we're using IPv4 addresses.
     */
    serv_addr.sin_family = AF_INET;

    /*
     * inet_addr() converts the human-readable IP string "127.0.0.1"
     * into a 32-bit binary number in network byte order.
     * This is the IP address of the server we want to connect to.
     */
    serv_addr.sin_addr.s_addr = inet_addr(IP);

    /*
     * htons() converts the port number from host byte order to network byte order.
     * Always required when setting port numbers in socket address structures.
     */
    serv_addr.sin_port = htons(PORTNO);

    /*
     * connect() initiates the TCP three-way handshake with the server.
     * It "dials" the server at the IP and port we specified in serv_addr.
     * This call BLOCKS until either:
     *   - The connection is established (returns 0), or
     *   - The connection fails (returns < 0, e.g., server not running).
     */
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR while connecting");
        exit(1);
    }

    /* Buffer to hold the messages we send and receive */
    char buffer[SIZE];

    /*
     * Keep looping: read user input, send to server, receive echo, repeat.
     * Break when "quit" is detected.
     */
    while (1) {
        printf("Enter a string: ");

        /*
         * Zero out the buffer to erase any previous message before reading new input.
         */
        bzero(buffer, SIZE);

        /*
         * scanf() reads a whitespace-delimited word from standard input (keyboard)
         * and stores it in buffer. Note: it stops at spaces, so it reads ONE word.
         * Use fgets() if you want to read full lines with spaces.
         */
        scanf("%s", buffer);

        /*
         * write() sends the contents of buffer to the server through the socket.
         * strlen(buffer) sends only the actual string, not the whole 256-byte buffer.
         * Returns the number of bytes written, or -1 on error.
         */
        int n = write(sockfd, buffer, strlen(buffer));
        if (n < 0){
            perror("ERROR while writing to socket");
            exit(1);
        }

        /*
         * read() waits for the server to send data back (the echo).
         * Reads up to SIZE-1 bytes into buffer.
         * This is a BLOCKING call — it waits until the server responds.
         */
        n = read(sockfd, buffer, SIZE-1);
        if (n < 0){
            perror("ERROR while reading from socket");
            exit(1);
        }

        /* Print the echoed reply from the server */
        printf("Server replied: %s \n", buffer);

        /*
         * If the server echoed "quit" back, we stop the loop.
         * bcmp() compares the first 4 bytes; returns 0 if they match.
         * !bcmp(...) → true when they ARE equal to "quit".
         */
        if (!bcmp(buffer, "quit", 4))
            break;
    }

    /*
     * close() shuts down the socket and releases its resources.
     * This sends a TCP FIN to the server, signaling we're done.
     */
    close(sockfd);

    return 0; /* Program finished successfully */
}
