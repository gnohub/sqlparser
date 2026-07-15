#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_sqlserver_control.h"
#include "sqlparser_dialect_sqlserver_scan.h"

typedef enum {
	SQLPARSER_SQLSERVER_CONTROL_COUNT = 0,
	SQLPARSER_SQLSERVER_CONTROL_LAYOUT,
	SQLPARSER_SQLSERVER_CONTROL_FILL
} sqlparser_sqlserver_control_mode_t;

typedef struct {
	const char *sql;
	const sqlparser_sqlserver_token_t *tokens;
	size_t token_count;
	size_t pos;
	sqlparser_sqlserver_control_mode_t mode;
	sqlparser_control_counts_t counts;
	sqlparser_control_state_t *state;
	size_t node_cursor;
	size_t branch_cursor;
	size_t item_cursor;
	size_t unit_cursor;
	size_t branch_ref_cursor;
	size_t item_ref_cursor;
} sqlparser_sqlserver_control_parser_t;

static sqlparser_status_t sqlparser_sqlserver_control_parse_if(
	sqlparser_sqlserver_control_parser_t *parser,
	size_t depth,
	size_t *out_node_index,
	sqlparser_error_t *out_error);

static int sqlparser_sqlserver_control_size_add(size_t *value, size_t additional)
{
	if (value == NULL || *value > SIZE_MAX - additional) {
		return -1;
	}
	*value += additional;
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_control_internal_error(
	sqlparser_error_t *out_error,
	const char *message)
{
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, message);
	return SQLPARSER_STATUS_INTERNAL_ERROR;
}

static int sqlparser_sqlserver_control_token_word(
	const sqlparser_sqlserver_control_parser_t *parser,
	size_t index,
	const char *word)
{
	return index < parser->token_count &&
		sqlparser_sqlserver_token_word_equal(parser->sql, &parser->tokens[index], word);
}

static int sqlparser_sqlserver_control_token_symbol(
	const sqlparser_sqlserver_control_parser_t *parser,
	size_t index,
	char symbol)
{
	return index < parser->token_count &&
		parser->tokens[index].kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		parser->tokens[index].symbol == symbol;
}

static int sqlparser_sqlserver_control_at_level(
	const sqlparser_sqlserver_control_parser_t *parser,
	size_t index,
	size_t block_depth)
{
	const sqlparser_sqlserver_token_t *token;

	if (index >= parser->token_count) {
		return 0;
	}
	token = &parser->tokens[index];
	return token->block_depth == block_depth &&
		token->paren_depth == 0U && token->case_depth == 0U;
}

static int sqlparser_sqlserver_control_block_begin(
	const sqlparser_sqlserver_control_parser_t *parser,
	size_t index)
{
	return sqlparser_sqlserver_control_token_word(parser, index, "begin") &&
		index + 1U < parser->token_count &&
		parser->tokens[index + 1U].block_depth == parser->tokens[index].block_depth + 1U;
}

static int sqlparser_sqlserver_control_block_end(
	const sqlparser_sqlserver_control_parser_t *parser,
	size_t index)
{
	const sqlparser_sqlserver_token_t *token;

	if (!sqlparser_sqlserver_control_token_word(parser, index, "end")) {
		return 0;
	}
	token = &parser->tokens[index];
	if (token->case_depth != 0U || token->block_depth == 0U) {
		return 0;
	}
	return index + 1U == parser->token_count ||
		parser->tokens[index + 1U].block_depth < token->block_depth;
}

static int sqlparser_sqlserver_control_statement_start(
	const sqlparser_sqlserver_control_parser_t *parser,
	size_t index)
{
	static const char *const words[] = {
		"alter", "begin", "break", "checkpoint", "commit", "continue",
		"create", "dbcc", "deallocate", "declare", "delete", "deny",
		"drop", "exec", "execute", "grant", "if", "insert", "kill",
		"merge", "open", "print", "raiserror", "reconfigure", "return",
		"revert", "revoke", "rollback", "save", "select", "set",
		"shutdown", "throw", "truncate", "update", "use", "waitfor", "while",
		"with"
	};
	size_t word_index;

	for (word_index = 0U; word_index < sizeof(words) / sizeof(words[0]); word_index++) {
		if (sqlparser_sqlserver_control_token_word(parser, index, words[word_index])) {
			if (sqlparser_sqlserver_control_token_word(parser, index, "update") &&
			    sqlparser_sqlserver_control_token_symbol(parser, index + 1U, '(')) {
				return 0;
			}
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_control_drop_type_prefix(
	const char *sql,
	const sqlparser_sqlserver_token_t *tokens,
	size_t count)
{
	if (count == 1U) {
		return tokens[0].kind == SQLPARSER_SQLSERVER_TOKEN_WORD;
	}
	if (count == 2U) {
		return (sqlparser_sqlserver_token_word_equal(sql, &tokens[0], "application") &&
		        sqlparser_sqlserver_token_word_equal(sql, &tokens[1], "role")) ||
		       (sqlparser_sqlserver_token_word_equal(sql, &tokens[0], "partition") &&
		        (sqlparser_sqlserver_token_word_equal(sql, &tokens[1], "function") ||
		         sqlparser_sqlserver_token_word_equal(sql, &tokens[1], "scheme")));
	}
	return count == 3U &&
		sqlparser_sqlserver_token_word_equal(sql, &tokens[0], "xml") &&
		sqlparser_sqlserver_token_word_equal(sql, &tokens[1], "schema") &&
		sqlparser_sqlserver_token_word_equal(sql, &tokens[2], "collection");
}

static int sqlparser_sqlserver_control_drop_if_exists(
	const sqlparser_sqlserver_control_parser_t *parser,
	size_t statement_start,
	size_t index)
{
	size_t type_count;

	if (statement_start >= index ||
	    !sqlparser_sqlserver_control_token_word(parser, statement_start, "drop") ||
	    !sqlparser_sqlserver_control_token_word(parser, index, "if") ||
	    !sqlparser_sqlserver_control_token_word(parser, index + 1U, "exists")) {
		return 0;
	}
	type_count = index - statement_start - 1U;
	return sqlparser_sqlserver_control_drop_type_prefix(
		parser->sql,
		&parser->tokens[statement_start + 1U],
		type_count);
}

static int sqlparser_sqlserver_control_has_line_break(
	const char *sql,
	size_t start,
	size_t end)
{
	for (; start < end; start++) {
		if (sql[start] == '\n' || sql[start] == '\r') {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_control_leaf_boundary(
	const sqlparser_sqlserver_control_parser_t *parser,
	size_t statement_start,
	size_t index)
{
	if (index <= statement_start) {
		return 0;
	}
	if (sqlparser_sqlserver_control_token_word(parser, index, "if")) {
		return !sqlparser_sqlserver_control_drop_if_exists(
			parser, statement_start, index);
	}
	if (!sqlparser_sqlserver_control_statement_start(parser, index) ||
	    !sqlparser_sqlserver_control_has_line_break(
		    parser->sql,
		    parser->tokens[index - 1U].end,
		    parser->tokens[index].start)) {
		return 0;
	}
	if (sqlparser_sqlserver_control_token_word(parser, index, "set") &&
	    (sqlparser_sqlserver_control_token_word(parser, statement_start, "update") ||
	     sqlparser_sqlserver_control_token_word(parser, statement_start, "merge"))) {
		return 0;
	}
	if (sqlparser_sqlserver_control_token_word(parser, index, "with")) {
		if (sqlparser_sqlserver_control_token_symbol(parser, index + 1U, '(') ||
		    sqlparser_sqlserver_control_token_word(parser, statement_start, "create")) {
			return 0;
		}
	}
	if (sqlparser_sqlserver_control_token_word(parser, index, "select")) {
		if (sqlparser_sqlserver_control_token_word(parser, statement_start, "insert") ||
		    sqlparser_sqlserver_control_token_word(parser, statement_start, "create") ||
		    sqlparser_sqlserver_control_token_word(parser, statement_start, "alter") ||
		    sqlparser_sqlserver_control_token_word(parser, statement_start, "with") ||
		    sqlparser_sqlserver_control_token_word(parser, index - 1U, "union") ||
		    sqlparser_sqlserver_control_token_word(parser, index - 1U, "all") ||
		    sqlparser_sqlserver_control_token_word(parser, index - 1U, "intersect") ||
		    sqlparser_sqlserver_control_token_word(parser, index - 1U, "except")) {
			return 0;
		}
	}
	if (sqlparser_sqlserver_control_token_word(parser, statement_start, "merge") &&
	    (sqlparser_sqlserver_control_token_word(parser, index, "insert") ||
	     sqlparser_sqlserver_control_token_word(parser, index, "update") ||
	     sqlparser_sqlserver_control_token_word(parser, index, "delete"))) {
		return 0;
	}
	return 1;
}

static sqlparser_status_t sqlparser_sqlserver_control_reserve_node(
	sqlparser_sqlserver_control_parser_t *parser,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_COUNT) {
		*out_index = parser->counts.node_count;
		if (sqlparser_sqlserver_control_size_add(&parser->counts.node_count, 1U) != 0) {
			return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control node count overflowed");
		}
		return SQLPARSER_STATUS_OK;
	}
	if (parser->node_cursor >= parser->state->node_count) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control node layout is inconsistent");
	}
	*out_index = parser->node_cursor++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_control_reserve_branch(
	sqlparser_sqlserver_control_parser_t *parser,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_COUNT) {
		*out_index = parser->counts.branch_count;
		if (sqlparser_sqlserver_control_size_add(&parser->counts.branch_count, 1U) != 0) {
			return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control branch count overflowed");
		}
		return SQLPARSER_STATUS_OK;
	}
	if (parser->branch_cursor >= parser->state->branch_count) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control branch layout is inconsistent");
	}
	*out_index = parser->branch_cursor++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_control_reserve_item(
	sqlparser_sqlserver_control_parser_t *parser,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_COUNT) {
		*out_index = parser->counts.item_count;
		if (sqlparser_sqlserver_control_size_add(&parser->counts.item_count, 1U) != 0) {
			return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control item count overflowed");
		}
		return SQLPARSER_STATUS_OK;
	}
	if (parser->item_cursor >= parser->state->item_count) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control item layout is inconsistent");
	}
	*out_index = parser->item_cursor++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_control_reserve_unit(
	sqlparser_sqlserver_control_parser_t *parser,
	sqlparser_control_unit_kind_t kind,
	size_t start,
	size_t end,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	if (start >= end) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control unit span is empty");
	}
	if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_COUNT) {
		*out_index = parser->counts.unit_count;
		if (sqlparser_sqlserver_control_size_add(&parser->counts.unit_count, 1U) != 0) {
			return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control unit count overflowed");
		}
		return SQLPARSER_STATUS_OK;
	}
	if (parser->unit_cursor >= parser->state->unit_count) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control unit layout is inconsistent");
	}
	*out_index = parser->unit_cursor++;
	if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL) {
		sqlparser_control_unit_t *unit;

		unit = &parser->state->units[*out_index];
		unit->kind = kind;
		unit->ast_statement_index = *out_index;
		unit->source_offset = start;
		unit->source_length = end - start;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_control_parse_leaf(
	sqlparser_sqlserver_control_parser_t *parser,
	size_t block_depth,
	size_t *out_unit_index,
	sqlparser_error_t *out_error)
{
	size_t end_index;
	size_t effective_start_index;
	size_t start_index;

	start_index = parser->pos;
	effective_start_index = start_index;
	end_index = start_index;
	while (end_index < parser->token_count) {
		if (sqlparser_sqlserver_control_at_level(parser, end_index, block_depth)) {
			if (effective_start_index == start_index && end_index > start_index &&
			    sqlparser_sqlserver_control_token_word(parser, start_index, "with") &&
			    (sqlparser_sqlserver_control_token_word(parser, end_index, "select") ||
			     sqlparser_sqlserver_control_token_word(parser, end_index, "insert") ||
			     sqlparser_sqlserver_control_token_word(parser, end_index, "update") ||
			     sqlparser_sqlserver_control_token_word(parser, end_index, "delete") ||
			     sqlparser_sqlserver_control_token_word(parser, end_index, "merge"))) {
				effective_start_index = end_index;
			}
			if (sqlparser_sqlserver_line_is_go(
				    parser->sql,
				    parser->tokens[end_index].start,
				    NULL)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"GO batch separator is not supported inside SQL Server control flow");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			if (sqlparser_sqlserver_control_token_symbol(parser, end_index, ';') ||
			    sqlparser_sqlserver_control_token_word(parser, end_index, "else") ||
			    sqlparser_sqlserver_control_block_end(parser, end_index) ||
			    sqlparser_sqlserver_control_leaf_boundary(
				    parser, effective_start_index, end_index)) {
				break;
			}
		}
		end_index++;
	}
	if (end_index == start_index) {
		return sqlparser_sqlserver_error_at(
			parser->sql,
			start_index < parser->token_count ? parser->tokens[start_index].start : strlen(parser->sql),
			"SQL Server IF branch requires a statement",
			out_error);
	}
	parser->pos = end_index;
	return sqlparser_sqlserver_control_reserve_unit(
		parser,
		SQLPARSER_CONTROL_UNIT_STATEMENT,
		parser->tokens[start_index].start,
		parser->tokens[end_index - 1U].end,
		out_unit_index,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_control_parse_item(
	sqlparser_sqlserver_control_parser_t *parser,
	size_t block_depth,
	size_t node_depth,
	size_t *out_item_index,
	sqlparser_error_t *out_error)
{
	size_t item_index;
	size_t target_index;
	sqlparser_status_t status;

	status = sqlparser_sqlserver_control_reserve_item(parser, &item_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (sqlparser_sqlserver_control_at_level(parser, parser->pos, block_depth) &&
	    sqlparser_sqlserver_control_token_word(parser, parser->pos, "if")) {
		status = sqlparser_sqlserver_control_parse_if(parser, node_depth, &target_index, out_error);
		if (status == SQLPARSER_STATUS_OK && parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL) {
			parser->state->items[item_index].kind = SQLPARSER_CONTROL_ITEM_NODE;
			parser->state->items[item_index].index = target_index;
		}
	} else {
		status = sqlparser_sqlserver_control_parse_leaf(
			parser, block_depth, &target_index, out_error);
		if (status == SQLPARSER_STATUS_OK && parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL) {
			parser->state->items[item_index].kind = SQLPARSER_CONTROL_ITEM_STATEMENT;
			parser->state->items[item_index].index = target_index;
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		*out_item_index = item_index;
	}
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_control_parse_sequence(
	sqlparser_sqlserver_control_parser_t *parser,
	size_t block_depth,
	int stop_at_end,
	size_t ref_offset,
	size_t node_depth,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	size_t count;
	sqlparser_status_t status;

	count = 0U;
	while (parser->pos < parser->token_count) {
		size_t item_index;

		while (parser->pos < parser->token_count &&
		       sqlparser_sqlserver_control_at_level(parser, parser->pos, block_depth) &&
		       sqlparser_sqlserver_control_token_symbol(parser, parser->pos, ';')) {
			parser->pos++;
		}
		if (parser->pos >= parser->token_count ||
		    (stop_at_end && sqlparser_sqlserver_control_block_end(parser, parser->pos))) {
			break;
		}
		if (!sqlparser_sqlserver_control_at_level(parser, parser->pos, block_depth)) {
			return sqlparser_sqlserver_error_at(
				parser->sql,
				parser->tokens[parser->pos].start,
				"SQL Server control statement boundary is invalid",
				out_error);
		}
		if (sqlparser_sqlserver_control_token_word(parser, parser->pos, "else")) {
			return sqlparser_sqlserver_error_at(
				parser->sql,
				parser->tokens[parser->pos].start,
				"SQL Server ELSE does not have a matching IF",
				out_error);
		}
		status = sqlparser_sqlserver_control_parse_item(
			parser, block_depth, node_depth, &item_index, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL) {
			if (ref_offset > parser->state->index_count ||
			    count >= parser->state->index_count - ref_offset) {
				return sqlparser_sqlserver_control_internal_error(
					out_error,
					"SQL Server control item references overflowed");
			}
			parser->state->index_pool[ref_offset + count] = item_index;
		}
		count++;
	}
	*out_count = count;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_control_parse_branch_body(
	sqlparser_sqlserver_control_parser_t *parser,
	size_t branch_index,
	size_t block_depth,
	size_t node_depth,
	sqlparser_error_t *out_error)
{
	size_t count;
	size_t expected_count;
	size_t ref_offset;
	sqlparser_status_t status;

	if (parser->pos >= parser->token_count) {
		return sqlparser_sqlserver_error_at(
			parser->sql,
			strlen(parser->sql),
			"SQL Server IF branch requires a statement",
			out_error);
	}
	expected_count = 0U;
	ref_offset = 0U;
	if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL) {
		expected_count = parser->state->branches[branch_index].items.count;
		if (parser->item_ref_cursor > parser->state->index_count ||
		    expected_count > parser->state->index_count - parser->item_ref_cursor) {
			return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control item references overflowed");
		}
		ref_offset = parser->item_ref_cursor;
		parser->item_ref_cursor += expected_count;
		parser->state->branches[branch_index].items.offset = ref_offset;
	}

	if (sqlparser_sqlserver_control_at_level(parser, parser->pos, block_depth) &&
	    sqlparser_sqlserver_control_block_begin(parser, parser->pos)) {
		size_t begin_pos;

		begin_pos = parser->tokens[parser->pos].start;
		parser->pos++;
		status = sqlparser_sqlserver_control_parse_sequence(
			parser,
			block_depth + 1U,
			1,
			ref_offset,
			node_depth + 1U,
			&count,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (parser->pos >= parser->token_count ||
		    !sqlparser_sqlserver_control_block_end(parser, parser->pos)) {
			return sqlparser_sqlserver_error_at(
				parser->sql,
				begin_pos,
				"SQL Server IF branch has an unterminated BEGIN block",
				out_error);
		}
		if (count == 0U) {
			return sqlparser_sqlserver_error_at(
				parser->sql,
				begin_pos,
				"SQL Server BEGIN block requires at least one statement",
				out_error);
		}
		parser->pos++;
	} else {
		size_t item_index;

		status = sqlparser_sqlserver_control_parse_item(
			parser, block_depth, node_depth + 1U, &item_index, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		count = 1U;
		if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL) {
			if (ref_offset >= parser->state->index_count) {
				return sqlparser_sqlserver_control_internal_error(
					out_error,
					"SQL Server control item references overflowed");
			}
			parser->state->index_pool[ref_offset] = item_index;
		}
	}

	if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_LAYOUT) {
		parser->state->branches[branch_index].items.count = count;
	} else if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL && count != expected_count) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control branch layout changed between passes");
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_control_parse_if(
	sqlparser_sqlserver_control_parser_t *parser,
	size_t depth,
	size_t *out_node_index,
	sqlparser_error_t *out_error)
{
	size_t base_block_depth;
	size_t branch_index;
	size_t branch_ordinal;
	size_t branch_start;
	size_t condition_start;
	size_t condition_unit;
	size_t node_index;
	size_t lookahead;
	size_t expected_branches;
	sqlparser_status_t status;

	if (depth > SQLPARSER_CONTROL_MAX_DEPTH) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server IF nesting exceeds the configured control depth");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	base_block_depth = parser->tokens[parser->pos].block_depth;
	status = sqlparser_sqlserver_control_reserve_node(parser, &node_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	expected_branches = 0U;
	if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL) {
		expected_branches = parser->state->nodes[node_index].branches.count;
		if (parser->branch_ref_cursor > parser->state->index_count ||
		    expected_branches > parser->state->index_count - parser->branch_ref_cursor) {
			return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control branch references overflowed");
		}
		parser->state->nodes[node_index].kind = SQLPARSER_CONTROL_NODE_IF;
		parser->state->nodes[node_index].branches.offset = parser->branch_ref_cursor;
		parser->branch_ref_cursor += expected_branches;
	}

	parser->pos++;
	condition_start = parser->pos;
	if (condition_start >= parser->token_count) {
		return sqlparser_sqlserver_error_at(parser->sql, strlen(parser->sql), "SQL Server IF requires a condition", out_error);
	}
	branch_start = parser->token_count;
	for (lookahead = condition_start; lookahead < parser->token_count; lookahead++) {
		if (!sqlparser_sqlserver_control_at_level(parser, lookahead, base_block_depth)) {
			continue;
		}
		if (sqlparser_sqlserver_control_token_symbol(parser, lookahead, ';') ||
		    sqlparser_sqlserver_control_token_word(parser, lookahead, "else") ||
		    sqlparser_sqlserver_control_block_end(parser, lookahead)) {
			break;
		}
		if (!sqlparser_sqlserver_control_statement_start(parser, lookahead)) {
			continue;
		}
		if (lookahead == condition_start) {
			if (sqlparser_sqlserver_control_token_word(parser, lookahead, "select")) {
				return sqlparser_sqlserver_error_at(
					parser->sql,
					parser->tokens[lookahead].start,
					"SQL Server IF condition SELECT must be parenthesized",
					out_error);
			}
			return sqlparser_sqlserver_error_at(
				parser->sql,
				parser->tokens[lookahead].start,
				"SQL Server IF requires a condition",
				out_error);
		}
		if (sqlparser_sqlserver_control_token_word(parser, lookahead, "select") &&
		    (sqlparser_sqlserver_control_token_word(parser, lookahead - 1U, "exists") ||
		     (lookahead >= 2U &&
		      sqlparser_sqlserver_control_token_word(parser, lookahead - 2U, "not") &&
		      sqlparser_sqlserver_control_token_word(parser, lookahead - 1U, "exists")))) {
			return sqlparser_sqlserver_error_at(
				parser->sql,
				parser->tokens[lookahead].start,
				"SQL Server IF condition SELECT must be parenthesized",
				out_error);
		}
		branch_start = lookahead;
		break;
	}
	if (branch_start == parser->token_count) {
		return sqlparser_sqlserver_error_at(
			parser->sql,
			condition_start < parser->token_count ? parser->tokens[condition_start].start : strlen(parser->sql),
			"SQL Server IF requires a branch statement",
			out_error);
	}
	status = sqlparser_sqlserver_control_reserve_unit(
		parser,
		SQLPARSER_CONTROL_UNIT_CONDITION,
		parser->tokens[condition_start].start,
		parser->tokens[branch_start - 1U].end,
		&condition_unit,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	parser->pos = branch_start;
	branch_ordinal = 0U;
	status = sqlparser_sqlserver_control_reserve_branch(parser, &branch_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL) {
		parser->state->index_pool[parser->state->nodes[node_index].branches.offset] = branch_index;
		parser->state->branches[branch_index].condition_statement_index = condition_unit;
		parser->state->branches[branch_index].has_condition = 1;
	}
	status = sqlparser_sqlserver_control_parse_branch_body(
		parser, branch_index, base_block_depth, depth, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	branch_ordinal++;

	lookahead = parser->pos;
	while (lookahead < parser->token_count &&
	       sqlparser_sqlserver_control_at_level(parser, lookahead, base_block_depth) &&
	       sqlparser_sqlserver_control_token_symbol(parser, lookahead, ';')) {
		lookahead++;
	}
	if (lookahead < parser->token_count &&
	    sqlparser_sqlserver_control_at_level(parser, lookahead, base_block_depth) &&
	    sqlparser_sqlserver_control_token_word(parser, lookahead, "else")) {
		parser->pos = lookahead + 1U;
		if (parser->pos >= parser->token_count ||
		    sqlparser_sqlserver_control_block_end(parser, parser->pos) ||
		    sqlparser_sqlserver_control_token_word(parser, parser->pos, "else")) {
			return sqlparser_sqlserver_error_at(
				parser->sql,
				parser->pos < parser->token_count ? parser->tokens[parser->pos].start : strlen(parser->sql),
				"SQL Server ELSE requires a branch statement",
				out_error);
		}
		status = sqlparser_sqlserver_control_reserve_branch(parser, &branch_index, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL) {
			parser->state->index_pool[
				parser->state->nodes[node_index].branches.offset + branch_ordinal] = branch_index;
			parser->state->branches[branch_index].has_condition = 0;
		}
		status = sqlparser_sqlserver_control_parse_branch_body(
			parser, branch_index, base_block_depth, depth, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		branch_ordinal++;
	}

	if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_LAYOUT) {
		parser->state->nodes[node_index].branches.count = branch_ordinal;
	} else if (parser->mode == SQLPARSER_SQLSERVER_CONTROL_FILL &&
	           branch_ordinal != expected_branches) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control node layout changed between passes");
	}
	*out_node_index = node_index;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_sqlserver_control_parser_reset(
	sqlparser_sqlserver_control_parser_t *parser,
	sqlparser_sqlserver_control_mode_t mode,
	sqlparser_control_state_t *state)
{
	parser->pos = 0U;
	parser->mode = mode;
	parser->state = state;
	parser->node_cursor = 0U;
	parser->branch_cursor = 0U;
	parser->item_cursor = 0U;
	parser->unit_cursor = 0U;
	parser->branch_ref_cursor = state != NULL ? state->roots.count : 0U;
	parser->item_ref_cursor = state != NULL ? state->roots.count + state->branch_count : 0U;
}

static sqlparser_status_t sqlparser_sqlserver_control_run_pass(
	sqlparser_sqlserver_control_parser_t *parser,
	sqlparser_sqlserver_control_mode_t mode,
	sqlparser_control_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t root_count;
	sqlparser_status_t status;

	sqlparser_sqlserver_control_parser_reset(parser, mode, state);
	status = sqlparser_sqlserver_control_parse_sequence(
		parser,
		0U,
		0,
		0U,
		1U,
		&root_count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (parser->pos != parser->token_count) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control parser did not consume all tokens");
	}
	if (mode == SQLPARSER_SQLSERVER_CONTROL_COUNT) {
		parser->counts.root_count = root_count;
		if (sqlparser_sqlserver_control_size_add(
			    &parser->counts.index_count,
			    parser->counts.branch_count) != 0 ||
		    sqlparser_sqlserver_control_size_add(
			    &parser->counts.index_count,
			    parser->counts.item_count) != 0) {
			return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control index count overflowed");
		}
	} else if (root_count != state->roots.count ||
	           parser->node_cursor != state->node_count ||
	           parser->branch_cursor != state->branch_count ||
	           parser->item_cursor != state->item_count ||
	           parser->unit_cursor != state->unit_count) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control pass counts are inconsistent");
	}
	if (mode == SQLPARSER_SQLSERVER_CONTROL_FILL &&
	    (parser->branch_ref_cursor != state->roots.count + state->branch_count ||
	     parser->item_ref_cursor != state->index_count)) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server control reference layout is inconsistent");
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_control_scan(
	const char *sql,
	size_t sql_len,
	sqlparser_sqlserver_token_t **out_tokens,
	size_t *out_token_count,
	int *out_has_control,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	sqlparser_sqlserver_token_t *tokens;
	size_t count;
	size_t drop_type_count;
	size_t index;
	size_t previous_token_end;
	sqlparser_sqlserver_token_t drop_type_tokens[3];
	int has_control;
	int at_statement_start;
	int current_statement_is_drop;
	int has_previous_token;
	int pending_drop_if_control;
	sqlparser_status_t status;

	*out_tokens = NULL;
	*out_token_count = 0U;
	*out_has_control = 0;
	status = sqlparser_sqlserver_scanner_init(&scanner, sql, 0U, sql_len, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	count = 0U;
	drop_type_count = 0U;
	has_control = 0;
	at_statement_start = 1;
	current_statement_is_drop = 0;
	has_previous_token = 0;
	pending_drop_if_control = 0;
	previous_token_end = 0U;
	for (;;) {
		status = sqlparser_sqlserver_scanner_next(&scanner, &token, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			if (pending_drop_if_control) {
				has_control = 1;
			}
			break;
		}
		if (pending_drop_if_control) {
			if (!sqlparser_sqlserver_token_word_equal(sql, &token, "exists")) {
				has_control = 1;
			}
			pending_drop_if_control = 0;
		}
		if (count == SIZE_MAX) {
			return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server token count overflowed");
		}
		count++;
		if (token.block_depth == 0U && token.paren_depth == 0U && token.case_depth == 0U) {
			if (at_statement_start ||
			    (has_previous_token &&
			     sqlparser_sqlserver_control_has_line_break(
				     sql, previous_token_end, token.start))) {
				if (sqlparser_sqlserver_token_word_equal(sql, &token, "else")) {
					has_control = 1;
				} else if (sqlparser_sqlserver_token_word_equal(sql, &token, "if")) {
					if (current_statement_is_drop &&
					    sqlparser_sqlserver_control_drop_type_prefix(
						    sql, drop_type_tokens, drop_type_count)) {
						pending_drop_if_control = 1;
					} else {
						has_control = 1;
					}
				}
			}
			if (token.kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL && token.symbol == ';') {
				at_statement_start = 1;
				current_statement_is_drop = 0;
				drop_type_count = 0U;
			} else {
				if (at_statement_start) {
					current_statement_is_drop =
						sqlparser_sqlserver_token_word_equal(sql, &token, "drop");
					drop_type_count = 0U;
				} else if (current_statement_is_drop) {
					if (drop_type_count < sizeof(drop_type_tokens) /
					                      sizeof(drop_type_tokens[0])) {
						drop_type_tokens[drop_type_count] = token;
					}
					if (drop_type_count <= sizeof(drop_type_tokens) /
					                       sizeof(drop_type_tokens[0])) {
						drop_type_count++;
					}
				}
				at_statement_start = 0;
			}
		}
		has_previous_token = 1;
		previous_token_end = token.end;
	}
	if (status != SQLPARSER_STATUS_OK || !has_control) {
		*out_has_control = has_control;
		return status;
	}
	if (count > SIZE_MAX / sizeof(*tokens)) {
		return sqlparser_sqlserver_control_internal_error(out_error, "SQL Server token storage overflowed");
	}
	tokens = (sqlparser_sqlserver_token_t *)malloc(count * sizeof(*tokens));
	if (tokens == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_sqlserver_scanner_init(&scanner, sql, 0U, sql_len, out_error);
	index = 0U;
	while (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_scanner_next(&scanner, &token, out_error);
		if (status != SQLPARSER_STATUS_OK || token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			break;
		}
		if (index >= count) {
			status = sqlparser_sqlserver_control_internal_error(out_error, "SQL Server token scan changed between passes");
			break;
		}
		tokens[index++] = token;
	}
	if (status != SQLPARSER_STATUS_OK || index != count) {
		free(tokens);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_control_internal_error(out_error, "SQL Server token scan count is inconsistent");
		}
		return status;
	}
	*out_tokens = tokens;
	*out_token_count = count;
	*out_has_control = 1;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_sqlserver_control_parse(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	sqlparser_control_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_control_parser_t parser;
	sqlparser_sqlserver_token_t *tokens;
	sqlparser_control_state_t *state;
	size_t sql_len;
	size_t token_count;
	int has_control;
	sqlparser_status_t status;

	if (out_state == NULL || input_sql == NULL || limits == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL Server control arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	sql_len = strlen(input_sql);
	tokens = NULL;
	token_count = 0U;
	has_control = 0;
	status = sqlparser_sqlserver_control_scan(
		input_sql, sql_len, &tokens, &token_count, &has_control, out_error);
	if (status != SQLPARSER_STATUS_OK || !has_control) {
		free(tokens);
		return status;
	}

	memset(&parser, 0, sizeof(parser));
	parser.sql = input_sql;
	parser.tokens = tokens;
	parser.token_count = token_count;
	status = sqlparser_sqlserver_control_run_pass(
		&parser, SQLPARSER_SQLSERVER_CONTROL_COUNT, NULL, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(tokens);
		return status;
	}
	state = NULL;
	status = sqlparser_control_state_allocate(&parser.counts, limits, &state, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_control_run_pass(
			&parser, SQLPARSER_SQLSERVER_CONTROL_LAYOUT, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_control_run_pass(
			&parser, SQLPARSER_SQLSERVER_CONTROL_FILL, state, out_error);
	}
	free(tokens);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_control_state_release(state);
		return status;
	}
	*out_state = state;
	return SQLPARSER_STATUS_OK;
}
