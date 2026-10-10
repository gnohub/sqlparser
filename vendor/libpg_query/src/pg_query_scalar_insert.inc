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
    size_t statement_start;
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
pg_query_scalar_insert_certify_statement(const char *sql, size_t length,
    size_t start, bool require_statement_size, PgQueryScalarInsertSource *source,
    size_t *next)
{
    PgQuerySimpleInsertSlice name;
    PgQueryScalarInsertCell cell;
    size_t pos = start;
    size_t i;

    source->graph_text_bytes = 0U;
    source->graph_compatible = true;
    source->length = length;
    source->statement_start = start;
    if (start > length || !pg_query_simple_insert_source_size(source->length) ||
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
        source->statement_length = (int) (pos - start);
        ++pos;
    }
    else if (pos != source->length)
        return false;
    /* Batch slices use the same conservative minimum as identity admission.
     * Singleton admission keeps its historical whole-source size behavior,
     * including padding after its optional final semicolon. */
    if (require_statement_size && !pg_query_simple_insert_source_size(pos - start))
        return false;
    *next = pos;
    return true;
}

static bool
pg_query_scalar_insert_certify(const char *sql, PgQueryScalarInsertSource *source)
{
    size_t length = strlen(sql), next;
    if (!pg_query_scalar_insert_certify_statement(sql, length, 0U, false, source, &next))
        return false;
    pg_query_simple_insert_gap(sql, length, &next);
    return next == length;
}

/* The entire immutable source must pass before even a native list is
 * allocated. An empty/mixed/ambiguous final slice invalidates the whole batch;
 * the caller then parses the original source with the ordinary grammar. */
static bool
pg_query_scalar_insert_batch_certify(const char *sql, size_t *statement_count,
    PgQueryScalarInsertSource *sources, size_t source_capacity)
{
    PgQueryScalarInsertSource source;
    size_t length = strlen(sql), start = 0U, count = 0U;
    *statement_count = 0U;
    if (!pg_query_simple_insert_source_size(length))
        return false;
    for (;;)
    {
        size_t next, probe;
        if (!pg_query_scalar_insert_certify_statement(sql, length, start, true, &source, &next) ||
            !pg_query_simple_insert_rows_size(++count))
            return false;
        /* Private caller-stack snapshots only. No descriptor may be consumed
         * until the complete immutable source succeeds and publishes count. */
        if (count <= source_capacity)
            sources[count - 1U] = source;
        probe = next;
        pg_query_simple_insert_gap(sql, length, &probe);
        if (probe == length)
            break;
        start = next;
    }
    if (count < 2U)
        return false;
    *statement_count = count;
    return true;
}

/* Return a request size only when text stays in the original node class.
 * Kept separate so overflow/capacity edges need no enormous source fixture. */
static Size
pg_query_scalar_insert_inline_text_bytes(const PgQueryScalarInsertCell *cell,
    Size text_capacity)
{
    if (text_capacity <= sizeof(A_Const) || text_capacity > MaxAllocSize ||
        (cell->kind != PG_QUERY_SCALAR_STRING &&
         (cell->kind != PG_QUERY_SCALAR_FLOAT || cell->negative)) ||
        sizeof(A_Const) >= MaxAllocSize ||
        cell->text.length > MaxAllocSize - sizeof(A_Const) - 1U ||
        cell->text.length >= text_capacity - sizeof(A_Const))
        return 0U;
    return sizeof(A_Const) + cell->text.length + 1U;
}

static Node *
pg_query_scalar_insert_make_cell(const char *sql, const PgQueryScalarInsertCell *cell,
    Size text_capacity)
{
    A_Const *constant;
    /* Only an owned-plan caller supplies the current AllocSet's real class.
     * Small text may use its existing slack without growing the node's chunk.
     * The interior string is borrowed by synchronous serializers and released
     * only with the complete parse context; never independently pfree it. */
    Size bytes = pg_query_scalar_insert_inline_text_bytes(cell, text_capacity);
    if (bytes != 0U)
    {
        char *text;
        constant = palloc0(bytes);
        constant->type = T_A_Const;
        constant->location = cell->location;
        text = (char *) constant + sizeof(A_Const);
        memcpy(text, sql + cell->text.start, cell->text.length);
        text[cell->text.length] = '\0';
        if (cell->kind == PG_QUERY_SCALAR_FLOAT)
        {
            constant->val.fval.type = T_Float;
            constant->val.fval.fval = text;
        }
        else
        {
            constant->val.sval.type = T_String;
            constant->val.sval.sval = text;
        }
        return (Node *) constant;
    }
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

/* Complete-source admission has already proved every list length. Allocate
 * its final native List layout once rather than repeatedly growing each row.
 * This is ordinary context-owned storage, including the inline ListCells;
 * no node or cell escapes the parser context. Keep the conservative admission
 * bounds used by the lappend constructor, even though exact storage needs less.
 * Callers fill every cell before any serializer can inspect the tree. */
static List *
pg_query_scalar_insert_list(size_t count)
{
    List *list;
    Size bytes;

    if (count == 0U || !pg_query_simple_insert_rows_size(count) ||
        count > (MaxAllocSize - offsetof(List, initial_elements)) / sizeof(ListCell))
        elog(ERROR, "certified scalar INSERT list size is invalid");
    bytes = offsetof(List, initial_elements) + count * sizeof(ListCell);
    list = palloc(bytes);
    list->type = T_List;
    list->length = (int) count;
    list->max_length = (int) count;
    list->elements = list->initial_elements;
    return list;
}

static RawStmt *
pg_query_scalar_insert_construct_with_text_capacity(const char *sql,
    const PgQueryScalarInsertSource *proved, Size text_capacity)
{
    const PgQueryScalarInsertSource source = *proved;
    PgQueryScalarInsertCell cell;
    PgQuerySimpleInsertSlice name;
    RawStmt *raw;
    InsertStmt *insert;
    SelectStmt *select;
    RangeVar *relation;
    size_t pos, row, column;
    char *names[3];

    /* No fallbacks after proof.  Every allocation belongs to the caller's
     * existing parse memory context and follows its PG_TRY cleanup path. */
    raw = makeNode(RawStmt);
    insert = makeNode(InsertStmt);
    select = makeNode(SelectStmt);
    raw->stmt = (Node *) insert;
    raw->stmt_location = (int) source.statement_start;
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
    insert->cols = pg_query_scalar_insert_list(source.columns);
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
        insert->cols->elements[column].ptr_value = target;
    }
    insert->selectStmt = (Node *) select;
    select->valuesLists = pg_query_scalar_insert_list(source.rows);
    pos = source.values_start;
    for (row = 0; row < source.rows; ++row)
    {
        List *values = pg_query_scalar_insert_list(source.columns);
        if ((row != 0 && !pg_query_simple_insert_punctuation(sql, source.length, &pos, ',')) ||
            !pg_query_simple_insert_punctuation(sql, source.length, &pos, '('))
            elog(ERROR, "certified scalar INSERT row changed during construction");
        for (column = 0; column < source.columns; ++column)
        {
            if (!pg_query_scalar_insert_cell(sql, source.length, &pos, &cell) ||
                !pg_query_simple_insert_punctuation(sql, source.length, &pos,
                    column + 1U == source.columns ? ')' : ','))
                elog(ERROR, "certified scalar INSERT cell changed during construction");
            values->elements[column].ptr_value = pg_query_scalar_insert_make_cell(sql, &cell, text_capacity);
        }
        select->valuesLists->elements[row].ptr_value = values;
    }
    return raw;
}

/* Legacy singleton and batch constructors retain their original layout. */
static RawStmt *
pg_query_scalar_insert_construct(const char *sql, const PgQueryScalarInsertSource *proved)
{
    return pg_query_scalar_insert_construct_with_text_capacity(sql, proved, 0U);
}

static List *
pg_query_scalar_insert_from_source(const char *sql,
    const PgQueryScalarInsertSource *source, PgQueryNativeScalarInsertProof *proof,
    bool owned_mysql_layout)
{
    Size text_capacity = owned_mysql_layout ?
        pg_query_allocset_same_class_capacity(CurrentMemoryContext, sizeof(A_Const)) : 0U;
    List *result = list_make1(pg_query_scalar_insert_construct_with_text_capacity(
        sql, source, text_capacity));
    /* Commit only after every native allocation and construction check. */
    if (proof != NULL && source->graph_compatible)
    {
        proof->source_length = source->length;
        proof->row_count = source->rows;
        proof->column_count = source->columns;
        proof->string_count = source->strings;
        proof->text_bytes = source->graph_text_bytes;
        proof->statement_length = source->statement_length;
    }
    return result;
}

static List *
pg_query_try_scalar_insert(const char *sql, PgQueryNativeScalarInsertProof *proof)
{
    PgQueryScalarInsertSource source;
    if (!pg_query_scalar_insert_certify(sql, &source))
        return NIL;
    return pg_query_scalar_insert_from_source(sql, &source, proof, false);
}

/* Dedicated batch caller only. No singleton proof is minted. The optional
 * batch candidate uses only this call's complete-source descriptors and is
 * not published to the caller until canonical serialization succeeds. */
static List *
pg_query_try_scalar_insert_batch(const char *sql, PgQueryNativeScalarInsertBatchProof *proof)
{
    PgQueryScalarInsertSource sources[PG_QUERY_NATIVE_SCALAR_BATCH_MAX_STATEMENTS];
    PgQueryScalarInsertSource source;
    List *result;
    size_t count, length, start = 0U, statement;
    if (!pg_query_scalar_insert_batch_certify(sql, &count, sources,
            sizeof(sources) / sizeof(sources[0])))
        return NIL;
    length = strlen(sql);
    result = pg_query_scalar_insert_list(count);
    if (count <= sizeof(sources) / sizeof(sources[0]))
    {
        /* Every descriptor came from this call's complete independent proof.
         * Keep the checked constructor, original offsets and legacy layout. */
        for (statement = 0U; statement < count; ++statement)
            result->elements[statement].ptr_value =
                pg_query_scalar_insert_construct(sql, &sources[statement]);
        if (proof != NULL)
        {
            for (statement = 0U; statement < count; ++statement)
                if (!sources[statement].graph_compatible)
                    return result;
            /* All native allocations/checks completed. No source or context
             * pointer escapes; unused fixed-capacity entries remain zero. */
            proof->source_length = length;
            for (statement = 0U; statement < count; ++statement)
            {
                const PgQueryScalarInsertSource *source = &sources[statement];
                PgQueryNativeScalarInsertBatchStatementProof *out = &proof->statements[statement];
                out->raw_start = source->statement_start;
                out->raw_length = source->statement_length;
                out->row_count = source->rows;
                out->column_count = source->columns;
                out->string_count = source->strings;
                out->text_bytes = source->graph_text_bytes;
            }
            proof->statement_count = count;
        }
        return result;
    }
    /* Oversized batches retain the original full recertification path. */
    for (statement = 0U; statement < count; ++statement)
    {
        size_t next;
        if (!pg_query_scalar_insert_certify_statement(sql, length, start, true, &source, &next))
            elog(ERROR, "certified scalar INSERT batch changed during construction");
        result->elements[statement].ptr_value = pg_query_scalar_insert_construct(sql, &source);
        start = next;
    }
    return result;
}
