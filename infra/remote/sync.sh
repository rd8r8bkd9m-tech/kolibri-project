#!/bin/bash
# Kolibri Remote Sync Script
# Created by Cluster-DevOps

REMOTE_HOST="ubuntu-home-wan"
REMOTE_USER="ladik"
REMOTE_PORT="2222"
REMOTE_DIR="/home/ladik/kolibri-project"

echo "🚀 Syncing Kolibri Core & Services to $REMOTE_HOST..."

# Sync core, services, infra and knowledge
rsync -avz -e "ssh -p $REMOTE_PORT -o BatchMode=yes" \
    --exclude '.git' \
    --exclude '.venv' \
    --exclude 'build' \
    --exclude 'web/node_modules' \
    ./ $REMOTE_USER@$REMOTE_HOST:$REMOTE_DIR/

echo "✅ Sync complete."
