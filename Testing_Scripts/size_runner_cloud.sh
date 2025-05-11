#!/bin/bash
# Packet Size Ping Experiment Runner
# Configuration
LOG_DIR="speed_ping_logs_size_test_internal"
TARGET_HOST="1.1.1.1" # Using the same target as your internal test
MODE=1                # Mode to use (standard/aggressive/intermittent)
COUNT=50              # Packets per test
TIMEOUT=2             # Timeout in seconds
INTERVAL=100          # Interval of 100 as it doesn't show much imporvement past that point

mkdir -p $LOG_DIR

# Root check
if [ "$EUID" -ne 0 ]; then
  echo "This script must be run as root to create raw sockets"
  exit 1
fi

# Compile the ping tool if not already compiled
if [ ! -f "./enhanced_ping" ]; then
  echo "Compiling enhanced_ping.c..."
  gcc -o enhanced_ping enhanced_ping.c
  if [ $? -ne 0 ]; then
    echo "Failed to compile enhanced_ping.c"
    exit 1
  fi
fi

# Define size ranges to test
# Start with small sizes
SMALL_SIZES=(8 16 32 64 128 256 512)
# Medium sizes
MED_SIZES=(1024 1472 2048 4096)
# Large sizes (may exceed typical MTU)
LARGE_SIZES=(8192 16384 32768)
# Extra large sizes (will likely fragment and possibly fail)
XL_SIZES=(65507 65508 65515)

for size in "${SMALL_SIZES[@]}"; do
  log_file="$LOG_DIR/${TARGET_HOST//[.:\/]/_}_s${size}_m${MODE}_i${INTERVAL}ms.log"
  echo "Testing packet size: $size bytes..."
  ./enhanced_ping $TARGET_HOST -s $size -c $COUNT -m $MODE -w $TIMEOUT -i $INTERVAL -l $log_file

done

# Test medium sizes
for size in "${MED_SIZES[@]}"; do
  log_file="$LOG_DIR/${TARGET_HOST//[.:\/]/_}_s${size}_m${MODE}_i${INTERVAL}ms.log"
  echo "Testing packet size: $size bytes..."
  ./enhanced_ping $TARGET_HOST -s $size -c $COUNT -m $MODE -w $TIMEOUT -i $INTERVAL -l $log_file
done

# Test large sizes
echo "Testing large packet sizes (may exceed MTU)..."
for size in "${LARGE_SIZES[@]}"; do
  log_file="$LOG_DIR/${TARGET_HOST//[.:\/]/_}_s${size}_m${MODE}_i${INTERVAL}ms.log"
  echo "Testing packet size: $size bytes..."
  ./enhanced_ping $TARGET_HOST -s $size -c $COUNT -m $MODE -w $TIMEOUT -i $INTERVAL -l $log_file

done

# Test extra large sizes (may fail due to system limits)
echo "Testing extra large packet sizes (may cause errors)..."
for size in "${XL_SIZES[@]}"; do
  log_file="$LOG_DIR/${TARGET_HOST//[.:\/]/_}_s${size}_m${MODE}_i${INTERVAL}ms.log"
  echo "Testing packet size: $size bytes (may exceed system limits)..."
  ./enhanced_ping $TARGET_HOST -s $size -c $COUNT -m $MODE -w $TIMEOUT -i $INTERVAL -l $log_file

  # Check if command succeeded
  if [ $? -ne 0 ]; then
    echo "  Failed to send packets of size $size bytes - this may be expected for very large sizes"
    echo "$TARGET_HOST,$size,$MODE,$INTERVAL,0,0,0,0,0,0,100" >>$RESULTS_FILE
  fi
done
