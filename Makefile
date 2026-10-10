CONFIG := ./config/Makefile.config
include $(CONFIG)

.DEFAULT_GOAL := all

VERSION_STRING := $(strip $(shell tr -d '\r\n' < ./VERSION 2>/dev/null))

ifeq ($(VERSION_STRING),)
$(error VERSION file is missing or empty)
endif

rwildcard = $(strip \
	$(foreach d,$(wildcard $(1)/*),$(call rwildcard,$(d),$(2))) \
	$(filter $(subst *,%,$(2)),$(wildcard $(1)/$(2))) \
)

ALL_SRC := $(sort $(call rwildcard,src,*.c))
UNIT_TEST_SRC := $(sort $(call rwildcard,tests/unit,*.c))
EXAMPLE_SRC := $(sort $(call rwildcard,examples,*.c))

ifeq ($(strip $(ALL_SRC)),)
$(error No C sources found under src)
endif

VENDOR_PG_QUERY_DIR := ./vendor/libpg_query
VENDOR_PG_QUERY_BUILD_DIR := $(BUILD_PATH)/vendor/libpg_query/build
VENDOR_PG_QUERY_LIB := $(VENDOR_PG_QUERY_BUILD_DIR)/libpg_query.a
VENDOR_PG_QUERY_MERGE_DIR := $(BUILD_PATH)/vendor/libpg_query/merge
VENDOR_PG_QUERY_STAMP := $(VENDOR_PG_QUERY_MERGE_DIR)/.merged.stamp
VENDOR_PG_QUERY_SRC_FILES := $(sort \
	$(wildcard $(VENDOR_PG_QUERY_DIR)/src/*.c) \
	$(wildcard $(VENDOR_PG_QUERY_DIR)/src/postgres/*.c) \
	$(VENDOR_PG_QUERY_DIR)/vendor/protobuf-c/protobuf-c.c \
	$(VENDOR_PG_QUERY_DIR)/vendor/xxhash/xxhash.c \
	$(VENDOR_PG_QUERY_DIR)/protobuf/pg_query.pb-c.c)
VENDOR_PG_QUERY_HEADER_FILES := $(sort \
	$(call rwildcard,$(VENDOR_PG_QUERY_DIR),*.h) \
	$(call rwildcard,$(VENDOR_PG_QUERY_DIR),*.inc))
VENDOR_PG_QUERY_INPUTS := \
	$(VENDOR_PG_QUERY_DIR)/Makefile \
	$(VENDOR_PG_QUERY_SRC_FILES) \
	$(VENDOR_PG_QUERY_HEADER_FILES) \
	$(VENDOR_PG_QUERY_DIR)/protobuf/pg_query.proto
BUILD_SIGNATURE_FILE := $(BUILD_PATH)/.build_signature
VENDOR_BUILD_SIGNATURE_FILE := $(BUILD_PATH)/vendor/.vendor_build_signature
JANSSON_DIR := ./vendor/jansson
JANSSON_SRC_DIR := $(JANSSON_DIR)/src
JANSSON_OBJ_PATH := $(BUILD_PATH)/vendor/jansson
JANSSON_SRC := $(sort $(wildcard $(JANSSON_SRC_DIR)/*.c))
JANSSON_OBJ_FILES := $(patsubst \
	$(JANSSON_SRC_DIR)/%.c,$(JANSSON_OBJ_PATH)/%.o,$(JANSSON_SRC))

OBJ_FILES := $(foreach src,$(ALL_SRC),$(OBJ_PATH)/$(patsubst src/%,%,$(src:.c=.o)))
DEP_FILES := $(OBJ_FILES:.o=.d) $(JANSSON_OBJ_FILES:.o=.d)
UNIT_TEST_BINS := $(foreach src,$(UNIT_TEST_SRC),$(BIN_PATH)/$(notdir $(src:.c=)))
CASE_MATRIX_BINS := $(filter %_case_matrix,$(UNIT_TEST_BINS))
CASE_RUNNER_HEADER := ./tests/unit/sqlparser_case_runner.h
EXAMPLE_BINS := $(patsubst examples/%.c,$(BIN_PATH)/examples/%,$(EXAMPLE_SRC))
TEST_API_SMOKE_BIN := $(BIN_PATH)/test_api_smoke
TEST_CORE_API_BIN := $(BIN_PATH)/test_core_api
TEST_CASE_MATRIX_BIN := $(BIN_PATH)/test_api_case_matrix
SQLPARSER_CLI_BIN := $(BIN_PATH)/sqlparser_cli
SQLPARSER_BENCH_BIN := $(BIN_PATH)/sqlparser_bench
LIBPG_QUERY_BASELINE_BIN := $(BIN_PATH)/libpg_query_baseline
SQLPARSER_CLI_BATCH_FIXTURE := ./tests/cases/sql_batch_input.json
SQLPARSER_CLI_BATCH_OUTPUT := $(BUILD_PATH)/tests/sqlparser_cli_batch_output.json
SQLPARSER_CLI_BATCH_VERIFY := ./tests/verify_cli_batch.py
INSTALL_SMOKE_SRC := ./tests/install/install_smoke.c
INSTALL_SMOKE_BIN := $(BIN_PATH)/install_smoke
SQLPARSER_BENCH_WRAP_LDFLAGS := \
	-Wl,--wrap=malloc \
	-Wl,--wrap=calloc \
	-Wl,--wrap=realloc \
	-Wl,--wrap=free \
	-Wl,--wrap=strdup \
	-Wl,--wrap=strndup

BENCH_PYTHON ?= python3
BENCH_OUTPUT_DIR ?= $(BUILD_PATH)/bench
BENCH_PROFILE ?= smoke
BENCH_STAGES ?= all
TEST_STAGE_DIR ?= $(BUILD_PATH)/stage
LOOP ?= 50
VERIFY_SANITIZE_CC_CANDIDATES ?= \
	$(CC) \
	gcc \
	cc \
	/opt/rh/devtoolset-11/root/usr/bin/gcc \
	/opt/rh/devtoolset-10/root/usr/bin/gcc \
	/opt/rh/devtoolset-9/root/usr/bin/gcc \
	/opt/rh/devtoolset-8/root/usr/bin/gcc
VERIFY_ASAN_CC ?=
VERIFY_UBSAN_CC ?=
SANITIZE_SUPPORT_CHECK := ./scripts/check_sanitize_support.sh
SANITIZE_COMPILER_FIND := ./scripts/find_sanitize_compiler.sh
VERIFY_VALGRIND_TOOL ?= valgrind
VALGRIND_RUNNER := ./scripts/run_valgrind.sh
VALGRIND_LOG_DIR ?= $(BUILD_PATH)/valgrind
ABI_EXPORT_CHECKER := ./scripts/check_abi_exports.sh
ABI_HEADER ?= ./include/sqlparser/sqlparser.h
ABI_LIBRARY ?= $(SHARED_LIB_PATH)
DIST_NAME := $(LIB_NAME)-$(VERSION_STRING)
DIST_DIR := $(BUILD_PATH)/dist
DIST_TARBALL := $(DIST_DIR)/$(DIST_NAME).tar.gz
DIST_SOURCE_PATHS := \
	./Makefile.msvc \
	./RELEASE_NOTES.md \
	./RELEASE_NOTES.en.md \
	./.github \
	./bench \
	./CHANGELOG.en.md \
	./CHANGELOG.md \
	./config \
	./doc \
	./examples \
	./include \
	./LICENSE \
	./Makefile \
	./README.en.md \
	./README.md \
	./scripts \
	./src \
	./tests \
	./THIRD_PARTY_NOTICES.md \
	./tools \
	./vendor \
	./VERSION

STATIC_LIB_PATH := $(LIB_PATH)/lib$(LIB_NAME).a
SHARED_LIB_SONAME := lib$(LIB_NAME).so.$(SONAME_MAJOR)
SHARED_LIB_REAL_PATH := $(LIB_PATH)/$(SHARED_LIB_SONAME)
SHARED_LIB_PATH := $(LIB_PATH)/lib$(LIB_NAME).so
PKGCONFIG_FILE := $(PKGCONFIG_BUILD_DIR)/$(LIB_NAME).pc

BASE_CPPFLAGS := \
	-I./include \
	-I./src/internal \
	-I./vendor/libpg_query \
	-I./vendor/libpg_query/vendor \
	-I$(JANSSON_SRC_DIR) \
	-DSQLPARSER_VERSION_TEXT=\"$(VERSION_STRING)\" \
	-DSQLPARSER_LIBPG_QUERY_TAG_TEXT=\"$(VENDOR_PG_QUERY_TAG)\" \
	$(PTHREAD_CFLAGS)

BASE_CFLAGS := -std=gnu11 -fPIC
BASE_LDFLAGS :=
BASE_LDLIBS := $(PTHREAD_LDLIBS) -lm

VENDOR_BUILD_CFLAGS := -std=gnu11 -fPIC

ifeq ($(DEBUG),1)
	BASE_CFLAGS += -g -O0
	VENDOR_BUILD_CFLAGS += -g -O0
else
	BASE_CFLAGS += -O2
	VENDOR_BUILD_CFLAGS += -O2
endif

ifeq ($(SHOW_WARNING),1)
	BASE_CFLAGS += -Wall -Wextra -Wpedantic
else
	BASE_CFLAGS += -w
endif

SHOW_VENDOR_WARNING ?= $(SHOW_WARNING)

ifeq ($(SHOW_VENDOR_WARNING),1)
	VENDOR_BUILD_CFLAGS += -Wall -Wextra -Wpedantic
else
	VENDOR_BUILD_CFLAGS += -w
endif

ifeq ($(STRICT),1)
	BASE_CFLAGS += -Werror
endif

ifneq ($(strip $(SANITIZE)),)
	BASE_CFLAGS += -fno-omit-frame-pointer -fsanitize=$(SANITIZE)
	BASE_LDFLAGS += -fno-omit-frame-pointer -fsanitize=$(SANITIZE)
	VENDOR_BUILD_CFLAGS += -fno-omit-frame-pointer -fsanitize=$(SANITIZE)
endif

CPPFLAGS := $(BASE_CPPFLAGS) $(EXTRA_CPPFLAGS)
CFLAGS := $(BASE_CFLAGS) $(EXTRA_CFLAGS)
LDFLAGS := $(BASE_LDFLAGS) $(EXTRA_LDFLAGS)
LDLIBS := $(BASE_LDLIBS) $(EXTRA_LDLIBS)

.PHONY: \
	all prep vendor static shared clean vendor-clean print-config test install cli bench-build \
	libpg-query-baseline-build libpg-query-baseline \
	test-cli-batch examples install-smoke bench-smoke test-loop verify verify-release verify-debug \
	verify-asan verify-ubsan verify-valgrind verify-ci abi-check test-unit test-examples test-cli-arg-order \
	test-parse test-inspect test-rewrite test-deparse test-view-json test-cli test-install \
	test-abi dist FORCE

all: prep static shared cli
	@echo "Build finished: $(STATIC_LIB_PATH) $(SHARED_LIB_PATH) $(SQLPARSER_CLI_BIN)"

prep:
	@mkdir -p \
		$(OBJ_PATH) \
		$(BIN_PATH) \
		$(LIB_PATH) \
		$(PKGCONFIG_BUILD_DIR) \
		$(VENDOR_PG_QUERY_MERGE_DIR) \
		$(JANSSON_OBJ_PATH)

vendor: $(VENDOR_PG_QUERY_LIB) $(JANSSON_OBJ_FILES)

static: $(STATIC_LIB_PATH)

shared: $(SHARED_LIB_PATH)

cli: $(SQLPARSER_CLI_BIN)

examples: $(EXAMPLE_BINS)

bench-build: $(SQLPARSER_BENCH_BIN)

libpg-query-baseline-build: $(LIBPG_QUERY_BASELINE_BIN)

test: cli test-unit test-examples test-cli

test-unit: $(UNIT_TEST_BINS)
	@failed=0; \
	for test_bin in $(UNIT_TEST_BINS); do \
		"$$test_bin" || failed=1; \
	done; \
	exit $$failed

test-examples: $(EXAMPLE_BINS)
	@set -e; \
	for test_bin in $(EXAMPLE_BINS); do \
		"$$test_bin"; \
	done

test-cli: test-cli-batch test-cli-arg-order

test-parse: $(TEST_API_SMOKE_BIN) $(TEST_CASE_MATRIX_BIN)
	@$(TEST_API_SMOKE_BIN)
	@$(TEST_CASE_MATRIX_BIN)

test-inspect: $(TEST_API_SMOKE_BIN) $(TEST_CORE_API_BIN) $(TEST_CASE_MATRIX_BIN)
	@$(TEST_API_SMOKE_BIN)
	@$(TEST_CORE_API_BIN)
	@$(TEST_CASE_MATRIX_BIN)

test-rewrite: $(TEST_CORE_API_BIN)
	@$(TEST_CORE_API_BIN)

test-deparse: $(TEST_CORE_API_BIN) $(CASE_MATRIX_BINS)
	@failed=0; \
	$(TEST_CORE_API_BIN) || failed=1; \
	for test_bin in $(CASE_MATRIX_BINS); do \
		"$$test_bin" || failed=1; \
	done; \
	exit $$failed

test-view-json: $(TEST_CORE_API_BIN)
	@$(TEST_CORE_API_BIN)

test-install: install-smoke

test-abi: abi-check

test-cli-batch: $(SQLPARSER_CLI_BIN) $(SQLPARSER_CLI_BATCH_FIXTURE) $(SQLPARSER_CLI_BATCH_VERIFY) | prep
	@mkdir -p $(BUILD_PATH)/tests
	@$(SQLPARSER_CLI_BIN) --batch-file $(SQLPARSER_CLI_BATCH_FIXTURE) --output $(SQLPARSER_CLI_BATCH_OUTPUT)
	@$(BENCH_PYTHON) $(SQLPARSER_CLI_BATCH_VERIFY) \
		--fixture $(SQLPARSER_CLI_BATCH_FIXTURE) \
		--output $(SQLPARSER_CLI_BATCH_OUTPUT)
	@printf '%s\n' '{"sqls":["SELECT 1"]}' > $(BUILD_PATH)/tests/cli_batch_sqls_rejected.json
	@if $(SQLPARSER_CLI_BIN) --batch-file $(BUILD_PATH)/tests/cli_batch_sqls_rejected.json > $(BUILD_PATH)/tests/cli_batch_sqls_rejected.out 2> $(BUILD_PATH)/tests/cli_batch_sqls_rejected.err; then \
		echo "cli batch must reject obsolete sqls root"; \
		exit 1; \
	fi

test-cli-arg-order: $(SQLPARSER_CLI_BIN)
	@mkdir -p $(BUILD_PATH)/tests
	@$(SQLPARSER_CLI_BIN) --mode view "INSERT INTO users (username, email, age) VALUES (?, ?, ?);" --dialect oracle > $(BUILD_PATH)/tests/cli_arg_order_view.json
	@$(SQLPARSER_CLI_BIN) --mode view "ALTER SESSION SET CURRENT_SCHEMA=APP" --dialect vastbase-oracle > $(BUILD_PATH)/tests/cli_arg_order_vastbase_view.json
	@$(BENCH_PYTHON) -m json.tool < $(BUILD_PATH)/tests/cli_arg_order_view.json >/dev/null
	@$(BENCH_PYTHON) -m json.tool < $(BUILD_PATH)/tests/cli_arg_order_vastbase_view.json >/dev/null
	@if grep -q '^==' $(BUILD_PATH)/tests/cli_arg_order_view.json; then \
		echo "cli view output must be plain JSON"; \
		exit 1; \
	fi
	@if grep -q '^==' $(BUILD_PATH)/tests/cli_arg_order_vastbase_view.json; then \
		echo "cli Vastbase view output must be plain JSON"; \
		exit 1; \
	fi
	@$(SQLPARSER_CLI_BIN) --mode deparse "SELECT 1" > $(BUILD_PATH)/tests/cli_deparse.txt
	@if grep -q '^==' $(BUILD_PATH)/tests/cli_deparse.txt; then \
		echo "cli deparse output must be plain SQL"; \
		exit 1; \
	fi

install-smoke: all $(PKGCONFIG_FILE) $(INSTALL_SMOKE_SRC) | prep
	@rm -rf $(TEST_STAGE_DIR)
	@$(MAKE) --no-print-directory install PREFIX=$(abspath $(TEST_STAGE_DIR)) DEBUG=$(DEBUG) SHOW_WARNING=$(SHOW_WARNING) STRICT=$(STRICT) SANITIZE="$(SANITIZE)" SHOW_VENDOR_WARNING=$(SHOW_VENDOR_WARNING)
	@mkdir -p $(dir $(INSTALL_SMOKE_BIN))
	@$(CC) -std=gnu11 $(PTHREAD_CFLAGS) \
		-DSQLPARSER_EXPECTED_VERSION_TEXT=\"$(VERSION_STRING)\" \
		-I$(abspath $(TEST_STAGE_DIR))/include \
		$(INSTALL_SMOKE_SRC) \
		-L$(abspath $(TEST_STAGE_DIR))/lib \
		-Wl,-rpath,$(abspath $(TEST_STAGE_DIR))/lib \
		-l$(LIB_NAME) $(PTHREAD_LDLIBS) -lm \
		-o $(INSTALL_SMOKE_BIN)
	@$(INSTALL_SMOKE_BIN)

bench-smoke: bench-build
	@$(BENCH_PYTHON) ./bench/run_benchmarks.py \
		--output-dir $(BENCH_OUTPUT_DIR) \
		--bench-bin $(SQLPARSER_BENCH_BIN) \
		--profile $(BENCH_PROFILE) \
		--stages $(BENCH_STAGES)

libpg-query-baseline: libpg-query-baseline-build
	@$(BENCH_PYTHON) ./bench/run_libpg_query_baseline.py \
		--output-dir $(BENCH_OUTPUT_DIR)/libpg_query_baseline \
		--baseline-bin $(LIBPG_QUERY_BASELINE_BIN) \
		--cc "$(CC)" \
		--profile $(BENCH_PROFILE)

dist:
	@mkdir -p $(DIST_DIR)
	@rm -f $(DIST_TARBALL)
	@tar -czf $(DIST_TARBALL) \
		--transform='s|^\./|$(DIST_NAME)/|' \
		--exclude='*.a' \
		--exclude='*.o' \
		--exclude='*.orig' \
		--exclude='*.so' \
		--exclude='.DS_Store' \
		--exclude='./bench/results' \
		--exclude='./vendor/libpg_query/.deps' \
		--exclude='./vendor/libpg_query/tmp' \
		$(DIST_SOURCE_PATHS)
	@echo "Source package: $(DIST_TARBALL)"

abi-check: shared
	@$(ABI_EXPORT_CHECKER) --header $(ABI_HEADER) --library $(ABI_LIBRARY) --nm "$(NM)"

test-loop: cli $(UNIT_TEST_BINS) $(EXAMPLE_BINS) test-cli-batch
	@./scripts/run_test_loop.sh \
		--loops $(LOOP) \
		--cli $(SQLPARSER_CLI_BIN) \
		--fixture $(SQLPARSER_CLI_BATCH_FIXTURE) \
		--output $(SQLPARSER_CLI_BATCH_OUTPUT) \
		--verify $(SQLPARSER_CLI_BATCH_VERIFY) \
		$(UNIT_TEST_BINS) \
		$(EXAMPLE_BINS)

verify-release:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory all test install-smoke DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0

verify-debug:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory all test DEBUG=1 SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0

verify-asan:
	@$(MAKE) --no-print-directory clean
	@verify_cc="$(VERIFY_ASAN_CC)"; \
	if [ -z "$$verify_cc" ]; then \
		verify_cc="$$($(SANITIZE_COMPILER_FIND) address $(VERIFY_SANITIZE_CC_CANDIDATES) 2>/dev/null || true)"; \
	fi; \
	if [ -n "$$verify_cc" ] && $(SANITIZE_SUPPORT_CHECK) "$$verify_cc" address >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory all test CC="$$verify_cc" DEBUG=1 SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0 SANITIZE=address; \
	else \
		echo "skip verify-asan: compiler/runtime does not support -fsanitize=address"; \
	fi

verify-ubsan:
	@$(MAKE) --no-print-directory clean
	@verify_cc="$(VERIFY_UBSAN_CC)"; \
	if [ -z "$$verify_cc" ]; then \
		verify_cc="$$($(SANITIZE_COMPILER_FIND) undefined $(VERIFY_SANITIZE_CC_CANDIDATES) 2>/dev/null || true)"; \
	fi; \
	if [ -n "$$verify_cc" ] && $(SANITIZE_SUPPORT_CHECK) "$$verify_cc" undefined >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory all test CC="$$verify_cc" DEBUG=1 SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0 SANITIZE=undefined; \
	else \
		echo "skip verify-ubsan: compiler/runtime does not support -fsanitize=undefined"; \
	fi

verify-valgrind:
	@$(MAKE) --no-print-directory clean
	@command -v "$(VERIFY_VALGRIND_TOOL)" >/dev/null 2>&1 || \
		{ echo "verify-valgrind: valgrind is not installed" >&2; exit 1; }
	@$(MAKE) --no-print-directory all test install-smoke DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0
	@failed=0; \
	for test_bin in $(UNIT_TEST_BINS) $(EXAMPLE_BINS); do \
		$(VALGRIND_RUNNER) --tool "$(VERIFY_VALGRIND_TOOL)" --log-dir "$(VALGRIND_LOG_DIR)" --name "$$(basename "$$test_bin")" -- "$$test_bin" || failed=1; \
	done; \
	$(VALGRIND_RUNNER) --tool "$(VERIFY_VALGRIND_TOOL)" --log-dir "$(VALGRIND_LOG_DIR)" --name "sqlparser_cli_batch" -- \
		$(SQLPARSER_CLI_BIN) --batch-file $(SQLPARSER_CLI_BATCH_FIXTURE) --output $(SQLPARSER_CLI_BATCH_OUTPUT) || failed=1; \
	$(BENCH_PYTHON) $(SQLPARSER_CLI_BATCH_VERIFY) \
		--fixture $(SQLPARSER_CLI_BATCH_FIXTURE) \
		--output $(SQLPARSER_CLI_BATCH_OUTPUT) || failed=1; \
	$(VALGRIND_RUNNER) --tool "$(VERIFY_VALGRIND_TOOL)" --log-dir "$(VALGRIND_LOG_DIR)" --name "install_smoke" -- \
		$(INSTALL_SMOKE_BIN) || failed=1; \
	exit $$failed

verify: verify-release verify-debug verify-asan verify-ubsan verify-valgrind
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory test-loop LOOP=$(LOOP) DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory bench-smoke BENCH_PROFILE=smoke BENCH_STAGES=parse,api,report DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0

verify-ci:
	@$(MAKE) --no-print-directory verify-release
	@$(MAKE) --no-print-directory abi-check DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0
	@$(MAKE) --no-print-directory verify-debug

install: all $(PKGCONFIG_FILE)
	@mkdir -p $(DESTDIR)$(INCLUDEDIR)/sqlparser $(DESTDIR)$(LIBDIR) $(DESTDIR)$(PKGCONFIGDIR)
	@cp include/sqlparser/*.h $(DESTDIR)$(INCLUDEDIR)/sqlparser/
	@cp $(STATIC_LIB_PATH) $(DESTDIR)$(LIBDIR)/
	@cp $(SHARED_LIB_REAL_PATH) $(DESTDIR)$(LIBDIR)/
	@ln -sf $(notdir $(SHARED_LIB_REAL_PATH)) $(DESTDIR)$(LIBDIR)/$(notdir $(SHARED_LIB_PATH))
	@cp $(PKGCONFIG_FILE) $(DESTDIR)$(PKGCONFIGDIR)/

print-config:
	@echo "PROJECT_NAME=$(PROJECT_NAME)"
	@echo "LIB_NAME=$(LIB_NAME)"
	@echo "VERSION=$(VERSION_STRING)"
	@echo "CROSS_COMPILE=$(CROSS_COMPILE)"
	@echo "CC=$(CC)"
	@echo "AR=$(AR)"
	@echo "RANLIB=$(RANLIB)"
	@echo "NM=$(NM)"
	@echo "READELF=$(READELF)"
	@echo "BUILD_PATH=$(BUILD_PATH)"
	@echo "BIN_PATH=$(BIN_PATH)"
	@echo "LIB_PATH=$(LIB_PATH)"
	@echo "STRICT=$(STRICT)"
	@echo "SANITIZE=$(SANITIZE)"
	@echo "SHOW_VENDOR_WARNING=$(SHOW_VENDOR_WARNING)"
	@echo "TEST_STAGE_DIR=$(TEST_STAGE_DIR)"
	@echo "BENCH_PROFILE=$(BENCH_PROFILE)"
	@echo "VERIFY_VALGRIND_TOOL=$(VERIFY_VALGRIND_TOOL)"
	@echo "VALGRIND_LOG_DIR=$(VALGRIND_LOG_DIR)"
	@echo "ABI_HEADER=$(ABI_HEADER)"
	@echo "ABI_LIBRARY=$(ABI_LIBRARY)"
	@echo "DIST_TARBALL=$(DIST_TARBALL)"
	@echo "PKGCONFIG_BUILD_DIR=$(PKGCONFIG_BUILD_DIR)"
	@echo "PKGCONFIGDIR=$(PKGCONFIGDIR)"
	@echo "VENDOR_PG_QUERY_TAG=$(VENDOR_PG_QUERY_TAG)"

clean:
	@rm -rf $(BUILD_PATH) $(BIN_PATH) $(LIB_PATH)

vendor-clean:
	@rm -rf $(BUILD_PATH)/vendor/libpg_query $(JANSSON_OBJ_PATH)

FORCE:

$(BUILD_SIGNATURE_FILE): FORCE Makefile $(CONFIG) $(SQLPARSER_SITE_CONFIG) VERSION | prep
	@tmp_file="$@.tmp"; \
	printf '%s\n' \
		"CC=$(CC)" \
		"AR=$(AR)" \
		"RANLIB=$(RANLIB)" \
		"CPPFLAGS=$(CPPFLAGS)" \
		"CFLAGS=$(CFLAGS)" \
		"LDFLAGS=$(LDFLAGS)" \
		"LDLIBS=$(LDLIBS)" > "$$tmp_file"; \
	if [ ! -f "$@" ] || ! cmp -s "$$tmp_file" "$@"; then \
		mv "$$tmp_file" "$@"; \
	else \
		rm -f "$$tmp_file"; \
	fi

$(VENDOR_BUILD_SIGNATURE_FILE): FORCE Makefile $(CONFIG) $(SQLPARSER_SITE_CONFIG) | prep
	@tmp_file="$@.tmp"; \
	printf '%s\n' \
		"CC=$(CC)" \
		"AR=$(AR)" \
		"DEBUG=$(DEBUG)" \
		"VENDOR_SOURCES=$(VENDOR_PG_QUERY_SRC_FILES)" \
		"VENDOR_CFLAGS=$(VENDOR_BUILD_CFLAGS)" > "$$tmp_file"; \
	if [ ! -f "$@" ] || ! cmp -s "$$tmp_file" "$@"; then \
		rm -rf $(VENDOR_PG_QUERY_BUILD_DIR) $(VENDOR_PG_QUERY_MERGE_DIR); \
		mv "$$tmp_file" "$@"; \
	else \
		rm -f "$$tmp_file"; \
	fi

$(VENDOR_PG_QUERY_LIB): $(VENDOR_BUILD_SIGNATURE_FILE) $(VENDOR_PG_QUERY_INPUTS)
	@$(MAKE) -C $(VENDOR_PG_QUERY_DIR) build \
		BUILD_DIR="$(abspath $(VENDOR_PG_QUERY_BUILD_DIR))" \
		CC="$(CC)" \
		AR="$(AR)" \
		ARFLAGS="rs" \
		DEBUG="$(DEBUG)" \
		CFLAGS="$(VENDOR_BUILD_CFLAGS)"

$(VENDOR_PG_QUERY_STAMP): $(VENDOR_PG_QUERY_LIB) | prep
	@rm -rf $(VENDOR_PG_QUERY_MERGE_DIR)
	@mkdir -p $(VENDOR_PG_QUERY_MERGE_DIR)
	@cd $(VENDOR_PG_QUERY_MERGE_DIR) && $(AR) x $(abspath $(VENDOR_PG_QUERY_LIB))
	@touch $@

$(STATIC_LIB_PATH): $(OBJ_FILES) $(JANSSON_OBJ_FILES) $(VENDOR_PG_QUERY_STAMP) | prep
	@rm -f $@
	@$(AR) rcs $@ $(OBJ_FILES) $(JANSSON_OBJ_FILES) $(VENDOR_PG_QUERY_MERGE_DIR)/*.o
	@$(RANLIB) $@

$(SHARED_LIB_REAL_PATH): $(OBJ_FILES) $(JANSSON_OBJ_FILES) $(VENDOR_PG_QUERY_LIB) | prep
	@$(CC) -shared \
		-Wl,-soname,$(SHARED_LIB_SONAME) \
		-Wl,--version-script=./config/sqlparser.map \
		-o $@ $(OBJ_FILES) $(JANSSON_OBJ_FILES) $(VENDOR_PG_QUERY_LIB) $(LDFLAGS) $(LDLIBS)

$(SHARED_LIB_PATH): $(SHARED_LIB_REAL_PATH) | prep
	@ln -sf $(notdir $(SHARED_LIB_REAL_PATH)) $@

$(PKGCONFIG_FILE): config/sqlparser.pc.in VERSION | prep
	@sed \
		-e 's|@PREFIX@|$(PREFIX)|g' \
		-e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' \
		-e 's|@LIBDIR@|$(LIBDIR)|g' \
		-e 's|@VERSION@|$(VERSION_STRING)|g' \
		$< > $@

$(OBJ_PATH)/%.o: src/%.c $(BUILD_SIGNATURE_FILE)
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(JANSSON_OBJ_PATH)/%.o: $(JANSSON_SRC_DIR)/%.c $(BUILD_SIGNATURE_FILE) $(VENDOR_BUILD_SIGNATURE_FILE)
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) -DHAVE_CONFIG_H $(VENDOR_BUILD_CFLAGS) -MMD -MP -c $< -o $@

$(CASE_MATRIX_BINS): $(CASE_RUNNER_HEADER)

$(UNIT_TEST_BINS): tests/unit/sqlparser_test_failure.h

$(BIN_PATH)/%: tests/unit/%.c $(STATIC_LIB_PATH) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB_PATH) $(LDFLAGS) $(LDLIBS) -o $@

# Compile the generic reference walker in a separate translation unit.
# An immutable pre-optimization source can also be supplied for external audits.
SURFACE_NODE_REFERENCE_SOURCE ?= src/dialect/sqlparser_dialect_ast_surface.c
SURFACE_NODE_REFERENCE_OBJECT := $(BUILD_PATH)/tests/surface_node_reference.o
-include $(SURFACE_NODE_REFERENCE_OBJECT:.o=.d)

$(SURFACE_NODE_REFERENCE_OBJECT): $(SURFACE_NODE_REFERENCE_SOURCE) $(BUILD_SIGNATURE_FILE) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) \
		-DSQLPARSER_DISABLE_SURFACE_NODE_DISPATCH \
		-Dsqlparser_dialect_ast_surface_visit=sqlparser_dialect_ast_surface_visit_reference \
		-Dsqlparser_dialect_ast_surface_visit_roots=sqlparser_dialect_ast_surface_visit_roots_reference \
		-Dsqlparser_dialect_ast_surface_clone=sqlparser_dialect_ast_surface_clone_reference \
		-MMD -MP -c $< -o $@

$(BIN_PATH)/test_surface_node_dispatch: tests/unit/test_surface_node_dispatch.c $(SURFACE_NODE_REFERENCE_OBJECT) $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_SURFACE_ALLOC_WRAPPERS $< \
		$(SURFACE_NODE_REFERENCE_OBJECT) $(STATIC_LIB_PATH) $(SQLPARSER_BENCH_WRAP_LDFLAGS) \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_wire_varint_dispatch: src/core/sqlparser_wire_insert.c

# Dependencies for focused INSERT parsing and graph regressions.
$(BIN_PATH)/test_certified_location_u32: src/core/sqlparser_wire_insert.c tests/unit/sqlparser_reference_certified_cell.inc
$(BIN_PATH)/test_dameng_wire_transcript: tests/unit/test_dameng_wire_transcript.c
$(BIN_PATH)/test_oracle_constructor_gates: tests/unit/test_oracle_constructor_gates.c tests/oracle_constructorgates/frozen_constructor_path.inc tests/oracle_constructorgates/cases.h tests/oracle_listbounds/frozen_scanner_reference.inc src/dialect/sqlparser_dialect_oracle.c
$(BIN_PATH)/test_oracle_header_cache: tests/unit/test_oracle_header_cache.c tests/oracle_headercache/frozen_header_path.inc tests/oracle_headercache/compare_helpers.inc src/dialect/sqlparser_dialect_oracle.c
$(BIN_PATH)/test_oracle_initial_span_certificate: tests/unit/test_oracle_initial_span_certificate.c tests/oracle_spancert/frozen_surface_scanners.inc tests/oracle_spancert/frozen_oracle_scanners.inc tests/oracle_spancert/frozen_view_scanners.inc tests/oracle_spancert/frozen_entry_scanners.inc src/core/sqlparser_view.c
$(BIN_PATH)/test_oracle_list_bounds: tests/unit/test_oracle_list_bounds.c tests/oracle_listbounds/frozen_scanner_reference.inc src/dialect/sqlparser_dialect_oracle.c
$(BIN_PATH)/test_oracle_list_bounds_public: tests/unit/test_oracle_list_bounds_public.c tests/oracle_listbounds/frozen_scanner_reference.inc tests/oracle_listbounds/frozen_list_scanners.inc src/dialect/sqlparser_dialect_oracle.c
$(BIN_PATH)/test_oracle_quote_call_gates: tests/unit/test_oracle_quote_call_gates.c tests/oracle_quotegates/frozen_view_scanners.inc tests/oracle_quotegates/frozen_surface_scanner.inc src/core/sqlparser_view.c
$(BIN_PATH)/test_oracle_returning_guard: tests/unit/test_oracle_returning_guard.c tests/oracle_returningguard/frozen_validator.inc tests/oracle_returningguard/frozen_surface_scanner.inc tests/oracle_returningguard/cases.h src/dialect/sqlparser_dialect_dml_result.c
$(BIN_PATH)/test_oracle_scan_gate: tests/unit/test_oracle_scan_gate.c tests/oracle_scangates/frozen_oracle_scanner.inc src/dialect/sqlparser_dialect_oracle.c
$(BIN_PATH)/test_oracle_statement_end: tests/unit/test_oracle_statement_end.c tests/oracle_statementend/frozen_statement_end.inc tests/oracle_listbounds/frozen_scanner_reference.inc src/dialect/sqlparser_dialect_oracle.c
$(BIN_PATH)/test_oracle_value_facts: tests/unit/test_oracle_value_facts.c tests/oracle_valuefacts/frozen_value_path.inc tests/oracle_valuefacts/cases.h src/dialect/sqlparser_dialect_oracle.c
$(BIN_PATH)/test_statement_scan_gate: tests/unit/test_statement_scan_gate.c tests/oracle_scangates/frozen_statement_span.inc src/core/sqlparser_view.c
$(BIN_PATH)/test_postgresql_identity_preprocess: tests/unit/test_postgresql_identity_preprocess.c src/dialect/sqlparser_dialect_postgresql.c
$(BIN_PATH)/test_postgresql_identity_tail: tests/unit/test_postgresql_identity_tail.c src/dialect/sqlparser_dialect_postgresql.c
$(BIN_PATH)/test_oracle_readonly_batch: tests/unit/test_oracle_readonly_batch.c

$(BIN_PATH)/test_mysql_inline_cell_layout: tests/unit/test_mysql_inline_cell_layout.c $(VENDOR_PG_QUERY_INPUTS) $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/postgres/include \
		$< $(STATIC_LIB_PATH) $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_native_target_cache: tests/unit/test_native_target_cache.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_TARGET_CACHE_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_oracle_column_oom: tests/unit/test_oracle_column_oom.c src/dialect/sqlparser_dialect_oracle.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_oracle_compact_lifecycle: tests/unit/test_oracle_compact_lifecycle.c $(wildcard tests/oracle_compact_backend/*) $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./src/core -I./src/dialect -DHEADER_CLASSIFICATION=1 \
		$< $(STATIC_LIB_PATH) $(LDFLAGS) $(LDLIBS) -o $@


$(BIN_PATH)/test_wire_insert_primary: tests/unit/test_wire_insert_primary.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_WIRE_GRAPH_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=pg_query__parse_result__unpack $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_wire_insert_codec: tests/unit/test_wire_insert_codec.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_WIRE_CODEC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_wire_insert_graph: tests/unit/test_wire_insert_graph.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_WIRE_GRAPH_WRAPPERS -DSQLPARSER_WIRE_GRAPH_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=pg_query__parse_result__unpack -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_insert_string_certificate: tests/unit/test_insert_string_certificate.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_CERTIFICATE_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_handle_commit_certified_insert_strings $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_mixed_insert_strings: tests/unit/test_mixed_insert_strings.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/postgres/include \
		-DSQLPARSER_MIXED_STRING_WRAPPERS -DSQLPARSER_MIXED_STRING_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=raw_parser_with_options -Wl,--wrap=sqlparser_handle_commit_certified_insert_strings \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_insert_source_proof: tests/unit/test_insert_source_proof.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_SOURCE_PROOF_WRAPPERS -DSQLPARSER_SOURCE_PROOF_OPTIMIZED $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_handle_commit_certified_insert_strings -Wl,--wrap=sqlparser_view_insert_cell_source_span \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_certified_insert_pack: tests/unit/test_certified_insert_pack.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_CERTIFIED_PACK_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_pack_certified_insert -Wl,--wrap=malloc -Wl,--wrap=free \
		-Wl,--wrap=protobuf_c_message_pack \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_patch_lifecycle: tests/unit/test_patch_lifecycle.c tests/unit/sqlparser_test_failure.h $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_LIFECYCLE_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_insert_graph_bulk_rows: tests/unit/test_insert_graph_bulk_rows.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_GRAPH_BULK_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_pg_query_thread_lifecycle: tests/unit/test_pg_query_thread_lifecycle.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_THREAD_LIFECYCLE_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=pthread_key_create -Wl,--wrap=pthread_setspecific $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_protobuf_output_oom: tests/unit/test_protobuf_output_oom.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_PROTOBUF_OUTPUT_OOM_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=pg_query_protobuf_alloc_output \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_protobuf_fresh_unpack: tests/unit/test_protobuf_fresh_unpack.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_FRESH_DEFAULT_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=free $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_protobuf_fastpath: tests/unit/test_protobuf_fastpath.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_FASTPATH_DEFAULT_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=free $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_protobuf_free: tests/unit/test_protobuf_free.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_PROTOBUF_FREE_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=free $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_parser_conversion_lifetime: tests/unit/test_parser_conversion_lifetime.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_CONVERSION_OUTPUT_OOM_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=pg_query_protobuf_alloc_output $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_direct_wire_lifecycle: tests/unit/test_direct_wire_lifecycle.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_DIRECT_WIRE_FAILURE_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=realloc -Wl,--wrap=pg_query_protobuf_alloc_output \
		-Wl,--wrap=pg_query_nodes_to_protobuf_observed -Wl,--wrap=pg_query_nodes_to_protobuf_certified \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_direct_wire_measure: tests/unit/test_direct_wire_measure.c $(VENDOR_PG_QUERY_INPUTS) $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/include \
		-I./vendor/libpg_query/src/postgres/include -fno-strict-aliasing -fwrapv \
		-Wno-unused-function -Wno-unused-variable -Wno-unused-value \
		$< $(STATIC_LIB_PATH) $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_simple_insert_native: tests/unit/test_simple_insert_native.c $(VENDOR_PG_QUERY_INPUTS) $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/postgres/include \
		-DSQLPARSER_SIMPLE_INSERT_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=raw_parser_with_options -Wl,--wrap=palloc -Wl,--wrap=palloc0 \
		-Wl,--wrap=MemoryContextAlloc -Wl,--wrap=repalloc \
		-Wl,--wrap=malloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=strdup -Wl,--wrap=strndup $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_scalar_insert_native: tests/unit/test_scalar_insert_native.c $(VENDOR_PG_QUERY_INPUTS) $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/postgres/include \
		-DSQLPARSER_SIMPLE_INSERT_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=raw_parser_with_options -Wl,--wrap=palloc -Wl,--wrap=palloc0 \
		-Wl,--wrap=MemoryContextAlloc -Wl,--wrap=repalloc \
		-Wl,--wrap=malloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=strdup -Wl,--wrap=strndup $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_mysql_owned_scalar_native: tests/unit/test_mysql_owned_scalar_native.c tests/unit/test_scalar_insert_native.c $(VENDOR_PG_QUERY_INPUTS) $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/postgres/include \
		-DSQLPARSER_SIMPLE_INSERT_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=raw_parser_with_options -Wl,--wrap=palloc -Wl,--wrap=palloc0 \
		-Wl,--wrap=MemoryContextAlloc -Wl,--wrap=repalloc \
		-Wl,--wrap=malloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=strdup -Wl,--wrap=strndup $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_scalar_insert_batch_native: tests/unit/test_scalar_insert_batch_native.c tests/unit/test_scalar_insert_native.c $(VENDOR_PG_QUERY_INPUTS) $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/postgres/include \
		-DSQLPARSER_SIMPLE_INSERT_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=raw_parser_with_options -Wl,--wrap=palloc -Wl,--wrap=palloc0 \
		-Wl,--wrap=MemoryContextAlloc -Wl,--wrap=repalloc \
		-Wl,--wrap=malloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=strdup -Wl,--wrap=strndup $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_simple_insert_size_bounds: tests/unit/test_simple_insert_size_bounds.c $(VENDOR_PG_QUERY_INPUTS) $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/postgres/include \
		$< $(STATIC_LIB_PATH) $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_simple_insert_pipeline: tests/unit/test_simple_insert_pipeline.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/postgres/include \
		-DSQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=raw_parser_with_options $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_validation_arena: tests/unit/test_validation_arena.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_ARENA_ALLOC_COUNTS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=free \
		-Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed \
		-Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native -Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan \
		-Wl,--wrap=pg_query__parse_result__unpack $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_mysql_validation_observer: tests/unit/test_mysql_validation_observer.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_OBSERVER_TEST_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed \
		-Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native -Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan \
		-Wl,--wrap=calloc -Wl,--wrap=pg_query__parse_result__unpack $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_patch_batch_counts: tests/unit/test_patch_batch.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_PATCH_BATCH_COUNTS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_parse_with_options -Wl,--wrap=sqlparser_handle_clone \
		-Wl,--wrap=sqlparser_deparse -Wl,--wrap=sqlparser_handle_commit_ast \
		-Wl,--wrap=sqlparser_handle_commit_ast_with_dialect_state \
		-Wl,--wrap=sqlparser_dialect_ast_surface_visit \
		-Wl,--wrap=sqlparser_dialect_ast_surface_visit_roots $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/examples/%: examples/%.c $(STATIC_LIB_PATH) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB_PATH) $(LDFLAGS) $(LDLIBS) -o $@

$(SQLPARSER_CLI_BIN): tools/sqlparser_cli.c $(STATIC_LIB_PATH) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB_PATH) $(LDFLAGS) $(LDLIBS) -o $@

$(SQLPARSER_BENCH_BIN): tools/sqlparser_bench.c $(STATIC_LIB_PATH) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB_PATH) $(SQLPARSER_BENCH_WRAP_LDFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

$(LIBPG_QUERY_BASELINE_BIN): tools/libpg_query_baseline.c $(VENDOR_PG_QUERY_LIB) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(VENDOR_PG_QUERY_LIB) $(SQLPARSER_BENCH_WRAP_LDFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

include tests/oracle_scalar_constructor/Makefile.inc

-include $(DEP_FILES)

$(BIN_PATH)/test_scalar_wire_pipeline: tests/unit/test_scalar_wire_pipeline.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_SCALAR_WIRE_WRAPPERS -DSQLPARSER_SCALAR_WIRE_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=pg_query__parse_result__unpack -Wl,--wrap=malloc -Wl,--wrap=calloc \
		-Wl,--wrap=realloc -Wl,--wrap=free -Wl,--wrap=pg_query_enter_memory_context \
		-Wl,--wrap=pg_query_exit_memory_context $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_scalar_wire_codec: tests/unit/test_scalar_wire_codec.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_SCALAR_CODEC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_native_wire_provenance: tests/unit/test_native_wire_provenance.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_NATIVE_PROVENANCE_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_wire_scalar_insert_certify -Wl,--wrap=sqlparser_wire_scalar_insert_from_native \
		-Wl,--wrap=pg_query__parse_result__unpack -Wl,--wrap=malloc -Wl,--wrap=calloc \
		-Wl,--wrap=realloc -Wl,--wrap=free -Wl,--wrap=pg_query_enter_memory_context \
		-Wl,--wrap=pg_query_exit_memory_context $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_mysql_identity_preprocess: tests/unit/test_mysql_identity_preprocess.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_IDENTITY_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_sqlserver_identity_preprocess: tests/unit/test_sqlserver_identity_preprocess.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_IDENTITY_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=pg_query__parse_result__unpack -Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_sqlserver_batch_identity_preprocess: tests/unit/test_sqlserver_batch_identity_preprocess.c tests/unit/test_sqlserver_identity_preprocess.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_IDENTITY_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=pg_query__parse_result__unpack -Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_oracle_cached_spans: tests/unit/test_oracle_cached_spans.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_SPAN_TEST_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_oracle_multi_insert_certified_cell_span $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_oracle_origin_replay: tests/unit/test_oracle_origin_replay.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_ORIGIN_REPLAY_WRAPPERS -DSQLPARSER_ORIGIN_REPLAY_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_dialect_preprocess_identifier_origins \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_sqlserver_raw_prefilter: tests/unit/test_sqlserver_raw_prefilter.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_PREFILTER_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_family_scalar_pipeline: tests/unit/test_family_scalar_pipeline.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./src/dialect -I./src/core -DSQLPARSER_FAMILY_SCALAR_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=pg_query__parse_result__unpack \
		-Wl,--wrap=sqlparser_handle_reparse_destructive \
		-Wl,--wrap=sqlparser_parse_insert_cell_node_sql \
		-Wl,--wrap=sqlparser_parse_protobuf_preserving_identifier_spelling \
		-Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native -Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_sqlserver_wire_pipeline: tests/unit/test_sqlserver_wire_pipeline.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./src/dialect -I./src/core -DSQLPARSER_SQLSERVER_WIRE_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_dialect_get_ops \
		-Wl,--wrap=sqlparser_dialect_supports_plain_scalar_insert \
		-Wl,--wrap=pg_query__parse_result__unpack \
		-Wl,--wrap=sqlparser_handle_reparse_destructive \
		-Wl,--wrap=sqlparser_parse_insert_cell_node_sql \
		-Wl,--wrap=sqlparser_parse_protobuf_preserving_identifier_spelling \
		-Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native -Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_sqlserver_batch_wire_pipeline: tests/unit/test_sqlserver_batch_wire_pipeline.c tests/unit/test_sqlserver_wire_pipeline.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./src/dialect -I./src/core -DSQLPARSER_SQLSERVER_WIRE_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_wire_scalar_batch_certify \
		-Wl,--wrap=sqlparser_dialect_get_ops \
		-Wl,--wrap=sqlparser_dialect_supports_plain_scalar_insert \
		-Wl,--wrap=pg_query__parse_result__unpack \
		-Wl,--wrap=sqlparser_handle_reparse_destructive \
		-Wl,--wrap=sqlparser_parse_insert_cell_node_sql \
		-Wl,--wrap=sqlparser_parse_protobuf_preserving_identifier_spelling \
		-Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native -Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		$(LDFLAGS) $(LDLIBS) -o $@

# Independent Oracle graph transcript can be linked to an immutable library:
# make bin/test_oracle_graph_classification_baseline ORACLE_GRAPH_BASELINE_LIB=/path/libsqlparser.a
ORACLE_GRAPH_TEST_FLAGS := -DSQLPARSER_ORACLE_GRAPH_WRAPPERS
ORACLE_GRAPH_TEST_WRAPS := -Wl,--wrap=sqlparser_parse_insert_cell_node_sql \
	-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
	-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context
$(BIN_PATH)/test_oracle_graph_classification: tests/unit/test_oracle_graph_classification.c tests/unit/sqlparser_oracle_graph_records.h $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(ORACLE_GRAPH_TEST_FLAGS) $< $(STATIC_LIB_PATH) \
		$(ORACLE_GRAPH_TEST_WRAPS) $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_oracle_graph_classification_baseline: tests/unit/test_oracle_graph_classification.c tests/unit/sqlparser_oracle_graph_records.h | prep
	@test -n "$(ORACLE_GRAPH_BASELINE_LIB)" || { echo "Set ORACLE_GRAPH_BASELINE_LIB to an immutable baseline archive"; exit 1; }
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(ORACLE_GRAPH_TEST_FLAGS) -DSQLPARSER_ORACLE_GRAPH_BASELINE \
		$< $(ORACLE_GRAPH_BASELINE_LIB) $(ORACLE_GRAPH_TEST_WRAPS) $(LDFLAGS) $(LDLIBS) -o $@

# Same commit contract caller, independently linked to the pre-commit archive.
# Optional --fixture SQL --golden-dir DIR; normal make test needs no private files.
ORACLE_COMMIT_TEST_FLAGS := -DSQLPARSER_ORACLE_COMMIT_WRAPPERS
ORACLE_COMMIT_TEST_WRAPS := -Wl,--wrap=sqlparser_handle_reparse_destructive \
	-Wl,--wrap=pg_query__parse_result__unpack \
	-Wl,--wrap=sqlparser_oracle_try_commit_multi_insert_strings \
	-Wl,--wrap=sqlparser_oracle_readonly_batch_commit \
	-Wl,--wrap=sqlparser_vastbase_oracle_multi_insert_identity_input \
	-Wl,--wrap=sqlparser_oracle_note_multi_insert_edit \
	-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
	-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context
$(BUILD_PATH)/tests/oracle_ascii_reference.o: tests/oracle_ascii_validation/patch_probe.c src/core/sqlparser_patch.c $(BUILD_SIGNATURE_FILE) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_ASCII_REFERENCE=1 -c $< -o $@

$(BUILD_PATH)/tests/oracle_ascii_probe.o: tests/oracle_ascii_validation/patch_probe.c src/core/sqlparser_patch.c $(BUILD_SIGNATURE_FILE) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DSQLPARSER_ASCII_REFERENCE=0 -c $< -o $@

$(BIN_PATH)/test_oracle_ascii_validation: tests/unit/test_oracle_ascii_validation.c tests/unit/test_oracle_owned_commit.c tests/unit/sqlparser_oracle_graph_records.h $(BUILD_PATH)/tests/oracle_ascii_reference.o $(BUILD_PATH)/tests/oracle_ascii_probe.o $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(ORACLE_COMMIT_TEST_FLAGS) $< $(BUILD_PATH)/tests/oracle_ascii_reference.o $(BUILD_PATH)/tests/oracle_ascii_probe.o $(STATIC_LIB_PATH) \
		$(ORACLE_COMMIT_TEST_WRAPS) -Wl,--wrap=sqlparser_parse_insert_cell_node_sql $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_oracle_owned_commit: tests/unit/test_oracle_owned_commit.c tests/unit/sqlparser_oracle_graph_records.h tests/unit/sqlparser_test_failure.h $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(ORACLE_COMMIT_TEST_FLAGS) $< $(STATIC_LIB_PATH) \
		$(ORACLE_COMMIT_TEST_WRAPS) $(LDFLAGS) $(LDLIBS) -o $@

# Complete independent graph/state transcript for certified lazy origins.
# Use matching reference sources and library for independent origin checks.
ORACLE_ORIGIN_CERT_TEST_FLAGS := $(ORACLE_COMMIT_TEST_FLAGS) -DSQLPARSER_ORIGIN_CERT_WRAPPERS
ORACLE_ORIGIN_CERT_TEST_WRAPS := $(ORACLE_COMMIT_TEST_WRAPS) \
	-Wl,--wrap=sqlparser_oracle_replay_identifier_origins
$(BIN_PATH)/test_oracle_certified_origins: tests/unit/test_oracle_certified_origins.c tests/unit/test_oracle_owned_commit.c tests/unit/sqlparser_oracle_graph_records.h tests/unit/sqlparser_test_failure.h $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(ORACLE_ORIGIN_CERT_TEST_FLAGS) $< $(STATIC_LIB_PATH) \
		$(ORACLE_ORIGIN_CERT_TEST_WRAPS) -Wl,--wrap=sqlparser_oracle_try_replay_certified_multi_insert_origins \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_oracle_certified_origins_baseline: tests/unit/test_oracle_certified_origins.c tests/unit/test_oracle_owned_commit.c tests/unit/sqlparser_oracle_graph_records.h tests/unit/sqlparser_test_failure.h | prep
	@test -n "$(ORACLE_ORIGIN_CERT_BASELINE_LIB)" -a -n "$(ORACLE_ORIGIN_CERT_BASELINE_ROOT)" || { echo "Set ORACLE_ORIGIN_CERT_BASELINE_LIB and ORACLE_ORIGIN_CERT_BASELINE_ROOT to the matching immutable archive and source"; exit 1; }
	@$(CC) -I$(ORACLE_ORIGIN_CERT_BASELINE_ROOT)/src/internal -I$(ORACLE_ORIGIN_CERT_BASELINE_ROOT)/include $(CPPFLAGS) $(CFLAGS) \
		$(ORACLE_ORIGIN_CERT_TEST_FLAGS) -DSQLPARSER_ORIGIN_CERT_BASELINE \
		-D'SQLPARSER_ORACLE_COMMIT_AST_HEADER="$(ORACLE_ORIGIN_CERT_BASELINE_ROOT)/src/core/sqlparser_ast_internal.h"' \
		-D'SQLPARSER_ORACLE_COMMIT_DIALECT_HEADER="$(ORACLE_ORIGIN_CERT_BASELINE_ROOT)/src/dialect/sqlparser_dialect_internal.h"' \
		-D'SQLPARSER_ORACLE_COMMIT_ORACLE_HEADER="$(ORACLE_ORIGIN_CERT_BASELINE_ROOT)/src/dialect/sqlparser_dialect_oracle_internal.h"' \
		$< $(ORACLE_ORIGIN_CERT_BASELINE_LIB) $(ORACLE_ORIGIN_CERT_TEST_WRAPS) $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_oracle_owned_commit_baseline: tests/unit/test_oracle_owned_commit.c tests/unit/sqlparser_oracle_graph_records.h tests/unit/sqlparser_test_failure.h | prep
	@test -n "$(ORACLE_COMMIT_BASELINE_LIB)" -a -n "$(ORACLE_COMMIT_BASELINE_ROOT)" || { echo "Set ORACLE_COMMIT_BASELINE_LIB and ORACLE_COMMIT_BASELINE_ROOT to the immutable archive and matching source"; exit 1; }
	@$(CC) -I$(ORACLE_COMMIT_BASELINE_ROOT)/src/internal -I$(ORACLE_COMMIT_BASELINE_ROOT)/include $(CPPFLAGS) $(CFLAGS) \
		$(ORACLE_COMMIT_TEST_FLAGS) -DSQLPARSER_ORACLE_COMMIT_BASELINE \
		-D'SQLPARSER_ORACLE_COMMIT_AST_HEADER="$(ORACLE_COMMIT_BASELINE_ROOT)/src/core/sqlparser_ast_internal.h"' \
		-D'SQLPARSER_ORACLE_COMMIT_DIALECT_HEADER="$(ORACLE_COMMIT_BASELINE_ROOT)/src/dialect/sqlparser_dialect_internal.h"' \
		-D'SQLPARSER_ORACLE_COMMIT_ORACLE_HEADER="$(ORACLE_COMMIT_BASELINE_ROOT)/src/dialect/sqlparser_dialect_oracle_internal.h"' \
		$< $(ORACLE_COMMIT_BASELINE_LIB) $(ORACLE_COMMIT_TEST_WRAPS) $(LDFLAGS) $(LDLIBS) -o $@

# Build reference-library checks with that library's matching private headers.
$(BIN_PATH)/test_oracle_owned_commit_reference: tests/unit/test_oracle_owned_commit.c tests/unit/sqlparser_oracle_graph_records.h tests/unit/sqlparser_test_failure.h | prep
	@test -n "$(ORACLE_COMMIT_REFERENCE_LIB)" -a -n "$(ORACLE_COMMIT_REFERENCE_ROOT)" || { echo "Set ORACLE_COMMIT_REFERENCE_LIB and ORACLE_COMMIT_REFERENCE_ROOT to the immutable archive and matching source"; exit 1; }
	@$(CC) -I$(ORACLE_COMMIT_REFERENCE_ROOT)/src/internal -I$(ORACLE_COMMIT_REFERENCE_ROOT)/include $(CPPFLAGS) $(CFLAGS) \
		$(ORACLE_COMMIT_TEST_FLAGS) -DSQLPARSER_ORACLE_COMMIT_REFERENCE \
		-D'SQLPARSER_ORACLE_COMMIT_AST_HEADER="$(ORACLE_COMMIT_REFERENCE_ROOT)/src/core/sqlparser_ast_internal.h"' \
		-D'SQLPARSER_ORACLE_COMMIT_DIALECT_HEADER="$(ORACLE_COMMIT_REFERENCE_ROOT)/src/dialect/sqlparser_dialect_internal.h"' \
		-D'SQLPARSER_ORACLE_COMMIT_ORACLE_HEADER="$(ORACLE_COMMIT_REFERENCE_ROOT)/src/dialect/sqlparser_dialect_oracle_internal.h"' \
		$< $(ORACLE_COMMIT_REFERENCE_LIB) $(ORACLE_COMMIT_TEST_WRAPS) $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_sqlserver_insert_batch: tests/unit/test_sqlserver_insert_batch.c tests/unit/sqlparser_test_failure.h $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./src/dialect -I./src/core -DSQLPARSER_SQLSERVER_BATCH_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_handle_reparse_destructive \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		$(LDFLAGS) $(LDLIBS) -o $@

# Includes the unchanged native parser for private-constructor route counters.
# Instrumentation and linker wrappers are test-only; never use this for timing.
$(BIN_PATH)/test_sqlserver_validation_proof: tests/unit/test_sqlserver_validation_proof.c tests/unit/test_sqlserver_identity_preprocess.c $(VENDOR_PG_QUERY_INPUTS) $(STATIC_LIB_PATH) | prep
	@mkdir -p $(BUILD_PATH)/tests
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/include \
		-I./vendor/libpg_query/src/postgres/include -fno-strict-aliasing -fwrapv \
		-finstrument-functions -DSQLPARSER_VALIDATION_PROOF_WRAPPERS \
		-c $< -o $(BUILD_PATH)/tests/test_sqlserver_validation_proof.o
	@$(CC) $(BUILD_PATH)/tests/test_sqlserver_validation_proof.o $(STATIC_LIB_PATH) \
		-Wl,--wrap=raw_parser_with_options \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		-Wl,--wrap=pg_query__parse_result__unpack -Wl,--wrap=pg_query__parse_result__free_unpacked \
		-Wl,--wrap=pg_query_nodes_to_protobuf_observed -Wl,--wrap=pg_query_nodes_to_protobuf_certified \
		-Wl,--wrap=sqlparser_parse_protobuf_preserving_identifier_spelling \
		-Wl,--wrap=sqlparser_dialect_get_ops -Wl,--wrap=sqlparser_dialect_validation_preprocessor \
		-Wl,--wrap=sqlparser_wire_scalar_insert_certify -Wl,--wrap=sqlparser_wire_scalar_insert_from_native \
		-Wl,--wrap=realloc -Wl,--wrap=free -Wl,--wrap=pg_query_protobuf_alloc_output \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_sqlserver_batch_native: tests/unit/test_sqlserver_batch_native.c tests/unit/test_sqlserver_validation_proof.c tests/unit/test_sqlserver_identity_preprocess.c $(VENDOR_PG_QUERY_INPUTS) $(STATIC_LIB_PATH) | prep
	@mkdir -p $(BUILD_PATH)/tests
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./vendor/libpg_query/src/include \
		-I./vendor/libpg_query/src/postgres/include -fno-strict-aliasing -fwrapv \
		-finstrument-functions -DSQLPARSER_VALIDATION_PROOF_WRAPPERS \
		-c $< -o $(BUILD_PATH)/tests/test_sqlserver_batch_native.o
	@$(CC) $(BUILD_PATH)/tests/test_sqlserver_batch_native.o $(STATIC_LIB_PATH) \
		-Wl,--wrap=raw_parser_with_options \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		-Wl,--wrap=pg_query__parse_result__unpack -Wl,--wrap=pg_query__parse_result__free_unpacked \
		-Wl,--wrap=pg_query_nodes_to_protobuf_observed -Wl,--wrap=pg_query_nodes_to_protobuf_certified \
		-Wl,--wrap=sqlparser_parse_protobuf_preserving_identifier_spelling \
		-Wl,--wrap=sqlparser_dialect_get_ops -Wl,--wrap=sqlparser_dialect_validation_preprocessor \
		-Wl,--wrap=sqlparser_wire_scalar_insert_certify -Wl,--wrap=sqlparser_wire_scalar_insert_from_native \
		-Wl,--wrap=realloc -Wl,--wrap=free -Wl,--wrap=pg_query_protobuf_alloc_output \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_dameng_wire_pipeline: tests/unit/test_dameng_wire_pipeline.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./src/dialect -I./src/core -DSQLPARSER_DAMENG_WIRE_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=sqlparser_dialect_get_ops \
		-Wl,--wrap=sqlparser_dialect_supports_plain_scalar_insert \
		-Wl,--wrap=pg_query__parse_result__unpack \
		-Wl,--wrap=sqlparser_handle_reparse_destructive \
		-Wl,--wrap=sqlparser_parse_insert_cell_node_sql \
		-Wl,--wrap=sqlparser_parse_protobuf_preserving_identifier_spelling \
		-Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native -Wl,--wrap=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		$(LDFLAGS) $(LDLIBS) -o $@

# These exact public-operation transcripts link independently to the immutable baseline.
$(BIN_PATH)/test_dameng_wire_transcript_baseline: tests/unit/test_dameng_wire_transcript.c tests/unit/sqlparser_oracle_graph_records.h | prep
	@test -n "$(DAMENG_BASELINE_LIB)" || { echo "set DAMENG_BASELINE_LIB to the immutable baseline archive"; exit 1; }
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(DAMENG_BASELINE_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_dameng_ascii_validation_baseline: tests/unit/test_dameng_ascii_validation.c | prep
	@test -n "$(DAMENG_BASELINE_LIB)" || { echo "set DAMENG_BASELINE_LIB to the immutable baseline archive"; exit 1; }
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(DAMENG_BASELINE_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/test_dameng_identity_preprocess: tests/unit/test_dameng_identity_preprocess.c src/dialect/sqlparser_dialect_dameng.c $(STATIC_LIB_PATH) | prep
	@$(CC) $(CPPFLAGS) $(CFLAGS) -I./src/dialect -DSQLPARSER_IDENTITY_ALLOC_WRAPPERS $< $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		-Wl,--wrap=pg_query__parse_result__unpack -Wl,--wrap=pg_query__parse_result__free_unpacked \
		-Wl,--wrap=sqlparser_parse_protobuf_preserving_identifier_spelling \
		$(LDFLAGS) $(LDLIBS) -o $@
