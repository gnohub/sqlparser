# v2.15.0 发布说明

`v2.15.0` 增加 Linux AArch64 交叉构建能力，并将 Linux 的 Jansson 依赖收敛为仓库内源码，使静态库、动态库和 CLI 的第三方依赖构建方式保持一致。

## Linux AArch64 交叉构建

- `CROSS_COMPILE` 作为统一工具前缀，自动选择 `gcc`、`ar`、`ranlib`、`nm` 和 `readelf`。未设置该变量时，Make 继续使用 Linux 原生工具链与默认输出目录。
- `scripts/build_linux_aarch64.sh` 默认读取 `/opt/toolchains/aarch64-linux-gnu`，并支持通过 `SQLPARSER_AARCH64_TOOLCHAIN` 指定其他工具链目录。
- AArch64 产物分别写入 `build/linux-aarch64`、`bin/linux-aarch64` 和 `lib/linux-aarch64`，不会与其他架构的对象、归档或可执行文件混用。
- 脚本完成构建和 ABI 检查后，进一步确认共享库与 CLI 的 ELF 架构、静态归档全部成员的文件格式与架构，以及是否存在 Jansson 或 `libpg_query` 动态依赖。

## 自包含 Jansson

- Linux 与 MSVC Windows 构建统一编译仓库内的 Jansson 2.15 源码，不再要求 Linux 安装系统 Jansson 或提供 `pkg-config` 查询结果。
- Jansson 的 13 个源文件直接编译为位置无关对象，并随项目对象与 `libpg_query` 一起进入静态库和动态库。
- `sqlparser.pc` 不再声明 `Requires.private: jansson`。链接 `libsqlparser.so.0` 不需要额外分发 `libjansson.so`。
- 共享库版本脚本继续限制公开导出，Jansson、`libpg_query` 及其他内部符号不会进入公开动态 ABI。

## Vendor 构建隔离与增量依赖

- `libpg_query` 的对象、依赖文件和归档移至顶层 `BUILD_PATH`，避免不同编译器或目标架构复用源码目录中的旧对象。
- 子构建生成 `-MMD -MP` 依赖文件，对象同时依赖其 Makefile；顶层目标跟踪实际源码、头文件和 protobuf 定义。
- 构建签名覆盖编译器、归档器、调试模式、编译参数及 vendor 源码集合。签名变化只清理对应 vendor 输出，不影响其他架构的构建目录。
- ABI 检查支持显式 `NM` 工具，使交叉构建使用目标工具链读取动态符号。

## 兼容性

- 公共 C API、枚举、结构体布局和资源所有权规则保持不变。
- 动态库 SONAME 仍为 `libsqlparser.so.0`，公开导出保持 152 个符号。
- Linux 原生构建命令保持不变；`CROSS_COMPILE` 为空时，`make` 和 `make test` 使用本机工具链。
- 动态库和 CLI 的运行时依赖仅包含 `libc`、`libm` 和 `libpthread`，不包含 Jansson 或 `libpg_query` 动态库。

## 验证

- Linux AArch64 交叉构建生成的共享库、静态库和 CLI 均通过架构与依赖检查；静态归档的 123 个成员全部确认为 AArch64。
- Linux AArch64 原生 `make test` 完成，九套 case matrix 共 2,758 条用例和 8,945 个 patch，失败数均为 0。
- 交叉构建与原生构建生成的 CLI 均成功处理相同输入，输出为有效且逐字节一致的 View JSON。
- 交叉构建的共享库和 CLI 最高 GLIBC 符号需求为 `GLIBC_2.17`。

内置 `libpg_query` 标签：`17-6.2.2`。
内置 Jansson 版本：`2.15`。
