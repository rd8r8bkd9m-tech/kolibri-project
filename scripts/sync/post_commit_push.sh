#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

BRANCH="$(git branch --show-current)"
if [[ -z "$BRANCH" ]]; then
  exit 0
fi

# Автопуш только если есть origin.
if ! git remote get-url origin >/dev/null 2>&1; then
  exit 0
fi

# Защита от рекурсии
if [[ "${KOLIBRI_AUTO_PUSH_RUNNING:-0}" == "1" ]]; then
  exit 0
fi
export KOLIBRI_AUTO_PUSH_RUNNING=1

for i in 1 2 3; do
  if git push origin "$BRANCH"; then
    exit 0
  fi
  sleep $((i * 2))
done

exit 0
