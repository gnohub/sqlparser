# v2.15.0 Release Notes

`v2.15.0` adds Linux AArch64 cross-build support and makes the Linux Jansson
dependency source-based and repository-contained. Static libraries, shared
libraries, and the CLI now use one consistent third-party build model.

## Linux AArch64 Cross Builds

- `CROSS_COMPILE` acts as the common tool prefix for `gcc`, `ar`, `ranlib`,
  `nm`, and `readelf`. An empty prefix preserves the native Linux toolchain and
  default output directories.
- `scripts/build_linux_aarch64.sh` reads
  `/opt/toolchains/aarch64-linux-gnu` by default. An alternative toolchain
  directory can be supplied through `SQLPARSER_AARCH64_TOOLCHAIN`.
- AArch64 artifacts are written to `build/linux-aarch64`,
  `bin/linux-aarch64`, and `lib/linux-aarch64`, preventing object, archive, or
  executable reuse across target architectures.
- After building and checking ABI exports, the script validates the ELF
  architecture of the shared library and CLI, the format and architecture of
  every static-archive member, and the absence of dynamic Jansson or
  `libpg_query` dependencies.

## Self-Contained Jansson

- Linux and MSVC Windows builds compile the vendored Jansson 2.15 source.
  Linux no longer requires a system Jansson package or a successful
  `pkg-config` query.
- The 13 Jansson source files are compiled as position-independent objects and
  incorporated into the static and shared libraries with the project and
  `libpg_query` objects.
- `sqlparser.pc` no longer declares `Requires.private: jansson`.
  Distributing `libsqlparser.so.0` does not require `libjansson.so`.
- The shared-library version script continues to restrict public exports.
  Jansson, `libpg_query`, and other internal symbols do not enter the public
  dynamic ABI.

## Vendor Build Isolation and Incremental Dependencies

- `libpg_query` objects, dependency files, and its archive are written under
  the top-level `BUILD_PATH`, preventing reuse of source-tree objects across
  compilers or target architectures.
- The sub-build emits `-MMD -MP` dependency files, and objects also depend on
  its Makefile. The top-level target tracks the actual sources, headers, and
  protobuf definition.
- Build signatures cover the compiler, archiver, debug mode, compiler flags,
  and vendor source set. Signature changes remove only the corresponding vendor
  output and leave other architecture-specific build directories intact.
- The ABI checker accepts an explicit `NM` tool so cross builds inspect dynamic
  symbols with the target toolchain.

## Compatibility

- Public C APIs, enums, structure layouts, and resource-ownership rules are
  unchanged.
- The shared-library SONAME remains `libsqlparser.so.0`, with 152 public
  exported symbols.
- Native Linux commands are unchanged. With an empty `CROSS_COMPILE`, `make`
  and `make test` use the native toolchain.
- Runtime dependencies of the shared library and CLI are limited to `libc`,
  `libm`, and `libpthread`; no dynamic Jansson or `libpg_query` library is
  required.

## Validation

- The Linux AArch64 cross build produced shared, static, and CLI artifacts that
  passed architecture and dependency checks. All 123 static-archive members
  were identified as AArch64.
- Native Linux AArch64 `make test` completed across nine case matrices with
  2,758 cases, 8,945 patches, and zero failures.
- Cross-built and native CLIs processed the same input successfully and emitted
  valid, byte-identical View JSON.
- The highest GLIBC symbol requirement in the cross-built shared library and
  CLI is `GLIBC_2.17`.

Vendored `libpg_query` tag: `17-6.2.2`.
Vendored Jansson version: `2.15`.
