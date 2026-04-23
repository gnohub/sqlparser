#!/usr/bin/env bash

set -euo pipefail

loops=""
cli=""
fixture=""
output=""
verify=""
bins=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		--loops)
			loops="$2"
			shift 2
			;;
		--cli)
			cli="$2"
			shift 2
			;;
		--fixture)
			fixture="$2"
			shift 2
			;;
		--output)
			output="$2"
			shift 2
			;;
		--verify)
			verify="$2"
			shift 2
			;;
		*)
			bins+=("$1")
			shift
			;;
	esac
done

if [[ -z "${loops}" || -z "${cli}" || -z "${fixture}" || -z "${output}" || -z "${verify}" ]]; then
	echo "usage: run_test_loop.sh --loops N --cli BIN --fixture FILE --output FILE --verify FILE [test bins...]" >&2
	exit 1
fi

for ((round = 1; round <= loops; round++)); do
	printf 'loop %d/%d\n' "${round}" "${loops}"
	for bin_path in "${bins[@]}"; do
		if ! "${bin_path}" >/tmp/sqlparser_test_loop.out 2>&1; then
			cat /tmp/sqlparser_test_loop.out >&2
			exit 1
		fi
	done
	if ! "${cli}" --batch-file "${fixture}" --output "${output}" >/tmp/sqlparser_test_loop.out 2>&1; then
		cat /tmp/sqlparser_test_loop.out >&2
		exit 1
	fi
	if ! python3 "${verify}" --fixture "${fixture}" --output "${output}" >/tmp/sqlparser_test_loop.out 2>&1; then
		cat /tmp/sqlparser_test_loop.out >&2
		exit 1
	fi
done
