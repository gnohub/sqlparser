#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REMOTE_HOST="${REMOTE_HOST:-}"
REMOTE_PORT="${REMOTE_PORT:-22}"
REMOTE_DIR="${REMOTE_DIR:-}"
RUN_STAMP="$(date +%Y%m%d_%H%M%S)"
REMOTE_RESULTS_DIR="${REMOTE_DIR}/bench/results/${RUN_STAMP}"
LOCAL_RESULTS_DIR="${ROOT_DIR}/bench/results/${RUN_STAMP}"

if [[ -z "${REMOTE_HOST}" || -z "${REMOTE_DIR}" ]]; then
	echo "REMOTE_HOST and REMOTE_DIR must be set before running remote_bench.sh" >&2
	exit 1
fi

"${ROOT_DIR}/scripts/remote_sync.sh"

ssh -p "${REMOTE_PORT}" "${REMOTE_HOST}" "
	set -euo pipefail
	cd '${REMOTE_DIR}'
	mkdir -p ./bench/results
	make clean >/dev/null
	make all bench-build DEBUG=0 SHOW_WARNING=0
	python3 ./bench/run_benchmarks.py --output-dir '${REMOTE_RESULTS_DIR}' --bench-bin ./bin/sqlparser_bench --stages parse,api,report
"

mkdir -p "${ROOT_DIR}/bench/results"
rsync -az -e "ssh -p ${REMOTE_PORT}" "${REMOTE_HOST}:${REMOTE_RESULTS_DIR}/" "${LOCAL_RESULTS_DIR}/"

rm -f "${ROOT_DIR}/bench/results/latest"
ln -s "${RUN_STAMP}" "${ROOT_DIR}/bench/results/latest"

printf 'Benchmark results copied to %s\n' "${LOCAL_RESULTS_DIR}"
