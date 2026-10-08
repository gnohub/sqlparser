/* Exercise the production private branch constructor directly. Compile this
 * translation unit against the static library without --whole-archive, as in
 * the scalar constructor tests: its included Oracle unit replaces that archive
 * member. Allocation strategy changes intentionally alter allocation ordinals;
 * every actual growth allocation is failed independently here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static size_t growth_calls, fail_growth;
static int observe_growth;
static void *branch_test_realloc(void *pointer, size_t size)
{
    if (observe_growth && ++growth_calls == fail_growth) return NULL;
    return realloc(pointer, size);
}
#define realloc branch_test_realloc
#include "../../src/dialect/sqlparser_dialect_oracle.c"
#undef realloc
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL branch growth line %d: %s\n", __LINE__, #x); abort(); } } while (0)

static size_t exercise(size_t fail_at)
{
    sqlparser_dialect_multi_insert_t multi = {0}, *clone = NULL;
    sqlparser_dialect_multi_insert_branch_t source = {0};
    sqlparser_error_t error;
    size_t i, j, failures = 0U;
    multi.mode = SQLPARSER_DIALECT_MULTI_INSERT_ALL;
    growth_calls = 0U;
    fail_growth = fail_at;
    observe_growth = 1;
    for (i = 0U; i < 129U; ++i) {
        sqlparser_dialect_multi_insert_branch_t *old = multi.branches;
        size_t capacity = multi.branch_capacity, calls = growth_calls;
        sqlparser_status_t status;
        source.condition_group_id = i + 7U;
        status = sqlparser_oracle_multi_insert_add_branch(&multi, &source, &error);
        if (status != SQLPARSER_STATUS_OK) {
            CHECK(status == SQLPARSER_STATUS_NO_MEMORY);
            CHECK(multi.branches == old && multi.branch_count == i && multi.branch_capacity == capacity);
            CHECK(error.code == SQLPARSER_STATUS_NO_MEMORY);
            ++failures;
            fail_growth = 0U;
            CHECK(sqlparser_oracle_multi_insert_add_branch(&multi, &source, &error) == SQLPARSER_STATUS_OK);
        }
        CHECK(multi.branch_count == i + 1U && multi.branch_capacity >= multi.branch_count);
        if (i < capacity) CHECK(growth_calls == calls);
        for (j = 0U; j <= i; ++j) {
            CHECK(multi.branches[j].ordinal == j);
            CHECK(multi.branches[j].condition_group_id == j + 7U);
        }
    }
    observe_growth = 0;
    CHECK(failures == (fail_at != 0U));
    CHECK(multi.branch_capacity == 256U);
    CHECK(sqlparser_oracle_multi_insert_clone(&multi, &clone, &error) == SQLPARSER_STATUS_OK);
    CHECK(clone->branch_count == multi.branch_count && clone->branch_capacity == clone->branch_count);
    CHECK(clone->branches != multi.branches);
    /* Exact-size clone storage can grow safely if reused by the constructor. */
    CHECK(sqlparser_oracle_multi_insert_add_branch(clone, &source, &error) == SQLPARSER_STATUS_OK);
    CHECK(clone->branch_count == 130U && clone->branch_capacity == 258U);
    sqlparser_oracle_multi_insert_destroy(clone);
    free(multi.branches);
    return growth_calls;
}
int main(void)
{
    size_t i, count = exercise(0U);
    sqlparser_dialect_multi_insert_t oversized = {0}, *clone = NULL;
    sqlparser_dialect_multi_insert_branch_t source = {0};
    sqlparser_error_t error;
    CHECK(count == 7U);
    for (i = 1U; i <= count; ++i) (void)exercise(i);
    oversized.branch_count = SIZE_MAX / sizeof(source);
    oversized.branch_capacity = oversized.branch_count;
    CHECK(sqlparser_oracle_multi_insert_add_branch(&oversized, &source, &error) == SQLPARSER_STATUS_RESOURCE_LIMIT);
    CHECK(oversized.branches == NULL);
    ++oversized.branch_count;
    CHECK(sqlparser_oracle_multi_insert_clone(&oversized, &clone, &error) == SQLPARSER_STATUS_RESOURCE_LIMIT);
    CHECK(clone == NULL);
    puts("Oracle branch geometric growth and allocation-failure checks passed");
    return 0;
}
