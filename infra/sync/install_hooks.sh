#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
HOOKS_DIR="$REPO_ROOT/.git/hooks"
mkdir -p "$HOOKS_DIR"

cat > "$HOOKS_DIR/post-commit" <<HOOK
#!/usr/bin/env bash
set -euo pipefail
"$(git rev-parse --show-toplevel)/scripts/sync/post_commit_push.sh" || true
HOOK

chmod +x "$HOOKS_DIR/post-commit"

echo "Installed: $HOOKS_DIR/post-commit"
