#!/usr/bin/env bash

set -euo pipefail

compiler="${1:-}"
sanitize="${2:-}"
tmp_c="$(mktemp /tmp/sqlparser_sanitize_XXXXXX.c)"
tmp_bin="$(mktemp /tmp/sqlparser_sanitize_XXXXXX.bin)"

cleanup() {
	rm -f "${tmp_c}" "${tmp_bin}"
}
trap cleanup EXIT

if [[ -z "${compiler}" || -z "${sanitize}" ]]; then
	echo "usage: check_sanitize_support.sh <compiler> <sanitize-kind>" >&2
	exit 2
fi

cat >"${tmp_c}" <<'EOF'
int main(void)
{
	return 0;
}
EOF

"${compiler}" -std=gnu11 -fno-omit-frame-pointer -fsanitize="${sanitize}" "${tmp_c}" -o "${tmp_bin}" >/dev/null 2>&1
