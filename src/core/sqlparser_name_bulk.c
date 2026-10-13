#include <stdint.h>
#include <string.h>

#include "sqlparser_ast_internal.h"
#include "protobuf/sqlparser_name_schema.h"

/* Read-only, synchronous caller storage. No retained state and no callback. */
typedef struct {
	sqlparser_name_view_t *views;
	size_t capacity;
	size_t seen;
	sqlparser_error_t *error;
} sqlparser_bulk_names_t;

/* Same immutable-schema guard and textual fallback as the ordinal walker.
 * Keeping this tiny adapter here leaves the old object/code paths untouched. */
static int sqlparser_bulk_name_container(const ProtobufCMessageDescriptor *d)
{
	int schema_class;
	if (d == NULL || d->short_name == NULL) return 0;
	schema_class = sqlparser_name_schema_class(d);
	if (schema_class != 0) return schema_class == 2;
	return strcmp(d->short_name, "Node") == 0 ||
	       strcmp(d->short_name, "List") == 0 ||
	       strcmp(d->short_name, "String") == 0;
}

static sqlparser_status_t sqlparser_bulk_name_atom(
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field,
	char **slot,
	sqlparser_bulk_names_t *bulk)
{
	const char *owner_type = NULL;
	const char *field_name = NULL;
	if (slot == NULL || *slot == NULL || (*slot)[0] == '\0')
		return SQLPARSER_STATUS_OK;
	if (sqlparser_bulk_name_container(descriptor) && context != NULL) {
		owner_type = context->owner_type;
		field_name = context->field_name;
	}
	if (owner_type == NULL && descriptor != NULL)
		owner_type = descriptor->short_name;
	if (field_name == NULL && field != NULL) field_name = field->name;
	if (owner_type == NULL || field_name == NULL ||
	    !sqlparser_name_atom_is_identifier(message, context, descriptor, field))
		return SQLPARSER_STATUS_OK;
	if (bulk->seen == SIZE_MAX) {
		sqlparser_error_set_message(bulk->error, SQLPARSER_STATUS_RESOURCE_LIMIT,
			"name count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	if (bulk->seen < bulk->capacity) {
		sqlparser_name_view_t *view = &bulk->views[bulk->seen];
		view->owner_type = owner_type;
		view->field_name = field_name;
		view->value = *slot;
	}
	bulk->seen++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_bulk_name_walk(
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	sqlparser_bulk_names_t *bulk)
{
	const ProtobufCMessageDescriptor *descriptor;
	const ProtobufCFieldDescriptor *fields;
	unsigned field_count;
	unsigned index;
	uint8_t *base;

visit_message:
	if (message == NULL) return SQLPARSER_STATUS_OK;
	descriptor = message->descriptor;
	if (descriptor == NULL) return SQLPARSER_STATUS_OK;
	base = (uint8_t *)message;
	if (descriptor == &pg_query__node__descriptor) {
		fields = protobuf_c_message_descriptor_get_field(descriptor,
			(uint32_t)((const PgQuery__Node *)message)->node_case);
		field_count = fields != NULL ? 1U : 0U;
		/* All contexts in this read-only traversal omit location ownership.
		 * Preserve the ordinary root path when no inherited context exists. */
		if (context != NULL && fields != NULL &&
		    fields->type == PROTOBUF_C_TYPE_MESSAGE &&
		    fields->label != PROTOBUF_C_LABEL_REPEATED &&
		    ((fields->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) == 0U ||
		     *(const int *)(base + fields->quantifier_offset) == (int)fields->id)) {
			message = *(ProtobufCMessage **)(base + fields->offset);
			goto visit_message;
		}
	} else {
		fields = descriptor->fields;
		field_count = descriptor->n_fields;
	}
	for (index = 0U; index < field_count; index++) {
		const ProtobufCFieldDescriptor *field = &fields[index];
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		    *(const int *)(base + field->quantifier_offset) != (int)field->id)
			continue;
		if (field->type == PROTOBUF_C_TYPE_STRING) {
			sqlparser_status_t status;
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t count = *(const size_t *)(base + field->quantifier_offset);
				char **items = *(char ***)(base + field->offset);
				size_t item;
				if (count != 0U && items == NULL) goto invalid_array;
				for (item = 0U; item < count; item++) {
					status = sqlparser_bulk_name_atom(message, context,
						descriptor, field, &items[item], bulk);
					if (status != SQLPARSER_STATUS_OK) return status;
				}
			} else {
				status = sqlparser_bulk_name_atom(message, context,
					descriptor, field, (char **)(base + field->offset), bulk);
				if (status != SQLPARSER_STATUS_OK) return status;
			}
		} else if (field->type == PROTOBUF_C_TYPE_MESSAGE) {
			sqlparser_name_context_t next_context =
				sqlparser_next_name_context(message, descriptor, field, context);
			sqlparser_status_t status;
			next_context.location_owner = NULL;
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t count = *(const size_t *)(base + field->quantifier_offset);
				ProtobufCMessage **items = *(ProtobufCMessage ***)(base + field->offset);
				size_t item;
				if (count != 0U && items == NULL) goto invalid_array;
				for (item = 0U; item < count; item++) {
					status = sqlparser_bulk_name_walk(items[item], &next_context, bulk);
					if (status != SQLPARSER_STATUS_OK) return status;
				}
			} else {
				ProtobufCMessage *child = *(ProtobufCMessage **)(base + field->offset);
				status = sqlparser_bulk_name_walk(child, &next_context, bulk);
				if (status != SQLPARSER_STATUS_OK) return status;
			}
		}
	}
	return SQLPARSER_STATUS_OK;
invalid_array:
	sqlparser_error_set_message(bulk->error, SQLPARSER_STATUS_INTERNAL_ERROR,
		"name traversal encountered a missing repeated field array");
	return SQLPARSER_STATUS_INTERNAL_ERROR;
}

sqlparser_status_t sqlparser_statement_names(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_name_view_t *caller_views,
	size_t capacity,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_bulk_names_t bulk;
	sqlparser_status_t status;
	if (out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_count = 0U;
	sqlparser_error_clear(out_error);
	if (capacity > SIZE_MAX / sizeof(*caller_views) ||
	    (capacity != 0U && caller_views == NULL)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name buffer capacity is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_get_statement_node((sqlparser_handle_t *)handle,
		statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) return status;
	bulk.views = caller_views;
	bulk.capacity = capacity;
	bulk.seen = 0U;
	bulk.error = out_error;
	status = sqlparser_bulk_name_walk((ProtobufCMessage *)statement, NULL, &bulk);
	if (status != SQLPARSER_STATUS_OK) return status;
	*out_count = bulk.seen;
	if (capacity != 0U && bulk.seen > capacity) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT,
			"name buffer is too small");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	return SQLPARSER_STATUS_OK;
}
