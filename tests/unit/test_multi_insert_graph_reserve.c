/* Direct storage-hint tests against the production view translation unit.
 * Link to the static library without --whole-archive so this view unit replaces
 * its archive member. Complete graph semantics remain covered by the existing
 * Oracle graph classification / owned-commit differential tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static size_t hint_calls, hint_size;
static int fail_hint;
static void *hint_realloc(void *pointer, size_t size)
{
    ++hint_calls;
    hint_size = size;
    return fail_hint ? NULL : realloc(pointer, size);
}
#define realloc hint_realloc
#include "../../src/core/sqlparser_view.c"
#undef realloc
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL graph reserve line %d: %s\n", __LINE__, #x); abort(); } } while (0)

int main(void)
{
    size_t capacity = 3U, calls, i;
    uint64_t *items = malloc(capacity * sizeof(*items));
    void *old;
    CHECK(items != NULL);
    for (i = 0U; i < 3U; ++i) items[i] = UINT64_C(0x12340000) + i;
    sqlparser_graph_try_reserve_multi_insert_array((void **)&items, &capacity, 3U, 4U, sizeof(*items));
    CHECK(capacity == 7U && hint_calls == 1U && hint_size == 7U * sizeof(*items));
    for (i = 0U; i < 3U; ++i) CHECK(items[i] == UINT64_C(0x12340000) + i);
    old = items;
    calls = hint_calls;
    sqlparser_graph_try_reserve_multi_insert_array((void **)&items, &capacity, 3U, 2U, sizeof(*items));
    CHECK(items == old && capacity == 7U && hint_calls == calls);
    fail_hint = 1;
    sqlparser_graph_try_reserve_multi_insert_array((void **)&items, &capacity, 7U, 1U, sizeof(*items));
    CHECK(items == old && capacity == 7U && hint_calls == calls + 1U);
    for (i = 0U; i < 3U; ++i) CHECK(items[i] == UINT64_C(0x12340000) + i);
    calls = hint_calls;
    sqlparser_graph_try_reserve_multi_insert_array((void **)&items, &capacity, SIZE_MAX, 1U, sizeof(*items));
    sqlparser_graph_try_reserve_multi_insert_array((void **)&items, &capacity, 0U, SIZE_MAX / sizeof(*items) + 1U, sizeof(*items));
    sqlparser_graph_try_reserve_multi_insert_array((void **)&items, &capacity, 0U, 100U, 0U);
    CHECK(items == old && capacity == 7U && hint_calls == calls);
    /* Exact reservations need no final shrinking allocation. */
    sqlparser_query_graph_shrink_array((void **)&items, &capacity, 7U, sizeof(*items));
    CHECK(items == old && hint_calls == calls);
    /* After a hint miss, the existing incremental reserve is still usable. */
    fail_hint = 0;
    CHECK(sqlparser_query_graph_reserve_array((void **)&items, &capacity, 8U, sizeof(*items), NULL) == 0);
    CHECK(capacity >= 8U);
    for (i = 0U; i < 3U; ++i) CHECK(items[i] == UINT64_C(0x12340000) + i);
    free(items);
    puts("Multi-insert graph exact-reserve and fallback checks passed");
    return 0;
}
