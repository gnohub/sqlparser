#include "pg_query_owned_ast.h"

#include "postgres.h"
#include "access/relation.h"
#include "nodes/parsenodes.h"
#include "nodes/plannodes.h"
#include "nodes/value.h"
#include "utils/datum.h"

/* Compile the complete generated native converters with independent malloc
 * ownership. OUT_TYPE adds a per-call context to their declarations and
 * definitions, so neither global state nor a retained PostgreSQL context is
 * involved. The observer converters deliberately keep their existing borrowed
 * strings/coallocated wrappers and are never used as an owning AST. */
typedef struct PgQueryOwnedAstContext
{
    bool failed;
} PgQueryOwnedAstContext;

static void *
pg_query_owned_alloc(PgQueryOwnedAstContext *ctx, size_t count, size_t width)
{
    void *allocation;
    if (ctx->failed)
        return NULL;
    if (count > SIZE_MAX / width)
    {
        ctx->failed = true;
        return NULL;
    }
    allocation = calloc(count, width);
    if (allocation == NULL)
        ctx->failed = true;
    return allocation;
}

static char *
pg_query_owned_string(PgQueryOwnedAstContext *ctx, const char *text)
{
    size_t length = strlen(text);
    char *copy;
    if (length == SIZE_MAX)
    {
        ctx->failed = true;
        return NULL;
    }
    copy = pg_query_owned_alloc(ctx, length + 1U, sizeof(char));
    if (copy != NULL)
        memcpy(copy, text, length + 1U);
    return copy;
}

#define OUT_TYPE(typename, typename_c) PgQueryOwnedAstContext *ctx, PgQuery__##typename_c *

/* Attach initialized children before descending, and publish zero-filled
 * arrays before filling them. Thus free_unpacked safely frees the reachable
 * partial graph after any allocation failure, including an active oneof. */
#define OUT_NODE(typename, typename_c, typename_underscore, typename_underscore_upcase, typename_cast, fldname) \
    do { \
        PgQuery__##typename_c *child = pg_query_owned_alloc(ctx, 1U, sizeof(*child)); \
        if (child == NULL) return; \
        pg_query__##typename_underscore##__init(child); \
        out->fldname = child; \
        out->node_case = PG_QUERY__NODE__NODE_##typename_underscore_upcase; \
        _out##typename_c(ctx, child, (const typename_cast *) obj); \
        if (ctx->failed) return; \
    } while (0)

#define WRITE_INT_FIELD(outname, outname_json, fldname) out->outname = node->fldname
#define WRITE_UINT_FIELD(outname, outname_json, fldname) out->outname = node->fldname
#define WRITE_UINT64_FIELD(outname, outname_json, fldname) out->outname = node->fldname
#define WRITE_LONG_FIELD(outname, outname_json, fldname) out->outname = node->fldname
#define WRITE_FLOAT_FIELD(outname, outname_json, fldname) out->outname = node->fldname
#define WRITE_BOOL_FIELD(outname, outname_json, fldname) out->outname = node->fldname
#define WRITE_ENUM_FIELD(typename, outname, outname_json, fldname) \
    out->outname = _enumToInt##typename(node->fldname)

#define WRITE_CHAR_FIELD(outname, outname_json, fldname) \
    do { \
        if (node->fldname != 0) { \
            char *text = pg_query_owned_alloc(ctx, 2U, sizeof(char)); \
            if (text == NULL) return; \
            text[0] = node->fldname; \
            out->outname = text; \
        } \
    } while (0)

#define WRITE_STRING_FIELD(outname, outname_json, fldname) \
    do { \
        if (node->fldname != NULL) { \
            out->outname = pg_query_owned_string(ctx, node->fldname); \
            if (ctx->failed) return; \
        } \
    } while (0)

#define WRITE_LIST_FIELD(outname, outname_json, fldname) \
    do { \
        if (node->fldname != NULL) { \
            const ListCell *cell; \
            size_t index = 0U; \
            out->n_##outname = list_length(node->fldname); \
            out->outname = pg_query_owned_alloc(ctx, out->n_##outname, sizeof(*out->outname)); \
            if (out->outname == NULL) return; \
            foreach(cell, node->fldname) { \
                PgQuery__Node *child = pg_query_owned_alloc(ctx, 1U, sizeof(*child)); \
                if (child == NULL) return; \
                pg_query__node__init(child); \
                out->outname[index++] = child; \
                _outNode(ctx, child, lfirst(cell)); \
                if (ctx->failed) return; \
            } \
        } \
    } while (0)

#define WRITE_BITMAPSET_FIELD(outname, outname_json, fldname) \
    do { \
        if (!bms_is_empty(node->fldname)) { \
            int member = -1; \
            size_t index = 0U; \
            out->n_##outname = bms_num_members(node->fldname); \
            out->outname = pg_query_owned_alloc(ctx, out->n_##outname, sizeof(*out->outname)); \
            if (out->outname == NULL) return; \
            while ((member = bms_next_member(node->fldname, member)) >= 0) \
                out->outname[index++] = member; \
        } \
    } while (0)

#define WRITE_NODE_FIELD(outname, outname_json, fldname) \
    do { \
        PgQuery__Node *child = pg_query_owned_alloc(ctx, 1U, sizeof(*child)); \
        if (child == NULL) return; \
        pg_query__node__init(child); \
        out->outname = child; \
        _outNode(ctx, child, &node->fldname); \
        if (ctx->failed) return; \
    } while (0)

#define WRITE_NODE_PTR_FIELD(outname, outname_json, fldname) \
    do { \
        if (node->fldname != NULL) { \
            PgQuery__Node *child = pg_query_owned_alloc(ctx, 1U, sizeof(*child)); \
            if (child == NULL) return; \
            pg_query__node__init(child); \
            out->outname = child; \
            _outNode(ctx, child, node->fldname); \
            if (ctx->failed) return; \
        } \
    } while (0)

#define WRITE_SPECIFIC_NODE_FIELD(typename, typename_underscore, outname, outname_json, fldname) \
    do { \
        PgQuery__##typename *child = pg_query_owned_alloc(ctx, 1U, sizeof(*child)); \
        if (child == NULL) return; \
        pg_query__##typename_underscore##__init(child); \
        out->outname = child; \
        _out##typename(ctx, child, &node->fldname); \
        if (ctx->failed) return; \
    } while (0)

#define WRITE_SPECIFIC_NODE_PTR_FIELD(typename, typename_underscore, outname, outname_json, fldname) \
    do { \
        if (node->fldname != NULL) { \
            PgQuery__##typename *child = pg_query_owned_alloc(ctx, 1U, sizeof(*child)); \
            if (child == NULL) return; \
            pg_query__##typename_underscore##__init(child); \
            out->outname = child; \
            _out##typename(ctx, child, node->fldname); \
            if (ctx->failed) return; \
        } \
    } while (0)

static void _outNode(PgQueryOwnedAstContext *ctx, PgQuery__Node *out, const void *obj);

#define OWNED_LIST_CONVERTER(typename) \
    static void _out##typename(PgQueryOwnedAstContext *ctx, PgQuery__##typename *out, const List *node) \
    { \
        const ListCell *cell; \
        size_t index = 0U; \
        out->n_items = list_length(node); \
        if (out->n_items == 0U) return; \
        out->items = pg_query_owned_alloc(ctx, out->n_items, sizeof(*out->items)); \
        if (out->items == NULL) return; \
        foreach(cell, node) { \
            PgQuery__Node *child = pg_query_owned_alloc(ctx, 1U, sizeof(*child)); \
            if (child == NULL) return; \
            pg_query__node__init(child); \
            out->items[index++] = child; \
            _outNode(ctx, child, lfirst(cell)); \
            if (ctx->failed) return; \
        } \
    }

OWNED_LIST_CONVERTER(List)
OWNED_LIST_CONVERTER(IntList)
OWNED_LIST_CONVERTER(OidList)

static void
_outInteger(PgQueryOwnedAstContext *ctx, PgQuery__Integer *out, const Integer *node)
{
    out->ival = node->ival;
}

static void
_outFloat(PgQueryOwnedAstContext *ctx, PgQuery__Float *out, const Float *node)
{
    WRITE_STRING_FIELD(fval, fval, fval);
}

static void
_outBoolean(PgQueryOwnedAstContext *ctx, PgQuery__Boolean *out, const Boolean *node)
{
    out->boolval = node->boolval;
}

static void
_outString(PgQueryOwnedAstContext *ctx, PgQuery__String *out, const String *node)
{
    WRITE_STRING_FIELD(sval, sval, sval);
    out->location = node->location;
}

static void
_outBitString(PgQueryOwnedAstContext *ctx, PgQuery__BitString *out, const BitString *node)
{
    WRITE_STRING_FIELD(bsval, bsval, bsval);
}

static void
_outAConst(PgQueryOwnedAstContext *ctx, PgQuery__AConst *out, const A_Const *node)
{
    out->isnull = node->isnull;
    out->location = node->location;
    if (node->isnull)
        return;

#define OWNED_CONSTANT(typename, typename_underscore, fldname, enumname) \
    do { \
        PgQuery__##typename *value = pg_query_owned_alloc(ctx, 1U, sizeof(*value)); \
        if (value == NULL) return; \
        pg_query__##typename_underscore##__init(value); \
        out->val_case = PG_QUERY__A__CONST__VAL_##enumname; \
        out->fldname = value; \
        _out##typename(ctx, value, &node->val.fldname); \
    } while (0)

    switch (nodeTag(&node->val.node))
    {
        case T_Integer: OWNED_CONSTANT(Integer, integer, ival, IVAL); break;
        case T_Float: OWNED_CONSTANT(Float, float, fval, FVAL); break;
        case T_Boolean: OWNED_CONSTANT(Boolean, boolean, boolval, BOOLVAL); break;
        case T_String: OWNED_CONSTANT(String, string, sval, SVAL); break;
        case T_BitString: OWNED_CONSTANT(BitString, bit_string, bsval, BSVAL); break;
        default: Assert(false); break;
    }
#undef OWNED_CONSTANT
}

#include "pg_query_enum_defs.c"
#include "pg_query_outfuncs_defs.c"

static void
_outNode(PgQueryOwnedAstContext *ctx, PgQuery__Node *out, const void *obj)
{
    if (obj == NULL)
        return;
    switch (nodeTag(obj))
    {
#include "pg_query_outfuncs_conds.c"
        default:
            printf("could not dump unrecognized node type: %d", (int) nodeTag(obj));
            elog(WARNING, "could not dump unrecognized node type: %d", (int) nodeTag(obj));
            return;
    }
}

PgQuery__ParseResult *
pg_query_nodes_to_owned_ast(const void *obj)
{
    PgQueryOwnedAstContext ctx = {false};
    PgQuery__ParseResult *result = pg_query_owned_alloc(&ctx, 1U, sizeof(*result));
    const ListCell *cell;
    size_t index = 0U;

    if (result == NULL)
        return NULL;
    pg_query__parse_result__init(result);
    result->version = PG_VERSION_NUM;
    result->n_stmts = list_length(obj);
    if (result->n_stmts != 0U)
    {
        result->stmts = pg_query_owned_alloc(&ctx, result->n_stmts, sizeof(*result->stmts));
        if (result->stmts == NULL)
            goto failed;
        foreach(cell, obj)
        {
            PgQuery__RawStmt *statement = pg_query_owned_alloc(&ctx, 1U, sizeof(*statement));
            if (statement == NULL)
                goto failed;
            pg_query__raw_stmt__init(statement);
            result->stmts[index++] = statement;
            _outRawStmt(&ctx, statement, lfirst(cell));
            if (ctx.failed)
                goto failed;
        }
    }
    return result;

failed:
    pg_query__parse_result__free_unpacked(result, NULL);
    return NULL;
}
