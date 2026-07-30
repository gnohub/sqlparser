#include "pg_query_readfuncs.h"

#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"

#include "protobuf/pg_query.pb-c.h"

static __thread const PostgresDeparseOpts *pg_query_readfuncs_options;

static const char *
pg_query_read_string_value(const char *input, size_t *identifier_index)
{
	const char *cursor;
	size_t value;
	bool has_digit;

	*identifier_index = (size_t)-1;
	if (input == NULL ||
		pg_query_readfuncs_options == NULL ||
		pg_query_readfuncs_options->generated_identifier_prefix == NULL ||
		pg_query_readfuncs_options->generated_identifier_prefix_length == 0 ||
		strncmp(
			input,
			pg_query_readfuncs_options->generated_identifier_prefix,
			pg_query_readfuncs_options->generated_identifier_prefix_length) != 0)
		return input;

	cursor =
		input +
		pg_query_readfuncs_options->generated_identifier_prefix_length;
	value = 0;
	has_digit = false;
	while (*cursor >= '0' && *cursor <= '9')
	{
		size_t digit = (size_t)(*cursor - '0');

		if (value > (SIZE_MAX - digit) / 10)
			return input;
		value = value * 10 + digit;
		has_digit = true;
		cursor++;
	}
	if (!has_digit || *cursor != ':')
		return input;
	*identifier_index = value;
	return cursor + 1;
}

static char *
pg_query_read_string_copy(const char *input, bool empty_is_null)
{
	const char *clean;
	char *result;
	size_t identifier_index;

	clean = pg_query_read_string_value(input, &identifier_index);
	if (clean == NULL || (empty_is_null && clean[0] == '\0'))
		return NULL;
	result = pstrdup(clean);
	if (identifier_index != (size_t)-1 &&
		pg_query_readfuncs_options->generated_identifier_reader != NULL)
		pg_query_readfuncs_options->generated_identifier_reader(
			pg_query_readfuncs_options->identifier_resolver_context,
			identifier_index,
			result);
	return result;
}

#define OUT_TYPE(typename, typename_c) PgQuery__##typename_c*

#define READ_COND(typename, typename_c, typename_underscore, typename_underscore_upcase, typename_cast, outname) \
	case PG_QUERY__NODE__NODE_##typename_underscore_upcase: \
		return (Node *) _read##typename_c(msg->outname);

#define READ_INT_FIELD(outname, outname_json, fldname) node->fldname = msg->outname;
#define READ_UINT_FIELD(outname, outname_json, fldname) node->fldname = msg->outname;
#define READ_UINT64_FIELD(outname, outname_json, fldname) node->fldname = msg->outname;
#define READ_LONG_FIELD(outname, outname_json, fldname) node->fldname = msg->outname;
#define READ_FLOAT_FIELD(outname, outname_json, fldname) node->fldname = msg->outname;
#define READ_BOOL_FIELD(outname, outname_json, fldname) node->fldname = msg->outname;

#define READ_CHAR_FIELD(outname, outname_json, fldname) \
	if (msg->outname != NULL) { \
		size_t identifier_index; \
		const char *clean = pg_query_read_string_value(msg->outname, &identifier_index); \
		if (clean != NULL && clean[0] != '\0') \
			node->fldname = clean[0]; \
	}

#define READ_STRING_FIELD(outname, outname_json, fldname) \
	node->fldname = pg_query_read_string_copy(msg->outname, true);

#define READ_ENUM_FIELD(typename, outname, outname_json, fldname) \
	node->fldname = _intToEnum##typename(msg->outname);

#define READ_LIST_FIELD(outname, outname_json, fldname) \
	{ \
		if (msg->n_##outname > 0) \
			node->fldname = list_make1(_readNode(msg->outname[0])); \
	    for (int i = 1; i < msg->n_##outname; i++) \
			node->fldname = lappend(node->fldname, _readNode(msg->outname[i])); \
	}

#define READ_BITMAPSET_FIELD(outname, outname_json, fldname) // FIXME

#define READ_NODE_FIELD(outname, outname_json, fldname) \
	node->fldname = *_readNode(msg->outname);

#define READ_NODE_PTR_FIELD(outname, outname_json, fldname) \
	if (msg->outname != NULL) { \
		node->fldname = _readNode(msg->outname); \
	}

#define READ_ABSTRACT_PTR_FIELD(outname, outname_json, fldname, fldtype) \
	if (msg->outname != NULL) { \
		node->fldname = (fldtype) _readNode(msg->outname); \
	}

#define READ_VALUE_FIELD(outname, outname_json, fldname) \
	if (msg->outname != NULL) { \
		node->fldname = *((Value *) _readNode(msg->outname)); \
	}

#define READ_VALUE_PTR_FIELD(outname, outname_json, fldname) \
	if (msg->outname != NULL) { \
		node->fldname = (Value *) _readNode(msg->outname); \
	}

#define READ_SPECIFIC_NODE_FIELD(typename, typename_underscore, outname, outname_json, fldname) \
	node->fldname = *_read##typename(msg->outname);

#define READ_SPECIFIC_NODE_PTR_FIELD(typename, typename_underscore, outname, outname_json, fldname) \
	if (msg->outname != NULL) { \
		node->fldname = _read##typename(msg->outname); \
	}

static Node * _readNode(PgQuery__Node *msg);

static String *
_readString(PgQuery__String* msg)
{
	return makeString(pg_query_read_string_copy(msg->sval, false));
}

#include "pg_query_enum_defs.c"
#include "pg_query_readfuncs_defs.c"

static List * _readList(PgQuery__List *msg)
{
	List *node = NULL;
	if (msg->n_items > 0)
		node = list_make1(_readNode(msg->items[0]));
	for (int i = 1; i < msg->n_items; i++)
		node = lappend(node, _readNode(msg->items[i]));
	return node;
}

static Node * _readNode(PgQuery__Node *msg)
{
	switch (msg->node_case)
	{
		#include "pg_query_readfuncs_conds.c"

		case PG_QUERY__NODE__NODE_INTEGER:
			return (Node *) makeInteger(msg->integer->ival);
		case PG_QUERY__NODE__NODE_FLOAT:
			return (Node *) makeFloat(pg_query_read_string_copy(msg->float_->fval, false));
		case PG_QUERY__NODE__NODE_BOOLEAN:
			return (Node *) makeBoolean(msg->boolean->boolval);
		case PG_QUERY__NODE__NODE_STRING:
			return (Node *) makeString(pg_query_read_string_copy(msg->string->sval, false));
		case PG_QUERY__NODE__NODE_BIT_STRING:
			return (Node *) makeBitString(pg_query_read_string_copy(msg->bit_string->bsval, false));
		case PG_QUERY__NODE__NODE_A_CONST: {
			A_Const *ac = makeNode(A_Const);
			ac->location = msg->a_const->location;

			if (msg->a_const->isnull) {
				ac->isnull = true;
			} else {
				switch (msg->a_const->val_case) {
					case PG_QUERY__A__CONST__VAL_IVAL:
						ac->val.ival = *makeInteger(msg->a_const->ival->ival);
						break;
					case PG_QUERY__A__CONST__VAL_FVAL:
						ac->val.fval = *makeFloat(pg_query_read_string_copy(msg->a_const->fval->fval, false));
						break;
					case PG_QUERY__A__CONST__VAL_BOOLVAL:
						ac->val.boolval = *makeBoolean(msg->a_const->boolval->boolval);
						break;
					case PG_QUERY__A__CONST__VAL_SVAL:
						ac->val.sval = *makeString(pg_query_read_string_copy(msg->a_const->sval->sval, false));
						break;
					case PG_QUERY__A__CONST__VAL_BSVAL:
						ac->val.bsval = *makeBitString(pg_query_read_string_copy(msg->a_const->bsval->bsval, false));
						break;
					case PG_QUERY__A__CONST__VAL__NOT_SET:
					case _PG_QUERY__A__CONST__VAL__CASE_IS_INT_SIZE:
						Assert(false);
						break;
				}
			}

			return (Node *) ac;
		}
		case PG_QUERY__NODE__NODE_LIST:
			return (Node *) _readList(msg->list);
		case PG_QUERY__NODE__NODE__NOT_SET:
			return NULL;
		default:
			elog(ERROR, "unsupported protobuf node type: %d",
				 (int) msg->node_case);
	}
}

List * pg_query_protobuf_to_nodes_opts(
	PgQueryProtobuf protobuf,
	const PostgresDeparseOpts *opts)
{
	PgQuery__ParseResult *result = NULL;
	List * list = NULL;
	size_t i = 0;
	const PostgresDeparseOpts *previous_options;

	previous_options = pg_query_readfuncs_options;
	pg_query_readfuncs_options = opts;
	result = pg_query__parse_result__unpack(NULL, protobuf.len, (const uint8_t *) protobuf.data);

	// TODO: Handle this by returning an error instead
	Assert(result != NULL);

	// TODO: Handle this by returning an error instead
	Assert(result->version == PG_VERSION_NUM);

	if (result->n_stmts > 0)
		list = list_make1(_readRawStmt(result->stmts[0]));
    for (i = 1; i < result->n_stmts; i++)
		list = lappend(list, _readRawStmt(result->stmts[i]));

	pg_query__parse_result__free_unpacked(result, NULL);
	pg_query_readfuncs_options = previous_options;

	return list;
}

List * pg_query_protobuf_to_nodes(PgQueryProtobuf protobuf)
{
	return pg_query_protobuf_to_nodes_opts(protobuf, NULL);
}

void pg_query_readfuncs_reset_options(void)
{
	pg_query_readfuncs_options = NULL;
}
