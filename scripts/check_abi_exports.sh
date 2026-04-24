#!/usr/bin/env bash

set -euo pipefail

header="./include/sqlparser/sqlparser.h"
library="./lib/libsqlparser.so"

usage() {
	echo "usage: check_abi_exports.sh [--header HEADER] [--library SHARED_LIBRARY]" >&2
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--header)
			header="$2"
			shift 2
			;;
		--library)
			library="$2"
			shift 2
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			usage
			exit 2
			;;
	esac
done

if [[ ! -f "${header}" ]]; then
	echo "ABI export check failed: header not found: ${header}" >&2
	exit 1
fi

if [[ ! -f "${library}" ]]; then
	echo "ABI export check failed: shared library not found: ${library}" >&2
	exit 1
fi

if ! command -v nm >/dev/null 2>&1; then
	echo "ABI export check failed: nm is required" >&2
	exit 1
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/sqlparser_abi_XXXXXX")"
cleanup() {
	rm -rf "${tmp_dir}"
}
trap cleanup EXIT

expected="${tmp_dir}/expected.txt"
actual="${tmp_dir}/actual.txt"
unexpected="${tmp_dir}/unexpected.txt"
missing="${tmp_dir}/missing.txt"
extra="${tmp_dir}/extra.txt"

grep -Eho '\bsqlparser_[A-Za-z0-9_]+[[:space:]]*\(' "${header}" \
	| sed -E 's/[[:space:]]*\($//' \
	| sort -u > "${expected}"

nm -D --defined-only "${library}" \
	| awk '
		{
			name = $NF
			sub(/@@.*/, "", name)
			sub(/@.*/, "", name)
			if (name == "" || name == "SQLPARSER_0") {
				next
			}
			if (name == "_init" || name == "_fini" || name == "_edata" || name == "_end" || name == "__bss_start") {
				next
			}
			if (name ~ /^sqlparser_[A-Za-z0-9_]+$/) {
				print name > actual_file
			} else {
				print name > unexpected_file
			}
		}
	' actual_file="${actual}" unexpected_file="${unexpected}"

touch "${actual}" "${unexpected}"
sort -u "${actual}" -o "${actual}"
sort -u "${unexpected}" -o "${unexpected}"

comm -23 "${expected}" "${actual}" > "${missing}"
comm -13 "${expected}" "${actual}" > "${extra}"

failed=0

if [[ -s "${unexpected}" ]]; then
	echo "ABI export check failed: unexpected non-public exported symbols:" >&2
	sed 's/^/  /' "${unexpected}" >&2
	failed=1
fi

if [[ -s "${missing}" ]]; then
	echo "ABI export check failed: public header symbols missing from shared library:" >&2
	sed 's/^/  /' "${missing}" >&2
	failed=1
fi

if [[ -s "${extra}" ]]; then
	echo "ABI export check failed: shared library exports symbols not declared in public header:" >&2
	sed 's/^/  /' "${extra}" >&2
	failed=1
fi

if [[ "${failed}" -ne 0 ]]; then
	exit 1
fi

count="$(wc -l < "${actual}" | tr -d '[:space:]')"
echo "ABI export check passed: ${count} public symbols"
