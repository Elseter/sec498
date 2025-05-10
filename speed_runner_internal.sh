#!/bin/bash
# Fine-Interval Ping Rate Experiment Runner

# Configuration
LOG_DIR="speed_ping_logs_internal_host_5000"
TARGET_HOST="192.168.122.34" # Change to your target: localhost, VM, or cloud
PACKET_SIZE=512              # Single packet size to test
MODE=1                       # Mode to use (standard/aggressive/intermittent)
COUNT=50                     # Packets per test
TIMEOUT=2                    # Timeout in seconds

mkdir -p $LOG_DIR

# Root check
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

# Header for results
echo "Target,PacketSize,Mode,IntervalMs,SentOriginal,SentTotal,Received,Retransmitted,ReceivedAfterRetry,Corrupted,PacketLoss" >$RESULTS_FILE

# Metric extraction
extract_metrics() {
  local log_file=$1
  sent_original=$(grep "Total packets:" $log_file | awk '{print $3}' | tr -d ',')
  sent_total=$(grep "Total packets:" $log_file | awk '{print $5}')
  received=$(grep "Received:" $log_file | awk '{print $2}' | tr -d '(')
  retransmitted=$(grep "Retransmitted:" $log_file | awk '{print $2}')
  rereceived=$(grep "Received after retry:" $log_file | awk '{print $4}')
  corrupted=$(grep "Corrupted packets:" $log_file | awk '{print $3}')

  if [ -n "$sent_original" ] && [ "$sent_original" -gt 0 ]; then
    packet_loss=$(echo "scale=2; ($sent_original - $received) * 100 / $sent_original" | bc)
  else
    packet_loss="N/A"
  fi

  echo "$sent_original,$sent_total,$received,$retransmitted,$rereceived,$corrupted,$packet_loss"
}

# Run decreasing intervals (200 ms down to 0 ms, step 1 ms)
for ((ms = 200; ms >= 0; ms -= 2)); do
  interval=$(awk "BEGIN { printf \"%.3f\", $ms / 1000 }")
  log_file="$LOG_DIR/${TARGET_HOST//[.:\/]/_}_s${PACKET_SIZE}_m${MODE}_i${ms}ms.log"

  echo "Running interval $interval sec ($ms ms)..."
  ./enhanced_ping $TARGET_HOST -s $PACKET_SIZE -c $COUNT -m $MODE -w $TIMEOUT -i $ms -l $log_file

done

echo "Experiment finished. Results in $RESULTS_FILE"
