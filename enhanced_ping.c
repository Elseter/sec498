//
// Enhanced Ping Tool for Network Throughput Analysis
// Based on code by Riley King, modified for cybersecurity testing
// Fixed version by Claude to address packet reception issues
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
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>

// Define constants
// -------------------------------------------------------------------
#define PACKET_SIZE     64      // Default packet size
#define MAX_PACKET_SIZE 65536   // Maximum allowed packet size
#define MAX_WAIT_TIME   5       // Maximum time to wait for response (seconds)
#define DEFAULT_TTL     64      // Default Time To Live value
#define DEFAULT_COUNT   -1      // Default ping count (-1 means infinite)
#define MAX_RETRY       3       // Maximum retry attempts per packet
#define RETRY_INTERVAL  500     // Time between retries (milliseconds)
#define MAX_HISTORY     1000    // Maximum history size for tracking packets

// Experiment modes
typedef enum {
    MODE_STANDARD,        // Standard ping behavior
    MODE_AGGRESSIVE,      // More frequent pings with shorter timeouts
    MODE_INTERMITTENT     // Random intervals between pings
} experiment_mode_t;

// Packet status tracking
typedef struct {
    int seq_num;              // Sequence number
    struct timeval sent_time; // Time when packet was sent
    int retries;              // Number of retries for this packet
    bool received;            // Whether packet was received
    double rtt;               // Round trip time in ms
    bool corrupted;           // Whether received packet was corrupted
} packet_history_t;

// Global variables for the program
int sockfd;
int send_count = 0;           // Total packets sent (including retries)
int original_send_count = 0;  // Original packets sent (excluding retries)
int recv_count = 0;           // Total packets received
int resend_count = 0;         // Number of packets resent
int rereceived_count = 0;     // Number of packets received after retry
int corrupt_count = 0;        // Number of corrupted packets detected
int stop_ping = 0;
struct sockaddr_in dest_addr;
packet_history_t packet_history[MAX_HISTORY];
int total_packets = 0;
char *logfile_name = NULL;    // Log file name
FILE *logfile = NULL;         // Log file pointer
experiment_mode_t mode = MODE_STANDARD;  // Default mode
unsigned short ident;         // Identifier for our ICMP packets

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

// Verify checksum on received packet
bool verify_checksum(unsigned short *buf, int size) {
    // Save original checksum
    unsigned short original_checksum = ((struct icmphdr *)buf)->checksum;
    
    // Zero out checksum field for calculation
    ((struct icmphdr *)buf)->checksum = 0;
    
    // Calculate new checksum
    unsigned short calculated = calculate_checksum(buf, size);
    
    // Restore original checksum
    ((struct icmphdr *)buf)->checksum = original_checksum;
    
    // Compare
    return original_checksum == calculated;
}

// Prepare the ICMP packet with data for integrity verification
void prepare_icmp_packet(struct icmphdr *icmp_header, int seq_num, int packet_size) {
    // Zero out the packet
    memset(icmp_header, 0, packet_size);

    // Fill in ICMP header fields
    icmp_header->type = ICMP_ECHO;        // ICMP Echo Request
    icmp_header->code = 0;                // No code for Echo Request
    icmp_header->un.echo.id = ident;      // Use our identifier
    icmp_header->un.echo.sequence = seq_num;  // Sequence number

    // Fill data part with timestamp and incrementing pattern for integrity verification
    unsigned char *ptr = (unsigned char *)(icmp_header + 1);
    
    // Add timestamp to data
    struct timeval tv;
    gettimeofday(&tv, NULL);
    memcpy(ptr, &tv, sizeof(tv));
    ptr += sizeof(tv);
    
    // Fill remaining data with pattern
    for (int i = 0; i < packet_size - sizeof(struct icmphdr) - sizeof(tv); i++) {
        *ptr++ = i & 0xFF;
    }

    // Calculate checksum after filling the data
    icmp_header->checksum = 0;
    icmp_header->checksum = calculate_checksum((unsigned short *)icmp_header, packet_size);
}

// Verify data integrity of received packet
bool verify_packet_integrity(struct icmphdr *icmp_header, int data_size) {
    unsigned char *ptr = (unsigned char *)(icmp_header + 1);
    
    // Skip timestamp
    ptr += sizeof(struct timeval);
    
    // Verify pattern in the data portion
    for (int i = 0; i < data_size - sizeof(struct timeval); i++) {
        if (*ptr != (i & 0xFF)) {
            return false;
        }
        ptr++;
    }
    
    return true;
}

// Log message to file and stdout
void log_message(const char *format, ...) {
    va_list args, args_copy;
    va_start(args, format);
    
    // Print to stdout
    va_copy(args_copy, args);
    vprintf(format, args_copy);
    va_end(args_copy);
    
    // Write to log file if available
    if (logfile) {
        va_copy(args_copy, args);
        vfprintf(logfile, format, args_copy);
        fflush(logfile); // Ensure data is written immediately
        va_end(args_copy);
    }
    
    va_end(args);
}

// Get current timestamp as milliseconds
long long current_timestamp_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)(tv.tv_sec) * 1000 + (tv.tv_usec / 1000);
}

// Add entry to packet history
void add_packet_to_history(int seq_num) {
    if (total_packets < MAX_HISTORY) {
        packet_history[total_packets].seq_num = seq_num;
        gettimeofday(&packet_history[total_packets].sent_time, NULL);
        packet_history[total_packets].retries = 0;
        packet_history[total_packets].received = false;
        packet_history[total_packets].corrupted = false;
        total_packets++;
    }
}

// Update packet history when packet is received
void update_packet_history(int seq_num, double rtt, bool corrupted) {
    for (int i = 0; i < total_packets; i++) {
        if (packet_history[i].seq_num == seq_num) {
            packet_history[i].received = true;
            packet_history[i].rtt = rtt;
            packet_history[i].corrupted = corrupted;
            
            if (packet_history[i].retries > 0) {
                rereceived_count++;
            }
            
            return;
        }
    }
}

// Find packet in history by sequence number
packet_history_t *find_packet(int seq_num) {
    for (int i = 0; i < total_packets; i++) {
        if (packet_history[i].seq_num == seq_num) {
            return &packet_history[i];
        }
    }
    return NULL;
}

// Print detailed statistics
void print_statistics() {
    log_message("\n--- Ping Statistics ---\n");
    log_message("Total packets: %d original, %d including retries\n", original_send_count, send_count);
    log_message("Received: %d (%.1f%% packet loss)\n", 
                recv_count, 
                original_send_count ? ((original_send_count - recv_count) * 100.0) / original_send_count : 0);
    log_message("Retransmitted: %d\n", resend_count);
    log_message("Received after retry: %d\n", rereceived_count);
    log_message("Corrupted packets: %d\n", corrupt_count);
    
    // Calculate RTT statistics
    double min_rtt = -1, max_rtt = -1, avg_rtt = 0;
    int rtt_count = 0;
    
    for (int i = 0; i < total_packets; i++) {
        if (packet_history[i].received && !packet_history[i].corrupted) {
            double rtt = packet_history[i].rtt;
            
            if (min_rtt == -1 || rtt < min_rtt) min_rtt = rtt;
            if (max_rtt == -1 || rtt > max_rtt) max_rtt = rtt;
            avg_rtt += rtt;
            rtt_count++;
        }
    }
    
    if (rtt_count > 0) {
        avg_rtt /= rtt_count;
        log_message("RTT min/avg/max = %.3f/%.3f/%.3f ms\n", min_rtt, avg_rtt, max_rtt);
    }
}

// Get delay between packets based on experiment mode
int get_ping_interval() {
    switch (mode) {
        case MODE_AGGRESSIVE:
            return 200; // 200ms between pings
        
        case MODE_INTERMITTENT:
            // Random interval between 500ms and 3000ms
            return 500 + (rand() % 2500);
            
        case MODE_STANDARD:
        default:
            return 1000; // 1 second between pings
    }
}

// Handle signals (Ctrl+C)
void signal_handler(int signo) {
    if (signo == SIGINT) {
        stop_ping = 1;
        print_statistics();

        // Close resources
        if (logfile) {
            fclose(logfile);
        }
        close(sockfd);
        exit(0);
    }
}

// Print usage information
void print_usage(char *prog_name) {
    fprintf(stderr, "Usage: %s <hostname/IP> [options]\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -s <size>      Packet size (default: %d)\n", PACKET_SIZE);
    fprintf(stderr, "  -t <ttl>       Time to live (default: %d)\n", DEFAULT_TTL);
    fprintf(stderr, "  -c <count>     Number of packets to send (default: infinite)\n");
    fprintf(stderr, "  -i <interval>  Wait interval in ms (default: mode dependent)\n");
    fprintf(stderr, "  -w <timeout>   Response timeout in seconds (default: %d)\n", MAX_WAIT_TIME);
    fprintf(stderr, "  -r <retries>   Number of retries per packet (default: %d)\n", MAX_RETRY);
    fprintf(stderr, "  -m <mode>      Experiment mode (1=standard, 2=aggressive, 3=intermittent)\n");
    fprintf(stderr, "  -l <file>      Log file name\n");
    fprintf(stderr, "  -h             Show this help message\n");
}

// Main function
// -------------------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Parse command line arguments
    char *target = NULL;
    int packet_size = PACKET_SIZE;
    int ttl = DEFAULT_TTL;
    int count = DEFAULT_COUNT;
    int interval = -1; // Will be set based on mode
    int timeout = MAX_WAIT_TIME;
    int retries = MAX_RETRY;
    
    // Initialize random seed
    srand(time(NULL));
    
    // Initialize identifier - we'll use process ID to match standard ping behavior
    ident = getpid() & 0xFFFF;
    
    // Parse args
    int opt;
    while ((opt = getopt(argc, argv, "s:t:c:i:w:r:m:l:h")) != -1) {
        switch (opt) {
            case 's':
                packet_size = atoi(optarg);
                break;
            case 't':
                ttl = atoi(optarg);
                break;
            case 'c':
                count = atoi(optarg);
                break;
            case 'i':
                interval = atoi(optarg);
                break;
            case 'w':
                timeout = atoi(optarg);
                break;
            case 'r':
                retries = atoi(optarg);
                break;
            case 'm':
                mode = atoi(optarg) - 1; // Convert 1-based user input to 0-based enum
                if (mode < MODE_STANDARD || mode > MODE_INTERMITTENT) {
                    fprintf(stderr, "Invalid mode. Must be between 1 and 3.\n");
                    return EXIT_FAILURE;
                }
                break;
            case 'l':
                logfile_name = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    // Get target from non-option arguments
    if (optind < argc) {
        target = argv[optind];
    } else {
        fprintf(stderr, "No target specified.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    // If interval not set, use mode default
    if (interval == -1) {
        interval = get_ping_interval();
    }

    // Validate packet size
    if (packet_size < sizeof(struct icmphdr) + 8 || packet_size > MAX_PACKET_SIZE) {
        fprintf(stderr, "Invalid packet size. Must be between %ld and %d bytes.\n",
                sizeof(struct icmphdr) + 8, MAX_PACKET_SIZE);
        return EXIT_FAILURE;
    }
    
    // Open log file if specified
    if (logfile_name) {
        logfile = fopen(logfile_name, "w");
        if (!logfile) {
            perror("Failed to open log file");
            return EXIT_FAILURE;
        }
    }

    // Create raw socket
    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket creation failed");
        fprintf(stderr, "Note: This program requires root privileges to create raw sockets.\n");
        if (logfile) fclose(logfile);
        return EXIT_FAILURE;
    }

    // Set TTL value
    if (setsockopt(sockfd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
        perror("setsockopt IP_TTL failed");
        close(sockfd);
        if (logfile) fclose(logfile);
        return EXIT_FAILURE;
    }

    // Set timeout for receiving
    struct timeval tv;
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO failed");
        close(sockfd);
        if (logfile) fclose(logfile);
        return EXIT_FAILURE;
    }

    // Resolve target hostname to IP address
    struct hostent *host_entity;
    char ip_addr[INET_ADDRSTRLEN];

    if ((host_entity = gethostbyname(target)) == NULL) {
        perror("gethostbyname failed");
        close(sockfd);
        if (logfile) fclose(logfile);
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
        if (logfile) fclose(logfile);
        return EXIT_FAILURE;
    }

    // Print experiment info
    log_message("PING %s (%s): %d bytes of data with %s mode\n", 
                target, ip_addr, 
                packet_size - sizeof(struct icmphdr),
                mode == MODE_STANDARD ? "standard" : 
                  (mode == MODE_AGGRESSIVE ? "aggressive" : "intermittent"));

    // Main ping loop
    int seq_num = 0;
    while (!stop_ping && (count == -1 || original_send_count < count)) {
        // Prepare packet
        prepare_icmp_packet((struct icmphdr *)packet, seq_num, packet_size);
        
        // Add packet to history
        add_packet_to_history(seq_num);
        original_send_count++;
        
        // Try sending the packet (with retries if needed)
        bool packet_received = false;
        int current_tries = 0;
        
        while (!packet_received && current_tries <= retries) {
            if (current_tries > 0) {
                // This is a retry
                resend_count++;
                
                // Update retry count in history
                packet_history_t *pkt = find_packet(seq_num);
                if (pkt) {
                    pkt->retries++;
                }
                
                // Prepare packet again with same sequence number
                prepare_icmp_packet((struct icmphdr *)packet, seq_num, packet_size);
                
                // Log retry
                log_message("Retrying seq=%d (attempt %d/%d)\n", seq_num, current_tries, retries);
            }
            
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
            
            // Loop to handle possible multiple responses (e.g., ICMP error messages)
            fd_set read_set;
            struct timeval wait_time;
            bool response_received = false;
            
            // Set wait time for select()
            wait_time.tv_sec = timeout;
            wait_time.tv_usec = 0;
            
            // Wait for response with select()
            FD_ZERO(&read_set);
            FD_SET(sockfd, &read_set);
            
            // Wait up to timeout for data to be available
            int ready = select(sockfd + 1, &read_set, NULL, NULL, &wait_time);
                
            if (ready > 0) {
                // Data is available to be read - try to receive it
                int bytes_received = recvfrom(sockfd, recv_packet, sizeof(recv_packet), 0,
                                          (struct sockaddr *)&recv_addr, &addr_len);
                
                if (bytes_received > 0) {
                    // Record receive time
                    struct timeval recv_time;
                    gettimeofday(&recv_time, NULL);
    
                    // Calculate round-trip time in milliseconds
                    double rtt = (recv_time.tv_sec - send_time.tv_sec) * 1000.0 +
                                (recv_time.tv_usec - send_time.tv_usec) / 1000.0;
    
                    // Parse IP header and ICMP header
                    struct iphdr *ip_header = (struct iphdr *)recv_packet;
                    int ip_header_len = ip_header->ihl * 4;  // IP header length
                    struct icmphdr *icmp_header = (struct icmphdr *)(recv_packet + ip_header_len);
                    int data_size = bytes_received - ip_header_len - sizeof(struct icmphdr);
    
                    // Check if it's our echo reply
                    if (icmp_header->type == ICMP_ECHOREPLY &&
                        icmp_header->un.echo.id == ident &&
                        icmp_header->un.echo.sequence == seq_num) {
                        
                        recv_count++;
                        packet_received = true;
                        response_received = true;
    
                        // Verify checksum and data integrity
                        bool checksum_valid = verify_checksum((unsigned short *)icmp_header, 
                                                            bytes_received - ip_header_len);
                        bool data_valid = verify_packet_integrity(icmp_header, data_size);
                        bool is_corrupted = !checksum_valid || !data_valid;
                        
                        if (is_corrupted) {
                            corrupt_count++;
                        }
                        
                        // Update packet history
                        update_packet_history(seq_num, rtt, is_corrupted);
    
                        // Print information
                        log_message("%d bytes from %s: icmp_seq=%d ttl=%d time=%.3f ms %s\n",
                                   bytes_received - ip_header_len,
                                   inet_ntoa(recv_addr.sin_addr),
                                   icmp_header->un.echo.sequence,
                                   ip_header->ttl,
                                   rtt,
                                   is_corrupted ? "[CORRUPTED]" : "");
                        
                        if (is_corrupted) {
                            log_message("  Corruption details: checksum=%s, data=%s\n",
                                       checksum_valid ? "valid" : "invalid",
                                       data_valid ? "valid" : "invalid");
                        }
                    }
                    else if (icmp_header->type == ICMP_DEST_UNREACH) {
                        // Handle destination unreachable message
                        log_message("From %s: Destination unreachable (code=%d) for icmp_seq=%d\n",
                                  inet_ntoa(recv_addr.sin_addr),
                                  icmp_header->code,
                                  seq_num);
                    }
                    else if (icmp_header->type == ICMP_TIME_EXCEEDED) {
                        // Handle time exceeded message
                        log_message("From %s: Time to live exceeded for icmp_seq=%d\n",
                                  inet_ntoa(recv_addr.sin_addr),
                                  seq_num);
                    }
                    // Other ICMP message types can be handled here if needed
                }
            }
            
            if (!response_received) {
                log_message("Request timeout for icmp_seq=%d (try %d/%d)\n", 
                          seq_num, current_tries + 1, retries + 1);
            }
            
            current_tries++;
            
            // If packet received or max retries reached, move to next sequence
            if (packet_received || current_tries > retries) {
                break;
            }
            
            // Wait before retry
            usleep(RETRY_INTERVAL * 1000);
        }

        seq_num++;

        // Check if we've reached the requested count
        if (count != -1 && original_send_count >= count) {
            break;
        }

        // Sleep for the interval before sending the next packet
        usleep(interval * 1000);
    }

    // Print statistics
    print_statistics();

    // Free resources
    free(packet);
    if (logfile) {
        fclose(logfile);
    }
    close(sockfd);

    return EXIT_SUCCESS;
}
