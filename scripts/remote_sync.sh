#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REMOTE_HOST="${REMOTE_HOST:-}"
REMOTE_PORT="${REMOTE_PORT:-22}"
REMOTE_DIR="${REMOTE_DIR:-}"

if [[ -z "${REMOTE_HOST}" || -z "${REMOTE_DIR}" ]]; then
	echo "REMOTE_HOST and REMOTE_DIR must be set before running remote_sync.sh" >&2
	exit 1
fi

rsync -az --delete \
	--exclude ".git/" \
	--exclude "build/" \
	--exclude "bin/" \
	--exclude "lib/" \
	--exclude "bench/results/" \
	--exclude ".DS_Store" \
	-e "ssh -p ${REMOTE_PORT}" \
	"${ROOT_DIR}/" "${REMOTE_HOST}:${REMOTE_DIR}/"
