#!/bin/bash
# Network Throughput Experiment Runner
# This script runs ping tests across different scenarios and configurations

# Configuration Variables
LOG_DIR="ping_logs"
RESULTS_FILE="experiment_results.csv"
LOCAL_HOST="localhost"     # Replace with actual local host IP if needed
VM_HOST="192.168.64.20"    # Fill in with your VM IP
CLOUD_HOST="1.1.1.1"       # Default cloud host (Cloudflare DNS)
PACKET_SIZES=(64 512 1472) # Small, Medium, Max size for standard Ethernet
MODES=(1 2 3)              # 1=Standard, 2=Aggressive, 3=Intermittent
COUNT=100                  # Number of packets per test
TIMEOUT=2                  # Timeout in seconds

# Create log directory if it doesn't exist
mkdir -p $LOG_DIR

# Check if the script is run as root
if [ "$EUID" -ne 0 ]; then
  echo "This script must be run as root to create raw sockets"
  exit 1
fi

# Compile the ping tool
gcc -o enhanced_ping enhanced_ping.c
if [ $? -ne 0 ]; then
  echo "Failed to compile enhanced_ping.c"
  exit 1
fi

# Initialize results file with header
echo "Scenario,Target,PacketSize,Mode,SentOriginal,SentTotal,Received,Retransmitted,ReceivedAfterRetry,Corrupted,PacketLoss" >$RESULTS_FILE

# Function to extract metrics from log file
extract_metrics() {
  local log_file=$1

  # Extract packet counts
  sent_original=$(grep "Total packets:" $log_file | awk '{print $3}' | tr -d ',')
  sent_total=$(grep "Total packets:" $log_file | awk '{print $5}')
  received=$(grep "Received:" $log_file | awk '{print $2}' | tr -d '(')
  retransmitted=$(grep "Retransmitted:" $log_file | awk '{print $2}')
  rereceived=$(grep "Received after retry:" $log_file | awk '{print $4}')
  corrupted=$(grep "Corrupted packets:" $log_file | awk '{print $3}')

  # Calculate packet loss
  if [ -n "$sent_original" ] && [ "$sent_original" -gt 0 ]; then
    packet_loss=$(echo "scale=2; ($sent_original - $received) * 100 / $sent_original" | bc)
  else
    packet_loss="N/A"
  fi

  # Output CSV format
  echo "$sent_original,$sent_total,$received,$retransmitted,$rereceived,$corrupted,$packet_loss"
}

# Function to run a single test
run_test() {
  local scenario=$1
  local target=$2
  local size=$3
  local mode=$4

  echo "Running test: $scenario to $target with packet size $size and mode $mode"

  # Create log file name
  log_file="$LOG_DIR/${scenario}_${target//[.:\/]/_}_s${size}_m${mode}.log"

  # Run ping command with appropriate parameters
  ./enhanced_ping $target -s $size -c $COUNT -m $mode -w $TIMEOUT -l $log_file

  # Process results
  metrics=$(extract_metrics $log_file)
  echo "$scenario,$target,$size,$mode,$metrics" >>$RESULTS_FILE

  echo "Test completed. Log saved to $log_file"
  echo "----------------------------------------"
}

# Check VM host is configured
if [ -z "$VM_HOST" ]; then
  echo "Please edit this script to set VM_HOST to your VM's IP address"
  exit 1
fi

# Run tests
echo "Starting ping throughput experiment..."

# Scenario 1: Local host to VM
echo "Scenario 1: Local host to VM"
for size in "${PACKET_SIZES[@]}"; do
  for mode in "${MODES[@]}"; do
    run_test "LocalToVM" $VM_HOST $size $mode
  done
done

# Scenario 2: Local network hosts
echo "Scenario 2: Local network hosts"
for size in "${PACKET_SIZES[@]}"; do
  for mode in "${MODES[@]}"; do
    run_test "LocalNetwork" $LOCAL_HOST $size $mode
  done
done

# Scenario 3: Local to cloud
echo "Scenario 3: Local to cloud"
for size in "${PACKET_SIZES[@]}"; do
  for mode in "${MODES[@]}"; do
    run_test "LocalToCloud" $CLOUD_HOST $size $mode
  done
done

echo "Experiment completed!"
echo "Results saved to $RESULTS_FILE"

# Generate summary
echo "Summary of results:"
echo "==================="
echo "Average packet loss by scenario:"
for scenario in "LocalToVM" "LocalNetwork" "LocalToCloud"; do
  avg=$(grep $scenario $RESULTS_FILE | awk -F, '{sum+=$NF; count++} END {print sum/count}')
  echo "$scenario: $avg%"
done

echo "Average packet loss by packet size:"
for size in "${PACKET_SIZES[@]}"; do
  avg=$(grep ",$size," $RESULTS_FILE | awk -F, '{sum+=$NF; count++} END {print sum/count}')
  echo "Size $size: $avg%"
done

echo "Complete logs are available in the $LOG_DIR directory"
