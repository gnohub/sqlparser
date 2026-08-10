#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
toolchain_root="${SQLPARSER_AARCH64_TOOLCHAIN:-/opt/toolchains/aarch64-linux-gnu}"
cross_compile="${toolchain_root}/bin/aarch64-linux-gnu-"
jobs="${JOBS:-2}"

if [[ ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
	echo "JOBS must be a positive integer: ${jobs}" >&2
	exit 2
fi

for tool in gcc ar ranlib nm objdump readelf; do
	if [[ ! -x "${cross_compile}${tool}" ]]; then
		echo "AArch64 tool is missing or not executable: ${cross_compile}${tool}" >&2
		exit 1
	fi
done

target="$("${cross_compile}gcc" -dumpmachine)"
if [[ "${target}" != "aarch64-linux-gnu" ]]; then
	echo "unexpected compiler target: ${target}" >&2
	exit 1
fi

make_args=(
	"CROSS_COMPILE=${cross_compile}"
	"BUILD_PATH=./build/linux-aarch64"
	"BIN_PATH=./bin/linux-aarch64"
	"LIB_PATH=./lib/linux-aarch64"
	"DEBUG=0"
	"SHOW_WARNING=1"
	"STRICT=1"
	"SHOW_VENDOR_WARNING=0"
)

make --no-print-directory -C "${project_root}" -j"${jobs}" "${make_args[@]}" all abi-check

shared_library="${project_root}/lib/linux-aarch64/libsqlparser.so"
static_library="${project_root}/lib/linux-aarch64/libsqlparser.a"
cli="${project_root}/bin/linux-aarch64/sqlparser_cli"

for artifact in "${shared_library}" "${cli}"; do
	machine="$("${cross_compile}readelf" -h "${artifact}" | awk -F: '/Machine:/ { value=$2; gsub(/^[[:space:]]+|[[:space:]]+$/, "", value); print value }')"
	if [[ "${machine}" != "AArch64" ]]; then
		echo "unexpected artifact architecture: ${artifact}: ${machine}" >&2
		exit 1
	fi
	dynamic_section="$("${cross_compile}readelf" -d "${artifact}")"
	if grep -Eqi '\(NEEDED\).*(jansson|pg_query)' <<< "${dynamic_section}"; then
		echo "unexpected dynamic vendor dependency: ${artifact}" >&2
		exit 1
	fi
done

archive_members="$("${cross_compile}ar" t "${static_library}")"
archive_member_count="$(awk 'NF { count++ } END { print count + 0 }' <<< "${archive_members}")"
archive_headers="$("${cross_compile}objdump" -f "${static_library}")"
archive_format_count="$(awk '$2 == "file" && $3 == "format" && $4 == "elf64-littleaarch64" { count++ } END { print count + 0 }' <<< "${archive_headers}")"
archive_arch_count="$(awk '$1 == "architecture:" && $2 ~ /^aarch64,/ { count++ } END { print count + 0 }' <<< "${archive_headers}")"

if [[ "${archive_member_count}" -eq 0 ||
	"${archive_format_count}" -ne "${archive_member_count}" ||
	"${archive_arch_count}" -ne "${archive_member_count}" ]]; then
	echo "static archive contains a non-AArch64 or unreadable member: ${static_library}" >&2
	exit 1
fi

for object in dtoa.o dump.o error.o hashtable.o hashtable_seed.o load.o memory.o \
	pack_unpack.o strbuffer.o strconv.o utf.o value.o version.o; do
	if ! grep -Fxq "${object}" <<< "${archive_members}"; then
		echo "vendored Jansson object is missing: ${object}" >&2
		exit 1
	fi
done

printf '%s\n' \
	"AArch64 build finished" \
	"  shared: ${shared_library}" \
	"  static: ${static_library}" \
	"  cli: ${cli}"
