/* GNU linker wrappers. Observe the exact ordinal, type and sizes; no timing.
 * PostgreSQL memory-context allocations retain the established exclusion,
 * since its native OOM path can longjmp or abort outside sqlparser ownership. */
typedef struct { void *pointer; size_t identity, bytes; } scalar_slot;
typedef struct { int kind; size_t count, size, old_identity; int success; } scalar_event;
static scalar_slot scalar_ledger[65536];
static scalar_event scalar_events[65536];
static size_t scalar_live, scalar_end, scalar_calls, scalar_fail;
static int scalar_active;
static unsigned scalar_native_depth;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
struct MemoryContextData;
struct MemoryContextData *__real_pg_query_enter_memory_context(void);
void __real_pg_query_exit_memory_context(struct MemoryContextData *);
struct MemoryContextData *__wrap_pg_query_enter_memory_context(void)
{ ++scalar_native_depth; return __real_pg_query_enter_memory_context(); }
void __wrap_pg_query_exit_memory_context(struct MemoryContextData *p)
{ __real_pg_query_exit_memory_context(p); CHECK(scalar_native_depth); --scalar_native_depth; }
static size_t scalar_find(void *p)
{
    size_t i;
    if (p && scalar_live) for (i = 0; i < scalar_end; ++i)
        if (scalar_ledger[i].pointer == p) return i;
    return COUNT(scalar_ledger);
}
static void scalar_track(void *p, size_t identity, size_t bytes)
{
    size_t i;
    if (!p) return;
    CHECK(scalar_find(p) == COUNT(scalar_ledger));
    for (i = 0; i < scalar_end && scalar_ledger[i].pointer; ++i) {}
    CHECK(i < COUNT(scalar_ledger));
    scalar_ledger[i] = (scalar_slot){p, identity, bytes}; ++scalar_live;
    if (i == scalar_end) ++scalar_end;
}
static scalar_event *scalar_begin_event(int kind, size_t count, size_t size, void *old)
{
    scalar_event *e; size_t slot;
    if (!scalar_active || scalar_native_depth) return NULL;
    CHECK(scalar_calls < COUNT(scalar_events));
    e = &scalar_events[scalar_calls++]; slot = scalar_find(old);
    *e = (scalar_event){kind, count, size,
        old ? (slot < COUNT(scalar_ledger) ? scalar_ledger[slot].identity : (size_t)-1) : 0, 0};
    return e;
}
void *__wrap_malloc(size_t n)
{
    scalar_event *e = scalar_begin_event(1, 1, n, NULL); void *p;
    if (e && scalar_calls == scalar_fail) return NULL;
    p = __real_malloc(n);
    if (e) { e->success = p != NULL; scalar_track(p, scalar_calls, n); }
    return p;
}
void *__wrap_calloc(size_t n, size_t size)
{
    scalar_event *e = scalar_begin_event(2, n, size, NULL); void *p;
    if (e && scalar_calls == scalar_fail) return NULL;
    p = __real_calloc(n, size);
    if (e) { e->success = p != NULL; scalar_track(p, scalar_calls, n * size); }
    return p;
}
void *__wrap_realloc(void *p, size_t n)
{
    size_t slot = scalar_find(p);
    scalar_event *e = scalar_begin_event(3, 1, n, p); void *q;
    if (e && scalar_calls == scalar_fail) return NULL;
    q = __real_realloc(p, n);
    if (e) e->success = q != NULL;
    if (q || !n) {
        if (slot < COUNT(scalar_ledger)) {
            scalar_ledger[slot].pointer = q;
            scalar_ledger[slot].bytes = n;
            if (!q) --scalar_live;
        } else if (e) scalar_track(q, scalar_calls, n);
    }
    return q;
}
void __wrap_free(void *p)
{
    size_t slot = scalar_find(p);
    if (slot < COUNT(scalar_ledger)) {
        scalar_ledger[slot].pointer = NULL; --scalar_live;
    }
    __real_free(p);
}
static void scalar_start(size_t fail)
{
    CHECK(!scalar_live && !scalar_native_depth && !scalar_active);
    scalar_calls = scalar_end = 0; scalar_fail = fail; scalar_active = 1;
}
static void scalar_stop(void)
{ CHECK(scalar_active && !scalar_native_depth); scalar_active = 0; }
static void scalar_record_allocations(void)
{
    size_t i; record_number(scalar_calls);
    for (i = 0; i < scalar_calls; ++i) {
        const scalar_event *e = &scalar_events[i];
        record_number(e->kind); record_number(e->count); record_number(e->size);
        record_number(e->old_identity); record_number(e->success);
    }
}
static void scalar_record_residue(void)
{
    size_t i; record_number(scalar_live);
    for (i = 0; i < scalar_end; ++i) if (scalar_ledger[i].pointer) {
        record_number(scalar_ledger[i].identity); record_number(scalar_ledger[i].bytes);
    }
}
static void scalar_reclaim_recorded_residue(void)
{
    size_t i;
    /* Legacy constructor OOM has a known leak. Observe and compare its exact
     * allocation identity/size first, then reclaim only test-ledger residues
     * so the next independent trial and sanitizer run remain useful. Never
     * use this for ordinary primitive/lifetime checks, which require zero. */
    CHECK(!scalar_active && !scalar_native_depth);
    for (i = 0; i < scalar_end; ++i) if (scalar_ledger[i].pointer) {
        __real_free(scalar_ledger[i].pointer); scalar_ledger[i].pointer = NULL; --scalar_live;
    }
    CHECK(!scalar_live);
}
