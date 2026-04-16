#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<USAGE
Usage: $0 [--host HOST] [--port PORT]

Environment variables:
  KOLIBRI_RESPONSE_MODE   Set to "llm" to enable LLM proxying (default: script)
  KOLIBRI_LLM_ENDPOINT    URL of the upstream LLM API
  KOLIBRI_LLM_API_KEY     Optional bearer token for the upstream API
  KOLIBRI_LLM_MODEL       Optional model identifier forwarded to the upstream API
  KOLIBRI_LLM_TIMEOUT     Request timeout in seconds (default: 30)
USAGE
}

host="0.0.0.0"
port="8000"

while (("$#" > 0)); do
    case "$1" in
        --host)
            host="$2"
            shift 2
            ;;
        --port)
            port="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export PYTHONPATH="$root_dir:${PYTHONPATH:-}"

exec uvicorn backend.service.main:app --host "$host" --port "$port"
