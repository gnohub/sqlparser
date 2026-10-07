/* Private native-tree shortcut for a completely proved, ordinary INSERT.
 * This is included only by pg_query_parse.c and runs inside its PG_TRY and
 * parse memory context. Admission is allocation-free and consumes the whole
 * source before construction. A miss has no input or parser side effects.
 *
 * Keep this deliberately smaller than the SQL grammar: ASCII non-keyword
 * names, exactly two explicit columns, plain decimal with at most one minus
 * (native scanner whitespace may follow it), plus one
 * unescaped printable-ASCII string per row, and at most one final semicolon.
 * The native grammar remains the independent authority for every other input.
 */
#include "common/keywords.h"
#include "nodes/makefuncs.h"
#include "nodes/parsenodes.h"
#include <limits.h>

typedef struct PgQuerySimpleInsertSlice
{
    size_t start;
    size_t length;
} PgQuerySimpleInsertSlice;

typedef struct PgQuerySimpleInsertRow
{
    int32 integer;
    bool negative;
    bool is_float;
    int integer_location;
    int string_location;
    PgQuerySimpleInsertSlice text;
    PgQuerySimpleInsertSlice digits;
} PgQuerySimpleInsertRow;

typedef struct PgQuerySimpleInsertSource
{
    PgQuerySimpleInsertSlice names[3];
    size_t length;
    size_t values_start;
    size_t rows;
    int statement_length;
} PgQuerySimpleInsertSource;

/* These scalar admission predicates also permit allocation-free boundary
 * tests; no test needs to materialize a gigabyte-sized source or native List.
 * Preserve deterministic native allocation-size errors by declining first. */
static bool
pg_query_simple_insert_source_size(size_t length)
{
    return length >= 4096U && length <= INT_MAX - 2U &&
        length <= INT32_MAX - 2U && length <= MaxAllocSize - 2U;
}

static bool
pg_query_simple_insert_rows_size(size_t rows)
{
    const size_t header_cells =
        (offsetof(List, initial_elements) - 1U) / sizeof(ListCell) + 1U;
    const size_t signed_budget = INT_MAX / 2U;
    const size_t allocation_budget = MaxAllocSize / sizeof(ListCell) / 2U;

    /* new_list rounds rows+LIST_HEADER_OVERHEAD before subtracting the
     * header cells; enlarge_list rounds rows alone. Reserve header space
     * even though both current constructors start with only one/two cells.
     * The half-budgets bound both native formulas, debug exact-size lists,
     * signed capacities, and the next append's min_size calculation. */
    return header_cells <= signed_budget && header_cells <= allocation_budget &&
        rows <= signed_budget - header_cells &&
        rows <= allocation_budget - header_cells;
}

static bool
pg_query_simple_insert_number_size(size_t length, bool negative)
{
    const size_t extra = negative ? 2U : 1U;

    /* Overflow tokens use pstrdup, not scan.c's growing literal buffer.
     * doNegateFloat then uses psprintf("-%s", token): a 128-byte first
     * attempt followed, if needed, by exactly length+2 bytes. Prove both
     * native requests before replacing scanner/parser construction. */
    return length != 0U && 128U <= MaxAllocSize && 128U <= INT_MAX &&
        length <= MaxAllocSize - extra && length <= INT_MAX - extra;
}

static bool
pg_query_simple_insert_string_size(size_t length)
{
    size_t capacity = 1024U;

    /* scan.c:addlit rounds length+1 up to a power of two, starting at
     * 1024; litbufdup subsequently requests length+1. A bounded exact-size
     * copy alone is insufficient: native literal-buffer growth may exceed
     * MaxAllocSize even when that copy and the complete source would fit.
     * Avoid length+1 or doubling until their bounds have been proved. */
    if (capacity > MaxAllocSize || capacity > INT_MAX)
        return false;
    while (length >= capacity)
    {
        if (capacity > MaxAllocSize / 2U || capacity > INT_MAX / 2U)
            return false;
        capacity *= 2U;
    }
    return true;
}

static void
pg_query_simple_insert_gap(const char *sql, size_t length, size_t *pos)
{
    while (*pos < length && scanner_isspace(sql[*pos]))
        ++*pos;
}

static bool
pg_query_simple_insert_name_char(unsigned char c, bool first)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
        (!first && c >= '0' && c <= '9');
}

static bool
pg_query_simple_insert_word(const char *sql, size_t length, size_t *pos,
                           const char *word, size_t word_length)
{
    size_t i;
    pg_query_simple_insert_gap(sql, length, pos);
    if (word_length > length - *pos)
        return false;
    for (i = 0; i < word_length; ++i)
    {
        unsigned char c = (unsigned char) sql[*pos + i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != (unsigned char) word[i])
            return false;
    }
    *pos += word_length;
    if (*pos < length &&
        (pg_query_simple_insert_name_char((unsigned char) sql[*pos], false) ||
         sql[*pos] == '$' || (unsigned char) sql[*pos] >= 0x80U))
        return false;
    return true;
}

static bool
pg_query_simple_insert_punctuation(const char *sql, size_t length, size_t *pos,
                                  char punctuation)
{
    pg_query_simple_insert_gap(sql, length, pos);
    if (*pos == length || sql[*pos] != punctuation)
        return false;
    ++*pos;
    return true;
}

static bool
pg_query_simple_insert_name(const char *sql, size_t length, size_t *pos,
                           PgQuerySimpleInsertSlice *name)
{
    char keyword[NAMEDATALEN];

    pg_query_simple_insert_gap(sql, length, pos);
    if (*pos == length ||
        !pg_query_simple_insert_name_char((unsigned char) sql[*pos], true))
        return false;
    name->start = *pos;
    do
        ++*pos;
    while (*pos < length &&
           pg_query_simple_insert_name_char((unsigned char) sql[*pos], false));
    name->length = *pos - name->start;

    /* ScanKeywordLookup needs a terminated token. Bound stack storage using
     * the actual native table, and decline if a future table outgrows it.
     * A longer name cannot be a keyword; spelling-preserving parsing neither
     * folds nor truncates such identifiers, so neither does construction. */
    if (ScanKeywords.max_kw_len < 0 ||
        (size_t) ScanKeywords.max_kw_len >= sizeof(keyword))
        return false;
    if (name->length <= (size_t) ScanKeywords.max_kw_len)
    {
        memcpy(keyword, sql + name->start, name->length);
        keyword[name->length] = '\0';
        if (ScanKeywordLookup(keyword, &ScanKeywords) >= 0)
            return false;
    }
    return true;
}

static bool
pg_query_simple_insert_row(const char *sql, size_t length, size_t *pos,
                          PgQuerySimpleInsertRow *row)
{
    uint32 number = 0;

    if (!pg_query_simple_insert_punctuation(sql, length, pos, '('))
        return false;
    pg_query_simple_insert_gap(sql, length, pos);
    row->integer_location = (int) *pos;
    row->negative = *pos < length && sql[*pos] == '-';
    row->is_float = false;
    if (row->negative)
    {
        ++*pos;
        pg_query_simple_insert_gap(sql, length, pos);
    }
    if (*pos == length || sql[*pos] < '0' || sql[*pos] > '9')
        return false;
    row->digits.start = *pos;
    do
    {
        unsigned digit = (unsigned) (sql[(*pos)++] - '0');
        if (!row->is_float)
        {
            if (number > ((uint32) INT32_MAX - digit) / 10U)
                row->is_float = true;
            else
                number = number * 10U + digit;
        }
    }
    while (*pos < length && sql[*pos] >= '0' && sql[*pos] <= '9');
    row->digits.length = *pos - row->digits.start;
    if (row->is_float &&
        !pg_query_simple_insert_number_size(row->digits.length, row->negative))
        return false;
    /* The scanner classifies the unsigned token before grammar doNegate.
     * Thus -0 is Integer 0, but even -2147483648 is a spelling-preserving
     * Float. Never convert the signed mathematical value back to int32. */
    row->integer = row->negative ? -(int32) number : (int32) number;

    if (!pg_query_simple_insert_punctuation(sql, length, pos, ','))
        return false;
    pg_query_simple_insert_gap(sql, length, pos);
    if (*pos == length || sql[*pos] != '\'')
        return false;
    row->string_location = (int) *pos;
    row->text.start = ++*pos;
    while (*pos < length && sql[*pos] != '\'')
    {
        unsigned char c = (unsigned char) sql[(*pos)++];
        if (c < 0x20U || c > 0x7eU || c == '\\')
            return false;
    }
    if (*pos == length)
        return false;
    row->text.length = *pos - row->text.start;
    if (!pg_query_simple_insert_string_size(row->text.length))
        return false;
    ++*pos;
    /* Doubled quotes and newline-separated continued literals both fail
     * this punctuation check and are left to the original scanner. */
    return pg_query_simple_insert_punctuation(sql, length, pos, ')');
}

static bool
pg_query_simple_insert_certify(const char *sql, PgQuerySimpleInsertSource *source)
{
    PgQuerySimpleInsertRow row;
    size_t pos = 0;
    unsigned i;

    source->length = strlen(sql);
    /* Preserve the caller's large-input gate. Also stay below the native
     * scanner's source allocation bound and every signed location bound. */
    if (!pg_query_simple_insert_source_size(source->length))
        return false;
    if (!pg_query_simple_insert_word(sql, source->length, &pos, "insert", 6U) ||
        !pg_query_simple_insert_word(sql, source->length, &pos, "into", 4U) ||
        !pg_query_simple_insert_name(sql, source->length, &pos, &source->names[0]) ||
        !pg_query_simple_insert_punctuation(sql, source->length, &pos, '('))
        return false;
    for (i = 1; i <= 2; ++i)
    {
        if (!pg_query_simple_insert_name(sql, source->length, &pos, &source->names[i]) ||
            !pg_query_simple_insert_punctuation(sql, source->length, &pos,
                                               i == 1 ? ',' : ')'))
            return false;
    }
    if (!pg_query_simple_insert_word(sql, source->length, &pos, "values", 6U))
        return false;
    source->values_start = pos;
    source->rows = 0;
    source->statement_length = 0;
    for (;;)
    {
        if (!pg_query_simple_insert_row(sql, source->length, &pos, &row))
            return false;
        ++source->rows;
        /* Leave room for List's geometric capacity growth and signed count.
         * Oversize sources receive precisely the original parser behavior. */
        if (!pg_query_simple_insert_rows_size(source->rows))
            return false;
        pg_query_simple_insert_gap(sql, source->length, &pos);
        if (pos == source->length || sql[pos] != ',')
            break;
        ++pos;
    }
    if (source->rows < 32U)
        return false;
    if (pos < source->length && sql[pos] == ';')
    {
        source->statement_length = (int) pos++;
        pg_query_simple_insert_gap(sql, source->length, &pos);
    }
    return pos == source->length;
}

static List *
pg_query_try_simple_insert(const char *sql)
{
    PgQuerySimpleInsertSource source;
    PgQuerySimpleInsertRow row;
    RawStmt *raw;
    InsertStmt *insert;
    SelectStmt *select;
    size_t pos, index;
    unsigned i;

    if (!pg_query_simple_insert_certify(sql, &source))
        return NIL;

    /* From here there are no recognition fallbacks. Native allocations are
     * owned by the existing guarded parse context, just like gram.c nodes. */
    raw = makeNode(RawStmt);
    insert = makeNode(InsertStmt);
    select = makeNode(SelectStmt);
    raw->stmt = (Node *) insert;
    raw->stmt_location = 0;
    raw->stmt_len = source.statement_length;
    insert->relation = makeRangeVar(NULL,
        pnstrdup(sql + source.names[0].start, source.names[0].length),
        (int) source.names[0].start);
    for (i = 1; i <= 2; ++i)
    {
        ResTarget *column = makeNode(ResTarget);
        column->name = pnstrdup(sql + source.names[i].start, source.names[i].length);
        column->location = (int) source.names[i].start;
        insert->cols = lappend(insert->cols, column);
    }
    insert->selectStmt = (Node *) select;
    pos = source.values_start;
    for (index = 0; index < source.rows; ++index)
    {
        A_Const *integer;
        Node *string;
        if ((index != 0 &&
             !pg_query_simple_insert_punctuation(sql, source.length, &pos, ',')) ||
            !pg_query_simple_insert_row(sql, source.length, &pos, &row))
            elog(ERROR, "certified INSERT source changed during native construction");
        integer = makeNode(A_Const);
        if (row.is_float)
        {
            char *number = pnstrdup(sql + row.digits.start, row.digits.length);
            integer->val.fval.type = T_Float;
            integer->val.fval.fval = row.negative ? psprintf("-%s", number) : number;
        }
        else
        {
            integer->val.ival.type = T_Integer;
            integer->val.ival.ival = row.integer;
        }
        integer->location = row.integer_location;
        string = makeStringConst(pnstrdup(sql + row.text.start, row.text.length),
                                 row.string_location);
        select->valuesLists = lappend(select->valuesLists, list_make2(integer, string));
    }
    return list_make1(raw);
}
