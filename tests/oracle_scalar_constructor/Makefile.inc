# Oracle scalar constructor equivalence. The selected Oracle C translation unit
# is included by the test before the static archive is linked, preserving real
# registered owners without copying or replacing an entire archive.
ORACLE_SCALAR_TEST_DIR := tests/oracle_scalar_constructor
ORACLE_SCALAR_TEST_BIN := $(BIN_PATH)/test_oracle_scalar_constructor
ORACLE_SCALAR_TEST_INPUTS := \
	$(ORACLE_SCALAR_TEST_DIR)/test_oracle_scalar_constructor.c \
	$(ORACLE_SCALAR_TEST_DIR)/frozen_legacy_parse_value_item.inc \
	$(ORACLE_SCALAR_TEST_DIR)/scalar_state.h \
	$(ORACLE_SCALAR_TEST_DIR)/scalar_alloc.h \
	tests/unit/test_oracle_owned_commit.c tests/unit/sqlparser_oracle_graph_records.h \
	tests/unit/sqlparser_test_failure.h src/dialect/sqlparser_dialect_oracle.c

.PHONY: test-oracle-scalar-constructor test-oracle-scalar-constructor-alloc
test-unit: test-oracle-scalar-constructor

$(ORACLE_SCALAR_TEST_BIN): $(ORACLE_SCALAR_TEST_INPUTS) $(STATIC_LIB_PATH) | prep
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -fno-builtin -DSCALAR_OPTIMIZED=1 \
		-DSCALAR_FROZEN_HELPER=\"../unit/test_oracle_owned_commit.c\" \
		-DSCALAR_ORACLE_SOURCE=\"../../src/dialect/sqlparser_dialect_oracle.c\" \
		$(ORACLE_SCALAR_TEST_DIR)/test_oracle_scalar_constructor.c $(STATIC_LIB_PATH) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
		-Wl,--wrap=pg_query_enter_memory_context -Wl,--wrap=pg_query_exit_memory_context \
		$(LDFLAGS) $(LDLIBS) -o $@

test-oracle-scalar-constructor: $(ORACLE_SCALAR_TEST_BIN)
	@$(ORACLE_SCALAR_TEST_BIN) --admission
	@$(ORACLE_SCALAR_TEST_BIN) --primitive >/dev/null
	@echo "PASS: Oracle scalar constructor independent legacy state/error/allocation parity"

test-oracle-scalar-constructor-alloc: $(ORACLE_SCALAR_TEST_BIN)
	@$(ORACLE_SCALAR_TEST_BIN) --primitive-oom >/dev/null
	@echo "PASS: Oracle scalar constructor independent legacy same-ordinal OOM parity"
