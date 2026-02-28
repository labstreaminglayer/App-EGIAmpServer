#!/bin/bash
# Test script to send a command to the AmpServer and get a response

AMP_IP="${AMP_IP:-10.10.10.51}"
CMD_PORT="${CMD_PORT:-9877}"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <command> [ampId] [channel] [value]"
    echo "Example: $0 cmd_GetAmpDetails 0 0 0"
    echo "         $0 cmd_NumberOfAmps 0 0 0"
    exit 1
fi

CMD_NAME="$1"
AMP_ID="${2:-0}"
CHANNEL="${3:-0}"
VALUE="${4:-0}"

COMMAND="(sendCommand $CMD_NAME $AMP_ID $CHANNEL $VALUE)"

echo "Sending to $AMP_IP:$CMD_PORT:"
echo "  $COMMAND"
echo ""

# Send command and receive response (timeout after 5 seconds)
RESPONSE=$(echo "$COMMAND" | nc -w 5 "$AMP_IP" "$CMD_PORT")

echo "Response:"
echo "  $RESPONSE"
