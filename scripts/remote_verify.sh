#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REMOTE_HOST="${REMOTE_HOST:-}"
REMOTE_PORT="${REMOTE_PORT:-22}"
REMOTE_DIR="${REMOTE_DIR:-}"
VERIFY_TARGET="${VERIFY_TARGET:-verify}"
VERIFY_LOOP="${VERIFY_LOOP:-50}"

if [[ -z "${REMOTE_HOST}" || -z "${REMOTE_DIR}" ]]; then
	echo "REMOTE_HOST and REMOTE_DIR must be set before running remote_verify.sh" >&2
	exit 1
fi

"${ROOT_DIR}/scripts/remote_sync.sh"

ssh -p "${REMOTE_PORT}" "${REMOTE_HOST}" "
	set -euo pipefail
	cd '${REMOTE_DIR}'
	make '${VERIFY_TARGET}' LOOP='${VERIFY_LOOP}'
"
