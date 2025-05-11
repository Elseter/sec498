# Enhanced Ping Tool

## Description
This Enhanced Ping Tool is a robust network diagnostic utility built as an extension of the standard ICMP ping utility. It provides advanced functionality for network throughput analysis and cybersecurity testing, including packet integrity verification, customizable retransmission strategies, and detailed statistics collection.

## Features
- **Enhanced Packet Validation**: Verifies both checksum and data integrity of received packets
- **Multiple Operating Modes**:
  - Standard: Regular interval pinging (1 second)
  - Aggressive: Rapid pinging with shorter timeouts (200ms)
  - Intermittent: Random intervals between pings (500-3000ms)
- **Advanced Retry Mechanism**: Configurable retry attempts for lost packets
- **Comprehensive Statistics**: Detailed metrics on packet loss, corruption, and retransmission
- **Flexible Packet Size**: Customizable packet size for different testing scenarios
- **Logging Capability**: Option to log all output to a file for later analysis

## Installation
The tool requires root privileges to create raw sockets needed for ICMP operations.

```bash
# Clone the repository (if applicable)
git clone https://github.com/yourusername/enhanced-ping-tool.git

# Navigate to the directory
cd enhanced-ping-tool

# Compile
gcc -o ping_enhanced ping_enhanced.c

# Make executable
chmod +x ping_enhanced
```

## Usage
```
sudo ./ping_enhanced <hostname/IP> [options]
```

### Options
- `-s <size>`: Packet size (default: 64 bytes)
- `-t <ttl>`: Time to live (default: 64)
- `-c <count>`: Number of packets to send (default: infinite)
- `-i <interval>`: Wait interval in ms (default: mode dependent)
- `-w <timeout>`: Response timeout in seconds (default: 5)
- `-r <retries>`: Number of retries per packet (default: 3)
- `-m <mode>`: Experiment mode (1=standard, 2=aggressive, 3=intermittent)
- `-l <file>`: Log file name
- `-h`: Show help message

## Examples

### Basic Usage
```bash
sudo ./ping_enhanced google.com
```

### Advanced Configuration
```bash
# Send 10 packets of 128 bytes with 2 retries in aggressive mode
sudo ./ping_enhanced target.com -s 128 -r 2 -c 10 -m 2

# Send packets with custom TTL and log to file
sudo ./ping_enhanced 192.168.1.1 -t 32 -l ping_results.log

# Send packets at random intervals
sudo ./ping_enhanced server.local -m 3
```

## Output Explanation
The tool outputs details for each packet:
```
64 bytes from 192.168.1.1: icmp_seq=1 ttl=64 time=0.456 ms
```

For corrupted packets, it provides additional corruption details:
```
64 bytes from 192.168.1.1: icmp_seq=2 ttl=64 time=0.523 ms [CORRUPTED]
  Corruption details: checksum=invalid, data=valid
```

When packets time out, it shows:
```
Request timeout for icmp_seq=3 (try 1/4)
```

## Statistics
Upon completion (or when interrupted with Ctrl+C), the tool displays comprehensive statistics:
```
--- Ping Statistics ---
Total packets: 10 original, 13 including retries
Received: 9 (10.0% packet loss)
Retransmitted: 3
Received after retry: 2
Corrupted packets: 1
RTT min/avg/max = 0.456/0.534/0.789 ms
```

## Use Cases
- **Network Performance Testing**: Measure packet loss and latency under different conditions
- **Cybersecurity Testing**: Identify network vulnerability to packet corruption or manipulation
- **Throughput Analysis**: Determine optimal network parameters for specific applications
- **Network Reliability Testing**: Evaluate connection stability with intermittent pinging

## Security Considerations
- This tool requires root privileges to operate
- It can generate significant network traffic in aggressive mode
- Some networks may block or rate-limit ICMP packets

## License
[Include appropriate license information here]

## Acknowledgements
- Based on original code by Riley King
- Fixed version by Claude to address packet reception issues
