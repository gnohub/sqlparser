#!/usr/bin/env bash

set -euo pipefail

tool="valgrind"
log_dir=""
name=""
cmd=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		--tool)
			tool="$2"
			shift 2
			;;
		--log-dir)
			log_dir="$2"
			shift 2
			;;
		--name)
			name="$2"
			shift 2
			;;
		--)
			shift
			cmd=("$@")
			break
			;;
		*)
			echo "usage: run_valgrind.sh [--tool TOOL] --log-dir DIR [--name LABEL] -- command [args...]" >&2
			exit 1
			;;
	esac
done

if [[ -z "${log_dir}" || ${#cmd[@]} -eq 0 ]]; then
	echo "usage: run_valgrind.sh [--tool TOOL] --log-dir DIR [--name LABEL] -- command [args...]" >&2
	exit 1
fi

if ! command -v "${tool}" >/dev/null 2>&1; then
	echo "valgrind tool not found: ${tool}" >&2
	exit 1
fi

mkdir -p "${log_dir}"

if [[ -z "${name}" ]]; then
	name="$(basename "${cmd[0]}")"
fi

log_file="${log_dir}/${name}.log"

printf 'valgrind: %s\n' "${name}"

if ! "${tool}" \
	--leak-check=full \
	--show-leak-kinds=all \
	--errors-for-leak-kinds=definite,indirect,possible \
	--track-origins=yes \
	--error-exitcode=1 \
	--num-callers=32 \
	--log-file="${log_file}" \
	"${cmd[@]}"; then
	cat "${log_file}" >&2
	exit 1
fi

grep -E 'HEAP SUMMARY|in use at exit|total heap usage|All heap blocks were freed|ERROR SUMMARY|definitely lost|indirectly lost|possibly lost|still reachable|suppressed' "${log_file}" || true
