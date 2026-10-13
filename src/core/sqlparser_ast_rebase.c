#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_rebase.h"

/* The native owned AST is a tree, not a graph. A bounded recursion also
 * rejects accidental cycles during the read-only validation pass. */
#define SQLPARSER_REBASE_MAX_DEPTH 512U

typedef struct {
    const sqlparser_surface_source_edits_t *edits;
    size_t old_length;
    size_t new_length;
    size_t prefix_count;
    int64_t prefix_delta;
    sqlparser_ast_rebase_plan_t *plan;
    sqlparser_ast_rebase_literal_callback_t callback;
    void *callback_context;
    int no_memory;
} sqlparser_rebase_context_t;

int sqlparser_ast_rebase_edits_valid(
    const sqlparser_surface_source_edits_t *edits,
    size_t old_length, size_t new_length)
{
    size_t index, old_cursor = 0U, new_cursor = 0U;

    if (edits == NULL || edits->count == 0U || edits->items == NULL ||
        edits->count > SIZE_MAX / sizeof(*edits->items) ||
        old_length > INT32_MAX || new_length > INT32_MAX) {
        return 0;
    }
    for (index = 0U; index < edits->count; index++) {
        const sqlparser_surface_source_edit_t *edit = &edits->items[index];
        size_t gap;

        if (edit->source_start < old_cursor ||
            edit->source_start >= edit->source_end ||
            edit->source_end > old_length ||
            edit->replacement == NULL || edit->replacement_length == 0U) {
            return 0;
        }
        gap = edit->source_start - old_cursor;
        if (gap > new_length - new_cursor) {
            return 0;
        }
        new_cursor += gap;
        if (edit->replacement_length > new_length - new_cursor) {
            return 0;
        }
        new_cursor += edit->replacement_length;
        old_cursor = edit->source_end;
    }
    return old_length - old_cursor == new_length - new_cursor;
}

/* Unchanged coordinates need no transaction storage. Grow only after the
 * inline buffer fills; a failed malloc/realloc leaves the old storage owned
 * by the plan so build's common failure path can release it. */
static int sqlparser_rebase_record(sqlparser_rebase_context_t *context,
    int32_t *slot, int32_t value)
{
    sqlparser_ast_rebase_plan_t *plan = context->plan;

    if (*slot == value) {
        return 1;
    }
    if (plan->count == plan->capacity) {
        const size_t maximum = SIZE_MAX / sizeof(*plan->items);
        size_t capacity;
        sqlparser_ast_rebase_write_t *items;

        if (plan->capacity >= maximum) {
            context->no_memory = 1;
            return 0;
        }
        capacity = plan->capacity > maximum / 2U ? maximum : plan->capacity * 2U;
        if (plan->items == plan->inline_writes) {
            items = (sqlparser_ast_rebase_write_t *)malloc(capacity * sizeof(*items));
            if (items != NULL) {
                memcpy(items, plan->items, plan->count * sizeof(*items));
            }
        } else {
            items = (sqlparser_ast_rebase_write_t *)realloc(plan->items,
                capacity * sizeof(*items));
        }
        if (items == NULL) {
            context->no_memory = 1;
            return 0;
        }
        plan->items = items;
        plan->capacity = capacity;
    }
    plan->items[plan->count].slot = slot;
    plan->items[plan->count].value = value;
    plan->count++;
    return 1;
}

static int64_t sqlparser_rebase_edit_delta(
    const sqlparser_surface_source_edit_t *edit)
{
    return (int64_t)edit->replacement_length -
        (int64_t)(edit->source_end - edit->source_start);
}

/* The cursor avoids rescanning all earlier edits for the usual source-order
 * walk, but can move backwards for statement endpoints and parent locations.
 * At an edit's start only preceding deltas apply; at its end its own delta
 * applies too. An interior location has no unambiguous source identity. */
static int sqlparser_rebase_point(sqlparser_rebase_context_t *context,
    size_t point, int32_t *mapped)
{
    const sqlparser_surface_source_edits_t *edits = context->edits;
    int64_t result;

    if (point > context->old_length) {
        return 0;
    }
    while (context->prefix_count != 0U &&
           edits->items[context->prefix_count - 1U].source_end > point) {
        context->prefix_count--;
        context->prefix_delta -=
            sqlparser_rebase_edit_delta(&edits->items[context->prefix_count]);
    }
    while (context->prefix_count < edits->count &&
           edits->items[context->prefix_count].source_end <= point) {
        context->prefix_delta +=
            sqlparser_rebase_edit_delta(&edits->items[context->prefix_count]);
        context->prefix_count++;
    }
    if (context->prefix_count < edits->count &&
        edits->items[context->prefix_count].source_start < point) {
        return 0;
    }
    result = (int64_t)point + context->prefix_delta;
    if (result < 0 || result > INT32_MAX ||
        (uint64_t)result > context->new_length) {
        return 0;
    }
    *mapped = (int32_t)result;
    return 1;
}

/* RawStmt's final zero length means "to the end of the source". Earlier
 * zero lengths, overlaps, and edits outside statements cannot be certified.
 * Check these before the tree walk, while every span is still in old space. */
static int sqlparser_rebase_statement_spans_valid(
    const PgQuery__ParseResult *ast,
    const sqlparser_surface_source_edits_t *edits, size_t old_length)
{
    size_t index, edit_index = 0U, previous_end = 0U;

    if (ast->n_stmts == 0U || ast->stmts == NULL ||
        ast->n_stmts > SIZE_MAX / sizeof(*ast->stmts)) {
        return 0;
    }
    for (index = 0U; index < ast->n_stmts; index++) {
        const PgQuery__RawStmt *raw = ast->stmts[index];
        size_t start, end;

        if (raw == NULL || raw->base.descriptor != &pg_query__raw_stmt__descriptor ||
            raw->stmt == NULL || raw->stmt_location < 0 || raw->stmt_len < 0) {
            return 0;
        }
        start = (size_t)raw->stmt_location;
        if (start < previous_end || start > old_length ||
            (size_t)raw->stmt_len > old_length - start) {
            return 0;
        }
        if (raw->stmt_len == 0 && index + 1U != ast->n_stmts) {
            return 0;
        }
        end = raw->stmt_len != 0 ? start + (size_t)raw->stmt_len : old_length;
        while (edit_index < edits->count &&
               edits->items[edit_index].source_start < end) {
            const sqlparser_surface_source_edit_t *edit = &edits->items[edit_index];

            if (edit->source_start < start || edit->source_end > end) {
                return 0;
            }
            edit_index++;
        }
        previous_end = end;
    }
    return edit_index == edits->count;
}

static int sqlparser_rebase_statement(sqlparser_rebase_context_t *context,
    int32_t *location, int32_t *length)
{
    int32_t mapped_location = *location, mapped_length = *length;

    if (*length < 0) {
        return 0;
    }
    if (*location >= 0 &&
        !sqlparser_rebase_point(context, (size_t)*location, &mapped_location)) {
        return 0;
    }
    if (*length > 0) {
        int32_t mapped_end;
        size_t start;

        if (*location < 0) {
            return 0;
        }
        start = (size_t)*location;
        if ((size_t)*length > context->old_length - start ||
            !sqlparser_rebase_point(context, start + (size_t)*length, &mapped_end) ||
            mapped_end < mapped_location) {
            return 0;
        }
        mapped_length = mapped_end - mapped_location;
    }
    return sqlparser_rebase_record(context, location, mapped_location) &&
        sqlparser_rebase_record(context, length, mapped_length);
}

static int sqlparser_rebase_name_ends_with(const char *name, const char *suffix)
{
    size_t name_length = strlen(name), suffix_length = strlen(suffix);

    return name_length >= suffix_length &&
        strcmp(name + name_length - suffix_length, suffix) == 0;
}

/* Fail closed if a future schema adds another numeric source coordinate.
 * Message-valued limit/start/end offsets are expressions, not coordinates. */
static int sqlparser_rebase_unhandled_source_field(
    const ProtobufCFieldDescriptor *field)
{
    if (field->type == PROTOBUF_C_TYPE_MESSAGE ||
        field->type == PROTOBUF_C_TYPE_STRING) {
        return 0;
    }
    return strstr(field->name, "location") != NULL ||
        strncmp(field->name, "source_", 7U) == 0 ||
        sqlparser_rebase_name_ends_with(field->name, "_offset") ||
        sqlparser_rebase_name_ends_with(field->name, "_length") ||
        sqlparser_rebase_name_ends_with(field->name, "_len") ||
        sqlparser_rebase_name_ends_with(field->name, "_start") ||
        sqlparser_rebase_name_ends_with(field->name, "_end");
}

static int sqlparser_rebase_field_active(const ProtobufCMessage *message,
    const ProtobufCFieldDescriptor *field)
{
    const uint8_t *base = (const uint8_t *)message;

    if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U) {
        const ProtobufCFieldDescriptor *selected;
        int case_value;

        memcpy(&case_value, base + field->quantifier_offset, sizeof(case_value));
        if (case_value == 0) {
            return 0;
        }
        selected = protobuf_c_message_descriptor_get_field(
            message->descriptor, (unsigned)case_value);
        if (selected == NULL ||
            (selected->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) == 0U ||
            selected->quantifier_offset != field->quantifier_offset) {
            return -1;
        }
        return selected == field;
    }
    if (field->label == PROTOBUF_C_LABEL_OPTIONAL &&
        field->quantifier_offset != 0U) {
        protobuf_c_boolean present;

        memcpy(&present, base + field->quantifier_offset, sizeof(present));
        return present != 0;
    }
    return 1;
}

static int sqlparser_rebase_message(sqlparser_rebase_context_t *context,
    ProtobufCMessage *message, void *owner_slot, unsigned depth,
    int literal_wrapper, int under_type_name)
{
    const ProtobufCMessageDescriptor *descriptor;
    const ProtobufCFieldDescriptor *fields;
    uint8_t *base;
    unsigned count, index;
    int statement;

    if (message == NULL || depth > SQLPARSER_REBASE_MAX_DEPTH ||
        message->descriptor == NULL || message->n_unknown_fields != 0U) {
        return 0;
    }
    descriptor = message->descriptor;
    if (descriptor->magic != PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC ||
        (descriptor->n_fields != 0U && descriptor->fields == NULL)) {
        return 0;
    }
    fields = descriptor->fields;
    count = descriptor->n_fields;
    /* Node's oneof has hundreds of alternatives; visit only its active one. */
    if (descriptor == &pg_query__node__descriptor) {
        unsigned node_case = (unsigned)((const PgQuery__Node *)message)->node_case;

        if (node_case == 0U) {
            return 0;
        }
        fields = protobuf_c_message_descriptor_get_field(descriptor, node_case);
        if (fields == NULL) {
            return 0;
        }
        count = 1U;
    }
    base = (uint8_t *)message;
    /* TypeName typmods have grammar-time dependencies that the literal
     * proof must reject, including constants in their nested expressions. */
    under_type_name |= descriptor == &pg_query__type_name__descriptor;
    if (descriptor == &pg_query__a__const__descriptor &&
        (owner_slot == NULL ||
         !context->callback((PgQuery__AConst **)owner_slot, under_type_name,
             context->callback_context))) {
        return 0;
    }
    statement = descriptor == &pg_query__raw_stmt__descriptor ||
        descriptor == &pg_query__query__descriptor;
    if (descriptor == &pg_query__raw_stmt__descriptor) {
        PgQuery__RawStmt *raw = (PgQuery__RawStmt *)message;

        if (!sqlparser_rebase_statement(context, &raw->stmt_location, &raw->stmt_len)) {
            return 0;
        }
    } else if (descriptor == &pg_query__query__descriptor) {
        PgQuery__Query *query = (PgQuery__Query *)message;

        if (!sqlparser_rebase_statement(context, &query->stmt_location, &query->stmt_len)) {
            return 0;
        }
    }
    /* Map this message's scalar coordinates before descending into children.
     * The statement pair was handled together using its original endpoint. */
    for (index = 0U; index < count; index++) {
        const ProtobufCFieldDescriptor *field = &fields[index];
        int active = sqlparser_rebase_field_active(message, field);

        if (active < 0 || field->name == NULL) {
            return 0;
        }
        if (!active) {
            continue;
        }
        if (strcmp(field->name, "stmt_location") == 0 ||
            strcmp(field->name, "stmt_len") == 0) {
            if (!statement || field->type != PROTOBUF_C_TYPE_INT32 ||
                field->label == PROTOBUF_C_LABEL_REPEATED) {
                return 0;
            }
        } else if (strcmp(field->name, "location") == 0 ||
                   strcmp(field->name, "name_location") == 0) {
            int32_t *location, mapped;

            /* This is a filesystem path, despite its coincidental name. */
            if (descriptor == &pg_query__create_table_space_stmt__descriptor &&
                field->type == PROTOBUF_C_TYPE_STRING &&
                strcmp(field->name, "location") == 0) {
                continue;
            }
            if (field->type != PROTOBUF_C_TYPE_INT32 ||
                field->label == PROTOBUF_C_LABEL_REPEATED) {
                return 0;
            }
            location = (int32_t *)(base + field->offset);
            /* Negative values encode generated/style metadata. AConst scalar
             * wrappers have a default zero, not a token position; only their
             * enclosing AConst owns the literal's actual source location. */
            if (*location < 0 || (literal_wrapper && *location == 0)) {
                continue;
            }
            if (!sqlparser_rebase_point(context, (size_t)*location, &mapped)) {
                return 0;
            }
            if (!sqlparser_rebase_record(context, location, mapped)) {
                return 0;
            }
        } else if (sqlparser_rebase_unhandled_source_field(field)) {
            return 0;
        }
    }
    for (index = 0U; index < count; index++) {
        const ProtobufCFieldDescriptor *field = &fields[index];
        int active, child_wrapper;

        if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
            continue;
        }
        active = sqlparser_rebase_field_active(message, field);
        if (active <= 0) {
            if (active < 0) {
                return 0;
            }
            continue;
        }
        child_wrapper = descriptor == &pg_query__a__const__descriptor &&
            (field->descriptor == &pg_query__string__descriptor ||
             field->descriptor == &pg_query__float__descriptor ||
             field->descriptor == &pg_query__bit_string__descriptor);
        if (field->label == PROTOBUF_C_LABEL_REPEATED) {
            size_t item_index, item_count;
            ProtobufCMessage **items;

            memcpy(&item_count, base + field->quantifier_offset, sizeof(item_count));
            memcpy(&items, base + field->offset, sizeof(items));
            if ((item_count != 0U && items == NULL) ||
                item_count > SIZE_MAX / sizeof(*items)) {
                return 0;
            }
            for (item_index = 0U; item_index < item_count; item_index++) {
                ProtobufCMessage *child = items[item_index];

                if (child == NULL || child->descriptor != field->descriptor ||
                    !sqlparser_rebase_message(context, child, &items[item_index],
                        depth + 1U, child_wrapper, under_type_name)) {
                    return 0;
                }
            }
        } else {
            ProtobufCMessage *child;

            memcpy(&child, base + field->offset, sizeof(child));
            if (child == NULL) {
                if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U ||
                    field->label == PROTOBUF_C_LABEL_REQUIRED) {
                    return 0;
                }
                continue;
            }
            if (child->descriptor != field->descriptor ||
                !sqlparser_rebase_message(context, child, base + field->offset,
                    depth + 1U, child_wrapper, under_type_name)) {
                return 0;
            }
        }
    }
    return 1;
}

int sqlparser_ast_rebase_plan_build(sqlparser_ast_rebase_plan_t *plan,
    PgQuery__ParseResult *ast, const sqlparser_surface_source_edits_t *edits,
    size_t old_length, size_t new_length,
    sqlparser_ast_rebase_literal_callback_t callback, void *callback_context)
{
    sqlparser_rebase_context_t context;
    int eligible;

    if (plan == NULL) {
        return 0;
    }
    sqlparser_ast_rebase_plan_release(plan);
    if (ast == NULL || callback == NULL ||
        ast->base.descriptor != &pg_query__parse_result__descriptor ||
        !sqlparser_ast_rebase_edits_valid(edits, old_length, new_length) ||
        !sqlparser_rebase_statement_spans_valid(ast, edits, old_length)) {
        return 0;
    }
    plan->items = plan->inline_writes;
    plan->capacity = SQLPARSER_AST_REBASE_INLINE_WRITES;
    context.edits = edits;
    context.old_length = old_length;
    context.new_length = new_length;
    context.prefix_count = 0U;
    context.prefix_delta = 0;
    context.plan = plan;
    context.callback = callback;
    context.callback_context = callback_context;
    context.no_memory = 0;
    eligible = sqlparser_rebase_message(&context, &ast->base, NULL, 0U, 0, 0);
    if (!eligible) {
        sqlparser_ast_rebase_plan_release(plan);
        return context.no_memory ? -1 : 0;
    }
    return 1;
}

void sqlparser_ast_rebase_plan_apply(const sqlparser_ast_rebase_plan_t *plan)
{
    size_t index;

    for (index = 0U; index < plan->count; index++) {
        *plan->items[index].slot = plan->items[index].value;
    }
}

void sqlparser_ast_rebase_plan_release(sqlparser_ast_rebase_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }
    if (plan->items != plan->inline_writes) {
        free(plan->items);
    }
    memset(plan, 0, sizeof(*plan));
}
