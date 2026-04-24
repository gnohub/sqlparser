#!/usr/bin/env bash
set -euo pipefail

sanitize="${1:-}"
if [[ -z "${sanitize}" ]]; then
	echo "usage: find_sanitize_compiler.sh <sanitize-kind> <compiler>..." >&2
	exit 2
fi
shift || true

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
check_script="${script_dir}/check_sanitize_support.sh"
seen=" "

for candidate in "$@"; do
	if [[ -z "${candidate}" ]]; then
		continue
	fi

	if [[ "${candidate}" == */* ]]; then
		resolved="${candidate}"
		if [[ ! -x "${resolved}" ]]; then
			continue
		fi
	else
		resolved="$(command -v "${candidate}" 2>/dev/null || true)"
		if [[ -z "${resolved}" ]]; then
			continue
		fi
	fi

	case "${seen}" in
		*" ${resolved} "*) continue ;;
	esac
	seen="${seen}${resolved} "

	if "${check_script}" "${resolved}" "${sanitize}" >/dev/null 2>&1; then
		printf '%s\n' "${resolved}"
		exit 0
	fi
done

exit 1
