#!/bin/bash

# Target host from ~/.ssh/config
TARGET_HOST=${1:-"ubuntu-home-wan"}

echo "Testing connection to $TARGET_HOST in BatchMode..."

# BatchMode=yes ensures ssh won't prompt for password/passphrase
# ConnectTimeout=5 avoids long hangs
ssh -o BatchMode=yes -o ConnectTimeout=5 "$TARGET_HOST" "echo '✅ Connection to $TARGET_HOST successful! Server time: \$(date)'"

if [ $? -eq 0 ]; then
    echo "SSH Test: SUCCESS"
else
    echo "SSH Test: FAILED (Check if keys are added to ssh-agent or if the server is up)"
    exit 1
fi
