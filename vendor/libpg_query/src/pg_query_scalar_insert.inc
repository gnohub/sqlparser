/* Private, positively proved scalar VALUES admission.  The preceding narrow
 * two-column shortcut is intentionally unchanged.  This separate recognizer
 * accepts only a subset of the actual native grammar and allocates nothing
 * until the complete source has passed.  All ambiguity goes to that grammar.
 *
 * Shared lexical and size predicates come from pg_query_simple_insert.inc.
 * Default parser options and spelling preservation are checked by the caller.
 */
#include "mb/pg_wchar.h"

typedef enum PgQueryScalarInsertKind
{
    PG_QUERY_SCALAR_INTEGER,
    PG_QUERY_SCALAR_FLOAT,
    PG_QUERY_SCALAR_STRING,
    PG_QUERY_SCALAR_SQL_VALUE
} PgQueryScalarInsertKind;

typedef struct PgQueryScalarInsertCell
{
    PgQueryScalarInsertKind kind;
    PgQuerySimpleInsertSlice text;
    int location;
    int32 integer;
    bool negative;
    SQLValueFunctionOp op;
    int32 typmod;
} PgQueryScalarInsertCell;

typedef struct PgQueryScalarInsertSource
{
    PgQuerySimpleInsertSlice relation[3];
    size_t relation_parts;
    size_t length;
    size_t columns_start;
    size_t columns;
    size_t values_start;
    size_t rows;
    size_t strings;
    size_t graph_text_bytes;
    bool graph_compatible;
    int statement_length;
} PgQueryScalarInsertSource;

static bool
pg_query_scalar_insert_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool
pg_query_scalar_insert_number(const char *sql, size_t length, size_t *pos,
                              PgQueryScalarInsertCell *cell)
{
    uint32 number = 0;
    size_t digits;
    bool is_float = false;

    cell->location = (int) *pos;
    cell->negative = sql[*pos] == '-';
    if (cell->negative)
    {
        ++*pos;
        pg_query_simple_insert_gap(sql, length, pos);
    }
    cell->text.start = *pos;
    digits = *pos;
    while (*pos < length && pg_query_scalar_insert_digit(sql[*pos]))
    {
        unsigned digit = (unsigned) (sql[(*pos)++] - '0');
        if (!is_float)
        {
            if (number > ((uint32) INT32_MAX - digit) / 10U)
                is_float = true;
            else
                number = number * 10U + digit;
        }
    }
    if (*pos < length && sql[*pos] == '.')
    {
        bool preceding_digits = *pos != digits;
        is_float = true;
        ++*pos;
        digits = *pos;
        while (*pos < length && pg_query_scalar_insert_digit(sql[*pos]))
            ++*pos;
        if (!preceding_digits && *pos == digits)
            return false;
    }
    else if (*pos == digits)
        return false;
    if (*pos < length && (sql[*pos] == 'e' || sql[*pos] == 'E'))
    {
        is_float = true;
        ++*pos;
        if (*pos < length && (sql[*pos] == '+' || sql[*pos] == '-'))
            ++*pos;
        digits = *pos;
        while (*pos < length && pg_query_scalar_insert_digit(sql[*pos]))
            ++*pos;
        if (*pos == digits)
            return false;
    }
    cell->text.length = *pos - cell->text.start;
    if (is_float &&
        !pg_query_simple_insert_number_size(cell->text.length, cell->negative))
        return false;
    /* The scanner first classifies the unsigned token, then doNegate changes
     * its value and location.  In particular -2147483648 remains a Float. */
    cell->kind = is_float ? PG_QUERY_SCALAR_FLOAT : PG_QUERY_SCALAR_INTEGER;
    cell->integer = cell->negative ? -(int32) number : (int32) number;
    return true;
}

static bool
pg_query_scalar_insert_string(const char *sql, size_t length, size_t *pos,
                              PgQueryScalarInsertCell *cell)
{
    bool non_ascii = false;

    cell->kind = PG_QUERY_SCALAR_STRING;
    cell->location = (int) *pos;
    cell->text.start = ++*pos;
    while (*pos < length && sql[*pos] != '\'')
    {
        unsigned char c = (unsigned char) sql[(*pos)++];
        if (c < 0x20U || c == 0x7fU || c == '\\')
            return false;
        non_ascii = non_ascii || c >= 0x80U;
    }
    if (*pos == length)
        return false;
    cell->text.length = *pos - cell->text.start;
    if (!pg_query_simple_insert_string_size(cell->text.length))
        return false;
    /* A no-error encoding check is allocation-free.  Invalid UTF-8 is a
     * recognition miss, leaving the real scanner's encoding/error behavior
     * intact, including builds using SQL_ASCII as their database encoding. */
    if (non_ascii && !pg_verify_mbstr(PG_UTF8, sql + cell->text.start,
                                     (int) cell->text.length, true))
        return false;
    ++*pos;
    return true;
}

static bool
pg_query_scalar_insert_sql_value(const char *sql, size_t length, size_t *pos,
                                 PgQueryScalarInsertCell *cell)
{
    static const struct
    {
        const char *word;
        size_t length;
        SQLValueFunctionOp op;
        SQLValueFunctionOp precision_op;
        bool precision;
    } words[] = {
        {"current_timestamp", 17U, SVFOP_CURRENT_TIMESTAMP, SVFOP_CURRENT_TIMESTAMP_N, true},
        {"current_date", 12U, SVFOP_CURRENT_DATE, SVFOP_CURRENT_DATE, false},
        {"current_time", 12U, SVFOP_CURRENT_TIME, SVFOP_CURRENT_TIME_N, true},
        {"localtimestamp", 14U, SVFOP_LOCALTIMESTAMP, SVFOP_LOCALTIMESTAMP_N, true},
        {"localtime", 9U, SVFOP_LOCALTIME, SVFOP_LOCALTIME_N, true},
        {"current_role", 12U, SVFOP_CURRENT_ROLE, SVFOP_CURRENT_ROLE, false},
        {"current_user", 12U, SVFOP_CURRENT_USER, SVFOP_CURRENT_USER, false},
        {"session_user", 12U, SVFOP_SESSION_USER, SVFOP_SESSION_USER, false},
        {"user", 4U, SVFOP_USER, SVFOP_USER, false},
        {"current_catalog", 15U, SVFOP_CURRENT_CATALOG, SVFOP_CURRENT_CATALOG, false},
        {"current_schema", 14U, SVFOP_CURRENT_SCHEMA, SVFOP_CURRENT_SCHEMA, false}
    };
    size_t i;
    cell->kind = PG_QUERY_SCALAR_SQL_VALUE;
    cell->location = (int) *pos;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
    {
        size_t next = *pos;
        if (pg_query_simple_insert_word(sql, length, &next,
                                       words[i].word, words[i].length))
        {
            cell->op = words[i].op;
            cell->typmod = -1;
            cell->text.start = (size_t) cell->location;
            cell->text.length = words[i].length;
            pg_query_simple_insert_gap(sql, length, &next);
            if (words[i].precision && next < length && sql[next] == '(')
            {
                PgQueryScalarInsertCell precision;
                ++next;
                pg_query_simple_insert_gap(sql, length, &next);
                /* gram.y permits Iconst only: no signs, overflow, decimals,
                 * or expression.  Do not impose parse-analysis limits here. */
                if (next == length || !pg_query_scalar_insert_digit(sql[next]) ||
                    !pg_query_scalar_insert_number(sql, length, &next, &precision) ||
                    precision.kind != PG_QUERY_SCALAR_INTEGER ||
                    !pg_query_simple_insert_punctuation(sql, length, &next, ')'))
                    return false;
                cell->op = words[i].precision_op;
                cell->typmod = precision.integer;
            }
            *pos = next;
            return true;
        }
    }
    return false;
}

static bool
pg_query_scalar_insert_cell(const char *sql, size_t length, size_t *pos,
                            PgQueryScalarInsertCell *cell)
{
    pg_query_simple_insert_gap(sql, length, pos);
    if (*pos == length)
        return false;
    if (sql[*pos] == '\'')
        return pg_query_scalar_insert_string(sql, length, pos, cell);
    if (pg_query_scalar_insert_digit(sql[*pos]) || sql[*pos] == '-' || sql[*pos] == '.')
        return pg_query_scalar_insert_number(sql, length, pos, cell);
    return pg_query_scalar_insert_sql_value(sql, length, pos, cell);
}

static bool
pg_query_scalar_insert_certify(const char *sql, PgQueryScalarInsertSource *source)
{
    PgQuerySimpleInsertSlice name;
    PgQueryScalarInsertCell cell;
    size_t pos = 0;
    size_t i;

    source->graph_text_bytes = 0U;
    source->graph_compatible = true;
    source->length = strlen(sql);
    if (!pg_query_simple_insert_source_size(source->length) ||
        !pg_query_simple_insert_word(sql, source->length, &pos, "insert", 6U) ||
        !pg_query_simple_insert_word(sql, source->length, &pos, "into", 4U))
        return false;
    source->relation_parts = 0;
    do
    {
        if (source->relation_parts == 3U ||
            !pg_query_simple_insert_name(sql, source->length, &pos,
                &source->relation[source->relation_parts]))
            return false;
        ++source->relation_parts;
        if (source->relation[source->relation_parts - 1U].length >=
            SIZE_MAX - source->graph_text_bytes)
            source->graph_compatible = false;
        else
            source->graph_text_bytes += source->relation[source->relation_parts - 1U].length + 1U;
        pg_query_simple_insert_gap(sql, source->length, &pos);
        if (pos == source->length || sql[pos] != '.')
            break;
        ++pos;
    } while (true);
    if (!pg_query_simple_insert_punctuation(sql, source->length, &pos, '('))
        return false;
    source->columns_start = pos;
    source->columns = 0;
    for (;;)
    {
        if (!pg_query_simple_insert_name(sql, source->length, &pos, &name))
            return false;
        ++source->columns;
        if (name.length >= SIZE_MAX - source->graph_text_bytes)
            source->graph_compatible = false;
        else
            source->graph_text_bytes += name.length + 1U;
        if (!pg_query_simple_insert_rows_size(source->columns))
            return false;
        pg_query_simple_insert_gap(sql, source->length, &pos);
        if (pos == source->length || sql[pos] != ',')
            break;
        ++pos;
    }
    if (!pg_query_simple_insert_punctuation(sql, source->length, &pos, ')') ||
        !pg_query_simple_insert_word(sql, source->length, &pos, "values", 6U))
        return false;
    source->values_start = pos;
    source->rows = 0;
    source->strings = 0;
    source->statement_length = 0;
    for (;;)
    {
        if (!pg_query_simple_insert_punctuation(sql, source->length, &pos, '('))
            return false;
        for (i = 0; i < source->columns; ++i)
        {
            if (!pg_query_scalar_insert_cell(sql, source->length, &pos, &cell) ||
                !pg_query_simple_insert_punctuation(sql, source->length, &pos,
                    i + 1U == source->columns ? ')' : ','))
                return false;
            if (cell.kind == PG_QUERY_SCALAR_STRING)
            {
                if (source->strings == SIZE_MAX)
                    return false;
                ++source->strings;
            }
            /* The initial wire graph admits only unsigned, digit-leading
             * numeric tokens and SQL value functions without precision.
             * Native parsing intentionally remains broader than this proof. */
            if (((cell.kind == PG_QUERY_SCALAR_INTEGER || cell.kind == PG_QUERY_SCALAR_FLOAT) &&
                 (cell.negative || !pg_query_scalar_insert_digit(sql[cell.text.start]))) ||
                (cell.kind == PG_QUERY_SCALAR_SQL_VALUE && cell.typmod != -1))
                source->graph_compatible = false;
            if (cell.kind != PG_QUERY_SCALAR_INTEGER)
            {
                if (cell.text.length >= SIZE_MAX - source->graph_text_bytes)
                    source->graph_compatible = false;
                else
                    source->graph_text_bytes += cell.text.length + 1U;
            }
        }
        ++source->rows;
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

static Node *
pg_query_scalar_insert_make_cell(const char *sql, const PgQueryScalarInsertCell *cell)
{
    A_Const *constant;
    if (cell->kind == PG_QUERY_SCALAR_SQL_VALUE)
    {
        SQLValueFunction *value = makeNode(SQLValueFunction);
        value->op = cell->op;
        value->typmod = cell->typmod;
        value->location = cell->location;
        return (Node *) value;
    }
    constant = makeNode(A_Const);
    constant->location = cell->location;
    if (cell->kind == PG_QUERY_SCALAR_INTEGER)
    {
        constant->val.ival.type = T_Integer;
        constant->val.ival.ival = cell->integer;
    }
    else if (cell->kind == PG_QUERY_SCALAR_FLOAT)
    {
        char *number = pnstrdup(sql + cell->text.start, cell->text.length);
        constant->val.fval.type = T_Float;
        constant->val.fval.fval = cell->negative ? psprintf("-%s", number) : number;
    }
    else
    {
        constant->val.sval.type = T_String;
        constant->val.sval.sval = pnstrdup(sql + cell->text.start, cell->text.length);
    }
    return (Node *) constant;
}

static List *
pg_query_try_scalar_insert(const char *sql, PgQueryNativeScalarInsertProof *proof)
{
    PgQueryScalarInsertSource source;
    PgQueryScalarInsertCell cell;
    PgQuerySimpleInsertSlice name;
    RawStmt *raw;
    InsertStmt *insert;
    SelectStmt *select;
    RangeVar *relation;
    size_t pos, row, column;
    char *names[3];

    if (!pg_query_scalar_insert_certify(sql, &source))
        return NIL;
    /* No fallbacks after proof.  Every allocation belongs to the caller's
     * existing parse memory context and follows its PG_TRY cleanup path. */
    raw = makeNode(RawStmt);
    insert = makeNode(InsertStmt);
    select = makeNode(SelectStmt);
    raw->stmt = (Node *) insert;
    raw->stmt_location = 0;
    raw->stmt_len = source.statement_length;
    for (column = 0; column < source.relation_parts; ++column)
        names[column] = pnstrdup(sql + source.relation[column].start,
                                source.relation[column].length);
    relation = makeRangeVar(NULL, names[source.relation_parts - 1U],
                            (int) source.relation[0].start);
    if (source.relation_parts >= 2U)
        relation->schemaname = names[source.relation_parts - 2U];
    if (source.relation_parts == 3U)
        relation->catalogname = names[0];
    insert->relation = relation;
    pos = source.columns_start;
    for (column = 0; column < source.columns; ++column)
    {
        ResTarget *target;
        if (!pg_query_simple_insert_name(sql, source.length, &pos, &name) ||
            !pg_query_simple_insert_punctuation(sql, source.length, &pos,
                column + 1U == source.columns ? ')' : ','))
            elog(ERROR, "certified scalar INSERT columns changed during construction");
        target = makeNode(ResTarget);
        target->name = pnstrdup(sql + name.start, name.length);
        target->location = (int) name.start;
        insert->cols = lappend(insert->cols, target);
    }
    insert->selectStmt = (Node *) select;
    pos = source.values_start;
    for (row = 0; row < source.rows; ++row)
    {
        List *values = NIL;
        if ((row != 0 && !pg_query_simple_insert_punctuation(sql, source.length, &pos, ',')) ||
            !pg_query_simple_insert_punctuation(sql, source.length, &pos, '('))
            elog(ERROR, "certified scalar INSERT row changed during construction");
        for (column = 0; column < source.columns; ++column)
        {
            if (!pg_query_scalar_insert_cell(sql, source.length, &pos, &cell) ||
                !pg_query_simple_insert_punctuation(sql, source.length, &pos,
                    column + 1U == source.columns ? ')' : ','))
                elog(ERROR, "certified scalar INSERT cell changed during construction");
            values = lappend(values, pg_query_scalar_insert_make_cell(sql, &cell));
        }
        select->valuesLists = lappend(select->valuesLists, values);
    }
    {
        List *result = list_make1(raw);
        /* Commit only after every native allocation and construction check. */
        if (proof != NULL && source.graph_compatible)
        {
            proof->source_length = source.length;
            proof->row_count = source.rows;
            proof->column_count = source.columns;
            proof->string_count = source.strings;
            proof->text_bytes = source.graph_text_bytes;
            proof->statement_length = source.statement_length;
        }
        return result;
    }
}
