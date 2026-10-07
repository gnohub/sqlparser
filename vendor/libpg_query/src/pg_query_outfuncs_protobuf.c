#include "pg_query_outfuncs.h"
#include "pg_query_observer.h"

#include "postgres.h"
#include <ctype.h>
#include "access/relation.h"
#include "nodes/parsenodes.h"
#include "nodes/plannodes.h"
#include "nodes/value.h"
#include "utils/datum.h"
#include "utils/memutils.h"

#include "protobuf/pg_query.pb-c.h"

#define OUT_TYPE(typename, typename_c) PgQuery__##typename_c*

/* Conversion nodes have one shared context lifetime and are never individually
 * freed or resized. Place each list's pointer table and wrappers in one chunk
 * to avoid a separate AllocSet header and size-class rounding for every node.
 * MAXALIGN keeps the wrapper region valid on all supported architectures. */
static inline PgQuery__Node **
pg_query_node_array(size_t count, PgQuery__Node **nodes)
{
	Size pointer_bytes;
	PgQuery__Node **items;

	/* Check before either multiplication or alignment, including padding.
	 * Very large lists retain the old per-wrapper path rather than acquiring
	 * a lower maximum size from the combined allocation's limit. */
	if (count > (MaxAllocSize - (MAXIMUM_ALIGNOF - 1)) /
		(sizeof(PgQuery__Node *) + sizeof(PgQuery__Node)))
	{
		*nodes = NULL;
		if (count > MaxAllocSize / sizeof(PgQuery__Node *))
			elog(ERROR, "protobuf node pointer array is too large");
		return palloc(sizeof(PgQuery__Node *) * count);
	}
	pointer_bytes = MAXALIGN(sizeof(PgQuery__Node *) * count);
	items = palloc(pointer_bytes + sizeof(PgQuery__Node) * count);
	*nodes = (PgQuery__Node *) ((char *) items + pointer_bytes);
	return items;
}

#define OUT_NODE(typename, typename_c, typename_underscore, typename_underscore_upcase, typename_cast, fldname) \
  { \
    PgQuery__##typename_c *__node = palloc(sizeof(PgQuery__##typename_c)); \
	pg_query__##typename_underscore##__init(__node); \
    _out##typename_c(__node, (const typename_cast *) obj); \
	out->fldname = __node; \
	out->node_case = PG_QUERY__NODE__NODE_##typename_underscore_upcase; \
  }

#define WRITE_INT_FIELD(outname, outname_json, fldname) out->outname = node->fldname;
#define WRITE_UINT_FIELD(outname, outname_json, fldname) out->outname = node->fldname;
#define WRITE_UINT64_FIELD(outname, outname_json, fldname) out->outname = node->fldname;
#define WRITE_LONG_FIELD(outname, outname_json, fldname) out->outname = node->fldname;
#define WRITE_FLOAT_FIELD(outname, outname_json, fldname) out->outname = node->fldname;
#define WRITE_BOOL_FIELD(outname, outname_json, fldname) out->outname = node->fldname;

#define WRITE_CHAR_FIELD(outname, outname_json, fldname) \
	if (node->fldname != 0) { \
		out->outname = palloc(sizeof(char) * 2); \
		out->outname[0] = node->fldname; \
		out->outname[1] = '\0'; \
	}
#define WRITE_STRING_FIELD(outname, outname_json, fldname) \
	if (node->fldname != NULL) { \
		out->outname = pstrdup(node->fldname); \
	}

#define WRITE_ENUM_FIELD(typename, outname, outname_json, fldname) \
	out->outname = _enumToInt##typename(node->fldname);

#define WRITE_LIST_FIELD(outname, outname_json, fldname) \
	if (node->fldname != NULL) { \
	  out->n_##outname = list_length(node->fldname); \
	  PgQuery__Node *__nodes; \
	  out->outname = pg_query_node_array(out->n_##outname, &__nodes); \
	  for (int i = 0; i < out->n_##outname; i++) \
      { \
	    PgQuery__Node *__node = __nodes != NULL ? &__nodes[i] : palloc(sizeof(PgQuery__Node)); \
	    pg_query__node__init(__node); \
	    out->outname[i] = __node; \
	    _outNode(out->outname[i], list_nth(node->fldname, i)); \
      } \
    }

#define WRITE_BITMAPSET_FIELD(outname, outname_json, fldname) \
	if (!bms_is_empty(node->fldname)) \
	{ \
		int x = -1; \
		int i = 0; \
		out->n_##outname = bms_num_members(node->fldname); \
		out->outname = palloc(sizeof(PgQuery__Node*) * out->n_##outname); \
		while ((x = bms_next_member(node->fldname, x)) >= 0) \
			out->outname[i++] = x; \
    }

#define WRITE_NODE_FIELD(outname, outname_json, fldname) \
	{ \
		PgQuery__Node *__node = palloc(sizeof(PgQuery__Node)); \
		pg_query__node__init(__node); \
		out->outname = __node; \
		_outNode(out->outname, &node->fldname); \
	}

#define WRITE_NODE_PTR_FIELD(outname, outname_json, fldname) \
	if (node->fldname != NULL) { \
		PgQuery__Node *__node = palloc(sizeof(PgQuery__Node)); \
		pg_query__node__init(__node); \
		out->outname = __node; \
		_outNode(out->outname, node->fldname); \
	}

#define WRITE_SPECIFIC_NODE_FIELD(typename, typename_underscore, outname, outname_json, fldname) \
	{ \
		PgQuery__##typename *__node = palloc(sizeof(PgQuery__##typename)); \
		pg_query__##typename_underscore##__init(__node); \
		_out##typename(__node, &node->fldname); \
		out->outname = __node; \
	}

#define WRITE_SPECIFIC_NODE_PTR_FIELD(typename, typename_underscore, outname, outname_json, fldname) \
	if (node->fldname != NULL) { \
		PgQuery__##typename *__node = palloc(sizeof(PgQuery__##typename)); \
		pg_query__##typename_underscore##__init(__node); \
		_out##typename(__node, node->fldname); \
		out->outname = __node; \
	}

static void _outNode(PgQuery__Node* out, const void *obj);

static void
_outList(PgQuery__List* out, const List *node)
{
	const ListCell *lc;
	int i = 0;
	PgQuery__Node *nodes;
	out->n_items = list_length(node);
	out->items = pg_query_node_array(out->n_items, &nodes);
    foreach(lc, node)
    {
		out->items[i] = nodes != NULL ? &nodes[i] : palloc(sizeof(PgQuery__Node));
		pg_query__node__init(out->items[i]);
	    _outNode(out->items[i], lfirst(lc));
		i++;
    }
}

static void
_outIntList(PgQuery__IntList* out, const List *node)
{
	const ListCell *lc;
	int i = 0;
	PgQuery__Node *nodes;
	out->n_items = list_length(node);
	out->items = pg_query_node_array(out->n_items, &nodes);
    foreach(lc, node)
    {
		out->items[i] = nodes != NULL ? &nodes[i] : palloc(sizeof(PgQuery__Node));
		pg_query__node__init(out->items[i]);
	    _outNode(out->items[i], lfirst(lc));
		i++;
    }
}

static void
_outOidList(PgQuery__OidList* out, const List *node)
{
	const ListCell *lc;
	int i = 0;
	PgQuery__Node *nodes;
	out->n_items = list_length(node);
	out->items = pg_query_node_array(out->n_items, &nodes);
    foreach(lc, node)
    {
		out->items[i] = nodes != NULL ? &nodes[i] : palloc(sizeof(PgQuery__Node));
		pg_query__node__init(out->items[i]);
	    _outNode(out->items[i], lfirst(lc));
		i++;
    }
}

// TODO: Add Bitmapset

static void
_outInteger(PgQuery__Integer* out, const Integer *node)
{
  out->ival = node->ival;
}

static void
_outFloat(PgQuery__Float* out, const Float *node)
{
  out->fval = node->fval;
}

static void
_outBoolean(PgQuery__Boolean* out, const Boolean *node)
{
  out->boolval = node->boolval;
}

static void
_outString(PgQuery__String* out, const String *node)
{
  out->sval = node->sval;
  out->location = node->location;
}

static void
_outBitString(PgQuery__BitString* out, const BitString *node)
{
  out->bsval = node->bsval;
}

static void
_outAConst(PgQuery__AConst* out, const A_Const *node)
{
  /* Like _outFloat/_outString/_outBitString, borrow scalar text from the
   * raw tree. It stays alive through the read-only observer and synchronous
   * packing below; only independently malloc-owned packed bytes escape. */
  out->isnull = node->isnull;
  out->location = node->location;

  if (!node->isnull) {
    switch (nodeTag(&node->val.node)) {
      case T_Integer: {
        PgQuery__Integer *value = palloc(sizeof(PgQuery__Integer));
        pg_query__integer__init(value);
        value->ival = node->val.ival.ival;

        out->val_case = PG_QUERY__A__CONST__VAL_IVAL;
        out->ival = value;
        break;
      }
      case T_Float: {
        PgQuery__Float *value = palloc(sizeof(PgQuery__Float));
        pg_query__float__init(value);
        value->fval = node->val.fval.fval;

        out->val_case = PG_QUERY__A__CONST__VAL_FVAL;
        out->fval = value;
        break;
      }
      case T_Boolean: {
        PgQuery__Boolean *value = palloc(sizeof(PgQuery__Boolean));
        pg_query__boolean__init(value);
        value->boolval = node->val.boolval.boolval;

        out->val_case = PG_QUERY__A__CONST__VAL_BOOLVAL;
        out->boolval = value;
        break;
      }
      case T_String: {
        PgQuery__String *value = palloc(sizeof(PgQuery__String));
	pg_query__string__init(value);
	value->sval = node->val.sval.sval;
	value->location = node->val.sval.location;

	out->val_case = PG_QUERY__A__CONST__VAL_SVAL;
	out->sval = value;
	break;
      }
      case T_BitString: {
        PgQuery__BitString *value = palloc(sizeof(PgQuery__BitString));
	pg_query__bit_string__init(value);
	value->bsval = node->val.bsval.bsval;

	out->val_case = PG_QUERY__A__CONST__VAL_BSVAL;
	out->bsval = value;
	break;
      }
      default:
        // Unreachable
        Assert(false);
    }
  }
}

#include "pg_query_enum_defs.c"
#include "pg_query_outfuncs_defs.c"

static void
_outNode(PgQuery__Node* out, const void *obj)
{
	if (obj == NULL)
		return; // Keep out as NULL

	switch (nodeTag(obj))
	{
		#include "pg_query_outfuncs_conds.c"

		default:
			printf("could not dump unrecognized node type: %d", (int) nodeTag(obj));
			elog(WARNING, "could not dump unrecognized node type: %d",
					(int) nodeTag(obj));

			return;
	}
}

#include "direct_wire_runtime.inc"

PgQueryProtobuf
pg_query_nodes_to_protobuf(const void *obj)
{
	return pg_query_nodes_to_protobuf_observed(obj, NULL, NULL);
}

PgQueryProtobuf
pg_query_nodes_to_protobuf_observed(
	const void *obj, PgQueryProtobufObserver observer, void *context)
{
	PgQueryProtobuf protobuf;
	const ListCell *lc;
	int i = 0;
	PgQuery__ParseResult parse_result = PG_QUERY__PARSE_RESULT__INIT;

	/* Direct raw-to-wire encoder. Preserve the full-tree observer path. */
	if (observer == NULL) {
		PgQueryProtobuf direct = {0};
		int direct_status = pg_query_try_direct_wire(obj, &direct);
		if (direct_status != 0) return direct;
	}

	parse_result.version = PG_VERSION_NUM;

	if (obj == NULL) {
		parse_result.n_stmts = 0;
		parse_result.stmts = NULL;
	}
	else
	{
		parse_result.n_stmts = list_length(obj);
		parse_result.stmts = palloc(sizeof(PgQuery__RawStmt*) * parse_result.n_stmts);
		foreach(lc, obj)
		{
			parse_result.stmts[i] = palloc(sizeof(PgQuery__RawStmt));
			pg_query__raw_stmt__init(parse_result.stmts[i]);
			_outRawStmt(parse_result.stmts[i], lfirst(lc));
			i++;
		}
	}

	if (observer != NULL)
		observer(&parse_result, context);

	protobuf.len = pg_query__parse_result__get_packed_size(&parse_result);
	// Note: This is intentionally malloc so exiting the memory context doesn't free this
	protobuf.data = pg_query_protobuf_alloc_output(sizeof(char) * protobuf.len);
	/* No error allocation is needed to report an exhausted allocator. The
	 * sqlparser boundary recognizes an absent buffer as NO_MEMORY, after
	 * giving any existing SQL parse error precedence. */
	if (protobuf.data == NULL)
	{
		protobuf.len = 0;
		return protobuf;
	}
	pg_query__parse_result__pack(&parse_result, (void*) protobuf.data); 

	return protobuf;
}

/* Only the separate internal caller may consume a validation certificate.
 * Existing observer callers always receive their complete legacy tree. */
PgQueryProtobuf
pg_query_nodes_to_protobuf_certified(
    const void *obj, PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified)
{
    PgQueryProtobuf direct = {0};
    int status;
    *statement_count = 0;
    *certified = 0;
    status = pg_query_try_direct_wire_certified(obj, &direct, statement_count);
    if (status != 0) {
        *certified = status == 1;
        return direct;
    }
    return pg_query_nodes_to_protobuf_observed(obj, observer, context);
}
