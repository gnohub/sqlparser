#ifndef SQLPARSER_MODEL_INTERNAL_H
#define SQLPARSER_MODEL_INTERNAL_H

#include <jansson.h>

#include "sqlparser_internal.h"

int sqlparser_model_strings_equal_nullable(const char *left, const char *right);
int sqlparser_model_literal_view_equals_value(
	const sqlparser_literal_view_t *view,
	const sqlparser_literal_value_t *value);
json_t *sqlparser_model_literal_view_to_json(const sqlparser_literal_view_t *view);
sqlparser_status_t sqlparser_model_literal_value_from_json(
	json_t *literal_json,
	sqlparser_literal_value_t *out_value,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_model_json_object_set_selector(
	json_t *object,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error);

#endif
