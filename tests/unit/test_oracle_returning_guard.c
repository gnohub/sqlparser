/* Production TU, an independently frozen validator plus all private helpers,
 * and an independent scalar quote/comment scanner. No production bypass. */
#include <ctype.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_internal.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "returning guard:%d case=%zu locale=%s: %s\n", __LINE__, comparisons, setlocale(LC_CTYPE,NULL), #x); abort(); } } while (0)
static size_t comparisons, actual_scans, frozen_scans, absent_routes, present_routes;
static size_t actual_idents, frozen_idents;
static int actual_isalnum(int c) { ++actual_idents; return isalnum(c); }
static int frozen_isalnum(int c) { ++frozen_idents; return isalnum(c); }
static size_t guarded_skip(sqlparser_dialect_t dialect, const char *sql, size_t position)
{ ++actual_scans; return sqlparser_public_skip_quoted_or_comment(dialect, sql, position); }
#define sqlparser_public_skip_quoted_or_comment guarded_skip
#undef isalnum
#define isalnum actual_isalnum
#include "../../src/dialect/sqlparser_dialect_dml_result.c"
#undef isalnum
#undef sqlparser_public_skip_quoted_or_comment
#include "sqlparser_reference_surface_scanner.inc"
static size_t frozen_skip_quoted_or_comment(sqlparser_dialect_t dialect, const char *sql, size_t position)
{ ++frozen_scans; return reference_skip_quoted_or_comment(dialect, sql, position); }
#define isalnum frozen_isalnum
/* Frozen reference validator and all private helpers.
 * Renames only; never include the current implementation implementation here. */
static int frozen_sqlparser_returning_into_is_ident_char(unsigned char value)
{
	return isalnum(value) || value == '_' || value == '$' ||
		value == '#' || value >= 0x80U;
}

static size_t frozen_sqlparser_returning_into_word_end(
	const char *sql,
	size_t start)
{
	size_t end;

	end = start;
	while (frozen_sqlparser_returning_into_is_ident_char(
		       (unsigned char)sql[end])) {
		end++;
	}
	return end;
}

static int frozen_sqlparser_returning_into_word_equal(
	const char *sql,
	size_t start,
	size_t end,
	const char *word)
{
	size_t index;
	size_t length;

	length = word != NULL ? strlen(word) : 0U;
	if (sql == NULL || word == NULL || end - start != length ||
	    (start > 0U && frozen_sqlparser_returning_into_is_ident_char(
				  (unsigned char)sql[start - 1U])) ||
	    frozen_sqlparser_returning_into_is_ident_char((unsigned char)sql[end])) {
		return 0;
	}
	for (index = 0U; index < length; index++) {
		if (tolower((unsigned char)sql[start + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static int frozen_sqlparser_returning_into_word_at(
	const char *sql,
	size_t start,
	const char *word)
{
	size_t index;
	size_t length;

	if (sql == NULL || word == NULL ||
	    (start > 0U && frozen_sqlparser_returning_into_is_ident_char(
			  (unsigned char)sql[start - 1U]))) {
		return 0;
	}
	length = strlen(word);
	for (index = 0U; index < length; index++) {
		if (sql[start + index] == '\0' ||
		    tolower((unsigned char)sql[start + index]) !=
			    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return !frozen_sqlparser_returning_into_is_ident_char(
		(unsigned char)sql[start + length]);
}

static size_t frozen_sqlparser_returning_into_skip_trivia(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t start)
{
	size_t next;

	while (sql[start] != '\0') {
		while (isspace((unsigned char)sql[start])) {
			start++;
		}
		if (!((sql[start] == '-' && sql[start + 1U] == '-') ||
		      (sql[start] == '/' && sql[start + 1U] == '*'))) {
			break;
		}
		next = frozen_skip_quoted_or_comment(
			dialect, sql, start);
		if (next <= start) {
			break;
		}
		start = next;
	}
	return start;
}

static int frozen_sqlparser_returning_into_statement_context(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t keyword_start,
	size_t *out_statement_index)
{
	size_t depth;
	size_t position;
	size_t statement_index;
	int statement_is_dml;
	int statement_word_seen;

	depth = 0U;
	position = 0U;
	statement_index = 0U;
	statement_is_dml = 0;
	statement_word_seen = 0;
	while (position < keyword_start) {
		size_t next;
		size_t word_end;

		next = frozen_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			position = next;
			continue;
		}
		if (sql[position] == '(') {
			depth++;
			position++;
			continue;
		}
		if (sql[position] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			position++;
			continue;
		}
		if (depth == 0U && sql[position] == ';') {
			if (statement_word_seen) {
				statement_index++;
			}
			statement_is_dml = 0;
			statement_word_seen = 0;
			position++;
			continue;
		}
		if (!frozen_sqlparser_returning_into_is_ident_char(
			    (unsigned char)sql[position]) ||
		    isdigit((unsigned char)sql[position])) {
			position++;
			continue;
		}
		word_end = frozen_sqlparser_returning_into_word_end(sql, position);
		if (depth == 0U && !statement_word_seen) {
			statement_word_seen = 1;
			statement_is_dml =
				frozen_sqlparser_returning_into_word_equal(
					sql, position, word_end, "insert") ||
				frozen_sqlparser_returning_into_word_equal(
					sql, position, word_end, "update") ||
				frozen_sqlparser_returning_into_word_equal(
					sql, position, word_end, "delete");
		}
		position = word_end;
	}
	if (depth != 0U || !statement_is_dml) {
		return 0;
	}
	if (out_statement_index != NULL) {
		*out_statement_index = statement_index;
	}
	return 1;
}

static size_t frozen_sqlparser_returning_into_bind_end(
	const char *sql,
	size_t start)
{
	size_t end;

	if (sql[start] != ':' || sql[start + 1U] == '\0' ||
	    sql[start + 1U] == ':' || sql[start + 1U] == '=') {
		return start;
	}
	end = start + 1U;
	if (isdigit((unsigned char)sql[end])) {
		while (isdigit((unsigned char)sql[end])) {
			end++;
		}
		return end;
	}
	if (!isalpha((unsigned char)sql[end]) && sql[end] != '_' &&
	    (unsigned char)sql[end] < 0x80U) {
		return start;
	}
	end++;
	while (frozen_sqlparser_returning_into_is_ident_char(
		       (unsigned char)sql[end])) {
		end++;
	}
	return end;
}

int frozen_sqlparser_dialect_returning_into_receiver_is_bind(
	sqlparser_dialect_t dialect,
	const char *sql)
{
	size_t end;
	size_t skipped_end;
	size_t start;

	if (sql == NULL) {
		return 0;
	}
	start = frozen_sqlparser_returning_into_skip_trivia(dialect, sql, 0U);
	end = frozen_sqlparser_returning_into_bind_end(sql, start);
	skipped_end = frozen_sqlparser_returning_into_skip_trivia(dialect, sql, end);
	return end > start && sql[skipped_end] == '\0';
}

static int frozen_sqlparser_returning_into_bulk_collect_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t start)
{
	size_t position;

	if (!frozen_sqlparser_returning_into_word_at(sql, start, "bulk")) {
		return 0;
	}
	position = frozen_sqlparser_returning_into_skip_trivia(
		dialect, sql, start + strlen("bulk"));
	if (!frozen_sqlparser_returning_into_word_at(sql, position, "collect")) {
		return 0;
	}
	position = frozen_sqlparser_returning_into_skip_trivia(
		dialect, sql, position + strlen("collect"));
	return frozen_sqlparser_returning_into_word_at(sql, position, "into");
}

int frozen_sqlparser_dialect_returning_into_clause_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t keyword_start,
	int allow_return_keyword,
	sqlparser_dialect_returning_into_clause_t *out_clause)
{
	sqlparser_dialect_returning_into_clause_t clause;
	size_t depth;
	size_t keyword_length;
	size_t position;
	size_t sink_end;
	size_t sink_start;
	size_t sink_count;
	size_t target_count;
	int found_into;
	int target_has_code;

	memset(&clause, 0, sizeof(clause));
	if (sql == NULL || out_clause == NULL) {
		return 0;
	}
	if (frozen_sqlparser_returning_into_word_at(
		    sql, keyword_start, "returning")) {
		keyword_length = strlen("returning");
	} else if (allow_return_keyword &&
		   frozen_sqlparser_returning_into_word_at(
			   sql, keyword_start, "return")) {
		keyword_length = strlen("return");
		clause.uses_return_keyword = 1;
	} else {
		return 0;
	}
	if (!frozen_sqlparser_returning_into_statement_context(
		    dialect,
		    sql,
		    keyword_start,
		    &clause.statement_index)) {
		return 0;
	}
	clause.keyword_end = keyword_start + keyword_length;
	depth = 0U;
	position = clause.keyword_end;
	found_into = 0;
	target_count = 0U;
	target_has_code = 0;
	while (sql[position] != '\0') {
		size_t next;
		size_t word_end;

		next = frozen_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			if (!((sql[position] == '-' && sql[position + 1U] == '-') ||
			      (sql[position] == '/' && sql[position + 1U] == '*'))) {
				target_has_code = 1;
			}
			position = next;
			continue;
		}
		if (sql[position] == '(') {
			depth++;
			target_has_code = 1;
			position++;
			continue;
		}
		if (sql[position] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			target_has_code = 1;
			position++;
			continue;
		}
		if (depth == 0U && sql[position] == ';') {
			return 0;
		}
		if (depth == 0U && sql[position] == ',') {
			if (!target_has_code) {
				return 0;
			}
			target_count++;
			target_has_code = 0;
			position++;
			continue;
		}
		if (depth == 0U &&
		    frozen_sqlparser_returning_into_is_ident_char(
			    (unsigned char)sql[position]) &&
		    !isdigit((unsigned char)sql[position])) {
			if (frozen_sqlparser_returning_into_bulk_collect_at(
				    dialect, sql, position)) {
				return 0;
			}
			word_end = frozen_sqlparser_returning_into_word_end(
				sql, position);
			if (frozen_sqlparser_returning_into_word_equal(
				    sql, position, word_end, "into")) {
				if (!target_has_code) {
					return 0;
				}
				target_count++;
				clause.into_start = position;
				found_into = 1;
				break;
			}
			position = word_end;
			target_has_code = 1;
			continue;
		}
		if (!isspace((unsigned char)sql[position])) {
			target_has_code = 1;
		}
		position++;
	}
	if (!found_into || target_count == 0U) {
		return 0;
	}
	sink_start = frozen_sqlparser_returning_into_skip_trivia(
		dialect,
		sql,
		clause.into_start + strlen("into"));
	sink_end = frozen_sqlparser_returning_into_bind_end(sql, sink_start);
	if (sink_end == sink_start) {
		return 0;
	}
	sink_count = 0U;
	for (;;) {
		sink_count++;
		position = frozen_sqlparser_returning_into_skip_trivia(
			dialect, sql, sink_end);
		if (sql[position] != ',') {
			break;
		}
		sink_start = frozen_sqlparser_returning_into_skip_trivia(
			dialect, sql, position + 1U);
		sink_end = frozen_sqlparser_returning_into_bind_end(sql, sink_start);
		if (sink_end == sink_start) {
			return 0;
		}
	}
	if ((sql[position] != '\0' && sql[position] != ';') ||
	    sink_count != target_count) {
		return 0;
	}
	clause.pair_count = target_count;
	*out_clause = clause;
	return 1;
}

sqlparser_status_t frozen_sqlparser_dialect_returning_into_validate(
	sqlparser_dialect_t dialect,
	const char *sql,
	int allow_return_keyword,
	int allow_plain_returning,
	sqlparser_error_t *out_error)
{
	size_t position;

	if (sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	position = 0U;
	while (sql[position] != '\0') {
		size_t next;
		size_t word_end;

		next = frozen_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			position = next;
			continue;
		}
		if (sql[position] == ':') {
			next = frozen_sqlparser_returning_into_bind_end(sql, position);
			if (next > position) {
				position = next;
				continue;
			}
		}
		if (!frozen_sqlparser_returning_into_is_ident_char(
			    (unsigned char)sql[position]) ||
		    isdigit((unsigned char)sql[position])) {
			position++;
			continue;
		}
		word_end = frozen_sqlparser_returning_into_word_end(sql, position);
		if (frozen_sqlparser_returning_into_word_equal(
			    sql, position, word_end, "returning") ||
		    (allow_return_keyword &&
		     frozen_sqlparser_returning_into_word_equal(
			     sql, position, word_end, "return"))) {
			sqlparser_dialect_returning_into_clause_t clause;
			size_t depth;

			if (!frozen_sqlparser_dialect_returning_into_clause_at(
				    dialect,
				    sql,
				    position,
				    allow_return_keyword,
				    &clause)) {
				if (allow_plain_returning &&
				    frozen_sqlparser_returning_into_word_equal(
					    sql,
					    position,
					    word_end,
					    "returning")) {
					position = word_end;
					continue;
				}
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"unsupported RETURN/RETURNING ... INTO syntax");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			depth = 0U;
			position = clause.into_start + strlen("into");
			while (sql[position] != '\0') {
				next = frozen_skip_quoted_or_comment(
					dialect, sql, position);
				if (next > position) {
					position = next;
					continue;
				}
				if (sql[position] == '(') {
					depth++;
				} else if (sql[position] == ')' && depth > 0U) {
					depth--;
				} else if (sql[position] == ';' && depth == 0U) {
					position++;
					break;
				}
				position++;
			}
			continue;
		}
		position = word_end;
	}
	return SQLPARSER_STATUS_OK;
}
#undef isalnum
/* Shared source corpus; expected behavior comes from the frozen validator and
 * from the same public caller linked to the immutable accepted archive. */
static const char *const returningguard_cases[] = {
    "", " ", "RETURN", "returning", "RETURNING", "rEtUrNiNg", "rrreturning", "returningreturning",
    "SELECT 1 FROM dual", "SELECT )", "SELECT 'unterminated", "SELECT \"unterminated", "/* unterminated", "-- tail",
    "INSERT ALL INTO t(a,b) VALUES ('雪', CURRENT_TIMESTAMP) SELECT 1 FROM dual",
    "INSERT ALL INTO t(a) VALUES ('old') INTO t(a) VALUES ('keep') SELECT 1 FROM dual",
    "INSERT INTO t(a) VALUES (1) RETURNING a INTO :out",
    "UPDATE t SET a=1 RETURNING a,b INTO :out,:other",
    "DELETE FROM t RETURNING a INTO :1",
    "UPDATE t SET a=1 RETURNING a", "UPDATE t SET a=1 RETURNING a,b INTO :out",
    "UPDATE t SET a=1 RETURNING a INTO target", "UPDATE t SET a=1 RETURNING a BULK COLLECT INTO :out",
    "UPDATE t SET a=1 RETURNING ,a INTO :out", "UPDATE t SET a=1 RETURNING a, INTO :out",
    "UPDATE t SET a=1 RETURNING a INTO :out trailing", "UPDATE t SET a=1 RETURNING (a INTO :out",
    "UPDATE t SET a=1 RETURN a INTO :out", "UPDATE t SET a=1 RETURN a", "SELECT RETURNING FROM dual",
    "UPDATE t SET a=1 RETURNING a INTO :returning", "SELECT :returning FROM dual", "SELECT :return FROM dual",
    "SELECT returning_suffix, prefix_returning, $returning, #returning, returning9 FROM dual",
    "SELECT 雪returning, returning雪 FROM dual", "SELECT 'RETURNING' FROM dual", "SELECT \"RETURNING\" FROM dual",
    "SELECT 'can''t RETURNING' FROM dual", "SELECT E'escaped\\\' RETURNING' FROM dual",
    "SELECT N'RETURNING' FROM dual", "SELECT q'[RETURNING]' FROM dual", "SELECT nq'{RETURNING}' FROM dual",
    "SELECT q'(RETURNING)' FROM dual", "SELECT Q'<RETURNING>' FROM dual", "SELECT q'!RETURNING!' FROM dual",
    "SELECT q'[unterminated RETURNING", "SELECT nq'[unterminated RETURNING", "SELECT q' RETURNING '",
    "SELECT $$RETURNING$$ FROM dual", "SELECT $tag$RETURNING$tag$ FROM dual", "SELECT $tag$unterminated RETURNING",
    "SELECT U&'\\0052ETURNING' FROM dual", "SELECT U&\"\\0052ETURNING\" FROM dual",
    "SELECT U&'RETURNING' FROM dual", "SELECT U&\"RETURNING\" FROM dual",
    "SELECT 1 /* RETURNING */ FROM dual", "SELECT 1 -- RETURNING\nFROM dual",
    "SELECT 1 /* outer /* inner */ RETURNING */ FROM dual", "SELECT RET/* gap */URNING FROM dual",
    "SELECT RET-- gap\nURNING FROM dual", "SELECT 1; UPDATE t SET a=1 RETURNING a INTO :out; SELECT 2",
    "UPDATE t SET a=1 RETURNING a INTO :out; UPDATE t SET a=2 RETURNING a,b INTO :out",
    "SELECT ); UPDATE t SET a=1 RETURNING a INTO :out", "SELECT (UPDATE t SET a=1 RETURNING a INTO :out)",
    "ALTER SESSION SET CURRENT_SCHEMA = app; SELECT 1 FROM dual",
    "ALTER SESSION SET CURRENT_SCHEMA = app; UPDATE t SET a=1 RETURNING a,b INTO :out",
    "EXECUTE IMMEDIATE 'UPDATE t SET a=1 RETURNING a INTO :out'",
    "BEGIN NULL; END;", "SELECT CURRENT_TIMESTAMP, CURRENT_TIMESTAMP FROM dual",
    "SELECT \001\037\177\200\377 FROM dual", "SELECT `RETURNING`, [RETURNING] FROM dual"
};
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static int scalar_raw_match(const char *sql)
{
    const unsigned char *p = (const unsigned char *)sql;
    static const unsigned char target[] = "returning";
    for (; *p; ++p) {
        size_t i;
        for (i=0; i<9U; ++i) if (!p[i] || tolower(p[i])!=tolower(target[i])) break;
        if (i==9U) return 1;
    }
    return 0;
}
static void compare_one(const char *sql, int dialect, int allow_return, int plain)
{
    sqlparser_error_t a,b; sqlparser_status_t ar,br; int may=sql ? scalar_raw_match(sql) : 0;
    memset(&a,0xa5,sizeof(a)); memcpy(&b,&a,sizeof(b)); actual_scans=frozen_scans=0;
    ar=sqlparser_dialect_returning_into_validate((sqlparser_dialect_t)dialect,sql,allow_return,plain,&a);
    br=frozen_sqlparser_dialect_returning_into_validate((sqlparser_dialect_t)dialect,sql,allow_return,plain,&b);
    CHECK(ar==br); CHECK(!memcmp(&a,&b,sizeof(a)));
    CHECK(ar==sqlparser_dialect_returning_into_validate((sqlparser_dialect_t)dialect,sql,allow_return,plain,NULL));
    CHECK(br==frozen_sqlparser_dialect_returning_into_validate((sqlparser_dialect_t)dialect,sql,allow_return,plain,NULL));
#if CHAR_BIT == 8 && UCHAR_MAX == 255
    if (sql && dialect==SQLPARSER_DIALECT_ORACLE && !allow_return) {
        CHECK(sqlparser_returning_into_raw_may_match(sql)==may);
        if (!may) { CHECK(actual_scans==0U); CHECK(ar==SQLPARSER_STATUS_OK); ++absent_routes; }
        else { CHECK(actual_scans==frozen_scans); ++present_routes; }
    } else CHECK(actual_scans==frozen_scans);
#else
    (void)may;
    CHECK(actual_scans==frozen_scans);
#endif
    ++comparisons;
}
static void all_modes(const char *sql)
{
    int d,r,p;
    for(d=-1;d<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER+1;d++) for(r=0;r<2;r++) for(p=0;p<2;p++) compare_one(sql,d,r,p);
}
static void corpus(void)
{
    size_t i,j; char word[10], input[160];
    all_modes(NULL);
    for(i=0;i<COUNT(returningguard_cases);i++) all_modes(returningguard_cases[i]);
    for(i=0;i<512U;i++) {
        for(j=0;j<9U;j++) word[j]=(char)((i&(1U<<j)) ? "RETURNING"[j] : "returning"[j]);
        word[9]=0; snprintf(input,sizeof(input),"UPDATE t SET a=1 %s a,b INTO :out",word); all_modes(input);
    }
    /* Exact-size allocation ends at NUL, including every partial keyword. */
    for(i=0;i<=9U;i++) for(j=0;j<16U;j++) {
        char *p=malloc(j+i+1U); CHECK(p); memset(p,'r',j); memcpy(p+j,"returning",i); p[j+i]=0; all_modes(p); free(p);
    }
    /* An embedded NUL ends the API input regardless of the following bytes. */
    { char x[]="SELECT 1\0RETURNING"; all_modes(x); }
}
static void byte_matrix(void)
{
    size_t i; unsigned v; char word[10], input[96];
    for(i=0;i<9U;i++) for(v=1;v<=255U;v++) {
        memcpy(word,"returning",10U); word[i]=(char)(unsigned char)v;
        snprintf(input,sizeof(input),"UPDATE t SET a=1 %s a,b INTO :out",word);
        compare_one(input,SQLPARSER_DIALECT_ORACLE,0,0); compare_one(input,SQLPARSER_DIALECT_ORACLE,0,1);
    }
}
static uint32_t state=UINT32_C(0x82041937);
static uint32_t random_value(void) { state^=state<<13; state^=state>>17; state^=state<<5; return state; }
static void fuzz(void)
{
    static const unsigned char chars[]="returningRETURNINGqQnNuU&$'\"\\/*-:;()[]{}<>_#123 ,\r\n\t\200\235\335\377";
    size_t i,j,n; char s[129];
    for(i=0;i<6000U;i++) {
        n=random_value()%128U; for(j=0;j<n;j++) s[j]=(char)chars[random_value()%(sizeof(chars)-1U)]; s[n]=0;
        if(n>=9U && i%3U==0U) memcpy(s+random_value()%(n-8U),"returning",9U);
        all_modes(s);
    }
}
int main(int argc,char **argv)
{
    static const char *const locales[]={"C","C.UTF-8","tr_TR","tr_TR.UTF-8","en_US","de_DE"};
    size_t i,accepted=0; FILE *list=NULL; char locale[256];
    CHECK(setlocale(LC_CTYPE,"C"));
    if(argc==3 && !strcmp(argv[1],"--fixture")) {
        FILE *f=fopen(argv[2],"rb"); long n; char *sql;
        CHECK(f && !fseek(f,0,SEEK_END)); n=ftell(f); CHECK(n>=0 && !fseek(f,0,SEEK_SET));
        sql=malloc((size_t)n+1U); CHECK(sql); CHECK(fread(sql,1,(size_t)n,f)==(size_t)n); fclose(f); sql[n]=0;
        compare_one(sql,SQLPARSER_DIALECT_ORACLE,0,0);
        printf("fixture bytes=%ld; raw_may_match=%d; actual/frozen scan calls=%zu/%zu; ident calls=%zu/%zu (two validation calls each)\n",n,sqlparser_returning_into_raw_may_match(sql),actual_scans,frozen_scans,actual_idents,frozen_idents);
        free(sql); return 0;
    }
    corpus(); fuzz();
    for(i=0;i<COUNT(locales);i++) if(setlocale(LC_CTYPE,locales[i])) { corpus(); byte_matrix(); ++accepted; }
    if(argc==3 && !strcmp(argv[1],"--locales")) {
        list=fopen(argv[2],"r"); CHECK(list);
        while(fgets(locale,sizeof(locale),list)) { locale[strcspn(locale,"\r\n")]=0; if(setlocale(LC_CTYPE,locale)) { byte_matrix(); ++accepted; } }
        CHECK(!ferror(list)); fclose(list);
    }
    CHECK(setlocale(LC_CTYPE,"C"));
    printf("RETURNING frozen differential: %zu comparisons; %zu absence and %zu retained routes; %zu locale runs\n",comparisons,absent_routes,present_routes,accepted);
    return 0;
}
