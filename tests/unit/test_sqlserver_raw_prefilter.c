/* Differential prefilter parity against the unchanged pre-optimization gate. */
#ifndef SQLPARSER_PREFILTER_IMPLEMENTATION
#define SQLPARSER_PREFILTER_IMPLEMENTATION "../../src/dialect/sqlparser_dialect_sqlserver.c"
#endif
#include SQLPARSER_PREFILTER_IMPLEMENTATION
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr,"check failed line %d: %s\n",__LINE__,#condition); abort(); } } while(0)
static int legacy_raw_contains_word_span(const char *sql, const char *word, size_t word_len)
{
	size_t pos;

	if (sql == NULL || word == NULL || word_len == 0U) {
		return 0;
	}

	for (pos = 0U; sql[pos] != '\0'; pos++) {
		size_t index;

		if (pos > 0U && sqlparser_sqlserver_is_ident_char((unsigned char)sql[pos - 1U])) {
			continue;
		}

		for (index = 0U; index < word_len; index++) {
			if (sql[pos + index] == '\0') {
				break;
			}
			if (tolower((unsigned char)sql[pos + index]) !=
			    tolower((unsigned char)word[index])) {
				break;
			}
		}
		if (index == word_len &&
		    !sqlparser_sqlserver_is_ident_char((unsigned char)sql[pos + word_len])) {
			return 1;
		}
	}

	return 0;
}

static int legacy_raw_contains_word(const char *sql, const char *word)
{
	return legacy_raw_contains_word_span(sql, word, word != NULL ? strlen(word) : 0U);
}

static int legacy_raw_may_contain_phrase(const char *sql, const char *phrase)
{
	size_t pos;
	int saw_token;

	if (sql == NULL || phrase == NULL) {
		return 0;
	}

	pos = 0U;
	saw_token = 0;
	while (phrase[pos] != '\0') {
		size_t start;
		size_t len;

		while (phrase[pos] != '\0' &&
		       !sqlparser_sqlserver_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		start = pos;
		while (phrase[pos] != '\0' &&
		       sqlparser_sqlserver_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		len = pos - start;
		if (len == 0U) {
			continue;
		}

		saw_token = 1;
		if (!legacy_raw_contains_word_span(sql, phrase + start, len)) {
			return 0;
		}
	}

	return saw_token;
}

static sqlparser_status_t legacy_reject_unsupported(
	const char *sql,
	sqlparser_error_t *out_error)
{
	static const char *const unsupported_phrases[] = {
		"cross apply",
		"outer apply",
		"pivot",
		"unpivot",
		"for xml",
		"declare",
		"create procedure",
		"alter procedure",
		"create function",
		"alter function",
		"create trigger",
		"alter trigger",
		"begin try",
		"openquery",
		"openrowset",
		"opendatasource",
		"openjson",
		"openxml",
		"table variable",
		"by source"
	};
	char *masked;
	sqlparser_status_t status;
	size_t index;
	int needs_mask;

	needs_mask =
		legacy_raw_contains_word(sql, "exec") ||
		legacy_raw_contains_word(sql, "execute") ||
		legacy_raw_contains_word(sql, "use") ||
		(strchr(sql, '@') != NULL &&
		 (legacy_raw_contains_word(sql, "from") ||
		  legacy_raw_contains_word(sql, "join") ||
		  legacy_raw_contains_word(sql, "update") ||
		  legacy_raw_contains_word(sql, "into"))) ||
		(legacy_raw_contains_word(sql, "top") &&
		 (legacy_raw_contains_word(sql, "insert") ||
		  legacy_raw_contains_word(sql, "update") ||
		  legacy_raw_contains_word(sql, "delete") ||
		  legacy_raw_contains_word(sql, "merge") ||
		  legacy_raw_contains_word(sql, "select"))) ||
		(legacy_raw_contains_word(sql, "output") &&
		 (legacy_raw_contains_word(sql, "insert") ||
		  legacy_raw_contains_word(sql, "update") ||
		  legacy_raw_contains_word(sql, "delete") ||
		  legacy_raw_contains_word(sql, "merge"))) ||
		(legacy_raw_contains_word(sql, "select") &&
		 legacy_raw_contains_word(sql, "top") &&
		 legacy_raw_contains_word(sql, "offset") &&
		 legacy_raw_contains_word(sql, "fetch"));
	for (index = 0U; !needs_mask &&
	     index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (legacy_raw_may_contain_phrase(sql, unsupported_phrases[index])) {
			needs_mask = 1;
		}
	}
	if (!needs_mask) {
		return SQLPARSER_STATUS_OK;
	}

	masked = NULL;
	status = sqlparser_sqlserver_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_sqlserver_starts_with_word(masked, "exec") ||
	    sqlparser_sqlserver_starts_with_word(masked, "execute") ||
	    sqlparser_sqlserver_starts_with_word(masked, "use")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: batch or procedure execution");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (sqlparser_sqlserver_contains_table_variable_reference(masked)) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: table variable");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (sqlparser_sqlserver_starts_with_dml_clause(masked, "top")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: DML TOP");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (sqlparser_sqlserver_starts_with_word(masked, "insert") &&
	    sqlparser_sqlserver_contains_phrase(masked, "output")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: OUTPUT");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if ((sqlparser_sqlserver_starts_with_word(masked, "update") ||
	     sqlparser_sqlserver_starts_with_word(masked, "delete") ||
	     sqlparser_sqlserver_starts_with_word(masked, "merge")) &&
	    sqlparser_sqlserver_contains_phrase(masked, "output")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: OUTPUT");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (sqlparser_sqlserver_starts_with_word(masked, "select") &&
	    sqlparser_sqlserver_contains_phrase(masked, "top") &&
	    sqlparser_sqlserver_contains_phrase(masked, "offset") &&
	    sqlparser_sqlserver_contains_phrase(masked, "fetch")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: TOP with OFFSET/FETCH");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	for (index = 0U; index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_sqlserver_contains_phrase(masked, unsupported_phrases[index])) {
			char message[256];

			(void)snprintf(
				message,
				sizeof(message),
				"unsupported SQL Server syntax: %s",
				unsupported_phrases[index]);
			free(masked);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, message);
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}

	free(masked);
	return SQLPARSER_STATUS_OK;
}

static int legacy_gate(const char *sql)
{
	static const char *const unsupported_phrases[] = {
		"cross apply",
		"outer apply",
		"pivot",
		"unpivot",
		"for xml",
		"declare",
		"create procedure",
		"alter procedure",
		"create function",
		"alter function",
		"create trigger",
		"alter trigger",
		"begin try",
		"openquery",
		"openrowset",
		"opendatasource",
		"openjson",
		"openxml",
		"table variable",
		"by source"
	};
	size_t index;
	int needs_mask;

	needs_mask =
		legacy_raw_contains_word(sql, "exec") ||
		legacy_raw_contains_word(sql, "execute") ||
		legacy_raw_contains_word(sql, "use") ||
		(strchr(sql, '@') != NULL &&
		 (legacy_raw_contains_word(sql, "from") ||
		  legacy_raw_contains_word(sql, "join") ||
		  legacy_raw_contains_word(sql, "update") ||
		  legacy_raw_contains_word(sql, "into"))) ||
		(legacy_raw_contains_word(sql, "top") &&
		 (legacy_raw_contains_word(sql, "insert") ||
		  legacy_raw_contains_word(sql, "update") ||
		  legacy_raw_contains_word(sql, "delete") ||
		  legacy_raw_contains_word(sql, "merge") ||
		  legacy_raw_contains_word(sql, "select"))) ||
		(legacy_raw_contains_word(sql, "output") &&
		 (legacy_raw_contains_word(sql, "insert") ||
		  legacy_raw_contains_word(sql, "update") ||
		  legacy_raw_contains_word(sql, "delete") ||
		  legacy_raw_contains_word(sql, "merge"))) ||
		(legacy_raw_contains_word(sql, "select") &&
		 legacy_raw_contains_word(sql, "top") &&
		 legacy_raw_contains_word(sql, "offset") &&
		 legacy_raw_contains_word(sql, "fetch"));
	for (index = 0U; !needs_mask &&
	     index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (legacy_raw_may_contain_phrase(sql, unsupported_phrases[index])) {
			needs_mask = 1;
		}
	}
	return needs_mask;
}
static int inventory_gate(const char *sql)
{
	static const char *const unsupported_phrases[] = {
		"cross apply",
		"outer apply",
		"pivot",
		"unpivot",
		"for xml",
		"declare",
		"create procedure",
		"alter procedure",
		"create function",
		"alter function",
		"create trigger",
		"alter trigger",
		"begin try",
		"openquery",
		"openrowset",
		"opendatasource",
		"openjson",
		"openxml",
		"table variable",
		"by source"
	};
	size_t index;
	int needs_mask;
	sqlparser_sqlserver_raw_word_inventory_t inventory;
	sqlparser_sqlserver_raw_word_inventory(sql, &inventory);

	needs_mask =
		sqlparser_sqlserver_raw_contains_word(&inventory, "exec") ||
		sqlparser_sqlserver_raw_contains_word(&inventory, "execute") ||
		sqlparser_sqlserver_raw_contains_word(&inventory, "use") ||
		(inventory.has_at &&
		 (sqlparser_sqlserver_raw_contains_word(&inventory, "from") ||
		  sqlparser_sqlserver_raw_contains_word(&inventory, "join") ||
		  sqlparser_sqlserver_raw_contains_word(&inventory, "update") ||
		  sqlparser_sqlserver_raw_contains_word(&inventory, "into"))) ||
		(sqlparser_sqlserver_raw_contains_word(&inventory, "top") &&
		 (sqlparser_sqlserver_raw_contains_word(&inventory, "insert") ||
		  sqlparser_sqlserver_raw_contains_word(&inventory, "update") ||
		  sqlparser_sqlserver_raw_contains_word(&inventory, "delete") ||
		  sqlparser_sqlserver_raw_contains_word(&inventory, "merge") ||
		  sqlparser_sqlserver_raw_contains_word(&inventory, "select"))) ||
		(sqlparser_sqlserver_raw_contains_word(&inventory, "output") &&
		 (sqlparser_sqlserver_raw_contains_word(&inventory, "insert") ||
		  sqlparser_sqlserver_raw_contains_word(&inventory, "update") ||
		  sqlparser_sqlserver_raw_contains_word(&inventory, "delete") ||
		  sqlparser_sqlserver_raw_contains_word(&inventory, "merge"))) ||
		(sqlparser_sqlserver_raw_contains_word(&inventory, "select") &&
		 sqlparser_sqlserver_raw_contains_word(&inventory, "top") &&
		 sqlparser_sqlserver_raw_contains_word(&inventory, "offset") &&
		 sqlparser_sqlserver_raw_contains_word(&inventory, "fetch"));
	for (index = 0U; !needs_mask &&
	     index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_sqlserver_raw_may_contain_phrase(&inventory, unsupported_phrases[index])) {
			needs_mask = 1;
		}
	}
	return needs_mask;
}

#ifdef SQLPARSER_PREFILTER_ALLOC_WRAPPERS
static int fail_next, allocation_calls;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size) { allocation_calls++; if(fail_next){fail_next=0;return NULL;}return __real_malloc(size); }
#endif
static size_t checks;
static void compare(const char *sql)
{
    sqlparser_sqlserver_raw_word_inventory_t inventory;
    sqlparser_error_t a = {0}, b = {0};
    sqlparser_status_t sa, sb;
    size_t i;
    CHECK(legacy_gate(sql) == inventory_gate(sql));
    sqlparser_sqlserver_raw_word_inventory(sql, &inventory);
    for (i = 0U; i < sizeof(sqlparser_sqlserver_prefilter_words) /
        sizeof(sqlparser_sqlserver_prefilter_words[0]); i++) {
        const char *word = sqlparser_sqlserver_prefilter_words[i];
        CHECK(legacy_raw_contains_word(sql, word) ==
            sqlparser_sqlserver_raw_contains_word(&inventory, word));
    }
    CHECK(legacy_raw_contains_word(sql, "unlisted_future_keyword") ==
        sqlparser_sqlserver_raw_contains_word(&inventory, "unlisted_future_keyword"));
    CHECK((strchr(sql, '@') != NULL) == inventory.has_at);
    sa = legacy_reject_unsupported(sql, &a);
    sb = sqlparser_sqlserver_reject_unsupported(sql, &b);
    if (sa != sb || memcmp(&a, &b, sizeof(a)) != 0) {
        fprintf(stderr, "prefilter mismatch: %s old=%d new=%d oldmsg=%s newmsg=%s\n",
            sql, sa, sb, a.message, b.message);
        abort();
    }
#ifdef SQLPARSER_PREFILTER_ALLOC_WRAPPERS
    {
        int old_calls, new_calls;
        memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));
        allocation_calls=0;fail_next=1;sa=legacy_reject_unsupported(sql,&a);fail_next=0;old_calls=allocation_calls;
        allocation_calls=0;fail_next=1;sb=sqlparser_sqlserver_reject_unsupported(sql,&b);fail_next=0;new_calls=allocation_calls;
        CHECK(old_calls==new_calls);CHECK(sa==sb);CHECK(memcmp(&a,&b,sizeof(a))==0);
    }
#endif
    checks++;
}
int main(void)
{
    static const char *const edges[] = {"", " ", "_", "@", "$", "#", "9", "z", "'", "\"", "[", "]", "/*", "*/", "--", "\n", "\x80", "\xff", "\xe4\xb8\xad"};
    static const char *const words[] = {"EXEC", "execute", "Use", "from", "join", "update", "into", "TOP", "insert", "delete", "merge", "select", "output", "offset", "fetch", "cross", "outer", "apply", "pivot", "unpivot", "for", "xml", "declare", "create", "alter", "procedure", "function", "trigger", "begin", "try", "openquery", "openrowset", "opendatasource", "openjson", "openxml", "table", "variable", "by", "source", "unlisted_future_keyword"};
    static const char *const combinations[] = {
        "SELECT TOP 3 name FROM t OFFSET 1 ROWS FETCH NEXT 1 ROWS ONLY",
        "SELECT 'cross apply' FROM t", "SELECT 1 /*cross apply*/", "SELECT [outer apply] FROM t",
        "SELECT * FROM t CROSS APPLY u", "SELECT * FROM t OUTER /*x*/ APPLY u",
        "SELECT * FROM @t", "SELECT '@t'", "UPDATE TOP(1) t SET a=1", "INSERT INTO t OUTPUT inserted.a VALUES(1)",
        "CREATE PROCEDURE p AS SELECT 1", "BEGIN TRY SELECT 1 END TRY", "SELECT 1 --openjson\n",
        "SELECT cross1, xapply, top_name, @output, #exec, $use FROM t", "EXECUTE p", "USE db",
        "SELECT * FROM t WHERE name='table variable'", "/*unclosed cross apply", "'unterminated output",
        "INSERT INTO t(a,b) VALUES(1,'one'),(2,'two')"
    };
    char sql[1024]; size_t i,j,k;
    for(i=0;i<sizeof(words)/sizeof(words[0]);i++)
        for(j=0;j<sizeof(edges)/sizeof(edges[0]);j++)
            for(k=0;k<sizeof(edges)/sizeof(edges[0]);k++) {
                snprintf(sql,sizeof(sql),"%s%s%s",edges[j],words[i],edges[k]);compare(sql);
            }
    for(i=0;i<sizeof(words)/sizeof(words[0]);i++)
        for(j=0;j<sizeof(words)/sizeof(words[0]);j++) {
            snprintf(sql,sizeof(sql),"%s /*ignored*/ %s",words[i],words[j]);compare(sql);
        }
    for(i=0;i<sizeof(combinations)/sizeof(combinations[0]);i++)compare(combinations[i]);
    printf("SQLSERVER_RAW_PREFILTER_PARITY checks=%zu\n",checks);
    return 0;
}
