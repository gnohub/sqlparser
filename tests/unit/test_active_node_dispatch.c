/* Regression coverage for active node dispatch. */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/core/sqlparser_ast_internal.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "active Node [%s]:%d: %s\n", case_name, __LINE__, #x); exit(1); } } while (0)
static const char *case_name = "initialization";
static size_t alternatives, fixtures, message_checks, name_checks, string_checks;
static size_t location_checks, slot_checks, public_statements;
static int dump;

typedef struct { void *items[4096]; size_t count, serial; } pool_t;
typedef struct {
    char **slot;
    ProtobufCMessage *location_owner, *field_owner;
    const char *owner_type, *field_name;
} name_t;
typedef struct {
    ProtobufCMessage *messages[4096];
    char **strings[4096];
    name_t names[4096];
    PgQuery__Node **slots[4096];
    ProtobufCMessage *parents[4096];
    size_t n_messages, n_strings, n_names, n_slots;
    PgQuery__SelectStmt *tail;
} reference_t;
static reference_t reference;

static int same_text(const char *a, const char *b)
{ return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0); }

static int32_t *reference_location(ProtobufCMessage *message)
{
    unsigned i;
    if (message == NULL || message->descriptor == NULL) return NULL;
    for (i = 0U; i < message->descriptor->n_fields; ++i) {
        const ProtobufCFieldDescriptor *f = &message->descriptor->fields[i];
        if (f->type == PROTOBUF_C_TYPE_INT32 && f->label != PROTOBUF_C_LABEL_REPEATED &&
            strcmp(f->name, "location") == 0) return (int32_t *)((char *)message + f->offset);
    }
    return NULL;
}

static void check_fields(ProtobufCMessage *message)
{
    const ProtobufCMessageDescriptor *d = message->descriptor;
    const ProtobufCFieldDescriptor *fields, *expected = NULL;
    unsigned count = UINT_MAX, i;
    int32_t *location = reference_location(message);
    fields = sqlparser_proto_traversal_fields(message, &count);
    if (d == &pg_query__node__descriptor) {
        for (i = 0U; i < d->n_fields; ++i)
            if ((int)d->fields[i].id == (int)((PgQuery__Node *)message)->node_case)
                expected = &d->fields[i];
        CHECK(fields == expected && count == (expected != NULL ? 1U : 0U));
        CHECK(location == NULL);
    } else {
        CHECK(fields == d->fields && count == d->n_fields);
        for (i = 0U; i < count; ++i) CHECK(&fields[i] == &d->fields[i]);
    }
    CHECK(sqlparser_proto_location_slot(message) == location);
    location_checks++;
}

static int name_container(const ProtobufCMessageDescriptor *d)
{
    return d->short_name != NULL && (strcmp(d->short_name, "Node") == 0 ||
        strcmp(d->short_name, "List") == 0 || strcmp(d->short_name, "String") == 0);
}

static void collect_string(ProtobufCMessage *message,
    const ProtobufCFieldDescriptor *field, char **slot,
    const sqlparser_name_context_t *context, ProtobufCMessage *location_owner)
{
    const ProtobufCMessageDescriptor *d = message->descriptor;
    name_t *name;
    CHECK(reference.n_strings < COUNT(reference.strings));
    reference.strings[reference.n_strings++] = slot;
    if (*slot == NULL || **slot == '\0' ||
        !sqlparser_name_atom_is_identifier(message, context, d, field)) return;
    CHECK(reference.n_names < COUNT(reference.names));
    name = &reference.names[reference.n_names++];
    name->slot = slot; name->location_owner = location_owner;
    name->field_owner = context != NULL ? context->field_owner : NULL;
    name->owner_type = name_container(d) && context != NULL ? context->owner_type : NULL;
    name->field_name = name_container(d) && context != NULL ? context->field_name : NULL;
    if (name->owner_type == NULL) name->owner_type = d->short_name;
    if (name->field_name == NULL) name->field_name = field->name;
    CHECK(name->owner_type != NULL && name->field_name != NULL);
}

static void collect_full(ProtobufCMessage *, const sqlparser_name_context_t *);
static void collect_child(ProtobufCMessage *parent, ProtobufCMessage **slot,
    const sqlparser_name_context_t *context)
{
    if (*slot != NULL && (*slot)->descriptor == &pg_query__node__descriptor &&
        !sqlparser_node_is_grouping_wrapper((PgQuery__Node *)*slot) &&
        !sqlparser_node_is_mysql_dml_tail_wrapper((PgQuery__Node *)*slot, reference.tail)) {
        CHECK(reference.n_slots < COUNT(reference.slots));
        reference.slots[reference.n_slots] = (PgQuery__Node **)slot;
        reference.parents[reference.n_slots++] = parent;
    }
    collect_full(*slot, context);
}

/* Full descriptor-order traversal is deliberately independent of the new
 * field-selection helper. Name classification/context helpers are unchanged
 * production policy, while ordering, selected slots and location ownership
 * are collected here and compared to all three actual common walkers. */
static void collect_full(ProtobufCMessage *message, const sqlparser_name_context_t *context)
{
    const ProtobufCMessageDescriptor *d;
    ProtobufCMessage *location_owner;
    unsigned i;
    char *base;
    if (message == NULL) return;
    CHECK(reference.n_messages < COUNT(reference.messages));
    reference.messages[reference.n_messages++] = message;
    d = message->descriptor;
    if (d == NULL) return;
    base = (char *)message;
    location_owner = reference_location(message) != NULL ? message :
        (context != NULL ? context->location_owner : NULL);
    for (i = 0U; i < d->n_fields; ++i) {
        const ProtobufCFieldDescriptor *f = &d->fields[i];
        size_t j, n;
        if ((f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
            *(int *)(base + f->quantifier_offset) != (int)f->id) continue;
        if (f->type == PROTOBUF_C_TYPE_STRING) {
            if (f->label == PROTOBUF_C_LABEL_REPEATED) {
                char **items = *(char ***)(base + f->offset);
                n = *(size_t *)(base + f->quantifier_offset);
                for (j = 0U; j < n; ++j) collect_string(message, f, &items[j], context, location_owner);
            } else collect_string(message, f, (char **)(base + f->offset), context, location_owner);
        } else if (f->type == PROTOBUF_C_TYPE_MESSAGE) {
            sqlparser_name_context_t next = sqlparser_next_name_context(message, d, f, context);
            next.location_owner = location_owner;
            if (f->label == PROTOBUF_C_LABEL_REPEATED) {
                ProtobufCMessage **items = *(ProtobufCMessage ***)(base + f->offset);
                n = *(size_t *)(base + f->quantifier_offset);
                for (j = 0U; j < n; ++j) collect_child(message, &items[j], &next);
            } else collect_child(message, (ProtobufCMessage **)(base + f->offset), &next);
        }
    }
}

static int accept_even_location(ProtobufCMessage *message)
{
    int32_t *location = reference_location(message);
    return location == NULL || (*location & 1) == 0;
}

static void compare_walkers(ProtobufCMessage *root, const sqlparser_name_context_t *context,
    PgQuery__SelectStmt *tail)
{
    size_t i, j;
    sqlparser_error_t error;
    sqlparser_name_search_t names = {0};
    sqlparser_string_search_t strings = {0};
    int32_t locations[4096];
    memset(&reference, 0, sizeof(reference)); reference.tail = tail;
    collect_full(root, context);
    for (i = 0U; i < reference.n_messages; ++i) {
        ProtobufCMessage *message = reference.messages[i];
        int32_t *location = reference_location(message);
        locations[i] = location != NULL ? *location : 0;
        if (message->descriptor != NULL) check_fields(message);
        for (int filtered = 0; filtered <= 1; ++filtered) {
            sqlparser_message_search_t search = {0};
            size_t expected = 0U, ordinal = 0U;
            if (filtered && !accept_even_location(message)) continue;
            for (j = 0U; j < reference.n_messages; ++j) {
                ProtobufCMessage *other = reference.messages[j];
                if (other->descriptor == message->descriptor &&
                    (!filtered || accept_even_location(other))) {
                    if (j < i) ordinal++;
                    expected++;
                }
            }
            search.descriptor = message->descriptor;
            search.accept = filtered ? accept_even_location : NULL;
            CHECK(sqlparser_walk_message_tree(root, &search, &error) == SQLPARSER_STATUS_OK);
            CHECK(search.seen == expected && search.target_message == NULL);
            search.seen = 0U; search.want_target = 1; search.target_index = ordinal;
            CHECK(sqlparser_walk_message_tree(root, &search, &error) == SQLPARSER_STATUS_OK);
            CHECK(search.target_message == message && search.seen == ordinal + 1U);
            search.seen = 0U; search.target_message = NULL; search.target_index = expected;
            CHECK(sqlparser_walk_message_tree(root, &search, &error) == SQLPARSER_STATUS_OK);
            CHECK(search.target_message == NULL && search.seen == expected);
            message_checks++;
        }
    }
    CHECK(sqlparser_walk_message_names(root, context, &names) == SQLPARSER_STATUS_OK);
    CHECK(names.seen == reference.n_names && names.target_slot == NULL);
    for (i = 0U; i <= reference.n_names; ++i) {
        sqlparser_name_view_t view = {0};
        memset(&names, 0, sizeof(names));
        names.want_target = 1; names.target_index = i; names.name_view = &view;
        CHECK(sqlparser_walk_message_names(root, context, &names) == SQLPARSER_STATUS_OK);
        if (i == reference.n_names) {
            CHECK(names.target_slot == NULL && names.seen == i); continue;
        }
        CHECK(names.seen == i + 1U && names.target_slot == reference.names[i].slot);
        CHECK(names.target_owner == reference.names[i].location_owner);
        CHECK(names.target_field_owner == reference.names[i].field_owner);
        CHECK(same_text(view.owner_type, reference.names[i].owner_type));
        CHECK(same_text(view.field_name, reference.names[i].field_name));
        CHECK(view.value == *reference.names[i].slot);
        memset(&names, 0, sizeof(names)); names.match_slot = reference.names[i].slot;
        CHECK(sqlparser_walk_message_names(root, context, &names) == SQLPARSER_STATUS_OK);
        CHECK(names.target_slot == reference.names[i].slot && names.target_index == i);
        CHECK(names.seen == reference.n_names); name_checks++;
    }
    CHECK(sqlparser_walk_message_strings(root, &strings) == SQLPARSER_STATUS_OK);
    CHECK(strings.seen == reference.n_strings && strings.target_slot == NULL);
    for (i = 0U; i <= reference.n_strings; ++i) {
        memset(&strings, 0, sizeof(strings)); strings.want_target = 1; strings.target_index = i;
        CHECK(sqlparser_walk_message_strings(root, &strings) == SQLPARSER_STATUS_OK);
        if (i == reference.n_strings) {
            CHECK(strings.target_slot == NULL && strings.seen == i); continue;
        }
        CHECK(strings.target_slot == reference.strings[i] && strings.seen == i + 1U);
        memset(&strings, 0, sizeof(strings)); strings.match_slot = reference.strings[i];
        strings.match_value = *reference.strings[i];
        CHECK(sqlparser_walk_message_strings(root, &strings) == SQLPARSER_STATUS_OK);
        CHECK(strings.target_slot == reference.strings[i] && strings.target_index == i);
        CHECK(strings.seen == i + 1U); string_checks++;
    }
    for (i = 0U; i < reference.n_messages; ++i) {
        int32_t *location = reference_location(reference.messages[i]);
        if (location != NULL) CHECK(*location == locations[i]);
    }
    fixtures++;
}

static void *pool_alloc(pool_t *pool, size_t size)
{
    void *p = calloc(1U, size);
    CHECK(p != NULL && pool->count < COUNT(pool->items));
    pool->items[pool->count++] = p; return p;
}
static ProtobufCMessage *new_message(pool_t *pool, const ProtobufCMessageDescriptor *d)
{
    ProtobufCMessage *message = pool_alloc(pool, d->sizeof_message);
    CHECK(d->message_init != NULL); d->message_init(message); return message;
}
static char *new_string(pool_t *pool)
{
    char *text = pool_alloc(pool, 40U);
    snprintf(text, 40U, "identifier_%zu", ++pool->serial); return text;
}
static void set_node(PgQuery__Node *node, int active, ProtobufCMessage *child)
{
    CHECK(sizeof(node->node_case) == sizeof(active));
    memcpy(&node->node_case, &active, sizeof(active));
    memcpy((char *)node + pg_query__node__descriptor.fields[0].offset, &child, sizeof(child));
}
static ProtobufCMessage *fixture_message(pool_t *pool, const ProtobufCMessageDescriptor *d, unsigned depth)
{
    ProtobufCMessage *message = new_message(pool, d);
    char *base = (char *)message;
    unsigned used[16], n_used = 0U, i;
    int32_t *location = reference_location(message);
    if (location != NULL) *location = (int32_t)(17U + ++pool->serial);
    if (d == &pg_query__node__descriptor) {
        const ProtobufCMessageDescriptor *child_d;
        int active;
        switch (pool->serial++ % 3U) {
            case 0: child_d = &pg_query__string__descriptor; active = PG_QUERY__NODE__NODE_STRING; break;
            case 1: child_d = &pg_query__range_var__descriptor; active = PG_QUERY__NODE__NODE_RANGE_VAR; break;
            default: child_d = &pg_query__a__const__descriptor; active = PG_QUERY__NODE__NODE_A_CONST; break;
        }
        set_node((PgQuery__Node *)message, active, fixture_message(pool, child_d, 0U));
        return message;
    }
    if (d == &pg_query__a__const__descriptor) {
        PgQuery__AConst *value = (PgQuery__AConst *)message;
        value->val_case = PG_QUERY__A__CONST__VAL_SVAL;
        value->sval = (PgQuery__String *)fixture_message(pool, &pg_query__string__descriptor, 0U);
        return message;
    }
    for (i = 0U; i < d->n_fields; ++i) {
        const ProtobufCFieldDescriptor *f = &d->fields[i];
        if ((f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U) {
            unsigned k;
            for (k = 0U; k < n_used && used[k] != f->quantifier_offset; ++k) {}
            if (k != n_used) continue;
            CHECK(n_used < COUNT(used)); used[n_used++] = f->quantifier_offset;
            *(int *)(base + f->quantifier_offset) = (int)f->id;
        }
        if (f->type == PROTOBUF_C_TYPE_STRING) {
            if (f->label == PROTOBUF_C_LABEL_REPEATED) {
                char **items = pool_alloc(pool, 4U * sizeof(*items));
                items[0] = new_string(pool); items[1] = NULL;
                items[2] = (char *)""; items[3] = new_string(pool);
                *(char ***)(base + f->offset) = items;
                *(size_t *)(base + f->quantifier_offset) = 4U;
            } else *(char **)(base + f->offset) = new_string(pool);
        } else if (f->type == PROTOBUF_C_TYPE_MESSAGE && depth != 0U) {
            const ProtobufCMessageDescriptor *child_d = f->descriptor;
            if (f->label == PROTOBUF_C_LABEL_REPEATED) {
                ProtobufCMessage **items = pool_alloc(pool, 3U * sizeof(*items));
                items[0] = fixture_message(pool, child_d, depth - 1U);
                items[2] = fixture_message(pool, child_d, depth - 1U);
                *(ProtobufCMessage ***)(base + f->offset) = items;
                *(size_t *)(base + f->quantifier_offset) = 3U;
            } else *(ProtobufCMessage **)(base + f->offset) = fixture_message(pool, child_d, depth - 1U);
        }
    }
    return message;
}

static void schema_cases(void)
{
    const ProtobufCMessageDescriptor *d = &pg_query__node__descriptor;
    unsigned i;
    CHECK(d->n_fields == 268U);
    for (i = 0U; i < d->n_fields; ++i) {
        const ProtobufCFieldDescriptor *f = &d->fields[i];
        pool_t pool = {0};
        PgQuery__Node wrapper = PG_QUERY__NODE__INIT;
        ProtobufCMessage *child;
        case_name = f->name;
        CHECK(f->type == PROTOBUF_C_TYPE_MESSAGE && (f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U);
        CHECK(f->offset == d->fields[0].offset);
        child = fixture_message(&pool, f->descriptor, 2U);
        set_node(&wrapper, (int)f->id, child);
        compare_walkers(&wrapper.base, NULL, NULL);
        /* An active field remains selected when its child pointer is absent. */
        set_node(&wrapper, (int)f->id, NULL);
        compare_walkers(&wrapper.base, NULL, NULL);
        CHECK(reference.n_messages == 1U && reference.n_names == 0U && reference.n_strings == 0U);
        while (pool.count != 0U) free(pool.items[--pool.count]);
        alternatives++;
    }
    {
        const int absent[] = {0, -1, INT_MIN, INT_MAX, 269, 99999};
        PgQuery__Node wrapper = PG_QUERY__NODE__INIT;
        PgQuery__String child = PG_QUERY__STRING__INIT;
        child.sval = (char *)"inactive child"; child.location = 319;
        case_name = "zero and unknown Node cases";
        for (i = 0U; i < COUNT(absent); ++i) {
            CHECK(protobuf_c_message_descriptor_get_field(d, (uint32_t)absent[i]) == NULL);
            set_node(&wrapper, absent[i], &child.base);
            compare_walkers(&wrapper.base, NULL, NULL);
            CHECK(reference.n_messages == 1U && reference.n_strings == 0U);
            CHECK(child.location == 319 && strcmp(child.sval, "inactive child") == 0);
        }
        /* Check all actual gaps instead of assuming dense protobuf field ids. */
        for (i = 1U; i < d->fields[d->n_fields - 1U].id; ++i)
            if (protobuf_c_message_descriptor_get_field(d, i) == NULL) {
                set_node(&wrapper, (int)i, &child.base);
                compare_walkers(&wrapper.base, NULL, NULL);
                CHECK(reference.n_messages == 1U);
            }
    }
    {
        struct custom_message { ProtobufCMessage base; int32_t location; } custom;
        ProtobufCMessageDescriptor descriptor = pg_query__node__descriptor;
        ProtobufCFieldDescriptor field = {0};
        unsigned count;
        case_name = "unrelated descriptor named Node";
        field.name = "location"; field.id = 700U; field.type = PROTOBUF_C_TYPE_INT32;
        field.label = PROTOBUF_C_LABEL_OPTIONAL;
        field.offset = offsetof(struct custom_message, location);
        descriptor.n_fields = 1U; descriptor.fields = &field;
        descriptor.sizeof_message = sizeof(custom);
        memset(&custom, 0, sizeof(custom)); custom.base.descriptor = &descriptor; custom.location = 271;
        CHECK(sqlparser_proto_traversal_fields(&custom.base, &count) == &field && count == 1U);
        CHECK(sqlparser_proto_location_slot(&custom.base) == &custom.location);
        compare_walkers(&custom.base, NULL, NULL); CHECK(custom.location == 271);
    }
    {
        ProtobufCMessage empty = {0};
        case_name = "empty entry points";
        compare_walkers(NULL, NULL, NULL); compare_walkers(&empty, NULL, NULL);
        CHECK(sqlparser_proto_location_slot(NULL) == NULL);
        CHECK(sqlparser_proto_location_slot(&empty) == NULL);
    }
}

static void print_text(const char *text)
{
    if (!dump) return;
    if (text == NULL) { printf("~"); return; }
    while (*text != '\0') printf("%02x", (unsigned char)*text++);
}
static void print_literal(const sqlparser_literal_view_t *v)
{
    if (!dump) return;
    printf("%d,%lld,%d,%d,", v->kind, v->integer_value, v->boolean_value, v->quoted_identifier);
    print_text(v->string_value); printf(","); print_text(v->float_value);
}
static void same_literal(const sqlparser_literal_view_t *a, const sqlparser_literal_view_t *b)
{
    CHECK(a->kind == b->kind && a->integer_value == b->integer_value);
    CHECK(a->boolean_value == b->boolean_value && a->quoted_identifier == b->quoted_identifier);
    CHECK(same_text(a->string_value, b->string_value) && same_text(a->float_value, b->float_value));
}
static void same_error(sqlparser_status_t a, const sqlparser_error_t *ea,
    sqlparser_status_t b, const sqlparser_error_t *eb)
{
    CHECK(a == b);
    if (a != SQLPARSER_STATUS_OK) {
        CHECK(ea->code == eb->code && ea->cursor == eb->cursor && ea->line == eb->line && ea->column == eb->column);
        CHECK(strcmp(ea->message, eb->message) == 0);
    }
    if (dump) { printf("|%d|", a); if (a != SQLPARSER_STATUS_OK) print_text(ea->message); printf("|"); }
}
static void selector_roundtrip(sqlparser_selector_t selector)
{
    sqlparser_selector_t parsed;
    sqlparser_error_t error;
    char *text = NULL;
    CHECK(sqlparser_selector_format(&selector, &text, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_selector_parse(text, &parsed, &error) == SQLPARSER_STATUS_OK);
    CHECK(parsed.kind == selector.kind && parsed.statement_index == selector.statement_index);
    CHECK(parsed.item_index == selector.item_index && parsed.row_index == selector.row_index && parsed.column_index == selector.column_index);
    print_text(text); sqlparser_string_free(text);
}

/* Include each valid ordinal and the first out-of-range ordinal. The transcript
 * is pointer-free, deterministic, and suitable for comparing the frozen library.
 * Public getter/selector pairs must agree within either implementation. */
static void public_ordinals(sqlparser_handle_t *handle, size_t statement)
{
    size_t family, count, i;
    sqlparser_error_t error, ea, eb;
    sqlparser_status_t a, b;
    sqlparser_selector_t selector = {0};
    selector.statement_index = statement;
    for (family = 0U; family < 6U; ++family) {
        switch (family) {
            case 0: selector.kind = SQLPARSER_SELECTOR_KIND_NAME; a = sqlparser_statement_name_count(handle, statement, &count, &error); break;
            case 1: selector.kind = SQLPARSER_SELECTOR_KIND_RELATION; a = sqlparser_statement_relation_count(handle, statement, &count, &error); break;
            case 2: selector.kind = SQLPARSER_SELECTOR_KIND_LITERAL; a = sqlparser_statement_literal_count(handle, statement, &count, &error); break;
            case 3: selector.kind = SQLPARSER_SELECTOR_KIND_WHERE_LITERAL; a = sqlparser_statement_where_literal_count(handle, statement, &count, &error); break;
            case 4: selector.kind = SQLPARSER_SELECTOR_KIND_CLAUSE; a = sqlparser_statement_clause_count(handle, statement, &count, &error); break;
            default: selector.kind = SQLPARSER_SELECTOR_KIND_WHERE; a = sqlparser_statement_where_count(handle, statement, &count, &error); break;
        }
        CHECK(a == SQLPARSER_STATUS_OK && count < 256U);
        if (dump) printf("F%zu:%zu:%zu\n", statement, family, count);
        for (i = 0U; i <= count; ++i) {
            selector.item_index = i; selector_roundtrip(selector);
            if (family == 0U) {
                sqlparser_name_view_t va = {0}, vb = {0};
                a = sqlparser_statement_name(handle, statement, i, &va, &ea);
                b = sqlparser_selector_name(handle, &selector, &vb, &eb); same_error(a, &ea, b, &eb);
                if (a == SQLPARSER_STATUS_OK) {
                    CHECK(same_text(va.owner_type, vb.owner_type) && same_text(va.field_name, vb.field_name) && same_text(va.value, vb.value));
                    print_text(va.owner_type); if (dump) printf(","); print_text(va.field_name); if (dump) printf(","); print_text(va.value);
                }
            } else if (family == 1U) {
                sqlparser_relation_view_t va = {0}, vb = {0};
                a = sqlparser_statement_relation(handle, statement, i, &va, &ea);
                b = sqlparser_selector_relation(handle, &selector, &vb, &eb); same_error(a, &ea, b, &eb);
                if (a == SQLPARSER_STATUS_OK) {
#define REL(member) CHECK(same_text(va.member, vb.member)); print_text(va.member); if (dump) printf(",")
                    REL(database_name); REL(schema_name); REL(table_name); REL(alias_name); REL(link_name);
#undef REL
                }
            } else if (family == 2U) {
                sqlparser_literal_view_t va = {0}, vb = {0};
                a = sqlparser_statement_literal(handle, statement, i, &va, &ea);
                b = sqlparser_selector_literal(handle, &selector, &vb, &eb); same_error(a, &ea, b, &eb);
                if (a == SQLPARSER_STATUS_OK) { same_literal(&va, &vb); print_literal(&va); }
            } else if (family == 3U) {
                sqlparser_where_literal_view_t va = {0}, vb = {0};
                a = sqlparser_statement_where_literal(handle, statement, i, &va, &ea);
                b = sqlparser_selector_where_literal(handle, &selector, &vb, &eb); same_error(a, &ea, b, &eb);
                if (a == SQLPARSER_STATUS_OK) {
                    CHECK(same_text(va.table_name, vb.table_name) && same_text(va.column_name, vb.column_name) && same_text(va.operator_name, vb.operator_name));
                    same_literal(&va.literal, &vb.literal); print_text(va.table_name); if (dump) printf(",");
                    print_text(va.column_name); if (dump) printf(","); print_text(va.operator_name); if (dump) printf(","); print_literal(&va.literal);
                }
            } else {
                char *va = NULL, *vb = NULL;
                if (family == 4U) {
                    sqlparser_clause_view_t ca = {0}, cb = {0};
                    a = sqlparser_statement_clause(handle, statement, i, &ca, &ea);
                    b = sqlparser_selector_clause(handle, &selector, &cb, &eb); same_error(a, &ea, b, &eb);
                    if (a == SQLPARSER_STATUS_OK) {
                        CHECK(ca.kind == cb.kind && ca.clause_index == cb.clause_index && ca.statement_index == cb.statement_index && ca.has_selector == cb.has_selector);
                        if (dump) printf("%d,%zu,%d,", ca.kind, ca.clause_index, ca.has_selector);
                        if (ca.has_selector) selector_roundtrip(ca.selector);
                    }
                    a = sqlparser_statement_clause_sql(handle, statement, i, &va, &ea);
                    b = sqlparser_selector_clause_sql(handle, &selector, &vb, &eb);
                } else {
                    a = sqlparser_statement_where_sql(handle, statement, i, &va, &ea);
                    b = sqlparser_selector_where_sql(handle, &selector, &vb, &eb);
                }
                same_error(a, &ea, b, &eb); CHECK(same_text(va, vb)); print_text(va);
                sqlparser_string_free(va); sqlparser_string_free(vb);
            }
            if (dump) printf("\n");
        }
    }
    public_statements++;
}

static void parsed_cases(void)
{
    static const struct { sqlparser_dialect_t dialect; const char *sql; } cases[] = {
        {SQLPARSER_DIALECT_POSTGRESQL, "WITH q AS (SELECT a, b FROM sch.t WHERE a = 1) SELECT q.a AS answer, lower(q.b), 'x' FROM q JOIN u ON q.a = u.a WHERE q.b LIKE 'a%' ESCAPE '!' AND EXISTS (SELECT 1 FROM v WHERE v.id = 7) GROUP BY q.a, q.b HAVING count(*) > 2 ORDER BY q.a LIMIT 3"},
        {SQLPARSER_DIALECT_POSTGRESQL, "(SELECT a FROM t WHERE x = 1 ORDER BY a LIMIT 2) UNION ALL (SELECT b FROM u WHERE y = 3) ORDER BY 1"},
        {SQLPARSER_DIALECT_POSTGRESQL, "WITH q AS (UPDATE u SET x = 'inner' WHERE id = 1 RETURNING id) UPDATE t SET a = 2, b = upper('mixed') FROM q WHERE t.id = q.id RETURNING t.a"},
        {SQLPARSER_DIALECT_POSTGRESQL, "INSERT INTO t(a,b) VALUES (1,'one'),(2,'two') ON CONFLICT(a) DO UPDATE SET b = 'new' WHERE t.a > 0 RETURNING a"},
        {SQLPARSER_DIALECT_POSTGRESQL, "DELETE FROM t USING u WHERE t.id = u.id AND u.flag = TRUE RETURNING t.id; SELECT (x).a, CAST('3' AS integer), ARRAY[1,2], $1 FROM s"},
        {SQLPARSER_DIALECT_POSTGRESQL, "MERGE INTO dst d USING src s ON d.id = s.id WHEN MATCHED AND s.v > 1 THEN UPDATE SET v = s.v WHEN NOT MATCHED THEN INSERT(id,v) VALUES(s.id,'new')"},
        {SQLPARSER_DIALECT_POSTGRESQL, "CREATE TABLE typed(id integer PRIMARY KEY, value text DEFAULT 'seed', CHECK (id > 0)); CREATE INDEX idx ON typed(value) WHERE id > 2"},
        {SQLPARSER_DIALECT_MYSQL, "UPDATE `t` SET a = 'one', b = 2 WHERE id = 3 ORDER BY a LIMIT 4"},
        {SQLPARSER_DIALECT_MYSQL, "DELETE FROM `t` WHERE id = 8 ORDER BY id LIMIT 2"},
        {SQLPARSER_DIALECT_MYSQL, "SELECT `a`, concat('x', b) FROM `t` WHERE id IN (1,2) ORDER BY a LIMIT 2,5"},
        {SQLPARSER_DIALECT_ORACLE, "SELECT t.a, NVL(t.b, 'fallback') FROM tab t WHERE t.id = :key UNION ALL SELECT u.a, u.b FROM other_tab u WHERE u.id = 2 ORDER BY 1"},
        {SQLPARSER_DIALECT_SQLSERVER, "SELECT TOP (3) [a], [b] FROM [dbo].[t] WHERE [id] = 2 ORDER BY [a]"}
    };
    size_t i, statement, j;
    for (i = 0U; i < COUNT(cases); ++i) {
        sqlparser_parse_options_t options;
        sqlparser_handle_t *handle = NULL;
        sqlparser_error_t error;
        case_name = cases[i].sql;
        sqlparser_parse_options_default(&options); options.dialect = cases[i].dialect;
        CHECK(sqlparser_parse_with_options(cases[i].sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_handle_ensure_ast(handle, &error) == SQLPARSER_STATUS_OK);
        if (dump) printf("CASE%zu:%d\n", i, cases[i].dialect);
        for (statement = 0U; statement < sqlparser_statement_count(handle); ++statement) {
            PgQuery__Node *root = NULL;
            PgQuery__SelectStmt *tail = NULL;
            CHECK(sqlparser_get_statement_node(handle, statement, &root, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_get_mysql_dml_tail_select(handle, statement, root, &tail, &error) == SQLPARSER_STATUS_OK);
            compare_walkers(&root->base, NULL, tail);
            for (j = 0U; j <= reference.n_slots; ++j) {
                PgQuery__Node **slot = NULL;
                ProtobufCMessage *parent = NULL;
                size_t ordinal = SIZE_MAX;
                sqlparser_status_t status = sqlparser_get_statement_node_slot_by_index(handle, statement, j, &slot, &parent, &error);
                if (j == reference.n_slots) { CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT && slot == NULL); continue; }
                CHECK(status == SQLPARSER_STATUS_OK && slot == reference.slots[j] && parent == reference.parents[j]);
                CHECK(sqlparser_find_statement_node_index_by_node(handle, statement, *slot, &ordinal, &error) == SQLPARSER_STATUS_OK && ordinal == j);
                if (dump) printf("NODE%zu:%zu:%d:%s\n", statement, j, (int)(*slot)->node_case, parent->descriptor->short_name);
                slot_checks++;
            }
            public_ordinals(handle, statement);
        }
        sqlparser_handle_destroy(handle);
    }
}

int main(int argc, char **argv)
{
    dump = argc == 2 && strcmp(argv[1], "--dump") == 0;
    schema_cases(); parsed_cases();
    CHECK(alternatives == 268U && public_statements >= 12U);
    if (!dump) printf("Active Node dispatch: %zu alternatives, %zu fixtures, %zu message, %zu name, %zu string, %zu location, %zu node-slot checks; %zu public statements passed\n",
        alternatives, fixtures, message_checks, name_checks, string_checks, location_checks, slot_checks, public_statements);
    return 0;
}
