/* Regression coverage for ast rebase plan. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_rebase.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "AST rebase plan [%s]:%d: %s\n", case_name, __LINE__, #x); exit(1); } } while (0)

static const char *case_name = "initialization";
static size_t cases, schema_admitted, schema_missed, heap_writes;

typedef struct {
    unsigned char *bytes;
    size_t length;
} snapshot_t;

typedef struct {
    PgQuery__AConst **slots[128];
    int32_t locations[128];
    int restricted[128];
    size_t count, restricted_count;
    sqlparser_ast_rebase_plan_t *plan;
    int32_t reject_location;
    int rejected_after_heap_growth;
} owners_t;

static snapshot_t snapshot(const ProtobufCMessage *message)
{
    snapshot_t result;
    result.length = protobuf_c_message_get_packed_size(message);
    result.bytes = malloc(result.length != 0U ? result.length : 1U);
    CHECK(result.bytes != NULL);
    CHECK(protobuf_c_message_pack(message, result.bytes) == result.length);
    return result;
}

static void unchanged(const ProtobufCMessage *message, const snapshot_t *before)
{
    snapshot_t after = snapshot(message);
    CHECK(after.length == before->length);
    CHECK(memcmp(after.bytes, before->bytes, after.length) == 0);
    free(after.bytes);
}

static void plan_zeroed(const sqlparser_ast_rebase_plan_t *plan)
{
    const unsigned char *bytes = (const unsigned char *)plan;
    size_t index;
    for (index = 0U; index < sizeof(*plan); index++) CHECK(bytes[index] == 0U);
}

/* Fixed callback storage: no allocation and no AST writes. The late-reject
 * case observes the plan but deliberately rejects only after heap growth. */
static int capture_owner(PgQuery__AConst **slot, int under_type_name, void *opaque)
{
    owners_t *owners = opaque;
    size_t index = owners->count;
    CHECK(slot != NULL && *slot != NULL && index < COUNT(owners->slots));
    owners->slots[index] = slot;
    owners->locations[index] = (*slot)->location;
    owners->restricted[index] = under_type_name;
    owners->count++;
    owners->restricted_count += under_type_name != 0;
    if ((*slot)->location == owners->reject_location) {
        CHECK(owners->plan != NULL);
        owners->rejected_after_heap_growth =
            owners->plan->count > SQLPARSER_AST_REBASE_INLINE_WRITES &&
            owners->plan->items != owners->plan->inline_writes;
        return 0;
    }
    return 1;
}

static owners_t empty_owners(void)
{
    owners_t result = {0};
    result.reject_location = -1;
    return result;
}

static PgQuery__AConst *owner_at(const owners_t *owners, int32_t location)
{
    size_t index;
    PgQuery__AConst *result = NULL;
    for (index = 0U; index < owners->count; index++) {
        if (owners->locations[index] == location) {
            CHECK(result == NULL);
            result = *owners->slots[index];
        }
    }
    CHECK(result != NULL);
    return result;
}

static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_handle_t *handle = NULL;
    sqlparser_parse_options_t options;
    sqlparser_error_t error;
    sqlparser_parse_options_default(&options);
    options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
    CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
    CHECK(handle != NULL);
    CHECK(sqlparser_handle_ensure_ast(handle, &error) == SQLPARSER_STATUS_OK);
    CHECK(handle->ast != NULL);
    return handle;
}

static char *prepare_edits(const char *source, const char *const *tokens,
    const char *const *replacements, size_t count,
    sqlparser_surface_source_edit_t *items)
{
    size_t index, old_cursor = 0U, cursor = 0U, capacity = strlen(source) + 1U;
    char *result;
    for (index = 0U; index < count; index++) capacity += strlen(replacements[index]);
    result = malloc(capacity);
    CHECK(result != NULL);
    for (index = 0U; index < count; index++) {
        const char *found = strstr(source + old_cursor, tokens[index]);
        size_t gap;
        CHECK(found != NULL);
        items[index].source_start = (size_t)(found - source);
        items[index].source_end = items[index].source_start + strlen(tokens[index]);
        items[index].replacement = (char *)replacements[index];
        items[index].replacement_length = strlen(replacements[index]);
        gap = items[index].source_start - old_cursor;
        memcpy(result + cursor, source + old_cursor, gap);
        cursor += gap;
        memcpy(result + cursor, replacements[index], items[index].replacement_length);
        cursor += items[index].replacement_length;
        old_cursor = items[index].source_end;
    }
    strcpy(result + cursor, source + old_cursor);
    return result;
}

/* Keep all coordinates in the rebased tree, including literal wrapper
 * coordinates, and normalize only the changed literal values for comparison. */
static void normalize_value(PgQuery__AConst *old, const PgQuery__AConst *fresh)
{
    CHECK(old->location == fresh->location && old->val_case == fresh->val_case);
    if (old->val_case == PG_QUERY__A__CONST__VAL_SVAL) {
        char *value = sqlparser_strdup(fresh->sval->sval);
        CHECK(value != NULL && old->sval != NULL);
        CHECK(old->sval->location == fresh->sval->location);
        free(old->sval->sval);
        old->sval->sval = value;
    } else {
        CHECK(old->val_case == PG_QUERY__A__CONST__VAL_IVAL);
        CHECK(old->ival != NULL && fresh->ival != NULL);
        old->ival->ival = fresh->ival->ival;
    }
}

static void successful(const char *name, const char *source,
    const char *const *tokens, const char *const *replacements, size_t count,
    int expect_zero, int expect_heap, size_t restricted_count)
{
    sqlparser_surface_source_edit_t items[8] = {{0}}, identity;
    sqlparser_surface_source_edits_t edits = {items, count, COUNT(items)}, self;
    sqlparser_ast_rebase_plan_t plan = {0}, fresh_plan = {0};
    owners_t owners = empty_owners(), fresh_owners = empty_owners();
    sqlparser_handle_t *handle, *fresh;
    snapshot_t before, expected;
    char *new_sql;
    size_t index, old_length = strlen(source), new_length;
    int64_t delta = 0;
    case_name = name;
    CHECK(count <= COUNT(items));
    new_sql = prepare_edits(source, tokens, replacements, count, items);
    new_length = strlen(new_sql);
    handle = parse(source);
    fresh = parse(new_sql);
    before = snapshot(&handle->ast->base);
    CHECK(sqlparser_ast_rebase_edits_valid(&edits, old_length, new_length));
    CHECK(sqlparser_ast_rebase_plan_build(&plan, handle->ast, &edits,
        old_length, new_length, capture_owner, &owners) == 1);
    unchanged(&handle->ast->base, &before);
    CHECK(owners.restricted_count == restricted_count);
    if (expect_zero) CHECK(plan.count == 0U && plan.items == plan.inline_writes);
    if (expect_heap) {
        /* More than 128 entries exercises inline-to-heap and heap realloc. */
        CHECK(plan.count > 2U * SQLPARSER_AST_REBASE_INLINE_WRITES);
        CHECK(plan.items != plan.inline_writes && plan.capacity >= plan.count);
        heap_writes = plan.count;
    }
    for (index = 0U; index < plan.count; index++)
        CHECK(*plan.items[index].slot != plan.items[index].value);
    identity = items[0];
    identity.source_end = identity.source_start + identity.replacement_length;
    self.items = &identity; self.count = 1U; self.capacity = 1U;
    CHECK(sqlparser_ast_rebase_plan_build(&fresh_plan, fresh->ast, &self,
        new_length, new_length, capture_owner, &fresh_owners) == 1);
    CHECK(fresh_plan.count == 0U && owners.count == fresh_owners.count);
    sqlparser_ast_rebase_plan_release(&fresh_plan);
    sqlparser_ast_rebase_plan_apply(&plan);
    sqlparser_ast_rebase_plan_release(&plan);
    plan_zeroed(&plan);
    sqlparser_ast_rebase_plan_release(&plan);
    for (index = 0U; index < count; index++) {
        int32_t mapped = (int32_t)((int64_t)items[index].source_start + delta);
        normalize_value(owner_at(&owners, (int32_t)items[index].source_start),
            owner_at(&fresh_owners, mapped));
        delta += (int64_t)items[index].replacement_length -
            (int64_t)(items[index].source_end - items[index].source_start);
    }
    expected = snapshot(&fresh->ast->base);
    unchanged(&handle->ast->base, &expected);
    free(expected.bytes); free(before.bytes); free(new_sql);
    sqlparser_handle_destroy(handle);
    sqlparser_handle_destroy(fresh);
    cases++;
}

static void ordinary_sql(void)
{
    const char *tokens[] = {"'old'"};
    const char *equal[] = {"'new'"};
    const char *utf8[] = {"'caf\xc3\xa9 ''quoted'' \xe6\x9d\xb1\xe4\xba\xac'"};
    const char *mixed_tokens[] = {"'a'", "12345", "'long original'", "'tail'"};
    const char *mixed[] = {"'\xe6\x9d\xb1\xe4\xba\xac cafe'", "9", "'x'", "'end larger'"};
    const char *type_tokens[] = {"'a'"};
    const char *type_values[] = {"'a longer string'"};
    successful("equal byte length", "SELECT 'old' AS label, t.col, 123 FROM data t WHERE t.id = $1;",
        tokens, equal, 1U, 1, 0, 0U);
    successful("growing UTF-8", "SELECT 'old' AS label, (SELECT r.id FROM rhs r WHERE r.id = $2) FROM lhs l JOIN rhs r ON l.id = r.id WHERE l.kind = $1; -- tail\n",
        tokens, utf8, 1U, 0, 0, 0U);
    successful("mixed growth shrink statements", "SELECT 'a' AS first, 12345 AS number, 'long original' AS last, t.id FROM data t; SELECT 'tail' AS word, $1;",
        mixed_tokens, mixed, COUNT(mixed), 0, 0, 0U);
    successful("TypeName ancestry", "SELECT 'a', 7::numeric(8,2), CAST(9 AS numeric(10,3)), $1",
        type_tokens, type_values, 1U, 0, 0, 4U);
}

static void heap_and_late_rejection(void)
{
    char source[4096];
    size_t index, length;
    const char *tokens[] = {"'a'"};
    const char *values[] = {"'a substantially longer value'"};
    sqlparser_surface_source_edit_t edit = {0};
    sqlparser_surface_source_edits_t edits = {&edit, 1U, 1U};
    sqlparser_ast_rebase_plan_t plan = {0};
    owners_t owners = empty_owners();
    sqlparser_handle_t *handle;
    snapshot_t before;
    char *new_sql;
    length = (size_t)snprintf(source, sizeof(source), "SELECT 'a' AS start");
    for (index = 0U; index < 96U; index++) {
        int written = snprintf(source + length, sizeof(source) - length, ", c%zu", index);
        CHECK(written > 0 && (size_t)written < sizeof(source) - length);
        length += (size_t)written;
    }
    CHECK((size_t)snprintf(source + length, sizeof(source) - length,
        ", 'late' AS finish FROM t WHERE id = $1;") < sizeof(source) - length);
    successful("heap growth", source, tokens, values, 1U, 0, 1, 0U);
    case_name = "callback rejects after heap growth";
    new_sql = prepare_edits(source, tokens, values, 1U, &edit);
    handle = parse(source);
    before = snapshot(&handle->ast->base);
    owners.plan = &plan;
    owners.reject_location = (int32_t)(strstr(source, "'late'") - source);
    CHECK(sqlparser_ast_rebase_plan_build(&plan, handle->ast, &edits,
        strlen(source), strlen(new_sql), capture_owner, &owners) == 0);
    CHECK(owners.rejected_after_heap_growth);
    plan_zeroed(&plan);
    unchanged(&handle->ast->base, &before);
    sqlparser_ast_rebase_plan_release(&plan);
    sqlparser_ast_rebase_plan_release(&plan);
    plan_zeroed(&plan);
    /* A released failed plan is reusable, with no partial callback proof. */
    owners = empty_owners();
    CHECK(sqlparser_ast_rebase_plan_build(&plan, handle->ast, &edits,
        strlen(source), strlen(new_sql), capture_owner, &owners) == 1);
    CHECK(plan.items != plan.inline_writes);
    unchanged(&handle->ast->base, &before);
    owners = empty_owners();
    CHECK(sqlparser_ast_rebase_plan_build(&plan, handle->ast, &edits,
        strlen(source), strlen(new_sql), capture_owner, &owners) == 1);
    sqlparser_ast_rebase_plan_release(&plan);
    free(before.bytes); free(new_sql); sqlparser_handle_destroy(handle);
    cases++;
}

typedef struct {
    PgQuery__ParseResult ast;
    PgQuery__RawStmt raw;
    PgQuery__Node node;
    PgQuery__RawStmt *statements[1];
} fixture_t;

static void fixture_init(fixture_t *fixture)
{
    pg_query__parse_result__init(&fixture->ast);
    pg_query__raw_stmt__init(&fixture->raw);
    pg_query__node__init(&fixture->node);
    fixture->statements[0] = &fixture->raw;
    fixture->ast.n_stmts = 1U;
    fixture->ast.stmts = fixture->statements;
    fixture->raw.stmt = &fixture->node;
}

static int build_fixture(fixture_t *fixture, sqlparser_ast_rebase_plan_t *plan,
    owners_t *owners)
{
    sqlparser_surface_source_edit_t edit = {1U, 2U, "99", 2U};
    sqlparser_surface_source_edits_t edits = {&edit, 1U, 1U};
    return sqlparser_ast_rebase_plan_build(plan, &fixture->ast, &edits,
        64U, 65U, capture_owner, owners);
}

static void negative_lengths(void)
{
    fixture_t fixture;
    PgQuery__Query query = PG_QUERY__QUERY__INIT;
    sqlparser_ast_rebase_plan_t plan = {0};
    owners_t owners = empty_owners();
    snapshot_t before;
    fixture_init(&fixture);
    fixture.node.node_case = PG_QUERY__NODE__NODE_QUERY;
    fixture.node.query = &query;
    case_name = "negative Query length";
    query.stmt_len = -1;
    before = snapshot(&fixture.ast.base);
    CHECK(build_fixture(&fixture, &plan, &owners) == 0);
    plan_zeroed(&plan); unchanged(&fixture.ast.base, &before); free(before.bytes);
    cases++;
    case_name = "negative RawStmt length";
    query.stmt_len = 0;
    fixture.raw.stmt_len = -1;
    before = snapshot(&fixture.ast.base);
    CHECK(build_fixture(&fixture, &plan, &owners) == 0);
    plan_zeroed(&plan); unchanged(&fixture.ast.base, &before); free(before.bytes);
    sqlparser_ast_rebase_plan_release(&plan);
    cases++;
}

/* A schema breadth check, not a claim that initialized defaults represent
 * valid SQL. Unsupported source metadata must conservatively reject. */
static void generated_node_alternatives(void)
{
    const ProtobufCMessageDescriptor *node_descriptor = &pg_query__node__descriptor;
    unsigned alternative;
    size_t string_locations = 0U, numeric_locations = 0U;
    for (alternative = 0U; alternative < node_descriptor->n_fields; alternative++) {
        const ProtobufCFieldDescriptor *branch = &node_descriptor->fields[alternative];
        const ProtobufCMessageDescriptor *descriptor = branch->descriptor;
        int negative;
        CHECK(branch->type == PROTOBUF_C_TYPE_MESSAGE);
        for (negative = 0; negative <= 1; negative++) {
            fixture_t fixture;
            ProtobufCMessage *child = malloc(descriptor->sizeof_message);
            sqlparser_ast_rebase_plan_t plan = {0};
            owners_t owners = empty_owners();
            snapshot_t before;
            unsigned index;
            int eligible;
            case_name = descriptor->name;
            CHECK(child != NULL);
            protobuf_c_message_init(descriptor, child);
            fixture_init(&fixture);
            fixture.node.node_case = (PgQuery__Node__NodeCase)branch->id;
            memcpy((unsigned char *)&fixture.node + branch->offset, &child, sizeof(child));
            for (index = 0U; index < descriptor->n_fields; index++) {
                const ProtobufCFieldDescriptor *field = &descriptor->fields[index];
                int coordinate = strcmp(field->name, "location") == 0 ||
                    strcmp(field->name, "name_location") == 0 ||
                    strcmp(field->name, "stmt_location") == 0;
                if (coordinate && field->type == PROTOBUF_C_TYPE_INT32 &&
                    field->label != PROTOBUF_C_LABEL_REPEATED &&
                    (field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) == 0U) {
                    int32_t value = negative ? -7 : 20;
                    memcpy((unsigned char *)child + field->offset, &value, sizeof(value));
                    numeric_locations++;
                } else if (strcmp(field->name, "stmt_len") == 0 &&
                    field->type == PROTOBUF_C_TYPE_INT32) {
                    int32_t value = negative ? 0 : 10;
                    memcpy((unsigned char *)child + field->offset, &value, sizeof(value));
                } else if (strcmp(field->name, "location") == 0 &&
                    field->type == PROTOBUF_C_TYPE_STRING) {
                    char *path = sqlparser_strdup("/unchanged/tablespace/path");
                    CHECK(path != NULL);
                    memcpy((unsigned char *)child + field->offset, &path, sizeof(path));
                    string_locations++;
                }
            }
            before = snapshot(&fixture.ast.base);
            eligible = build_fixture(&fixture, &plan, &owners);
            CHECK(eligible == 0 || eligible == 1);
            unchanged(&fixture.ast.base, &before);
            if (eligible) {
                schema_admitted++;
                sqlparser_ast_rebase_plan_apply(&plan);
                for (index = 0U; index < descriptor->n_fields; index++) {
                    const ProtobufCFieldDescriptor *field = &descriptor->fields[index];
                    int coordinate = strcmp(field->name, "location") == 0 ||
                        strcmp(field->name, "name_location") == 0 ||
                        strcmp(field->name, "stmt_location") == 0;
                    if (coordinate && field->type == PROTOBUF_C_TYPE_INT32 &&
                        field->label != PROTOBUF_C_LABEL_REPEATED &&
                        (field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) == 0U) {
                        int32_t value;
                        memcpy(&value, (unsigned char *)child + field->offset, sizeof(value));
                        CHECK(value == (negative ? -7 : 21));
                    } else if (strcmp(field->name, "location") == 0 &&
                        field->type == PROTOBUF_C_TYPE_STRING) {
                        const char *path;
                        memcpy(&path, (unsigned char *)child + field->offset, sizeof(path));
                        CHECK(strcmp(path, "/unchanged/tablespace/path") == 0);
                    }
                }
                if (negative) CHECK(plan.count == 0U);
            } else {
                schema_missed++;
                plan_zeroed(&plan);
            }
            sqlparser_ast_rebase_plan_release(&plan);
            sqlparser_ast_rebase_plan_release(&plan);
            free(before.bytes);
            protobuf_c_message_free_unpacked(child, NULL);
        }
    }
    CHECK(string_locations == 2U && numeric_locations > 0U);
    CHECK(schema_admitted + schema_missed == 2U * node_descriptor->n_fields);
    cases++;
}

static void repeated_nodes_and_active_oneofs(void)
{
    fixture_t fixture;
    PgQuery__List list = PG_QUERY__LIST__INIT;
    PgQuery__Node nodes[2] = {PG_QUERY__NODE__INIT, PG_QUERY__NODE__INIT};
    PgQuery__Node *items[2] = {&nodes[0], &nodes[1]};
    PgQuery__AConst constants[2] = {PG_QUERY__A__CONST__INIT, PG_QUERY__A__CONST__INIT};
    PgQuery__String strings[2] = {PG_QUERY__STRING__INIT, PG_QUERY__STRING__INIT};
    sqlparser_ast_rebase_plan_t plan = {0};
    owners_t owners = empty_owners();
    snapshot_t before;
    size_t index;
    case_name = "repeated nodes and active oneofs";
    fixture_init(&fixture);
    fixture.node.node_case = PG_QUERY__NODE__NODE_LIST;
    fixture.node.list = &list;
    list.n_items = COUNT(items); list.items = items;
    for (index = 0U; index < COUNT(items); index++) {
        nodes[index].node_case = PG_QUERY__NODE__NODE_A_CONST;
        nodes[index].a_const = &constants[index];
        constants[index].location = index == 0U ? 1 : 20;
        constants[index].val_case = PG_QUERY__A__CONST__VAL_SVAL;
        constants[index].sval = &strings[index];
    }
    before = snapshot(&fixture.ast.base);
    CHECK(build_fixture(&fixture, &plan, &owners) == 1);
    CHECK(owners.count == 2U && plan.count == 1U);
    unchanged(&fixture.ast.base, &before);
    sqlparser_ast_rebase_plan_apply(&plan);
    sqlparser_ast_rebase_plan_release(&plan);
    CHECK(constants[0].location == 1 && constants[1].location == 21);
    CHECK(strings[0].location == 0 && strings[1].location == 0);
    free(before.bytes);
    /* Invalid active selection and an absent repeated array fail closed. */
    nodes[1].node_case = (PgQuery__Node__NodeCase)999999;
    owners = empty_owners();
    CHECK(build_fixture(&fixture, &plan, &owners) == 0);
    plan_zeroed(&plan);
    nodes[1].node_case = PG_QUERY__NODE__NODE_A_CONST;
    list.items = NULL;
    owners = empty_owners();
    CHECK(build_fixture(&fixture, &plan, &owners) == 0);
    plan_zeroed(&plan);
    list.items = items;
    constants[1].sval = NULL;
    owners = empty_owners();
    CHECK(build_fixture(&fixture, &plan, &owners) == 0);
    plan_zeroed(&plan);
    cases++;
}

int main(void)
{
    ordinary_sql();
    heap_and_late_rejection();
    negative_lengths();
    generated_node_alternatives();
    repeated_nodes_and_active_oneofs();
    printf("AST rebase plan: %zu contract groups passed; %zu heap writes; "
        "%zu initialized schema cases admitted, %zu conservatively rejected\n",
        cases, heap_writes, schema_admitted, schema_missed);
    return 0;
}
