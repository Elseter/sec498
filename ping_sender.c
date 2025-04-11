//
// Created by Riley King on 4/11/25.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>

// Define constants
// -------------------------------------------------------------------
#define PACKET_SIZE     64      // Default packet size
#define MAX_PACKET_SIZE 65536   // Maximum allowed packet size
#define MAX_WAIT_TIME   5       // Maximum time to wait for response (seconds)
#define DEFAULT_TTL     64      // Default Time To Live value

// Global variables for the program
int sockfd;
int send_count = 0;
int recv_count = 0;
int stop_ping = 0;
struct sockaddr_in dest_addr;

// Functions used in creating the ICMP packet
// -------------------------------------------------------------------
// Calculate ICMP checksum
unsigned short calculate_checksum(unsigned short *buf, int size) {
    unsigned long sum = 0;

    // Add up 16-bit words
    while (size > 1) {
        sum += *buf++;
        size -= 2;
    }

    // Add left-over byte, if any
    if (size == 1) {
        sum += *(unsigned char *)buf;
    }

    // Fold 32-bit sum to 16 bits
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);

    return (unsigned short)(~sum);
}

// Prepare the ICMP packet
void prepare_icmp_packet(struct icmphdr *icmp_header, int seq_num, int packet_size) {
    // Zero out the packet
    memset(icmp_header, 0, packet_size);

    // Fill in ICMP header fields
    icmp_header->type = ICMP_ECHO;        // ICMP Echo Request
    icmp_header->code = 0;                // No code for Echo Request
    icmp_header->un.echo.id = getpid();   // Use process ID as identifier
    icmp_header->un.echo.sequence = seq_num;  // Sequence number

    // Fill data part with some pattern
    unsigned char *ptr = (unsigned char *)(icmp_header + 1);
    for (int i = 0; i < packet_size - sizeof(struct icmphdr); i++) {
        *ptr++ = i & 0xFF;
    }

    // Calculate checksum after filling the data
    icmp_header->checksum = 0;
    icmp_header->checksum = calculate_checksum((unsigned short *)icmp_header, packet_size);
}

// Handle signals (Ctrl+C)
void signal_handler(int signo) {
    if (signo == SIGINT) {
        printf("\n--- Ping statistics ---\n");
        printf("%d packets transmitted, %d received, %.1f%% packet loss\n",
               send_count, recv_count,
               send_count ? ((send_count - recv_count) * 100.0) / send_count : 0);

        // Close socket and exit
        close(sockfd);
        exit(0);
    }
}

// Main function
// -------------------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <hostname/IP> [packet_size] [ttl]\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Parse command line arguments
    char *target = argv[1];
    int packet_size = (argc > 2) ? atoi(argv[2]) : PACKET_SIZE;
    int ttl = (argc > 3) ? atoi(argv[3]) : DEFAULT_TTL;

    // Validate packet size
    if (packet_size < sizeof(struct icmphdr) || packet_size > MAX_PACKET_SIZE) {
        fprintf(stderr, "Invalid packet size. Must be between %ld and %d bytes.\n",
                sizeof(struct icmphdr), MAX_PACKET_SIZE);
        return EXIT_FAILURE;
    }

    // Create raw socket
    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket creation failed");
        fprintf(stderr, "Note: This program requires root privileges to create raw sockets.\n");
        return EXIT_FAILURE;
    }

    // Set TTL value
    if (setsockopt(sockfd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
        perror("setsockopt IP_TTL failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Set timeout for receiving
    struct timeval tv;
    tv.tv_sec = MAX_WAIT_TIME;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Resolve target hostname to IP address
    struct hostent *host_entity;
    char ip_addr[INET_ADDRSTRLEN];

    if ((host_entity = gethostbyname(target)) == NULL) {
        perror("gethostbyname failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Convert IP address to readable format
    strcpy(ip_addr, inet_ntoa(*(struct in_addr *)host_entity->h_addr));

    // Setup destination address
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = 0; // Not used in ICMP
    inet_aton(ip_addr, &dest_addr.sin_addr);

    // Register signal handler for Ctrl+C
    signal(SIGINT, signal_handler);

    // Allocate memory for packet
    char *packet = malloc(packet_size);
    if (!packet) {
        perror("Failed to allocate memory for packet");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Print ping info
    printf("PING %s (%s) %d bytes of data.\n", target, ip_addr, packet_size - sizeof(struct icmphdr));

    // Main ping loop
    int seq_num = 0;
    while (!stop_ping) {
        // Prepare packet
        prepare_icmp_packet((struct icmphdr *)packet, seq_num, packet_size);

        // Record send time
        struct timeval send_time;
        gettimeofday(&send_time, NULL);

        // Send packet
        int bytes_sent = sendto(sockfd, packet, packet_size, 0,
                                (struct sockaddr *)&dest_addr, sizeof(dest_addr));

        if (bytes_sent < 0) {
            perror("sendto failed");
        } else {
            send_count++;
        }

        // Receive buffer
        char recv_packet[MAX_PACKET_SIZE];
        struct sockaddr_in recv_addr;
        socklen_t addr_len = sizeof(recv_addr);

        // Wait for reply
        int bytes_received = recvfrom(sockfd, recv_packet, sizeof(recv_packet), 0,
                                      (struct sockaddr *)&recv_addr, &addr_len);

        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Request timeout for icmp_seq=%d\n", seq_num);
            } else {
                perror("recvfrom failed");
            }
        } else {
            // Record receive time
            struct timeval recv_time;
            gettimeofday(&recv_time, NULL);

            // Calculate round-trip time in milliseconds
            double rtt = (recv_time.tv_sec - send_time.tv_sec) * 1000.0 +
                         (recv_time.tv_usec - send_time.tv_usec) / 1000.0;

            // Parse IP header and ICMP header
            struct iphdr *ip_header = (struct iphdr *)recv_packet;
            int ip_header_len = ip_header->ihl * 4;
            struct icmphdr *icmp_header = (struct icmphdr *)(recv_packet + ip_header_len);

            // Check if it's our echo reply
            if (icmp_header->type == ICMP_ECHOREPLY &&
                icmp_header->un.echo.id == getpid()) {

                recv_count++;

                // Print information
                printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%.1f ms\n",
                       bytes_received - ip_header_len,
                       inet_ntoa(recv_addr.sin_addr),
                       icmp_header->un.echo.sequence,
                       ip_header->ttl,
                       rtt);
            }
        }

        seq_num++;

        // Sleep for 1 second before sending the next packet
        sleep(1);
    }

    // Free resources
    free(packet);
    close(sockfd);

    return EXIT_SUCCESS;
}
