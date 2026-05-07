CONFIG := ./config/Makefile.config
include $(CONFIG)

.DEFAULT_GOAL := all

VERSION_STRING := $(strip $(shell tr -d '\r\n' < ./VERSION 2>/dev/null))
JANSSON_CFLAGS := $(shell $(PKG_CONFIG) --cflags jansson 2>/dev/null)
JANSSON_LDLIBS := $(shell $(PKG_CONFIG) --libs jansson 2>/dev/null)

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
VENDOR_PG_QUERY_LIB := $(VENDOR_PG_QUERY_DIR)/libpg_query.a
VENDOR_PG_QUERY_MERGE_DIR := $(BUILD_PATH)/vendor/libpg_query
VENDOR_PG_QUERY_STAMP := $(VENDOR_PG_QUERY_MERGE_DIR)/.merged.stamp
BUILD_SIGNATURE_FILE := $(BUILD_PATH)/.build_signature
VENDOR_BUILD_SIGNATURE_FILE := $(BUILD_PATH)/vendor/.vendor_build_signature

OBJ_FILES := $(foreach src,$(ALL_SRC),$(OBJ_PATH)/$(patsubst src/%,%,$(src:.c=.o)))
DEP_FILES := $(OBJ_FILES:.o=.d)
UNIT_TEST_BINS := $(foreach src,$(UNIT_TEST_SRC),$(BIN_PATH)/$(notdir $(src:.c=)))
EXAMPLE_BINS := $(foreach src,$(EXAMPLE_SRC),$(BIN_PATH)/examples/$(notdir $(src:.c=)))
TEST_API_SMOKE_BIN := $(BIN_PATH)/test_api_smoke
TEST_CORE_API_BIN := $(BIN_PATH)/test_core_api
TEST_CASE_MATRIX_BIN := $(BIN_PATH)/test_api_case_matrix
SQLPARSER_CLI_BIN := $(BIN_PATH)/sqlparser_cli
SQLPARSER_BENCH_BIN := $(BIN_PATH)/sqlparser_bench
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
	-DSQLPARSER_VERSION_TEXT=\"$(VERSION_STRING)\" \
	-DSQLPARSER_LIBPG_QUERY_TAG_TEXT=\"$(VENDOR_PG_QUERY_TAG)\" \
	$(JANSSON_CFLAGS) \
	$(PTHREAD_CFLAGS)

BASE_CFLAGS := -std=gnu11 -fPIC
BASE_LDFLAGS :=
BASE_LDLIBS := $(JANSSON_LDLIBS) $(PTHREAD_LDLIBS) -lm

VENDOR_PG_QUERY_BUILD_CFLAGS := -std=gnu11 -fPIC

ifeq ($(DEBUG),1)
	BASE_CFLAGS += -g -O0
	VENDOR_PG_QUERY_BUILD_CFLAGS += -g -O0
else
	BASE_CFLAGS += -O2
	VENDOR_PG_QUERY_BUILD_CFLAGS += -O2
endif

ifeq ($(SHOW_WARNING),1)
	BASE_CFLAGS += -Wall -Wextra -Wpedantic
else
	BASE_CFLAGS += -w
endif

SHOW_VENDOR_WARNING ?= $(SHOW_WARNING)

ifeq ($(SHOW_VENDOR_WARNING),1)
	VENDOR_PG_QUERY_BUILD_CFLAGS += -Wall -Wextra -Wpedantic
else
	VENDOR_PG_QUERY_BUILD_CFLAGS += -w
endif

ifeq ($(STRICT),1)
	BASE_CFLAGS += -Werror
endif

ifneq ($(strip $(SANITIZE)),)
	BASE_CFLAGS += -fno-omit-frame-pointer -fsanitize=$(SANITIZE)
	BASE_LDFLAGS += -fno-omit-frame-pointer -fsanitize=$(SANITIZE)
	VENDOR_PG_QUERY_BUILD_CFLAGS += -fno-omit-frame-pointer -fsanitize=$(SANITIZE)
endif

CPPFLAGS := $(BASE_CPPFLAGS) $(EXTRA_CPPFLAGS)
CFLAGS := $(BASE_CFLAGS) $(EXTRA_CFLAGS)
LDFLAGS := $(BASE_LDFLAGS) $(EXTRA_LDFLAGS)
LDLIBS := $(BASE_LDLIBS) $(EXTRA_LDLIBS)

.PHONY: \
	all prep vendor static shared clean vendor-clean print-config test install cli bench-build \
	test-cli-batch examples install-smoke bench-smoke test-loop verify verify-release verify-debug \
	verify-asan verify-ubsan verify-valgrind verify-ci abi-check test-unit test-examples \
	test-parse test-inspect test-rewrite test-deparse test-model-json test-cli test-install \
	test-abi dist

all: prep static shared cli
	@echo "Build finished: $(STATIC_LIB_PATH) $(SHARED_LIB_PATH) $(SQLPARSER_CLI_BIN)"

prep:
	@mkdir -p $(OBJ_PATH) $(BIN_PATH) $(LIB_PATH) $(PKGCONFIG_BUILD_DIR) $(VENDOR_PG_QUERY_MERGE_DIR)

vendor: $(VENDOR_PG_QUERY_LIB)

static: $(STATIC_LIB_PATH)

shared: $(SHARED_LIB_PATH)

cli: $(SQLPARSER_CLI_BIN)

examples: $(EXAMPLE_BINS)

bench-build: $(SQLPARSER_BENCH_BIN)

test: cli test-unit test-examples test-cli

test-unit: $(UNIT_TEST_BINS)
	@set -e; \
	for test_bin in $(UNIT_TEST_BINS); do \
		"$$test_bin"; \
	done

test-examples: $(EXAMPLE_BINS)
	@set -e; \
	for test_bin in $(EXAMPLE_BINS); do \
		"$$test_bin"; \
	done

test-cli: test-cli-batch

test-parse: $(TEST_API_SMOKE_BIN) $(TEST_CASE_MATRIX_BIN)
	@$(TEST_API_SMOKE_BIN)
	@$(TEST_CASE_MATRIX_BIN)

test-inspect: $(TEST_API_SMOKE_BIN) $(TEST_CORE_API_BIN) $(TEST_CASE_MATRIX_BIN)
	@$(TEST_API_SMOKE_BIN)
	@$(TEST_CORE_API_BIN)
	@$(TEST_CASE_MATRIX_BIN)

test-rewrite: $(TEST_CORE_API_BIN)
	@$(TEST_CORE_API_BIN)

test-deparse: $(TEST_CORE_API_BIN) $(TEST_CASE_MATRIX_BIN)
	@$(TEST_CORE_API_BIN)
	@$(TEST_CASE_MATRIX_BIN)

test-model-json: $(TEST_CORE_API_BIN)
	@$(TEST_CORE_API_BIN)

test-install: install-smoke

test-abi: abi-check

test-cli-batch: $(SQLPARSER_CLI_BIN) $(SQLPARSER_CLI_BATCH_FIXTURE) $(SQLPARSER_CLI_BATCH_VERIFY) | prep
	@mkdir -p $(BUILD_PATH)/tests
	@$(SQLPARSER_CLI_BIN) --batch-file $(SQLPARSER_CLI_BATCH_FIXTURE) --output $(SQLPARSER_CLI_BATCH_OUTPUT)
	@$(BENCH_PYTHON) $(SQLPARSER_CLI_BATCH_VERIFY) \
		--fixture $(SQLPARSER_CLI_BATCH_FIXTURE) \
		--output $(SQLPARSER_CLI_BATCH_OUTPUT)

install-smoke: all $(PKGCONFIG_FILE) $(INSTALL_SMOKE_SRC) | prep
	@rm -rf $(TEST_STAGE_DIR)
	@$(MAKE) --no-print-directory install PREFIX=$(abspath $(TEST_STAGE_DIR)) DEBUG=$(DEBUG) SHOW_WARNING=$(SHOW_WARNING) STRICT=$(STRICT) SANITIZE="$(SANITIZE)" SHOW_VENDOR_WARNING=$(SHOW_VENDOR_WARNING)
	@mkdir -p $(dir $(INSTALL_SMOKE_BIN))
	@$(CC) -std=gnu11 $(PTHREAD_CFLAGS) \
		-I$(abspath $(TEST_STAGE_DIR))/include \
		$(INSTALL_SMOKE_SRC) \
		-L$(abspath $(TEST_STAGE_DIR))/lib \
		-Wl,-rpath,$(abspath $(TEST_STAGE_DIR))/lib \
		-l$(LIB_NAME) $(JANSSON_LDLIBS) $(PTHREAD_LDLIBS) -lm \
		-o $(INSTALL_SMOKE_BIN)
	@$(INSTALL_SMOKE_BIN)

bench-smoke: bench-build
	@$(BENCH_PYTHON) ./bench/run_benchmarks.py \
		--output-dir $(BENCH_OUTPUT_DIR) \
		--bench-bin $(SQLPARSER_BENCH_BIN) \
		--profile $(BENCH_PROFILE) \
		--stages $(BENCH_STAGES)

dist:
	@mkdir -p $(DIST_DIR)
	@rm -f $(DIST_TARBALL)
	@tar -czf $(DIST_TARBALL) \
		--transform='s|^\./|$(DIST_NAME)/|' \
		--exclude='*.a' \
		--exclude='*.o' \
		--exclude='*.so' \
		--exclude='.DS_Store' \
		--exclude='./bench/results' \
		--exclude='./vendor/libpg_query/.deps' \
		--exclude='./vendor/libpg_query/tmp' \
		$(DIST_SOURCE_PATHS)
	@echo "Source package: $(DIST_TARBALL)"

abi-check: shared
	@$(ABI_EXPORT_CHECKER) --header $(ABI_HEADER) --library $(ABI_LIBRARY)

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
	@if command -v "$(VERIFY_VALGRIND_TOOL)" >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory all test install-smoke DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0; \
		for test_bin in $(UNIT_TEST_BINS) $(EXAMPLE_BINS); do \
			$(VALGRIND_RUNNER) --tool "$(VERIFY_VALGRIND_TOOL)" --log-dir "$(VALGRIND_LOG_DIR)" --name "$$(basename "$$test_bin")" -- "$$test_bin"; \
		done; \
		$(VALGRIND_RUNNER) --tool "$(VERIFY_VALGRIND_TOOL)" --log-dir "$(VALGRIND_LOG_DIR)" --name "sqlparser_cli_batch" -- \
			$(SQLPARSER_CLI_BIN) --batch-file $(SQLPARSER_CLI_BATCH_FIXTURE) --output $(SQLPARSER_CLI_BATCH_OUTPUT); \
		$(BENCH_PYTHON) $(SQLPARSER_CLI_BATCH_VERIFY) \
			--fixture $(SQLPARSER_CLI_BATCH_FIXTURE) \
			--output $(SQLPARSER_CLI_BATCH_OUTPUT); \
		$(VALGRIND_RUNNER) --tool "$(VERIFY_VALGRIND_TOOL)" --log-dir "$(VALGRIND_LOG_DIR)" --name "install_smoke" -- \
			$(INSTALL_SMOKE_BIN); \
	else \
		echo "skip verify-valgrind: valgrind is not installed"; \
	fi

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
	@echo "CC=$(CC)"
	@echo "AR=$(AR)"
	@echo "RANLIB=$(RANLIB)"
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
	@echo "REMOTE_HOST=$(REMOTE_HOST)"
	@echo "REMOTE_PORT=$(REMOTE_PORT)"
	@echo "REMOTE_WORKDIR=$(REMOTE_WORKDIR)"
	@echo "JANSSON_CFLAGS=$(JANSSON_CFLAGS)"
	@echo "JANSSON_LDLIBS=$(JANSSON_LDLIBS)"

clean:
	@rm -rf $(BUILD_PATH) $(BIN_PATH) $(LIB_PATH)

vendor-clean:
	@$(MAKE) -C $(VENDOR_PG_QUERY_DIR) clean >/dev/null 2>&1 || true
	@rm -rf $(VENDOR_PG_QUERY_MERGE_DIR)

$(BUILD_SIGNATURE_FILE): Makefile $(CONFIG) $(SQLPARSER_SITE_CONFIG) VERSION | prep
	@tmp_file="$@.tmp"; \
	printf '%s\n' \
		"CC=$(CC)" \
		"CPPFLAGS=$(CPPFLAGS)" \
		"CFLAGS=$(CFLAGS)" \
		"LDFLAGS=$(LDFLAGS)" \
		"LDLIBS=$(LDLIBS)" > "$$tmp_file"; \
	if [ ! -f "$@" ] || ! cmp -s "$$tmp_file" "$@"; then \
		mv "$$tmp_file" "$@"; \
	else \
		rm -f "$$tmp_file"; \
	fi

$(VENDOR_BUILD_SIGNATURE_FILE): Makefile $(CONFIG) $(SQLPARSER_SITE_CONFIG) | prep
	@tmp_file="$@.tmp"; \
	printf '%s\n' \
		"CC=$(CC)" \
		"VENDOR_CFLAGS=$(VENDOR_PG_QUERY_BUILD_CFLAGS)" > "$$tmp_file"; \
	if [ ! -f "$@" ] || ! cmp -s "$$tmp_file" "$@"; then \
		$(MAKE) -C $(VENDOR_PG_QUERY_DIR) clean >/dev/null 2>&1 || true; \
		mv "$$tmp_file" "$@"; \
	else \
		rm -f "$$tmp_file"; \
	fi

$(VENDOR_PG_QUERY_LIB): $(VENDOR_BUILD_SIGNATURE_FILE)
	@$(MAKE) -C $(VENDOR_PG_QUERY_DIR) build \
		CC="$(CC)" \
		CFLAGS="$(VENDOR_PG_QUERY_BUILD_CFLAGS)"

$(VENDOR_PG_QUERY_STAMP): $(VENDOR_PG_QUERY_LIB) | prep
	@rm -rf $(VENDOR_PG_QUERY_MERGE_DIR)
	@mkdir -p $(VENDOR_PG_QUERY_MERGE_DIR)
	@cd $(VENDOR_PG_QUERY_MERGE_DIR) && $(AR) x $(abspath $(VENDOR_PG_QUERY_LIB))
	@touch $@

$(STATIC_LIB_PATH): $(OBJ_FILES) $(VENDOR_PG_QUERY_STAMP) | prep
	@rm -f $@
	@$(AR) rcs $@ $(OBJ_FILES) $(VENDOR_PG_QUERY_MERGE_DIR)/*.o
	@$(RANLIB) $@

$(SHARED_LIB_REAL_PATH): $(OBJ_FILES) $(VENDOR_PG_QUERY_LIB) | prep
	@$(CC) -shared \
		-Wl,-soname,$(SHARED_LIB_SONAME) \
		-Wl,--version-script=./config/sqlparser.map \
		-o $@ $(OBJ_FILES) $(VENDOR_PG_QUERY_LIB) $(LDFLAGS) $(LDLIBS)

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

$(BIN_PATH)/%: tests/unit/%.c $(STATIC_LIB_PATH) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB_PATH) $(LDFLAGS) $(LDLIBS) -o $@

$(BIN_PATH)/examples/%: examples/%.c $(STATIC_LIB_PATH) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB_PATH) $(LDFLAGS) $(LDLIBS) -o $@

$(SQLPARSER_CLI_BIN): tools/sqlparser_cli.c $(STATIC_LIB_PATH) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB_PATH) $(LDFLAGS) $(LDLIBS) -o $@

$(SQLPARSER_BENCH_BIN): tools/sqlparser_bench.c $(STATIC_LIB_PATH) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB_PATH) $(SQLPARSER_BENCH_WRAP_LDFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

-include $(DEP_FILES)
