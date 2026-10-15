/* Regression coverage for schema name policy. */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "sqlparser_ast_internal.h"

#define TEST_CONTEXT(o,f,w,l,b) {.owner_type=(o), .field_name=(f), .field_owner=(w), .location_owner=(l), .identifier_forbidden=(b)}
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "name read context [%s]:%d: %s\n", case_name, __LINE__, #x); exit(1); } } while (0)
static const char *case_name = "initialization";
static size_t alternatives, fixtures, name_checks, public_statements;
static int dump;
static size_t omitted_checks, owner_checks, field_owner_checks, corpus_inputs, override_inputs;
static size_t statuses, failures, public_names, mutations, source_patches, lifetime_checks;
static const char *phase = "schema";

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

/* reference string policy below is a verbatim extraction except for static
 * linkage and golden_ function names. It does not use reference annotations. */
static int golden_message_is_name_container(const ProtobufCMessageDescriptor *descriptor)
{
	const char *short_name;

	if (descriptor == NULL || descriptor->short_name == NULL) {
		return 0;
	}

	short_name = descriptor->short_name;
	return strcmp(short_name, "Node") == 0 || strcmp(short_name, "List") == 0 ||
	       strcmp(short_name, "String") == 0;
}

static int golden_name_atom_is_identifier(
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field)
{
	static const char *const identifier_fields[][2] = {
		{"AIndirection", "indirection"},
		{"AccessPriv", "cols"},
		{"Alias", "aliasname"},
		{"Alias", "colnames"},
		{"AlterCollationStmt", "collname"},
		{"AlterDatabaseRefreshCollStmt", "dbname"},
		{"AlterDatabaseSetStmt", "dbname"},
		{"AlterDatabaseStmt", "dbname"},
		{"AlterDomainStmt", "name"},
		{"AlterDomainStmt", "type_name"},
		{"AlterEnumStmt", "type_name"},
		{"AlterEventTrigStmt", "trigname"},
		{"AlterExtensionContentsStmt", "extname"},
		{"AlterExtensionContentsStmt", "object"},
		{"AlterExtensionStmt", "extname"},
		{"AlterFdwStmt", "fdwname"},
		{"AlterForeignServerStmt", "servername"},
		{"AlterObjectDependsStmt", "extname"},
		{"AlterObjectDependsStmt", "object"},
		{"AlterObjectSchemaStmt", "newschema"},
		{"AlterObjectSchemaStmt", "object"},
		{"AlterOpFamilyStmt", "amname"},
		{"AlterOwnerStmt", "object"},
		{"AlterPolicyStmt", "policy_name"},
		{"AlterPublicationStmt", "pubname"},
		{"AlterRoleSetStmt", "database"},
		{"AlterStatsStmt", "defnames"},
		{"AlterSubscriptionStmt", "subname"},
		{"AlterTSConfigurationStmt", "cfgname"},
		{"AlterTSConfigurationStmt", "dicts"},
		{"AlterTSConfigurationStmt", "tokentype"},
		{"AlterTSDictionaryStmt", "dictname"},
		{"AlterTableCmd", "name"},
		{"AlterTableMoveAllStmt", "new_tablespacename"},
		{"AlterTableMoveAllStmt", "orig_tablespacename"},
		{"AlterTableSpaceOptionsStmt", "tablespacename"},
		{"AlterTypeStmt", "type_name"},
		{"AlterUserMappingStmt", "servername"},
		{"CTECycleClause", "cycle_col_list"},
		{"CTECycleClause", "cycle_mark_column"},
		{"CTECycleClause", "cycle_path_column"},
		{"CTESearchClause", "search_col_list"},
		{"CTESearchClause", "search_seq_column"},
		{"ClosePortalStmt", "portalname"},
		{"ClusterStmt", "indexname"},
		{"CollateClause", "collname"},
		{"ColumnDef", "colname"},
		{"ColumnDef", "compression"},
		{"ColumnDef", "storage_name"},
		{"ColumnRef", "fields"},
		{"CommentStmt", "object"},
		{"CommonTableExpr", "aliascolnames"},
		{"CommonTableExpr", "ctecolnames"},
		{"CommonTableExpr", "ctename"},
		{"Constraint", "access_method"},
		{"Constraint", "conname"},
		{"Constraint", "fk_attrs"},
		{"Constraint", "fk_del_set_cols"},
		{"Constraint", "including"},
		{"Constraint", "indexname"},
		{"Constraint", "indexspace"},
		{"Constraint", "keys"},
		{"Constraint", "pk_attrs"},
		{"ConstraintsSetStmt", "constraints"},
		{"CreateAmStmt", "amname"},
		{"CreateAmStmt", "handler_name"},
		{"CreateConversionStmt", "conversion_name"},
		{"CreateConversionStmt", "func_name"},
		{"CreateDomainStmt", "domainname"},
		{"CreateEnumStmt", "type_name"},
		{"CreateEventTrigStmt", "eventname"},
		{"CreateEventTrigStmt", "funcname"},
		{"CreateEventTrigStmt", "trigname"},
		{"CreateExtensionStmt", "extname"},
		{"CreateFdwStmt", "fdwname"},
		{"CreateForeignServerStmt", "fdwname"},
		{"CreateForeignServerStmt", "servername"},
		{"CreateForeignTableStmt", "servername"},
		{"CreateFunctionStmt", "funcname"},
		{"CreateOpClassItem", "order_family"},
		{"CreateOpClassStmt", "amname"},
		{"CreateOpClassStmt", "opclassname"},
		{"CreateOpClassStmt", "opfamilyname"},
		{"CreateOpFamilyStmt", "amname"},
		{"CreateOpFamilyStmt", "opfamilyname"},
		{"CreatePLangStmt", "plhandler"},
		{"CreatePLangStmt", "plinline"},
		{"CreatePLangStmt", "plname"},
		{"CreatePLangStmt", "plvalidator"},
		{"CreatePolicyStmt", "policy_name"},
		{"CreatePublicationStmt", "pubname"},
		{"CreateRangeStmt", "type_name"},
		{"CreateRoleStmt", "role"},
		{"CreateSchemaStmt", "schemaname"},
		{"CreateStatsStmt", "defnames"},
		{"CreateStmt", "access_method"},
		{"CreateStmt", "tablespacename"},
		{"CreateSubscriptionStmt", "publication"},
		{"CreateSubscriptionStmt", "subname"},
		{"CreateTableSpaceStmt", "tablespacename"},
		{"CreateTransformStmt", "lang"},
		{"CreateTrigStmt", "columns"},
		{"CreateTrigStmt", "funcname"},
		{"CreateTrigStmt", "trigname"},
		{"CreateUserMappingStmt", "servername"},
		{"CreatedbStmt", "dbname"},
		{"CurrentOfExpr", "cursor_name"},
		{"DeallocateStmt", "name"},
		{"DeclareCursorStmt", "portalname"},
		{"DropOwnedStmt", "roles"},
		{"DropRoleStmt", "roles"},
		{"DropStmt", "objects"},
		{"DropSubscriptionStmt", "subname"},
		{"DropTableSpaceStmt", "tablespacename"},
		{"DropUserMappingStmt", "servername"},
		{"DropdbStmt", "dbname"},
		{"ExecuteStmt", "name"},
		{"FetchStmt", "portalname"},
		{"FuncCall", "funcname"},
		{"FunctionParameter", "name"},
		{"GrantStmt", "objects"},
		{"ImportForeignSchemaStmt", "local_schema"},
		{"ImportForeignSchemaStmt", "remote_schema"},
		{"ImportForeignSchemaStmt", "server_name"},
		{"ImportForeignSchemaStmt", "table_list"},
		{"IndexElem", "collation"},
		{"IndexElem", "indexcolname"},
		{"IndexElem", "name"},
		{"IndexElem", "opclass"},
		{"IndexStmt", "access_method"},
		{"IndexStmt", "idxname"},
		{"IndexStmt", "table_space"},
		{"InferClause", "conname"},
		{"IntoClause", "access_method"},
		{"IntoClause", "col_names"},
		{"IntoClause", "table_space_name"},
		{"JoinExpr", "using_clause"},
		{"JsonArgument", "name"},
		{"JsonExpr", "column_name"},
		{"JsonFuncExpr", "column_name"},
		{"JsonTableColumn", "name"},
		{"JsonTablePath", "name"},
		{"JsonTablePathSpec", "name"},
		{"ListenStmt", "conditionname"},
		{"NamedArgExpr", "name"},
		{"NotifyStmt", "conditionname"},
		{"PLAssignStmt", "indirection"},
		{"PLAssignStmt", "name"},
		{"PartitionElem", "collation"},
		{"PartitionElem", "name"},
		{"PartitionElem", "opclass"},
		{"PrepareStmt", "name"},
		{"PublicationObjSpec", "name"},
		{"PublicationTable", "columns"},
		{"RangeTableFuncCol", "colname"},
		{"RangeTableSample", "method"},
		{"RangeTblEntry", "ctename"},
		{"RangeTblEntry", "enrname"},
		{"RangeVar", "catalogname"},
		{"RangeVar", "relname"},
		{"RangeVar", "schemaname"},
		{"ReassignOwnedStmt", "roles"},
		{"ReindexStmt", "name"},
		{"RenameStmt", "newname"},
		{"RenameStmt", "object"},
		{"RenameStmt", "subname"},
		{"ReplicaIdentityStmt", "name"},
		{"ResTarget", "indirection"},
		{"ResTarget", "name"},
		{"RoleSpec", "rolename"},
		{"RowExpr", "colnames"},
		{"RuleStmt", "rulename"},
		{"SecLabelStmt", "object"},
		{"SecLabelStmt", "provider"},
		{"StatsElem", "name"},
		{"TargetEntry", "resname"},
		{"TransactionStmt", "savepoint_name"},
		{"TriggerTransition", "name"},
		{"TypeName", "names"},
		{"UnlistenStmt", "conditionname"},
		{"VacuumRelation", "va_cols"},
		{"VariableShowStmt", "name"},
		{"ViewStmt", "aliases"},
		{"WindowClause", "name"},
		{"WindowClause", "refname"},
		{"WindowDef", "name"},
		{"WindowDef", "refname"},
		{"WithCheckOption", "polname"},
		{"WithCheckOption", "relname"},
		{"XmlExpr", "name"}
	};
	const char *field_name;
	const char *owner_type;
	size_t high;
	size_t low;

	owner_type = NULL;
	field_name = NULL;
	if (golden_message_is_name_container(descriptor) &&
	    context != NULL) {
		owner_type = context->owner_type;
		field_name = context->field_name;
	}
	if (owner_type == NULL && descriptor != NULL) {
		owner_type = descriptor->short_name;
	}
	if (field_name == NULL && field != NULL) {
		field_name = field->name;
	}
	if (message == NULL || owner_type == NULL || field_name == NULL ||
	    (context != NULL && context->identifier_forbidden)) {
		return 0;
	}
	if (strcmp(owner_type, "VariableSetStmt") == 0 &&
	    strcmp(field_name, "name") == 0) {
		if (message->descriptor !=
		    &pg_query__variable_set_stmt__descriptor) {
			return 0;
		}
		return ((PgQuery__VariableSetStmt *)message)->kind !=
			PG_QUERY__VARIABLE_SET_KIND__VAR_SET_MULTI;
	}
	if (strcmp(owner_type, "FuncCall") == 0 &&
	    strcmp(field_name, "funcname") == 0) {
		return context != NULL &&
			context->field_owner != NULL &&
			context->field_owner->descriptor ==
				&pg_query__func_call__descriptor &&
			((PgQuery__FuncCall *)context->field_owner)->funcformat ==
				PG_QUERY__COERCION_FORM__COERCE_EXPLICIT_CALL;
	}
	if (strcmp(owner_type, "Constraint") == 0 &&
	    strcmp(field_name, "keys") == 0 &&
	    context != NULL &&
	    context->field_owner != NULL &&
	    context->field_owner->descriptor ==
		    &pg_query__constraint__descriptor &&
	    ((PgQuery__Constraint *)context->field_owner)->contype ==
		    PG_QUERY__CONSTR_TYPE__CONSTR_NOTNULL) {
		return 0;
	}
	if (strcmp(owner_type, "DefElem") == 0) {
		PgQuery__AlterTableCmd *alter_table_cmd;
		PgQuery__Constraint *constraint;

		if ((strcmp(field_name, "defname") != 0 &&
		     strcmp(field_name, "defnamespace") != 0) ||
		    context == NULL || context->owner_type == NULL ||
		    context->field_name == NULL) {
			return 0;
		}
		if (strcmp(context->owner_type, "Constraint") == 0 &&
		    strcmp(context->field_name, "options") == 0) {
			if (context->field_owner == NULL ||
			    context->field_owner->descriptor !=
				    &pg_query__constraint__descriptor) {
				return 0;
			}
			constraint = (PgQuery__Constraint *)context->field_owner;
			return constraint->contype ==
					PG_QUERY__CONSTR_TYPE__CONSTR_PRIMARY ||
				constraint->contype ==
					PG_QUERY__CONSTR_TYPE__CONSTR_UNIQUE ||
				constraint->contype ==
					PG_QUERY__CONSTR_TYPE__CONSTR_EXCLUSION;
		}
		if (strcmp(context->owner_type, "AlterTableCmd") == 0 &&
		    strcmp(context->field_name, "def") == 0) {
			if (context->field_owner == NULL ||
			    context->field_owner->descriptor !=
				    &pg_query__alter_table_cmd__descriptor) {
				return 0;
			}
			alter_table_cmd =
				(PgQuery__AlterTableCmd *)context->field_owner;
			if (alter_table_cmd->subtype ==
					PG_QUERY__ALTER_TABLE_TYPE__AT_SetOptions ||
			    alter_table_cmd->subtype ==
					PG_QUERY__ALTER_TABLE_TYPE__AT_ResetOptions ||
			    alter_table_cmd->subtype ==
					PG_QUERY__ALTER_TABLE_TYPE__AT_SetRelOptions ||
			    alter_table_cmd->subtype ==
					PG_QUERY__ALTER_TABLE_TYPE__AT_ResetRelOptions) {
				return 1;
			}
			return strcmp(field_name, "defname") == 0 &&
				(alter_table_cmd->subtype ==
					PG_QUERY__ALTER_TABLE_TYPE__AT_AlterColumnGenericOptions ||
				 alter_table_cmd->subtype ==
					 PG_QUERY__ALTER_TABLE_TYPE__AT_GenericOptions);
		}
		if ((strcmp(context->owner_type, "AlterFdwStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "AlterForeignServerStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "AlterUserMappingStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "ColumnDef") == 0 &&
		     strcmp(context->field_name, "fdwoptions") == 0) ||
		    (strcmp(context->owner_type, "CreateFdwStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "CreateForeignServerStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "CreateForeignTableStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "CreateUserMappingStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "ImportForeignSchemaStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0)) {
			return strcmp(field_name, "defname") == 0;
		}
		return
			(strcmp(context->owner_type, "AlterTableSpaceOptionsStmt") == 0 &&
			 strcmp(context->field_name, "options") == 0) ||
			(strcmp(context->owner_type, "CreateStmt") == 0 &&
			 strcmp(context->field_name, "options") == 0) ||
			(strcmp(context->owner_type, "CreateTableSpaceStmt") == 0 &&
			 strcmp(context->field_name, "options") == 0) ||
			(strcmp(context->owner_type, "IndexElem") == 0 &&
			 strcmp(context->field_name, "opclassopts") == 0) ||
			(strcmp(context->owner_type, "IndexStmt") == 0 &&
			 strcmp(context->field_name, "options") == 0) ||
			(strcmp(context->owner_type, "IntoClause") == 0 &&
			 strcmp(context->field_name, "options") == 0) ||
			(strcmp(context->owner_type, "ViewStmt") == 0 &&
			 strcmp(context->field_name, "options") == 0);
	}

	low = 0U;
	high = sizeof(identifier_fields) / sizeof(identifier_fields[0]);
	while (low < high) {
		const size_t middle = low + (high - low) / 2U;
		int comparison;

		comparison =
			strcmp(owner_type, identifier_fields[middle][0]);
		if (comparison == 0) {
			comparison =
				strcmp(field_name, identifier_fields[middle][1]);
		}
		if (comparison < 0) {
			high = middle;
		} else if (comparison > 0) {
			low = middle + 1U;
		} else {
			return 1;
		}
	}
	return 0;
}


static int golden_func_call_is_implicit_pattern_wrapper(
	const PgQuery__FuncCall *func_call,
	const PgQuery__AExpr *a_expr)
{
	const char *function_name;
	const char *schema_name;

	if (func_call == NULL || a_expr == NULL ||
	    func_call->n_funcname != 2U || func_call->funcname == NULL ||
	    !sqlparser_node_string_value(
		    func_call->funcname[0],
		    &schema_name) ||
	    !sqlparser_node_string_value(
		    func_call->funcname[1],
		    &function_name) ||
	    strcmp(schema_name, "pg_catalog") != 0) {
		return 0;
	}
	if (a_expr->kind == PG_QUERY__A__EXPR__KIND__AEXPR_SIMILAR) {
		return strcmp(function_name, "similar_to_escape") == 0 &&
			(func_call->n_args == 1U || func_call->n_args == 2U);
	}
	if ((a_expr->kind == PG_QUERY__A__EXPR__KIND__AEXPR_LIKE ||
	     a_expr->kind == PG_QUERY__A__EXPR__KIND__AEXPR_ILIKE) &&
	    strcmp(function_name, "like_escape") == 0) {
		return func_call->n_args == 2U &&
			func_call->location == a_expr->location;
	}
	return 0;
}

static sqlparser_name_context_t golden_next_name_context(
	ProtobufCMessage *message,
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field,
	const sqlparser_name_context_t *context)
{
	sqlparser_name_context_t next_context;

	memset(&next_context, 0, sizeof(next_context));
	if (descriptor == &pg_query__a__indirection__descriptor &&
	    sqlparser_a_indirection_is_grouping(
		    (PgQuery__AIndirection *)message)) {
		if (context != NULL) {
			next_context = *context;
		}
		return next_context;
	}
	if (golden_message_is_name_container(descriptor)) {
		if (context != NULL) {
			next_context = *context;
		}
		return next_context;
	}

	if (descriptor != NULL) {
		next_context.owner_type = descriptor->short_name;
	}
	if (field != NULL) {
		next_context.field_name = field->name;
	}
	next_context.field_owner = message;
	if (descriptor == &pg_query__func_call__descriptor &&
	    field != NULL && strcmp(field->name, "funcname") == 0 &&
	    context != NULL &&
	    context->owner_type != NULL &&
	    strcmp(context->owner_type, "AExpr") == 0 &&
	    context->field_name != NULL &&
	    strcmp(context->field_name, "rexpr") == 0 &&
	    context->field_owner != NULL &&
	    context->field_owner->descriptor ==
		    &pg_query__a__expr__descriptor &&
	    golden_func_call_is_implicit_pattern_wrapper(
		    (PgQuery__FuncCall *)message,
		    (PgQuery__AExpr *)context->field_owner)) {
		next_context.identifier_forbidden = 1;
	}
	return next_context;
}


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
        !golden_name_atom_is_identifier(message, context, d, field)) return;
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
 * field-selection helper. Name classification/context helpers are reference string
 * policy, while ordering, selected slots and location ownership
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
            sqlparser_name_context_t next = golden_next_name_context(message, d, f, context);
            next.location_owner = location_owner;
            if (f->label == PROTOBUF_C_LABEL_REPEATED) {
                ProtobufCMessage **items = *(ProtobufCMessage ***)(base + f->offset);
                n = *(size_t *)(base + f->quantifier_offset);
                for (j = 0U; j < n; ++j) collect_child(message, &items[j], &next);
            } else collect_child(message, (ProtobufCMessage **)(base + f->offset), &next);
        }
    }
}

/* The reference scans every descriptor field. Only classification policy is
 * frozen from reference; enumeration, nearest-location tracking and ordering are independent. */
static void compare_walkers(ProtobufCMessage *root, const sqlparser_name_context_t *context,
    PgQuery__SelectStmt *tail)
{
    size_t i;
    int omitted, max_omitted = 0;
    int32_t locations[4096];
    memset(&reference, 0, sizeof(reference)); reference.tail = tail;
    collect_full(root, context);
    for (i = 0; i < reference.n_messages; i++) {
        int32_t *p = reference_location(reference.messages[i]);
        locations[i] = p ? *p : 0;
    }
#ifdef SQLPARSER_NAME_READ_OMIT
    max_omitted = 1;
#endif
    for (omitted = 0; omitted <= max_omitted; omitted++) {
        sqlparser_name_search_t search = {0};
#ifdef SQLPARSER_NAME_READ_OMIT
        search.omit_location_owner = omitted;
#endif
        CHECK(sqlparser_walk_message_names(root, context, &search) == SQLPARSER_STATUS_OK);
        CHECK(search.seen == reference.n_names && search.target_slot == NULL);
        CHECK(search.target_owner == NULL && search.target_field_owner == NULL);
        for (i = 0; i <= reference.n_names; i++) {
            sqlparser_name_view_t view = {0};
            memset(&search, 0, sizeof(search));
#ifdef SQLPARSER_NAME_READ_OMIT
            search.omit_location_owner = omitted;
#endif
            search.want_target = 1; search.target_index = i; search.name_view = &view;
            CHECK(sqlparser_walk_message_names(root, context, &search) == SQLPARSER_STATUS_OK);
            if (i == reference.n_names) {
                CHECK(search.target_slot == NULL && search.seen == i);
                CHECK(search.target_owner == NULL && search.target_field_owner == NULL);
                CHECK(view.value == NULL && view.owner_type == NULL && view.field_name == NULL);
                continue;
            }
            CHECK(search.seen == i + 1U && search.target_slot == reference.names[i].slot);
            CHECK(search.target_owner == (omitted ? NULL : reference.names[i].location_owner));
            CHECK(search.target_field_owner == reference.names[i].field_owner);
            CHECK(same_text(view.owner_type, reference.names[i].owner_type));
            CHECK(same_text(view.field_name, reference.names[i].field_name));
            CHECK(view.value == *reference.names[i].slot);
            if (omitted) omitted_checks++;
            else { name_checks++; owner_checks += search.target_owner != NULL; field_owner_checks += search.target_field_owner != NULL; }
            memset(&search, 0, sizeof(search)); search.match_slot = reference.names[i].slot;
#ifdef SQLPARSER_NAME_READ_OMIT
            search.omit_location_owner = omitted;
#endif
            CHECK(sqlparser_walk_message_names(root, context, &search) == SQLPARSER_STATUS_OK);
            CHECK(search.target_slot == reference.names[i].slot && search.target_index == i);
            CHECK(search.seen == reference.n_names);
            CHECK(search.target_owner == (omitted ? NULL : reference.names[i].location_owner));
            CHECK(search.target_field_owner == reference.names[i].field_owner);
        }
        memset(&search, 0, sizeof(search)); search.want_target = 1; search.target_index = SIZE_MAX;
#ifdef SQLPARSER_NAME_READ_OMIT
        search.omit_location_owner = omitted;
#endif
        CHECK(sqlparser_walk_message_names(root, context, &search) == SQLPARSER_STATUS_OK);
        CHECK(search.target_slot == NULL && search.seen == reference.n_names);
    }
    for (i = 0; i < reference.n_messages; i++) {
        int32_t *p = reference_location(reference.messages[i]);
        if (p) CHECK(*p == locations[i]);
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
        {
            PgQuery__RangeVar parent = PG_QUERY__RANGE_VAR__INIT;
            sqlparser_name_context_t inherited = TEST_CONTEXT("RangeVar", "relname", &parent.base, &parent.base, 0);
            parent.location = 173;
            compare_walkers(&wrapper.base, &inherited, NULL);
            inherited.identifier_forbidden = 1;
            compare_walkers(&wrapper.base, &inherited, NULL);
            /* Descending into a semantic owner can reset inherited filtering.
             * Compare the exact reference policy, rather than suppress all descendants. */
            CHECK(parent.location == 173);
        }
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

static void text(const char *s)
{
    if (!dump) return;
    if (!s) { putchar('~'); return; }
    for (; *s; s++) printf("%02x", (unsigned char)*s);
}
static void seed(sqlparser_error_t *e) { memset(e, 0xa5, sizeof(*e)); }
static int result(const char *op, sqlparser_status_t status, const sqlparser_error_t *e)
{
    statuses++;
    CHECK(e->code == status);
    if (status == SQLPARSER_STATUS_OK)
        CHECK(e->cursor == 0 && e->line == 0 && e->column == 0 && e->message[0] == '\0');
    else {
        failures++;
        CHECK(memchr(e->message, '\0', sizeof(e->message)) != NULL);
        fprintf(stderr, "STATUS|%s|%s|%s|%d|%s\n", case_name, phase, op, status, e->message);
    }
    if (dump) {
        printf("STATUS|%s|%d|%d|%d|%d|", op, status, e->cursor, e->line, e->column);
        text(e->message); putchar('\n');
    }
    return status == SQLPARSER_STATUS_OK;
}
static void begin(const char *name, sqlparser_dialect_t dialect, const char *sql)
{
    case_name = name;
    if (dump) { printf("CASE|%d|", dialect); text(name); putchar('|'); text(sql); putchar('\n'); }
}
static void set_phase(const char *value)
{
    phase = value;
    if (dump) printf("PHASE|%s\n", phase);
}
static sqlparser_handle_t *parse(const char *sql, sqlparser_dialect_t dialect)
{
    sqlparser_parse_options_t o;
    sqlparser_handle_t *h = NULL;
    sqlparser_error_t e;
    sqlparser_status_t status;
    sqlparser_parse_options_default(&o); o.dialect = dialect;
    seed(&e); status = sqlparser_parse_with_options(sql, &o, &h, &e);
    if (!result("parse", status, &e)) { CHECK(h == NULL); return NULL; }
    CHECK(h && strcmp(sqlparser_original_sql(h), sql) == 0);
    return h;
}
static int render(sqlparser_handle_t *h, char **out)
{
    sqlparser_error_t e;
    sqlparser_status_t status;
    *out = NULL; seed(&e); status = sqlparser_deparse(h, out, &e);
    if (!result("deparse", status, &e)) { CHECK(*out == NULL); return 0; }
    CHECK(*out != NULL);
    if (dump) { fputs("SQL|", stdout); text(*out); putchar('\n'); }
    return 1;
}
static void print_name(size_t statement, size_t ordinal, const sqlparser_name_view_t *v)
{
    if (dump) {
        printf("NAME|%zu|%zu|", statement, ordinal); text(v->owner_type); putchar('|');
        text(v->field_name); putchar('|'); text(v->value); putchar('\n');
    }
    public_names++;
}
static void provenance(sqlparser_handle_t *h)
{
    size_t i;
    if (!dump) return;
    printf("PROVENANCE|%zu\n", h->identifier_mutation_count);
    for (i = 0; i < h->identifier_mutation_count; i++) {
        const sqlparser_identifier_mutation_t *m = &h->identifier_mutations[i];
        printf("ORIGIN|%zu|%zu|%zu|%zu|%d|%d|", m->raw_statement_index, m->string_index,
            m->relation_group, m->source_component_index, m->has_source_component, m->source_present);
        text(m->value); putchar('|'); text(m->original); putchar('|'); text(m->spelling); putchar('\n');
    }
}

/* Every public failure ends this handle's path immediately, even when a getter
 * error is normally recoverable. Negative cases use fresh parses. */
static int inspect(sqlparser_handle_t *h, int mode, size_t *first_statement)
{
    size_t statement, n_statements = sqlparser_statement_count(h);
    unsigned long generation = h->generation;
    size_t mutation_count = h->identifier_mutation_count;
    sqlparser_error_t e;
    sqlparser_status_t status;
    char *source = strdup(sqlparser_original_sql(h));
    CHECK(source != NULL);
    if (first_statement) *first_statement = SIZE_MAX;
    if (dump) printf("STATEMENTS|%zu\n", n_statements);
    for (statement = 0; statement < n_statements; statement++) {
        size_t count = 1, i;
        if (mode != 1) {
            seed(&e); status = sqlparser_statement_name_count(h, statement, &count, &e);
            if (!result("name-count", status, &e)) { free(source); return 0; }
            if (dump) printf("COUNT|%zu|%zu\n", statement, count);
            if (count && first_statement && *first_statement == SIZE_MAX) *first_statement = statement;
        }
        if (mode == 2) continue;
        if (mode == 3 && count > 1) count = 1;
        for (i = 0; i < count; i++) {
            sqlparser_name_view_t v = {0}, again = {0};
            sqlparser_selector_t selector = {0};
            char *saved;
            seed(&e); status = sqlparser_statement_name(h, statement, i, &v, &e);
            if (!result("name", status, &e)) { free(source); return 0; }
            CHECK(v.owner_type && v.field_name && v.value && *v.value);
            print_name(statement, i, &v);
            if (mode == 1) continue; /* genuinely getter-only */
            saved = strdup(v.value); CHECK(saved);
            selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
            selector.statement_index = statement; selector.item_index = i;
            seed(&e); status = sqlparser_selector_name(h, &selector, &again, &e);
            if (!result("selector-name", status, &e)) { free(saved); free(source); return 0; }
            CHECK(same_text(v.owner_type, again.owner_type) && same_text(v.field_name, again.field_name));
            CHECK(v.value == again.value && strcmp(v.value, saved) == 0); free(saved); lifetime_checks++;
        }
        if (mode == 4) {
            PgQuery__Node *node = NULL;
            PgQuery__SelectStmt *tail = NULL;
            memset(&e, 0, sizeof(e)); status = sqlparser_get_statement_node(h, statement, &node, &e);
            if (!result("statement-node", status, &e)) { free(source); return 0; }
            memset(&e, 0, sizeof(e)); status = sqlparser_get_mysql_dml_tail_select(h, statement, node, &tail, &e);
            if (!result("mysql-tail", status, &e)) { free(source); return 0; }
            compare_walkers(&node->base, NULL, tail);
            CHECK(count == reference.n_names);
            for (i = 0; i < count; i++) {
                sqlparser_name_view_t v = {0};
                size_t ordinal = SIZE_MAX;
                seed(&e); status = sqlparser_statement_name(h, statement, i, &v, &e);
                if (!result("reference-name", status, &e)) { free(source); return 0; }
                CHECK(v.value == *reference.names[i].slot);
                CHECK(same_text(v.owner_type, reference.names[i].owner_type));
                CHECK(same_text(v.field_name, reference.names[i].field_name));
                memset(&e, 0, sizeof(e)); status = sqlparser_find_statement_name_index_by_slot(h, statement,
                    reference.names[i].slot, &ordinal, &e);
                if (!result("find-name-by-slot", status, &e)) { free(source); return 0; }
                CHECK(ordinal == i);
            }
        }
        public_statements++;
    }
    CHECK(h->generation == generation && h->identifier_mutation_count == mutation_count);
    CHECK(strcmp(source, sqlparser_original_sql(h)) == 0); free(source);
    provenance(h);
    return 1;
}

static int copy_first_target(sqlparser_handle_t *h)
{
    size_t statement;
    sqlparser_error_t e;
    sqlparser_status_t status;
    for (statement = 0; statement < sqlparser_statement_count(h); statement++) {
        size_t clauses = 0, i, list = 0;
        seed(&e); status = sqlparser_statement_clause_count(h, statement, &clauses, &e);
        if (!result("copy-clause-count", status, &e)) return 0;
        for (i = 0; i < clauses; i++) {
            sqlparser_clause_view_t v;
            seed(&e); status = sqlparser_statement_clause(h, statement, i, &v, &e);
            if (!result("copy-clause", status, &e)) return 0;
            if (v.kind == SQLPARSER_CLAUSE_KIND_SELECT_LIST) {
                char target[100], source[100];
                size_t count = 0;
                sqlparser_patch_t patch = {0};
                sqlparser_patch_list_t patches = {&patch, 1};
                seed(&e); status = sqlparser_select_target_count(h, statement, list, &count, &e);
                if (!result("copy-target-count", status, &e)) return 0;
                if (count == 0) { list++; continue; }
                snprintf(source, sizeof(source), "stmt[%zu].select_target[%zu][0]", statement, list);
                snprintf(target, sizeof(target), "stmt[%zu].select_target[%zu][%zu]", statement, list, count - 1);
                patch.op = SQLPARSER_PATCH_REPLACE; patch.selector = target; patch.source_selector = source;
                seed(&e); status = sqlparser_apply_patch(h, &patches, &e);
                if (!result("source-selector-copy", status, &e)) return 0;
                source_patches++; return 1;
            }
        }
    }
    if (dump) fputs("NO_SELECT_SOURCE\n", stdout);
    return 1;
}

static void public_case(const char *sql, sqlparser_dialect_t dialect)
{
    static const char *modes[] = {"cold", "get-only", "count-only", "count-one", "count-all", "read-set-copy", "read-name-patch", "out-of-range"};
    size_t mode;
    for (mode = 0; mode < COUNT(modes); mode++) {
        sqlparser_handle_t *h;
        sqlparser_error_t e;
        sqlparser_status_t status;
        char *output = NULL, *snapshot = NULL;
        size_t first = SIZE_MAX;
        set_phase(modes[mode]); h = parse(sql, dialect);
        if (!h) continue;
        if (mode == 0) goto render_done;
        if (mode == 7) {
            sqlparser_name_view_t v = {0};
            seed(&e); status = sqlparser_statement_name(h, 0, SIZE_MAX, &v, &e);
            CHECK(status != SQLPARSER_STATUS_OK);
            (void)result("name-out-of-range", status, &e);
            goto done;
        }
        if (!inspect(h, mode <= 4 ? (int)mode : 4, &first)) goto done;
        if (mode == 5 || mode == 6) {
            if (first == SIZE_MAX) goto render_done;
            if (mode == 5) {
                seed(&e); status = sqlparser_statement_set_name(h, first, 0, "m11_renamed", &e);
                if (!result("set-name", status, &e)) goto done;
                mutations++;
                if (!inspect(h, 4, NULL)) goto done;
                if (!copy_first_target(h)) goto done;
            } else {
                char selector[80];
                sqlparser_patch_t patch = {0};
                sqlparser_patch_list_t patches = {&patch, 1};
                snprintf(selector, sizeof(selector), "stmt[%zu].name[0]", first);
                patch.op = SQLPARSER_PATCH_REPLACE; patch.selector = selector;
                patch.sql = (dialect == SQLPARSER_DIALECT_MYSQL ||
                    dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL || dialect == SQLPARSER_DIALECT_KINGBASE_MYSQL) ?
                    "`Name 日本`" : (dialect == SQLPARSER_DIALECT_SQLSERVER ||
                    dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER || dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER) ?
                    "[Name 日本]" : "\"Name 日本\"";
                seed(&e); status = sqlparser_apply_patch(h, &patches, &e);
                if (!result("name-sql-patch", status, &e)) goto done;
                mutations++;
            }
            if (!inspect(h, 4, NULL)) goto done;
        }
render_done:
        if (render(h, &output)) { snapshot = strdup(output); CHECK(snapshot); }
done:
        sqlparser_handle_destroy(h);
        if (output) {
            CHECK(snapshot && strcmp(output, snapshot) == 0); lifetime_checks++;
            sqlparser_string_free(output); free(snapshot);
        }
    }
}

static void corpora(const char *directory)
{
    static const char *files[] = {
        "sql_batch_input.json", "mysql_dialect_input.json", "oracle_dialect_input.json",
        "sqlserver_dialect_input.json", "dameng_dialect_input.json", "vastbase_oracle_dialect_input.json",
        "vastbase_mysql_dialect_input.json", "vastbase_postgresql_dialect_input.json",
        "vastbase_sqlserver_dialect_input.json", "kingbase_oracle_dialect_input.json",
        "kingbase_mysql_dialect_input.json", "kingbase_postgresql_dialect_input.json",
        "kingbase_sqlserver_dialect_input.json"
    };
    static const char *names[] = {"postgresql", "mysql", "oracle", "sqlserver", "dameng", "vastbase-oracle", "vastbase-mysql", "vastbase-postgresql", "vastbase-sqlserver", "kingbase-oracle", "kingbase-mysql", "kingbase-postgresql", "kingbase-sqlserver"};
    size_t f;
    for (f = 0; f < COUNT(files); f++) {
        char path[4096];
        json_error_t e;
        json_t *root, *items, *item;
        size_t i;
        CHECK(snprintf(path, sizeof(path), "%s/%s", directory, files[f]) < (int)sizeof(path));
        root = json_load_file(path, 0, &e); CHECK(root);
        items = json_object_get(root, "items"); CHECK(json_is_array(items));
        json_array_foreach(items, i, item) {
            const char *sql = json_string_value(json_object_get(item, "sql"));
            const char *name = json_string_value(json_object_get(item, "name"));
            const char *declared = json_string_value(json_object_get(item, "dialect"));
            size_t dialect = f;
            CHECK(sql && name);
            if (declared) {
                for (dialect = 0; dialect < COUNT(names) && strcmp(names[dialect], declared); dialect++) {}
                CHECK(dialect < COUNT(names));
                override_inputs += dialect != f;
            }
            begin(name, (sqlparser_dialect_t)dialect, sql);
            public_case(sql, (sqlparser_dialect_t)dialect); corpus_inputs++;
        }
        fprintf(stderr, "CORPUS|%s|%zu\n", files[f], json_array_size(items));
        json_decref(root);
    }
    CHECK(corpus_inputs == 3887U && override_inputs == 7U);
}
static size_t find_name(sqlparser_handle_t *h, const char *owner, const char *field, const char *value)
{
    sqlparser_error_t e;
    size_t count, i;
    CHECK(sqlparser_statement_name_count(h, 0, &count, &e) == SQLPARSER_STATUS_OK);
    for (i = 0; i < count; i++) {
        sqlparser_name_view_t v;
        CHECK(sqlparser_statement_name(h, 0, i, &v, &e) == SQLPARSER_STATUS_OK);
        if (same_text(v.owner_type, owner) && same_text(v.field_name, field) && same_text(v.value, value)) return i;
    }
    return SIZE_MAX;
}

static void positive_lifetimes(void)
{
    const char *sql = "/*head 日本*/ SELECT Foo AS left_alias, KeepCol AS right_alias FROM MixedTable /*tail*/";
    size_t dialect;
    for (dialect = 0; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        sqlparser_handle_t *h;
        sqlparser_error_t e;
        sqlparser_name_view_t borrowed, again;
        sqlparser_patch_t p[2] = {{0}};
        sqlparser_patch_list_t patches = {p, 2};
        char a[80], b[80], *input, *out = NULL, *old = NULL;
        size_t left, right, count;
        unsigned long generation;
        begin("borrowed-name-inputs", (sqlparser_dialect_t)dialect, sql);
        set_phase("read-borrow-set-patch-copy");
        input = strdup(sql); CHECK(input);
        h = parse(input, (sqlparser_dialect_t)dialect); CHECK(h);
        /* Parse owns its input. The caller buffer can change immediately. */
        memset(input, '!', strlen(input)); free(input);
        CHECK(strcmp(sqlparser_original_sql(h), sql) == 0);
        CHECK(inspect(h, 4, NULL));
        CHECK(render(h, &old)); CHECK(strcmp(old, sql) == 0);
        left = find_name(h, "ResTarget", "name", "left_alias");
        right = find_name(h, "ResTarget", "name", "right_alias");
        CHECK(left != SIZE_MAX && right != SIZE_MAX);
        seed(&e); CHECK(result("borrow-name", sqlparser_statement_name(h, 0, left, &borrowed, &e), &e));
        generation = h->generation;
        seed(&e); CHECK(result("borrow-count", sqlparser_statement_name_count(h, 0, &count, &e), &e));
        seed(&e); CHECK(result("borrow-again", sqlparser_statement_name(h, 0, left, &again, &e), &e));
        CHECK(h->generation == generation && borrowed.value == again.value);
        CHECK(strcmp(borrowed.value, "left_alias") == 0);
        /* Semantic setter consumes a borrowed view safely, including exact no-op. */
        seed(&e); CHECK(result("borrowed-set-name", sqlparser_statement_set_name(h, 0, right, borrowed.value, &e), &e));
        mutations++;
        CHECK(find_name(h, "ResTarget", "name", "left_alias") != SIZE_MAX);
        CHECK(inspect(h, 4, NULL));
        seed(&e); CHECK(result("reborrow-name", sqlparser_statement_name(h, 0, left, &borrowed, &e), &e));
        snprintf(a, sizeof(a), "stmt[0].name[%zu]", left);
        snprintf(b, sizeof(b), "stmt[0].name[%zu]", right);
        p[0].op = p[1].op = SQLPARSER_PATCH_REPLACE;
        p[0].selector = a; p[0].default_sql = "SELECT";
        p[1].selector = b; p[1].default_sql = borrowed.value;
        /* The first edit invalidates the source storage for the second edit;
         * apply_patch must copy its borrowed input before performing edits. */
        seed(&e); CHECK(result("borrowed-name-patch", sqlparser_apply_patch(h, &patches, &e), &e));
        mutations++;
        CHECK(find_name(h, "ResTarget", "name", "SELECT") != SIZE_MAX);
        CHECK(find_name(h, "ResTarget", "name", "left_alias") != SIZE_MAX);
        CHECK(inspect(h, 4, NULL));
        CHECK(h->identifier_mutation_count > 0U);
        CHECK(copy_first_target(h)); CHECK(inspect(h, 4, NULL));
        CHECK(render(h, &out));
        CHECK(strstr(out, "SELECT") && strstr(out, "Foo"));
        CHECK(strcmp(old, sql) == 0);
        sqlparser_handle_destroy(h);
        CHECK(strcmp(old, sql) == 0);
        CHECK(out && *out); lifetime_checks += 3;
        sqlparser_string_free(old); sqlparser_string_free(out);
    }
}

static void exact_source_fidelity(void)
{
    const char *sql = "/*head 日本*/ SELECT Foo AS left_alias, KeepCol AS right_alias FROM MixedTable /*tail*/";
    size_t dialect;
    for (dialect = 0; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        sqlparser_handle_t *h;
        sqlparser_error_t e;
        sqlparser_patch_t p = {0};
        sqlparser_patch_list_t patches = {&p, 1};
        char selector[80], expected[512], *out = NULL;
        size_t left;
        const char *quoted = (dialect == SQLPARSER_DIALECT_MYSQL || dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL || dialect == SQLPARSER_DIALECT_KINGBASE_MYSQL) ?
            "`日本 Alias`" : (dialect == SQLPARSER_DIALECT_SQLSERVER || dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER || dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER) ?
            "[日本 Alias]" : "\"日本 Alias\"";
        begin("exact-name-source-fidelity", (sqlparser_dialect_t)dialect, sql);
        set_phase("read-quoted-name-patch");
        h = parse(sql, (sqlparser_dialect_t)dialect); CHECK(h);
        CHECK(inspect(h, 4, NULL));
        left = find_name(h, "ResTarget", "name", "left_alias"); CHECK(left != SIZE_MAX);
        snprintf(selector, sizeof(selector), "stmt[0].name[%zu]", left);
        p.op = SQLPARSER_PATCH_REPLACE; p.selector = selector; p.sql = quoted;
        seed(&e); CHECK(result("exact-name-patch", sqlparser_apply_patch(h, &patches, &e), &e));
        mutations++;
        CHECK(inspect(h, 4, NULL));
        CHECK(render(h, &out));
        snprintf(expected, sizeof(expected), "/*head 日本*/ SELECT Foo AS %s, KeepCol AS right_alias FROM MixedTable /*tail*/", quoted);
        if (strcmp(out, expected) != 0) {
            /* reference drops the leading comment on this SQL Server-family
             * name-patch path. Retain the exact gap without accepting any
             * other source-fidelity change. */
            CHECK(dialect == SQLPARSER_DIALECT_SQLSERVER || dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER || dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER);
            CHECK(strcmp(out, expected + strlen("/*head 日本*/ ")) == 0);
            if (dump) { printf("BASELINE_FIDELITY_GAP|%zu|", dialect); text(expected); putchar('|'); text(out); putchar('\n'); }
            memmove(expected, expected + strlen("/*head 日本*/ "), strlen(expected + strlen("/*head 日本*/ ")) + 1U);
        }
        sqlparser_handle_destroy(h);
        CHECK(strcmp(out, expected) == 0); lifetime_checks++; sqlparser_string_free(out);
    }
}

static void dedicated_cases(void)
{
    static const struct { sqlparser_dialect_t dialect; const char *sql; } cases[] = {
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT 1"},
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT NULL, TRUE, 'not_identifier', 2 + 3, $1"},
        {SQLPARSER_DIALECT_POSTGRESQL, "/*源*/ SELECT \"日本\".\"café\" AS \"Alias\", lower(\"日本\".\"café\") FROM \"Sch\".\"日本\" WHERE \"日本\".x = 'literal'"},
        {SQLPARSER_DIALECT_POSTGRESQL, "WITH q AS (SELECT a, b FROM sch.t WHERE a = 1) SELECT q.a AS answer, lower(q.b) FROM q JOIN u ON q.a = u.a WHERE EXISTS (SELECT 1 FROM v WHERE v.id = 7) GROUP BY q.a, q.b HAVING count(*) > 2 ORDER BY q.a LIMIT 3"},
        {SQLPARSER_DIALECT_POSTGRESQL, "(SELECT a FROM t WHERE x = 1 ORDER BY a LIMIT 2) UNION ALL (SELECT b FROM u WHERE y = 3) ORDER BY 1"},
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT a = ANY(ARRAY[1,2]), a LIKE 'x%' ESCAPE '!', CAST(a AS text), INTERVAL '2 days' FROM t"},
        {SQLPARSER_DIALECT_POSTGRESQL, "INSERT INTO t(a,b) VALUES (1,'one'),(2,'two') ON CONFLICT(a) DO UPDATE SET b = 'new' WHERE t.a > 0 RETURNING a"},
        {SQLPARSER_DIALECT_POSTGRESQL, "WITH q AS (UPDATE u SET x = 'inner' WHERE id = 1 RETURNING id) UPDATE t SET a = 2 FROM q WHERE t.id = q.id RETURNING t.a"},
        {SQLPARSER_DIALECT_POSTGRESQL, "CREATE TABLE typed(id integer PRIMARY KEY, value text DEFAULT 'seed', CHECK (id > 0)); CREATE INDEX idx ON typed(value) WHERE id > 2"},
        {SQLPARSER_DIALECT_MYSQL, "UPDATE `日本` SET `café` = 'one', b = 2 WHERE id = 3 ORDER BY `café` LIMIT 4"},
        {SQLPARSER_DIALECT_MYSQL, "DELETE FROM `t` WHERE id = 8 ORDER BY id LIMIT 2"},
        {SQLPARSER_DIALECT_MYSQL, "SELECT `a`, concat('x', b) AS `别名` FROM `t` WHERE id IN (1,2) ORDER BY a LIMIT 2,5"},
        {SQLPARSER_DIALECT_ORACLE, "SELECT t.a, NVL(t.b, 'fallback') FROM tab t WHERE t.id = :key UNION ALL SELECT u.a, u.b FROM other_tab u WHERE u.id = 2 ORDER BY 1"},
        {SQLPARSER_DIALECT_ORACLE, "SELECT LEVEL, PRIOR employee_id FROM employees CONNECT BY PRIOR employee_id = manager_id START WITH manager_id IS NULL"},
        {SQLPARSER_DIALECT_SQLSERVER, "SELECT TOP (3) [日本], [café] AS [别名] FROM [dbo].[t] WHERE [id] = 2 ORDER BY [日本]"},
        {SQLPARSER_DIALECT_POSTGRESQL, ""},
        {SQLPARSER_DIALECT_POSTGRESQL, "-- comments only\n/*日本*/"},
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT FROM"},
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT 'unterminated"}
    };
    size_t i;
    for (i = 0; i < COUNT(cases); i++) {
        char label[60]; snprintf(label, sizeof(label), "dedicated-%zu", i);
        begin(label, cases[i].dialect, cases[i].sql);
        public_case(cases[i].sql, cases[i].dialect);
    }
}

static void zero_and_forbidden(void)
{
    PgQuery__String atom = PG_QUERY__STRING__INIT;
    PgQuery__ColumnRef parent = PG_QUERY__COLUMN_REF__INIT;
    sqlparser_name_context_t context = TEST_CONTEXT("ColumnRef", "fields", &parent.base, &parent.base, 0);
    sqlparser_handle_t *h;
    sqlparser_error_t e;
    size_t n, i;
    case_name = "explicit-forbidden-context";
    atom.sval = (char *)"identifier"; atom.location = 39; parent.location = 17;
    compare_walkers(&atom.base, &context, NULL); CHECK(reference.n_names == 1U);
    CHECK(reference.names[0].location_owner == &atom.base);
    context.identifier_forbidden = 1;
    compare_walkers(&atom.base, &context, NULL); CHECK(reference.n_names == 0U);
    begin("zero-identifiers", SQLPARSER_DIALECT_POSTGRESQL, "SELECT NULL, TRUE, 'value', 42, 1 + 2");
    set_phase("zero-name-proof");
    h = parse("SELECT NULL, TRUE, 'value', 42, 1 + 2", SQLPARSER_DIALECT_POSTGRESQL); CHECK(h);
    CHECK(sqlparser_statement_name_count(h, 0, &n, &e) == SQLPARSER_STATUS_OK && n == 0U);
    CHECK(inspect(h, 4, NULL)); sqlparser_handle_destroy(h);
    begin("implicit-wrapper-filter", SQLPARSER_DIALECT_POSTGRESQL, "SELECT a LIKE 'x%' ESCAPE '!' FROM t");
    set_phase("implicit-wrapper-proof");
    h = parse("SELECT a LIKE 'x%' ESCAPE '!' FROM t", SQLPARSER_DIALECT_POSTGRESQL); CHECK(h);
    CHECK(sqlparser_statement_name_count(h, 0, &n, &e) == SQLPARSER_STATUS_OK && n == 2U);
    for (i = 0; i < n; i++) {
        sqlparser_name_view_t v;
        CHECK(sqlparser_statement_name(h, 0, i, &v, &e) == SQLPARSER_STATUS_OK);
        CHECK(!strcmp(v.value, "a") || !strcmp(v.value, "t"));
    }
    CHECK(inspect(h, 4, NULL)); sqlparser_handle_destroy(h);
}

static void argument_errors(void)
{
    sqlparser_error_t e;
    sqlparser_status_t status;
    size_t i, count = 123;
    sqlparser_name_view_t v = {0};
    case_name = "argument-errors"; set_phase("negative");
    seed(&e); status = sqlparser_statement_name_count(NULL, 0, &count, &e);
    CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT); (void)result("null-count-handle", status, &e);
    seed(&e); status = sqlparser_statement_name(NULL, 0, 0, &v, &e);
    CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT); (void)result("null-name-handle", status, &e);
    for (i = 0; i < 5; i++) {
        sqlparser_handle_t *h = parse(i == 4 ? "SELECT 1" : "SELECT a FROM t", SQLPARSER_DIALECT_POSTGRESQL);
        CHECK(h); seed(&e);
        if (i == 0) status = sqlparser_statement_name_count(h, 0, NULL, &e);
        else if (i == 1) status = sqlparser_statement_name(h, 0, 0, NULL, &e);
        else if (i == 2) status = sqlparser_statement_name_count(h, SIZE_MAX, &count, &e);
        else if (i == 3) status = sqlparser_statement_name(h, SIZE_MAX, 0, &v, &e);
        else status = sqlparser_statement_name(h, 0, 0, &v, &e);
        CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT); (void)result("invalid-public-argument", status, &e);
        sqlparser_handle_destroy(h);
    }
}

static const ProtobufCMessageDescriptor *const test_descriptors[] = {
    &pg_query__parse_result__descriptor,
    &pg_query__scan_result__descriptor,
    &pg_query__node__descriptor,
    &pg_query__integer__descriptor,
    &pg_query__float__descriptor,
    &pg_query__boolean__descriptor,
    &pg_query__string__descriptor,
    &pg_query__bit_string__descriptor,
    &pg_query__list__descriptor,
    &pg_query__oid_list__descriptor,
    &pg_query__int_list__descriptor,
    &pg_query__a__const__descriptor,
    &pg_query__alias__descriptor,
    &pg_query__range_var__descriptor,
    &pg_query__table_func__descriptor,
    &pg_query__into_clause__descriptor,
    &pg_query__var__descriptor,
    &pg_query__param__descriptor,
    &pg_query__aggref__descriptor,
    &pg_query__grouping_func__descriptor,
    &pg_query__window_func__descriptor,
    &pg_query__window_func_run_condition__descriptor,
    &pg_query__merge_support_func__descriptor,
    &pg_query__subscripting_ref__descriptor,
    &pg_query__func_expr__descriptor,
    &pg_query__named_arg_expr__descriptor,
    &pg_query__op_expr__descriptor,
    &pg_query__distinct_expr__descriptor,
    &pg_query__null_if_expr__descriptor,
    &pg_query__scalar_array_op_expr__descriptor,
    &pg_query__bool_expr__descriptor,
    &pg_query__sub_link__descriptor,
    &pg_query__sub_plan__descriptor,
    &pg_query__alternative_sub_plan__descriptor,
    &pg_query__field_select__descriptor,
    &pg_query__field_store__descriptor,
    &pg_query__relabel_type__descriptor,
    &pg_query__coerce_via_io__descriptor,
    &pg_query__array_coerce_expr__descriptor,
    &pg_query__convert_rowtype_expr__descriptor,
    &pg_query__collate_expr__descriptor,
    &pg_query__case_expr__descriptor,
    &pg_query__case_when__descriptor,
    &pg_query__case_test_expr__descriptor,
    &pg_query__array_expr__descriptor,
    &pg_query__row_expr__descriptor,
    &pg_query__row_compare_expr__descriptor,
    &pg_query__coalesce_expr__descriptor,
    &pg_query__min_max_expr__descriptor,
    &pg_query__sqlvalue_function__descriptor,
    &pg_query__xml_expr__descriptor,
    &pg_query__json_format__descriptor,
    &pg_query__json_returning__descriptor,
    &pg_query__json_value_expr__descriptor,
    &pg_query__json_constructor_expr__descriptor,
    &pg_query__json_is_predicate__descriptor,
    &pg_query__json_behavior__descriptor,
    &pg_query__json_expr__descriptor,
    &pg_query__json_table_path__descriptor,
    &pg_query__json_table_path_scan__descriptor,
    &pg_query__json_table_sibling_join__descriptor,
    &pg_query__null_test__descriptor,
    &pg_query__boolean_test__descriptor,
    &pg_query__merge_action__descriptor,
    &pg_query__coerce_to_domain__descriptor,
    &pg_query__coerce_to_domain_value__descriptor,
    &pg_query__set_to_default__descriptor,
    &pg_query__current_of_expr__descriptor,
    &pg_query__next_value_expr__descriptor,
    &pg_query__inference_elem__descriptor,
    &pg_query__target_entry__descriptor,
    &pg_query__range_tbl_ref__descriptor,
    &pg_query__join_expr__descriptor,
    &pg_query__from_expr__descriptor,
    &pg_query__on_conflict_expr__descriptor,
    &pg_query__query__descriptor,
    &pg_query__type_name__descriptor,
    &pg_query__column_ref__descriptor,
    &pg_query__param_ref__descriptor,
    &pg_query__a__expr__descriptor,
    &pg_query__type_cast__descriptor,
    &pg_query__collate_clause__descriptor,
    &pg_query__role_spec__descriptor,
    &pg_query__func_call__descriptor,
    &pg_query__a__star__descriptor,
    &pg_query__a__indices__descriptor,
    &pg_query__a__indirection__descriptor,
    &pg_query__a__array_expr__descriptor,
    &pg_query__res_target__descriptor,
    &pg_query__multi_assign_ref__descriptor,
    &pg_query__sort_by__descriptor,
    &pg_query__window_def__descriptor,
    &pg_query__range_subselect__descriptor,
    &pg_query__range_function__descriptor,
    &pg_query__range_table_func__descriptor,
    &pg_query__range_table_func_col__descriptor,
    &pg_query__range_table_sample__descriptor,
    &pg_query__column_def__descriptor,
    &pg_query__table_like_clause__descriptor,
    &pg_query__index_elem__descriptor,
    &pg_query__def_elem__descriptor,
    &pg_query__locking_clause__descriptor,
    &pg_query__xml_serialize__descriptor,
    &pg_query__partition_elem__descriptor,
    &pg_query__partition_spec__descriptor,
    &pg_query__partition_bound_spec__descriptor,
    &pg_query__partition_range_datum__descriptor,
    &pg_query__single_partition_spec__descriptor,
    &pg_query__partition_cmd__descriptor,
    &pg_query__range_tbl_entry__descriptor,
    &pg_query__rtepermission_info__descriptor,
    &pg_query__range_tbl_function__descriptor,
    &pg_query__table_sample_clause__descriptor,
    &pg_query__with_check_option__descriptor,
    &pg_query__sort_group_clause__descriptor,
    &pg_query__grouping_set__descriptor,
    &pg_query__window_clause__descriptor,
    &pg_query__row_mark_clause__descriptor,
    &pg_query__with_clause__descriptor,
    &pg_query__infer_clause__descriptor,
    &pg_query__on_conflict_clause__descriptor,
    &pg_query__ctesearch_clause__descriptor,
    &pg_query__ctecycle_clause__descriptor,
    &pg_query__common_table_expr__descriptor,
    &pg_query__merge_when_clause__descriptor,
    &pg_query__trigger_transition__descriptor,
    &pg_query__json_output__descriptor,
    &pg_query__json_argument__descriptor,
    &pg_query__json_func_expr__descriptor,
    &pg_query__json_table_path_spec__descriptor,
    &pg_query__json_table__descriptor,
    &pg_query__json_table_column__descriptor,
    &pg_query__json_key_value__descriptor,
    &pg_query__json_parse_expr__descriptor,
    &pg_query__json_scalar_expr__descriptor,
    &pg_query__json_serialize_expr__descriptor,
    &pg_query__json_object_constructor__descriptor,
    &pg_query__json_array_constructor__descriptor,
    &pg_query__json_array_query_constructor__descriptor,
    &pg_query__json_agg_constructor__descriptor,
    &pg_query__json_object_agg__descriptor,
    &pg_query__json_array_agg__descriptor,
    &pg_query__raw_stmt__descriptor,
    &pg_query__insert_stmt__descriptor,
    &pg_query__delete_stmt__descriptor,
    &pg_query__update_stmt__descriptor,
    &pg_query__merge_stmt__descriptor,
    &pg_query__select_stmt__descriptor,
    &pg_query__set_operation_stmt__descriptor,
    &pg_query__return_stmt__descriptor,
    &pg_query__plassign_stmt__descriptor,
    &pg_query__create_schema_stmt__descriptor,
    &pg_query__alter_table_stmt__descriptor,
    &pg_query__replica_identity_stmt__descriptor,
    &pg_query__alter_table_cmd__descriptor,
    &pg_query__alter_collation_stmt__descriptor,
    &pg_query__alter_domain_stmt__descriptor,
    &pg_query__grant_stmt__descriptor,
    &pg_query__object_with_args__descriptor,
    &pg_query__access_priv__descriptor,
    &pg_query__grant_role_stmt__descriptor,
    &pg_query__alter_default_privileges_stmt__descriptor,
    &pg_query__copy_stmt__descriptor,
    &pg_query__variable_set_stmt__descriptor,
    &pg_query__variable_show_stmt__descriptor,
    &pg_query__create_stmt__descriptor,
    &pg_query__constraint__descriptor,
    &pg_query__create_table_space_stmt__descriptor,
    &pg_query__drop_table_space_stmt__descriptor,
    &pg_query__alter_table_space_options_stmt__descriptor,
    &pg_query__alter_table_move_all_stmt__descriptor,
    &pg_query__create_extension_stmt__descriptor,
    &pg_query__alter_extension_stmt__descriptor,
    &pg_query__alter_extension_contents_stmt__descriptor,
    &pg_query__create_fdw_stmt__descriptor,
    &pg_query__alter_fdw_stmt__descriptor,
    &pg_query__create_foreign_server_stmt__descriptor,
    &pg_query__alter_foreign_server_stmt__descriptor,
    &pg_query__create_foreign_table_stmt__descriptor,
    &pg_query__create_user_mapping_stmt__descriptor,
    &pg_query__alter_user_mapping_stmt__descriptor,
    &pg_query__drop_user_mapping_stmt__descriptor,
    &pg_query__import_foreign_schema_stmt__descriptor,
    &pg_query__create_policy_stmt__descriptor,
    &pg_query__alter_policy_stmt__descriptor,
    &pg_query__create_am_stmt__descriptor,
    &pg_query__create_trig_stmt__descriptor,
    &pg_query__create_event_trig_stmt__descriptor,
    &pg_query__alter_event_trig_stmt__descriptor,
    &pg_query__create_plang_stmt__descriptor,
    &pg_query__create_role_stmt__descriptor,
    &pg_query__alter_role_stmt__descriptor,
    &pg_query__alter_role_set_stmt__descriptor,
    &pg_query__drop_role_stmt__descriptor,
    &pg_query__create_seq_stmt__descriptor,
    &pg_query__alter_seq_stmt__descriptor,
    &pg_query__define_stmt__descriptor,
    &pg_query__create_domain_stmt__descriptor,
    &pg_query__create_op_class_stmt__descriptor,
    &pg_query__create_op_class_item__descriptor,
    &pg_query__create_op_family_stmt__descriptor,
    &pg_query__alter_op_family_stmt__descriptor,
    &pg_query__drop_stmt__descriptor,
    &pg_query__truncate_stmt__descriptor,
    &pg_query__comment_stmt__descriptor,
    &pg_query__sec_label_stmt__descriptor,
    &pg_query__declare_cursor_stmt__descriptor,
    &pg_query__close_portal_stmt__descriptor,
    &pg_query__fetch_stmt__descriptor,
    &pg_query__index_stmt__descriptor,
    &pg_query__create_stats_stmt__descriptor,
    &pg_query__stats_elem__descriptor,
    &pg_query__alter_stats_stmt__descriptor,
    &pg_query__create_function_stmt__descriptor,
    &pg_query__function_parameter__descriptor,
    &pg_query__alter_function_stmt__descriptor,
    &pg_query__do_stmt__descriptor,
    &pg_query__inline_code_block__descriptor,
    &pg_query__call_stmt__descriptor,
    &pg_query__call_context__descriptor,
    &pg_query__rename_stmt__descriptor,
    &pg_query__alter_object_depends_stmt__descriptor,
    &pg_query__alter_object_schema_stmt__descriptor,
    &pg_query__alter_owner_stmt__descriptor,
    &pg_query__alter_operator_stmt__descriptor,
    &pg_query__alter_type_stmt__descriptor,
    &pg_query__rule_stmt__descriptor,
    &pg_query__notify_stmt__descriptor,
    &pg_query__listen_stmt__descriptor,
    &pg_query__unlisten_stmt__descriptor,
    &pg_query__transaction_stmt__descriptor,
    &pg_query__composite_type_stmt__descriptor,
    &pg_query__create_enum_stmt__descriptor,
    &pg_query__create_range_stmt__descriptor,
    &pg_query__alter_enum_stmt__descriptor,
    &pg_query__view_stmt__descriptor,
    &pg_query__load_stmt__descriptor,
    &pg_query__createdb_stmt__descriptor,
    &pg_query__alter_database_stmt__descriptor,
    &pg_query__alter_database_refresh_coll_stmt__descriptor,
    &pg_query__alter_database_set_stmt__descriptor,
    &pg_query__dropdb_stmt__descriptor,
    &pg_query__alter_system_stmt__descriptor,
    &pg_query__cluster_stmt__descriptor,
    &pg_query__vacuum_stmt__descriptor,
    &pg_query__vacuum_relation__descriptor,
    &pg_query__explain_stmt__descriptor,
    &pg_query__create_table_as_stmt__descriptor,
    &pg_query__refresh_mat_view_stmt__descriptor,
    &pg_query__check_point_stmt__descriptor,
    &pg_query__discard_stmt__descriptor,
    &pg_query__lock_stmt__descriptor,
    &pg_query__constraints_set_stmt__descriptor,
    &pg_query__reindex_stmt__descriptor,
    &pg_query__create_conversion_stmt__descriptor,
    &pg_query__create_cast_stmt__descriptor,
    &pg_query__create_transform_stmt__descriptor,
    &pg_query__prepare_stmt__descriptor,
    &pg_query__execute_stmt__descriptor,
    &pg_query__deallocate_stmt__descriptor,
    &pg_query__drop_owned_stmt__descriptor,
    &pg_query__reassign_owned_stmt__descriptor,
    &pg_query__alter_tsdictionary_stmt__descriptor,
    &pg_query__alter_tsconfiguration_stmt__descriptor,
    &pg_query__publication_table__descriptor,
    &pg_query__publication_obj_spec__descriptor,
    &pg_query__create_publication_stmt__descriptor,
    &pg_query__alter_publication_stmt__descriptor,
    &pg_query__create_subscription_stmt__descriptor,
    &pg_query__alter_subscription_stmt__descriptor,
    &pg_query__drop_subscription_stmt__descriptor,
    &pg_query__scan_token__descriptor,
    &pg_query__summary_result__descriptor,
    &pg_query__summary_result__table__descriptor,
    &pg_query__summary_result__aliases_entry__descriptor,
    &pg_query__summary_result__function__descriptor,
    &pg_query__summary_result__filter_column__descriptor,
};

static size_t descriptor_checks, field_checks, policy_checks, context_checks;
static size_t fallback_checks, dynamic_checks, pattern_checks;

static void equal_context(const sqlparser_name_context_t *a, const sqlparser_name_context_t *b)
{
    CHECK(a->owner_type == b->owner_type && a->field_name == b->field_name);
    CHECK(a->field_owner == b->field_owner && a->location_owner == b->location_owner);
    CHECK(a->identifier_forbidden == b->identifier_forbidden);
    context_checks++;
}

static void equal_atom(ProtobufCMessage *m, const ProtobufCMessageDescriptor *d,
    const ProtobufCFieldDescriptor *f, const sqlparser_name_context_t *a,
    const sqlparser_name_context_t *b)
{
    int actual = sqlparser_name_atom_is_identifier(m, a, d, f);
    int expected = golden_name_atom_is_identifier(m, b, d, f);
    if (actual != expected) fprintf(stderr, "MISMATCH descriptor=%s field=%s context=%s.%s actual=%d expected=%d\n",
        d && d->short_name ? d->short_name : "NULL", f && f->name ? f->name : "NULL",
        a && a->owner_type ? a->owner_type : "NULL", a && a->field_name ? a->field_name : "NULL", actual, expected);
    CHECK(actual == expected); policy_checks++;
}

/* Exercise inherited cached contexts, the three transparent containers, and
 * DefElem's semantic-owner dependency as well as direct atom classification. */
static void check_policy(ProtobufCMessage *m, const ProtobufCMessageDescriptor *d,
    const ProtobufCFieldDescriptor *f, const sqlparser_name_context_t *context)
{
    PgQuery__String atom = PG_QUERY__STRING__INIT;
    PgQuery__Node node = PG_QUERY__NODE__INIT;
    PgQuery__List list = PG_QUERY__LIST__INIT;
    PgQuery__AIndirection grouping = PG_QUERY__A__INDIRECTION__INIT;
    PgQuery__DefElem def = PG_QUERY__DEF_ELEM__INIT;
    sqlparser_name_context_t a, b;
    const ProtobufCFieldDescriptor *sf = protobuf_c_message_descriptor_get_field_by_name(&pg_query__string__descriptor, "sval");
    const ProtobufCFieldDescriptor *df = protobuf_c_message_descriptor_get_field_by_name(&pg_query__def_elem__descriptor, "defname");
    const ProtobufCFieldDescriptor *nf = protobuf_c_message_descriptor_get_field_by_name(&pg_query__def_elem__descriptor, "defnamespace");
    atom.sval = (char *)"alpha";
    equal_atom(m, d, f, context, context);
    a = sqlparser_next_name_context(m, d, f, context);
    b = golden_next_name_context(m, d, f, context);
    equal_context(&a, &b);
    equal_atom(&atom.base, atom.base.descriptor, sf, &a, &b);
    equal_atom(&def.base, def.base.descriptor, df, &a, &b);
    equal_atom(&def.base, def.base.descriptor, nf, &a, &b);
    a = sqlparser_next_name_context(&node.base, node.base.descriptor, &node.base.descriptor->fields[0], &a);
    b = golden_next_name_context(&node.base, node.base.descriptor, &node.base.descriptor->fields[0], &b);
    equal_context(&a, &b);
    a = sqlparser_next_name_context(&grouping.base, grouping.base.descriptor, &grouping.base.descriptor->fields[0], &a);
    b = golden_next_name_context(&grouping.base, grouping.base.descriptor, &grouping.base.descriptor->fields[0], &b);
    equal_context(&a, &b);
    a = sqlparser_next_name_context(&list.base, list.base.descriptor, &list.base.descriptor->fields[0], &a);
    b = golden_next_name_context(&list.base, list.base.descriptor, &list.base.descriptor->fields[0], &b);
    equal_context(&a, &b);
    a = sqlparser_next_name_context(&atom.base, atom.base.descriptor, sf, &a);
    b = golden_next_name_context(&atom.base, atom.base.descriptor, sf, &b);
    equal_context(&a, &b);
    equal_atom(&atom.base, atom.base.descriptor, sf, &a, &b);
}

static void all_schema_policy(void)
{
    size_t i;
    PgQuery__RangeVar owner = PG_QUERY__RANGE_VAR__INIT;
    sqlparser_name_context_t contexts[] = {
        {0}, TEST_CONTEXT("Alias", "aliasname", NULL, NULL, 0),
        TEST_CONTEXT("RangeVar", "relname", &owner.base, &owner.base, 0),
        TEST_CONTEXT("RangeVar", "relname", &owner.base, &owner.base, 1),
        TEST_CONTEXT(NULL, "relname", &owner.base, &owner.base, 0),
        TEST_CONTEXT("RangeVar", NULL, &owner.base, &owner.base, 0),
        TEST_CONTEXT("UnknownOwner", "unknown_field", &owner.base, &owner.base, 0)
    };
    unsigned char arbitrary[32] = {0};
    void *unknown[] = {NULL, (void *)(uintptr_t)1U, (void *)UINTPTR_MAX,
        arbitrary, arbitrary + 1, arbitrary + sizeof(arbitrary)};
    CHECK(COUNT(test_descriptors) == 277U);
    if (sizeof(void *) == 8U) CHECK(sizeof(sqlparser_name_context_t) == 40U);
    for (i = 0; i < COUNT(test_descriptors); i++) {
        const ProtobufCMessageDescriptor *d = test_descriptors[i];
        ProtobufCMessage *m = calloc(1, d->sizeof_message);
        unsigned j;
        CHECK(m); d->message_init(m); CHECK(m->descriptor == d);
        case_name = d->short_name; descriptor_checks++;
        for (j = 0; j < d->n_fields; j++) {
            const ProtobufCFieldDescriptor *f = &d->fields[j];
            ProtobufCMessageDescriptor copy = *d;
            ProtobufCFieldDescriptor field = *f;
            size_t k;
            field_checks++;
            check_policy(m, d, f, NULL);
            for (k = 0; k < COUNT(contexts); k++) check_policy(m, d, f, &contexts[k]);
            /* Exact copied metadata must be rejected by identity, not adopted. */
            check_policy(m, &copy, f, NULL); fallback_checks++;
            m->descriptor = &copy; check_policy(m, &copy, f, NULL); fallback_checks++;
            copy.short_name = "Alias";
            field.name = "aliasname"; copy.fields = &field; copy.n_fields = 1U;
            check_policy(m, &copy, &field, NULL); fallback_checks++;
            /* Legitimate custom descriptor object with opaque reserved values.
             * Values are never dereferenced by this test or used as messages. */
            for (k = 0; k < COUNT(unknown); k++) {
                copy.reserved1 = unknown[k]; copy.reserved2 = unknown[(k + 1U) % COUNT(unknown)];
                copy.reserved3 = unknown[(k + 2U) % COUNT(unknown)];
                field.reserved_flags = UINT_MAX; field.reserved2 = unknown[k]; field.reserved3 = unknown[k];
                check_policy(m, &copy, &field, NULL); fallback_checks++;
                /* Keep the real tag while the slot is opaque: exercise the
                 * range and alignment tests after tag acceptance as well. */
                copy.reserved2 = d->reserved2;
                check_policy(m, &copy, &field, NULL); fallback_checks++;
            }
            if (d->reserved1 != NULL) {
                copy.reserved1 = (void *)((uintptr_t)d->reserved1 + 1U);
                copy.reserved2 = d->reserved2;
                check_policy(m, &copy, &field, NULL); fallback_checks++;
                copy.reserved1 = test_descriptors[(i + 1U) % COUNT(test_descriptors)]->reserved1;
                check_policy(m, &copy, &field, NULL); fallback_checks++;
            }
            copy = *d; copy.fields = &field; copy.n_fields = 1U;
            copy.reserved1 = copy.reserved2 = copy.reserved3 = NULL;
            field = *f; field.reserved_flags = 0U; field.reserved2 = field.reserved3 = NULL;
            check_policy(m, &copy, &field, NULL); fallback_checks++;
            copy.short_name = NULL; field.name = NULL;
            check_policy(m, &copy, &field, NULL); fallback_checks++;
            m->descriptor = d;
            field = *f; check_policy(m, d, &field, NULL); fallback_checks++;
            field.name = "__not_an_identifier__";
            check_policy(m, d, &field, NULL); fallback_checks++;
            field.name = "aliasname"; check_policy(m, d, &field, NULL); fallback_checks++;
            field = *f; field.reserved_flags = UINT_MAX;
            field.reserved2 = arbitrary + 1; field.reserved3 = (void *)UINTPTR_MAX;
            check_policy(m, d, &field, NULL); fallback_checks++;
            /* Actual canonical field from a different descriptor, plus another
             * field in this descriptor: packed ordinal alone is insufficient. */
            {
                size_t next = (i + 1U) % COUNT(test_descriptors);
                const ProtobufCMessageDescriptor *other;
                while (!test_descriptors[next]->n_fields) next = (next + 1U) % COUNT(test_descriptors);
                other = test_descriptors[next];
                check_policy(m, d, &other->fields[j % other->n_fields], NULL); fallback_checks++;
                check_policy(m, d, &d->fields[(j + 1U) % d->n_fields], NULL);
            }
        }
        check_policy(m, d, NULL, NULL);
        free(m);
    }
    CHECK(descriptor_checks == 277U && field_checks == 1693U);
    {
        ProtobufCMessage empty = {0};
        case_name = "empty policy entry points";
        equal_atom(NULL, NULL, NULL, NULL, NULL);
        check_policy(&empty, NULL, NULL, NULL);
        check_policy(&empty, NULL, NULL, &contexts[1]);
    }
}

static void check_dynamic_owner(ProtobufCMessage *owner)
{
    const ProtobufCMessageDescriptor *d = owner->descriptor;
    unsigned j;
    for (j = 0; j < d->n_fields; j++) {
        check_policy(owner, d, &d->fields[j], NULL); dynamic_checks++;
    }
}

static void dynamic_policy(void)
{
    PgQuery__VariableSetStmt variable = PG_QUERY__VARIABLE_SET_STMT__INIT;
    PgQuery__FuncCall func = PG_QUERY__FUNC_CALL__INIT;
    PgQuery__Constraint con = PG_QUERY__CONSTRAINT__INIT;
    PgQuery__AlterTableCmd alter = PG_QUERY__ALTER_TABLE_CMD__INIT;
    struct dynamic { ProtobufCMessage *message; const char *field; } rows[] = {
        {&variable.base, "kind"}, {&func.base, "funcformat"},
        {&con.base, "contype"}, {&alter.base, "subtype"}
    };
    size_t i;
    for (i = 0; i < COUNT(rows); i++) {
        ProtobufCMessage *m = rows[i].message;
        const ProtobufCFieldDescriptor *f = protobuf_c_message_descriptor_get_field_by_name(m->descriptor, rows[i].field);
        const ProtobufCEnumDescriptor *e;
        unsigned j;
        CHECK(f && f->type == PROTOBUF_C_TYPE_ENUM); e = f->descriptor;
        case_name = m->descriptor->short_name;
        for (j = 0; j < e->n_values; j++) {
            int value = e->values[j].value;
            memcpy((char *)m + f->offset, &value, sizeof(value));
            check_dynamic_owner(m);
        }
    }
    /* Manual contexts and wrong/missing dynamic owners must keep old policy. */
    {
        PgQuery__String atom = PG_QUERY__STRING__INIT;
        PgQuery__DefElem def = PG_QUERY__DEF_ELEM__INIT;
        PgQuery__RangeVar wrong = PG_QUERY__RANGE_VAR__INIT;
        const char *owners[] = {"VariableSetStmt", "FuncCall", "Constraint", "AlterTableCmd"};
        const char *fields[] = {"name", "funcname", "keys", "options", "def", NULL};
        ProtobufCMessage *messages[] = {NULL, &variable.base, &func.base, &con.base, &alter.base, &wrong.base};
        size_t a, b, c;
        case_name = "manual dynamic contexts";
        for (a = 0; a < COUNT(owners); a++) for (b = 0; b < COUNT(fields); b++) for (c = 0; c < COUNT(messages); c++) {
            sqlparser_name_context_t context = TEST_CONTEXT(owners[a], fields[b], messages[c], &wrong.base, 0);
            unsigned f;
            check_policy(&atom.base, atom.base.descriptor, &atom.base.descriptor->fields[0], &context);
            for (f = 0; f < def.base.descriptor->n_fields; f++) check_policy(&def.base, def.base.descriptor, &def.base.descriptor->fields[f], &context);
            context.identifier_forbidden = 1;
            check_policy(&atom.base, atom.base.descriptor, &atom.base.descriptor->fields[0], &context);
            dynamic_checks++;
        }
    }
}

static void pattern_policy(void)
{
    PgQuery__AExpr expr = PG_QUERY__A__EXPR__INIT;
    PgQuery__FuncCall func = PG_QUERY__FUNC_CALL__INIT;
    PgQuery__String schema = PG_QUERY__STRING__INIT, name = PG_QUERY__STRING__INIT;
    PgQuery__Node nodes[3] = {PG_QUERY__NODE__INIT, PG_QUERY__NODE__INIT, PG_QUERY__NODE__INIT};
    PgQuery__Node *names[] = {&nodes[0], &nodes[1], &nodes[2]};
    const ProtobufCFieldDescriptor *ef = protobuf_c_message_descriptor_get_field_by_name(expr.base.descriptor, "rexpr");
    const ProtobufCFieldDescriptor *ff = protobuf_c_message_descriptor_get_field_by_name(func.base.descriptor, "funcname");
    const ProtobufCFieldDescriptor *kind = protobuf_c_message_descriptor_get_field_by_name(expr.base.descriptor, "kind");
    const ProtobufCFieldDescriptor *format = protobuf_c_message_descriptor_get_field_by_name(func.base.descriptor, "funcformat");
    const ProtobufCEnumDescriptor *kinds = kind->descriptor, *formats = format->descriptor;
    const char *schemas[] = {"pg_catalog", "PG_CATALOG", "public", ""};
    const char *functions[] = {"like_escape", "similar_to_escape", "LIKE_ESCAPE", "similar_escape", ""};
    size_t k, s, n, a, loc, fmt;
    sqlparser_name_context_t parent;
    case_name = "pattern wrapper cartesian";
    nodes[0].node_case = PG_QUERY__NODE__NODE_STRING; nodes[0].string = &schema;
    nodes[1].node_case = PG_QUERY__NODE__NODE_STRING; nodes[1].string = &name;
    func.n_funcname = 2U; func.funcname = names; expr.location = 131;
    parent = sqlparser_next_name_context(&expr.base, expr.base.descriptor, ef, NULL);
    for (k = 0; k < kinds->n_values; k++) for (s = 0; s < COUNT(schemas); s++) for (n = 0; n < COUNT(functions); n++)
        for (a = 0; a <= 3U; a++) for (loc = 0; loc < 2U; loc++) for (fmt = 0; fmt < formats->n_values; fmt++) {
            expr.kind = (PgQuery__AExprKind)kinds->values[k].value;
            func.funcformat = (PgQuery__CoercionForm)formats->values[fmt].value;
            schema.sval = (char *)schemas[s]; name.sval = (char *)functions[n];
            func.n_args = a; func.location = expr.location + (int)loc;
            check_policy(&func.base, func.base.descriptor, ff, &parent); pattern_checks++;
        }
    schema.sval = (char *)"pg_catalog"; name.sval = (char *)"like_escape";
    expr.kind = PG_QUERY__A__EXPR__KIND__AEXPR_LIKE;
    func.funcformat = PG_QUERY__COERCION_FORM__COERCE_EXPLICIT_CALL;
    func.n_args = 2U; func.location = expr.location;
    for (n = 0; n <= 3; n++) {
        func.n_funcname = n; check_policy(&func.base, func.base.descriptor, ff, &parent); pattern_checks++;
    }
    func.n_funcname = 2; names[0] = NULL;
    check_policy(&func.base, func.base.descriptor, ff, &parent); pattern_checks++;
    names[0] = &nodes[0]; names[1] = NULL;
    check_policy(&func.base, func.base.descriptor, ff, &parent); pattern_checks++;
    names[1] = &nodes[1]; func.funcname = NULL;
    check_policy(&func.base, func.base.descriptor, ff, &parent); pattern_checks++;
    func.funcname = names;
    parent.field_name = "lexpr";
    check_policy(&func.base, func.base.descriptor, ff, &parent); pattern_checks++;
    parent.field_name = "rexpr"; parent.owner_type = "UnknownOwner";
    check_policy(&func.base, func.base.descriptor, ff, &parent); pattern_checks++;
    parent.owner_type = "AExpr"; parent.field_owner = NULL;
    check_policy(&func.base, func.base.descriptor, ff, &parent); pattern_checks++;
}

int main(int argc, char **argv)
{
    const char *directory = "tests/cases";
    int i, no_corpora = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump")) dump = 1;
        else if (!strcmp(argv[i], "--targeted")) no_corpora = 1;
        else if (!strcmp(argv[i], "--corpora") && i + 1 < argc) directory = argv[++i];
        else { fprintf(stderr, "usage: %s [--dump] [--targeted] [--corpora path]\n", argv[0]); return 2; }
    }
    all_schema_policy(); dynamic_policy(); pattern_policy();
    schema_cases(); dedicated_cases(); positive_lifetimes(); exact_source_fidelity(); zero_and_forbidden(); argument_errors();
    if (!no_corpora) corpora(directory);
    CHECK(alternatives == 268U && name_checks > 0 && owner_checks > 0 && field_owner_checks > 0);
#ifdef SQLPARSER_NAME_READ_OMIT
    CHECK(omitted_checks == name_checks);
#endif
    fprintf(stderr, "POLICY descriptors=%zu fields=%zu comparisons=%zu contexts=%zu fallbacks=%zu dynamic=%zu patterns=%zu context_bytes=%zu\n", descriptor_checks, field_checks, policy_checks, context_checks, fallback_checks, dynamic_checks, pattern_checks, sizeof(sqlparser_name_context_t));
    fprintf(stderr, "SUMMARY alternatives=%zu fixtures=%zu default_name_checks=%zu omitted_name_checks=%zu owner_checks=%zu field_owner_checks=%zu corpus_inputs=%zu dialect_overrides=%zu public_statements=%zu public_names=%zu mutations=%zu source_patches=%zu lifetime_checks=%zu statuses=%zu recorded_non_success=%zu\n",
        alternatives, fixtures, name_checks, omitted_checks, owner_checks, field_owner_checks,
        corpus_inputs, override_inputs, public_statements, public_names, mutations, source_patches,
        lifetime_checks, statuses, failures);
    return 0;
}
