#!/usr/bin/env bash

set -euo pipefail

TARGET="${1:-all}"
REMOTE_HOST="${REMOTE_HOST:-}"
REMOTE_PORT="${REMOTE_PORT:-22}"
REMOTE_DIR="${REMOTE_DIR:-}"

if [[ -z "${REMOTE_HOST}" || -z "${REMOTE_DIR}" ]]; then
	echo "REMOTE_HOST and REMOTE_DIR must be set before running remote_build.sh" >&2
	exit 1
fi

case "${TARGET}" in
	*[!A-Za-z0-9_.-]*)
		echo "unsupported make target: ${TARGET}" >&2
		exit 1
		;;
esac

ssh -p "${REMOTE_PORT}" "${REMOTE_HOST}" \
	"cd '${REMOTE_DIR}' && make '${TARGET}'"
