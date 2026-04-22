CONFIG := ./config/Makefile.config
include $(CONFIG)

.DEFAULT_GOAL := all

VERSION_STRING := $(shell cat ./VERSION 2>/dev/null)
JANSSON_CFLAGS := $(shell $(PKG_CONFIG) --cflags jansson 2>/dev/null)
JANSSON_LDLIBS := $(shell $(PKG_CONFIG) --libs jansson 2>/dev/null)

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

OBJ_FILES := $(foreach src,$(ALL_SRC),$(OBJ_PATH)/$(patsubst src/%,%,$(src:.c=.o)))
DEP_FILES := $(OBJ_FILES:.o=.d)
UNIT_TEST_BINS := $(foreach src,$(UNIT_TEST_SRC),$(BIN_PATH)/$(notdir $(src:.c=)))
EXAMPLE_BINS := $(foreach src,$(EXAMPLE_SRC),$(BIN_PATH)/examples/$(notdir $(src:.c=)))
SQLPARSER_CLI_BIN := $(BIN_PATH)/sqlparser_cli
SQLPARSER_BENCH_BIN := $(BIN_PATH)/sqlparser_bench
SQLPARSER_CLI_BATCH_FIXTURE := ./tests/cases/sql_batch_input.json
SQLPARSER_CLI_BATCH_OUTPUT := $(BUILD_PATH)/tests/sqlparser_cli_batch_output.json
SQLPARSER_BENCH_WRAP_LDFLAGS := \
	-Wl,--wrap=malloc \
	-Wl,--wrap=calloc \
	-Wl,--wrap=realloc \
	-Wl,--wrap=free \
	-Wl,--wrap=strdup \
	-Wl,--wrap=strndup

STATIC_LIB_PATH := $(LIB_PATH)/lib$(LIB_NAME).a
SHARED_LIB_PATH := $(LIB_PATH)/lib$(LIB_NAME).so
SHARED_LIB_SONAME := lib$(LIB_NAME).so.$(SONAME_MAJOR)

BASE_CPPFLAGS := \
	-I./include \
	-I./src/internal \
	-I./vendor/libpg_query \
	-I./vendor/libpg_query/vendor \
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

CPPFLAGS := $(BASE_CPPFLAGS) $(EXTRA_CPPFLAGS)
CFLAGS := $(BASE_CFLAGS) $(EXTRA_CFLAGS)
LDFLAGS := $(BASE_LDFLAGS) $(EXTRA_LDFLAGS)
LDLIBS := $(BASE_LDLIBS) $(EXTRA_LDLIBS)

.PHONY: all prep vendor static shared clean vendor-clean print-config test install cli bench-build test-cli-batch examples

all: prep static shared cli
	@echo "Build finished: $(STATIC_LIB_PATH) $(SHARED_LIB_PATH) $(SQLPARSER_CLI_BIN)"

prep:
	@mkdir -p $(OBJ_PATH) $(BIN_PATH) $(LIB_PATH) $(VENDOR_PG_QUERY_MERGE_DIR)

vendor: $(VENDOR_PG_QUERY_LIB)

static: $(STATIC_LIB_PATH)

shared: $(SHARED_LIB_PATH)

cli: $(SQLPARSER_CLI_BIN)

examples: $(EXAMPLE_BINS)

bench-build: $(SQLPARSER_BENCH_BIN)

test: cli $(UNIT_TEST_BINS) $(EXAMPLE_BINS) test-cli-batch
	@set -e; \
	for test_bin in $(UNIT_TEST_BINS) $(EXAMPLE_BINS); do \
		"$$test_bin"; \
	done

test-cli-batch: $(SQLPARSER_CLI_BIN) $(SQLPARSER_CLI_BATCH_FIXTURE) | prep
	@mkdir -p $(BUILD_PATH)/tests
	@$(SQLPARSER_CLI_BIN) --batch-file $(SQLPARSER_CLI_BATCH_FIXTURE) --output $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"total": 31' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"succeeded": 30' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"failed": 1' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"parse_tree"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"summary"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"model"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"sqlparser.model/v1"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"selected_columns"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"join_columns"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"where_columns"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"insert_columns"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"update_columns"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"all_referenced_columns"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"TransactionStmt"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"DropStmt"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"ViewStmt"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"select-cte"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"insert-from-select"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"drop-view"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"quoted-identifiers"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"literal-semicolon"' $(SQLPARSER_CLI_BATCH_OUTPUT)
	@grep -q '"parse-error"' $(SQLPARSER_CLI_BATCH_OUTPUT)

install: all
	@mkdir -p $(DESTDIR)$(INCLUDEDIR)/sqlparser $(DESTDIR)$(LIBDIR)
	@cp include/sqlparser/*.h $(DESTDIR)$(INCLUDEDIR)/sqlparser/
	@cp $(STATIC_LIB_PATH) $(DESTDIR)$(LIBDIR)/
	@cp $(SHARED_LIB_PATH) $(DESTDIR)$(LIBDIR)/

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

$(VENDOR_PG_QUERY_LIB):
	@$(MAKE) -C $(VENDOR_PG_QUERY_DIR) build CFLAGS="$(VENDOR_PG_QUERY_BUILD_CFLAGS)"

$(VENDOR_PG_QUERY_STAMP): $(VENDOR_PG_QUERY_LIB) | prep
	@rm -rf $(VENDOR_PG_QUERY_MERGE_DIR)
	@mkdir -p $(VENDOR_PG_QUERY_MERGE_DIR)
	@cd $(VENDOR_PG_QUERY_MERGE_DIR) && $(AR) x $(abspath $(VENDOR_PG_QUERY_LIB))
	@touch $@

$(STATIC_LIB_PATH): $(OBJ_FILES) $(VENDOR_PG_QUERY_STAMP) | prep
	@rm -f $@
	@$(AR) rcs $@ $(OBJ_FILES) $(VENDOR_PG_QUERY_MERGE_DIR)/*.o
	@$(RANLIB) $@

$(SHARED_LIB_PATH): $(OBJ_FILES) $(VENDOR_PG_QUERY_LIB) | prep
	@$(CC) -shared \
		-Wl,-soname,$(SHARED_LIB_SONAME) \
		-Wl,--version-script=./config/sqlparser.map \
		-o $@ $(OBJ_FILES) $(VENDOR_PG_QUERY_LIB) $(LDFLAGS) $(LDLIBS)

$(OBJ_PATH)/%.o: src/%.c
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
