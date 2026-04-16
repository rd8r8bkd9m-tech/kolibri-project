#!/bin/bash
# Kolibri Remote Execution Script
# Created by Cluster-DevOps

REMOTE_HOST="ubuntu-home-wan"
REMOTE_USER="ladik"
REMOTE_PORT="2222"
REMOTE_DIR="/home/ladik/kolibri-project"

COMMAND=$@

if [ -z "$COMMAND" ]; then
    COMMAND="make -C build -j8"
fi

echo "🛠 Executing on $REMOTE_HOST: $COMMAND"

ssh -p $REMOTE_PORT -o BatchMode=yes $REMOTE_USER@$REMOTE_HOST \
    "cd $REMOTE_DIR && $COMMAND"
