/* Row-local structural edits retain independent cell and dialect-owner lifetimes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser/sqlparser.h"
#include "sqlparser_test_failure.h"

static sqlparser_dialect_t dialect;
static const char *stage;
static size_t fault_index;
static sqlparser_error_t error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s dialect=%s stage=%s fault=%zu error=%s\n", __FILE__, __LINE__, #x, sqlparser_dialect_name(dialect), stage, fault_index, error.message); abort(); } } while (0)

#ifdef SQLPARSER_STRUCTURAL_ALLOC_WRAPPERS
static int armed;
static size_t allocation_calls, allocation_fail_at, allocation_failures;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
static int reject_allocation(void)
{
    if (!armed) return 0;
    allocation_calls++;
    if (allocation_calls != allocation_fail_at) return 0;
    allocation_failures++;
    return 1;
}
void *__wrap_malloc(size_t n) { return reject_allocation() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t s) { return reject_allocation() ? NULL : __real_calloc(n, s); }
void *__wrap_realloc(void *p, size_t n) { return reject_allocation() ? NULL : __real_realloc(p, n); }
#endif

static int mysql_style(void)
{
    return dialect == SQLPARSER_DIALECT_MYSQL ||
        dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL ||
        dialect == SQLPARSER_DIALECT_KINGBASE_MYSQL;
}

/* PostgreSQL does not promise national-prefix retention for source SQL.
 * Its compatibility modes follow the same contract. Other dialects exercise
 * independent national-literal owner records through every structural edit. */
static const char *national_sql(const char *sql)
{
    return dialect == SQLPARSER_DIALECT_POSTGRESQL ||
        dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL ||
        dialect == SQLPARSER_DIALECT_KINGBASE_POSTGRESQL ? sql + 1 : sql;
}

static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_parse_options_t options;
    sqlparser_handle_t *handle = NULL;
    sqlparser_parse_options_default(&options);
    options.dialect = dialect;
    CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
    CHECK(handle != NULL);
    return handle;
}

static void apply(sqlparser_handle_t *handle, sqlparser_patch_t patch)
{
    sqlparser_patch_list_t list = {&patch, 1U};
    CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
}

static void cell_sql(sqlparser_handle_t *handle, size_t row, size_t column, const char *expected)
{
    char *sql = NULL;
    CHECK(sqlparser_insert_cell_sql(handle, 0U, row, column, &sql, &error) == SQLPARSER_STATUS_OK);
    if (strcmp(sql, expected) != 0) fprintf(stderr, "actual cell=%s expected=%s\n", sql, expected);
    CHECK(strcmp(sql, expected) == 0);
    sqlparser_string_free(sql);
}

static void stable_roundtrip(sqlparser_handle_t *handle)
{
    sqlparser_handle_t *reparsed;
    char *sql = NULL, *again = NULL;
    CHECK(sqlparser_deparse(handle, &sql, &error) == SQLPARSER_STATUS_OK);
    reparsed = parse(sql);
    CHECK(sqlparser_deparse(reparsed, &again, &error) == SQLPARSER_STATUS_OK);
    if (strcmp(sql, again) != 0) fprintf(stderr, "roundtrip first=%s second=%s\n", sql, again);
    CHECK(strcmp(sql, again) == 0);
    sqlparser_string_free(sql);
    sqlparser_string_free(again);
    sqlparser_handle_destroy(reparsed);
}

static const char values_sql[] = "INSERT INTO t(a,b) VALUES (1,'one'),(2,'two'),(3,'three')";

static void structural_rounds(void)
{
    sqlparser_handle_t *handle;
    sqlparser_patch_t patch = {0};
    size_t row, count;
    stage = "structural-rounds";
    handle = parse(values_sql);
    patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
    patch.selector = "stmt[0].insert_columns";
    patch.name = "first_copy";
    patch.default_sql = national_sql("N'new'");
    apply(handle, patch);
    for (row = 0U; row < 3U; row++) cell_sql(handle, row, 0U, national_sql("N'new'"));
    stable_roundtrip(handle);

    patch.index = 2U;
    patch.name = "middle_copy";
    patch.default_sql = NULL;
    patch.source_selector = "stmt[0].insert_cell[0][0]";
    apply(handle, patch);
    for (row = 0U; row < 3U; row++) cell_sql(handle, row, 2U, national_sql("N'new'"));
    stable_roundtrip(handle);

    patch = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
        .selector="stmt[0].insert_cell[0][0]", .sql=national_sql("N'changed'")};
    apply(handle, patch);
    cell_sql(handle, 0U, 0U, national_sql("N'changed'"));
    cell_sql(handle, 1U, 0U, national_sql("N'new'"));
    cell_sql(handle, 2U, 0U, national_sql("N'new'"));
    for (row = 0U; row < 3U; row++) cell_sql(handle, row, 2U, national_sql("N'new'"));

    patch = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_COLUMN,
        .selector="stmt[0].insert_columns", .index=2U};
    apply(handle, patch);
    stable_roundtrip(handle);
    patch.index = 0U;
    apply(handle, patch);
    cell_sql(handle, 0U, 0U, "1");
    cell_sql(handle, 1U, 1U, "'two'");
    stable_roundtrip(handle);

    patch = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN,
        .selector="stmt[0].insert_columns", .index=(size_t)-1,
        .name="last_copy", .default_sql="'last'"};
    apply(handle, patch);
    for (row = 0U; row < 3U; row++) cell_sql(handle, row, 2U, "'last'");
    stable_roundtrip(handle);
    patch = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_ROW,
        .selector="stmt[0].insert_row[1]"};
    apply(handle, patch);
    CHECK(sqlparser_insert_row_count(handle, 0U, &count, &error) == SQLPARSER_STATUS_OK && count == 2U);
    cell_sql(handle, 1U, 0U, "3");
    cell_sql(handle, 1U, 1U, "'three'");
    patch = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_COLUMN,
        .selector="stmt[0].insert_columns", .index=2U};
    apply(handle, patch);
    CHECK(sqlparser_insert_column_count(handle, 0U, &count, &error) == SQLPARSER_STATUS_OK && count == 2U);
    stable_roundtrip(handle);
    sqlparser_handle_destroy(handle);
}

static void boundary_deletions(void)
{
    static const struct {
        const char *sql, *selector;
        sqlparser_patch_op_t op;
        size_t index;
        sqlparser_status_t status;
    } cases[] = {
        {"INSERT INTO t(a) VALUES(1)", "stmt[0].insert_columns", SQLPARSER_PATCH_DELETE_COLUMN, 0U, SQLPARSER_STATUS_UNSUPPORTED},
        {"INSERT INTO t VALUES(1)", "stmt[0].insert_columns", SQLPARSER_PATCH_DELETE_COLUMN, 0U, SQLPARSER_STATUS_UNSUPPORTED},
        {"INSERT INTO t(a) VALUES(1)", "stmt[0].insert_row[0]", SQLPARSER_PATCH_DELETE_ROW, 0U, SQLPARSER_STATUS_UNSUPPORTED},
        {"INSERT INTO t(a,b) VALUES(1,2)", "stmt[0].insert_columns", SQLPARSER_PATCH_DELETE_COLUMN, 2U, SQLPARSER_STATUS_INVALID_ARGUMENT},
        {"INSERT INTO t(a,b) VALUES(1,2)", "stmt[0].insert_row[2]", SQLPARSER_PATCH_DELETE_ROW, 0U, SQLPARSER_STATUS_INVALID_ARGUMENT}
    };
    size_t i;
    stage = "delete-boundaries";
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        sqlparser_handle_t *handle = parse(cases[i].sql);
        sqlparser_patch_t patch = {.op=cases[i].op, .selector=cases[i].selector, .index=cases[i].index};
        sqlparser_patch_list_t list = {&patch, 1U};
        CHECK(sqlparser_apply_patch(handle, &list, &error) == cases[i].status);
        CHECK(sqlparser_test_failed_handle(handle));
        sqlparser_handle_destroy(handle);
    }
}

static void query_and_merge_deletions(void)
{
    sqlparser_handle_t *handle;
    sqlparser_patch_t patch = {.op=SQLPARSER_PATCH_DELETE_COLUMN,
        .selector="stmt[0].insert_columns", .index=0U};
    const char *query_sql = (dialect == SQLPARSER_DIALECT_ORACLE ||
        dialect == SQLPARSER_DIALECT_DAMENG ||
        dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE ||
        dialect == SQLPARSER_DIALECT_KINGBASE_ORACLE) ?
        "INSERT INTO t(a) SELECT 1 FROM dual" : "INSERT INTO t(a) SELECT 1";
    size_t count;
    stage = "query-delete-last-column";
    handle = parse(query_sql);
    apply(handle, patch);
    CHECK(sqlparser_insert_column_count(handle, 0U, &count, &error) == SQLPARSER_STATUS_OK && count == 0U);
    stable_roundtrip(handle);
    sqlparser_handle_destroy(handle);
    if (mysql_style()) return;
    stage = "merge-pair-compaction";
    handle = parse("MERGE INTO t USING s ON (t.id=s.id) WHEN NOT MATCHED THEN INSERT(a,b,c) VALUES(s.a,N'old',s.c)");
    patch.selector = "stmt[0].insert_branch_columns[0]";
    patch.index = 1U;
    apply(handle, patch);
    stable_roundtrip(handle);
    patch.index = 0U;
    apply(handle, patch);
    stable_roundtrip(handle);
    {
        sqlparser_patch_list_t list = {&patch, 1U};
        CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_UNSUPPORTED);
        CHECK(sqlparser_test_failed_handle(handle));
    }
    sqlparser_handle_destroy(handle);
}

#ifdef SQLPARSER_STRUCTURAL_ALLOC_WRAPPERS
static void allocation_sweep(void)
{
    sqlparser_patch_t operations[] = {
        {.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_columns", .index=1U, .name="copied", .default_sql=national_sql("N'new'")},
        {.op=SQLPARSER_PATCH_DELETE_COLUMN, .selector="stmt[0].insert_columns", .index=1U},
        {.op=SQLPARSER_PATCH_DELETE_ROW, .selector="stmt[0].insert_row[1]"}
    };
    size_t operation;
    stage = "allocation-sweep";
    for (operation = 0U; operation < sizeof(operations)/sizeof(operations[0]); operation++) {
        size_t total = 0U;
        char *expected = NULL;
        for (fault_index = 0U; fault_index <= total; fault_index++) {
            sqlparser_handle_t *handle = parse(values_sql);
            sqlparser_patch_list_t list = {&operations[operation], 1U};
            sqlparser_status_t status;
            char *actual = NULL;
            allocation_calls = allocation_failures = 0U;
            allocation_fail_at = fault_index;
            armed = 1;
            status = sqlparser_apply_patch(handle, &list, &error);
            armed = 0;
            if (fault_index == 0U) {
                total = allocation_calls;
                CHECK(status == SQLPARSER_STATUS_OK && total > 0U);
            } else {
                CHECK(allocation_failures == 1U);
            }
            if (status != SQLPARSER_STATUS_OK) {
                CHECK(sqlparser_test_failed_handle(handle));
            } else {
                CHECK(sqlparser_deparse(handle, &actual, &error) == SQLPARSER_STATUS_OK);
                if (fault_index == 0U) {
                    expected = actual;
                    actual = NULL;
                } else CHECK(strcmp(actual, expected) == 0);
            }
            sqlparser_string_free(actual);
            sqlparser_handle_destroy(handle);
        }
        sqlparser_string_free(expected);
        printf("structural allocation sweep dialect=%s operation=%zu allocations=%zu ok\n", sqlparser_dialect_name(dialect), operation, total);
    }
    fault_index = 0U;
}
#endif

int main(void)
{
    for (dialect = SQLPARSER_DIALECT_POSTGRESQL; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        structural_rounds();
        boundary_deletions();
        query_and_merge_deletions();
#ifdef SQLPARSER_STRUCTURAL_ALLOC_WRAPPERS
        allocation_sweep();
#endif
    }
    puts("structural row ownership and deletion tests passed");
    return 0;
}
