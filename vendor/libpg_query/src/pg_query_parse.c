#include "pg_query.h"
#include "pg_query_internal.h"
#include "pg_query_outfuncs.h"
#include "pg_query_observer.h"

#include "parser/parser.h"
#include "parser/scanner.h"
#include "parser/scansup.h"

#include <unistd.h>
#include <fcntl.h>

#include "pg_query_simple_insert.inc"
#include "pg_query_scalar_insert.inc"

/* PostgreSQL accepts these unquoted names, while the existing MySQL
 * preprocessor rejects or rewrites them even in identifier positions. Keep
 * that behavior authoritative rather than expanding accepted dialect SQL.
 * Occurrences inside proved ordinary string tokens are harmless. */
static int
pg_query_mysql_identity_name(const char *name, size_t length)
{
    static const struct { const char *word; size_t length; } excluded[] = {
        {"auto_increment", 14U}, {"unsigned", 8U},
        {"zerofill", 8U}, {"straight_join", 13U}
    };
    size_t i;
    for (i = 0; i < sizeof(excluded) / sizeof(excluded[0]); ++i)
    {
        size_t pos = 0U;
        if (length == excluded[i].length &&
            pg_query_simple_insert_word(name, length, &pos,
                excluded[i].word, excluded[i].length))
            return false;
    }
    return true;
}

int
pg_query_prove_identity_scalar_insert(
    const char *input, PgQueryIdentityScalarInsertNamePredicate name_predicate,
    PgQueryIdentityScalarInsertProof *proof)
{
    PgQueryScalarInsertSource source;
    PgQuerySimpleInsertSlice name;
    size_t i, pos;

    if (proof == NULL)
        return 0;
    memset(proof, 0, sizeof(*proof));
    if (input == NULL || name_predicate == NULL)
        return 0;
    /* A prefix is only a cheap rejection filter. Avoid strlen over a large
     * SELECT, and reject short input with a bounded scan. Acceptance still
     * requires the independent complete-source proof below. */
    pos = 0;
    while (scanner_isspace(input[pos]))
        ++pos;
    for (i = 0; i < 6U; ++i)
    {
        unsigned char c = (unsigned char) input[pos + i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != (unsigned char) "insert"[i])
            return 0;
    }
    if (strnlen(input, 4096U) < 4096U ||
        !pg_query_scalar_insert_certify(input, &source))
        return 0;
    for (i = 0; i < source.relation_parts; ++i)
        if (!name_predicate(input + source.relation[i].start,
                            source.relation[i].length))
            return 0;
    pos = source.columns_start;
    for (i = 0; i < source.columns; ++i)
        if (!pg_query_simple_insert_name(input, source.length, &pos, &name) ||
            !name_predicate(input + name.start, name.length) ||
            !pg_query_simple_insert_punctuation(input, source.length, &pos,
                i + 1U == source.columns ? ')' : ','))
            return 0;
    proof->source_length = source.length;
    proof->row_count = source.rows;
    proof->column_count = source.columns;
    proof->string_count = source.strings;
    proof->statement_length = source.statement_length;
    return 1;
}

int
pg_query_prove_mysql_identity_scalar_insert(
    const char *input, PgQueryIdentityScalarInsertProof *proof)
{
    return pg_query_prove_identity_scalar_insert(
        input, pg_query_mysql_identity_name, proof);
}

static bool
pg_query_identity_insert_source_names(const char *input,
    const PgQueryScalarInsertSource *source,
    PgQueryIdentityScalarInsertNamePredicate name_predicate)
{
    PgQuerySimpleInsertSlice name;
    size_t i, pos = source->columns_start;
    for (i = 0U; i < source->relation_parts; ++i)
        if (!name_predicate(input + source->relation[i].start,
                            source->relation[i].length))
            return false;
    for (i = 0U; i < source->columns; ++i)
        if (!pg_query_simple_insert_name(input, source->length, &pos, &name) ||
            !name_predicate(input + name.start, name.length) ||
            !pg_query_simple_insert_punctuation(input, source->length, &pos,
                i + 1U == source->columns ? ')' : ','))
            return false;
    return true;
}

PgQueryIdentityInsertSequenceKind
pg_query_prove_identity_insert_sequence(const char *input,
    PgQueryIdentityScalarInsertNamePredicate name_predicate,
    PgQueryIdentityInsertSequenceProof *proof)
{
    PgQueryIdentityInsertSequenceProof complete = {0};
    PgQueryScalarInsertSource source;
    size_t length, start = 0U, pos = 0U, i;
    if (proof == NULL)
        return PG_QUERY_IDENTITY_INSERT_NONE;
    memset(proof, 0, sizeof(*proof));
    if (input == NULL || name_predicate == NULL)
        return PG_QUERY_IDENTITY_INSERT_NONE;
    /* Match the old proof's cheap non-INSERT/short-input rejection. */
    while (scanner_isspace(input[pos])) ++pos;
    for (i = 0U; i < 6U; ++i)
    {
        unsigned char c = (unsigned char)input[pos + i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)"insert"[i])
            return PG_QUERY_IDENTITY_INSERT_NONE;
    }
    if (strnlen(input, 4096U) < 4096U)
        return PG_QUERY_IDENTITY_INSERT_NONE;
    length = strlen(input);
    for (;;)
    {
        size_t next, probe;
        if (!pg_query_scalar_insert_certify_statement(input, length, start,
                complete.statement_count != 0U, &source, &next) ||
            next <= start || next > length ||
            !pg_query_identity_insert_source_names(input, &source, name_predicate) ||
            complete.statement_count == SIZE_MAX ||
            source.strings > SIZE_MAX - complete.string_count)
            return PG_QUERY_IDENTITY_INSERT_NONE;
        ++complete.statement_count;
        complete.string_count += source.strings;
        probe = next;
        pg_query_simple_insert_gap(input, length, &probe);
        if (probe == length)
        {
            complete.source_length = length;
            if (complete.statement_count == 1U)
            {
                complete.single.source_length = length;
                complete.single.row_count = source.rows;
                complete.single.column_count = source.columns;
                complete.single.string_count = source.strings;
                complete.single.statement_length = source.statement_length;
            }
            *proof = complete;
            return complete.statement_count == 1U ?
                PG_QUERY_IDENTITY_INSERT_SINGLE : PG_QUERY_IDENTITY_INSERT_BATCH;
        }
        /* Only SINGLE may use padding after its final semicolon to meet the
         * minimum source size. A batch's first slice includes the semicolon,
         * but not the following gap, exactly like the old owned scratch. */
        if (complete.statement_count == 1U &&
            !pg_query_simple_insert_source_size(next - start))
            return PG_QUERY_IDENTITY_INSERT_NONE;
        /* Keep the gap after the preceding semicolon in the next slice. */
        start = next;
    }
}

/* A complete descriptor is meaningful only beside the final immutable owner.
 * memcpy keeps opaque byte storage independent of alignment/effective type. */
typedef struct PgQueryMysqlOwnedScalarInsertPlanData
{
    PgQueryScalarInsertSource source;
    const char *owner;
    size_t length;
    unsigned int valid;
} PgQueryMysqlOwnedScalarInsertPlanData;
#define PG_QUERY_MYSQL_OWNED_PLAN_VALID 0x4d595350U
_Static_assert(sizeof(PgQueryMysqlOwnedScalarInsertPlanData) <=
    sizeof(PgQueryMysqlOwnedScalarInsertPlan), "owned scalar plan storage too small");

int
pg_query_prove_mysql_owned_scalar_insert(const char *owned_input, size_t input_length,
    PgQueryMysqlOwnedScalarInsertPlan *plan, size_t *string_count)
{
    PgQueryMysqlOwnedScalarInsertPlanData data = {0};
    PgQuerySimpleInsertSlice name;
    size_t i, pos;
    if (string_count != NULL) *string_count = 0U;
    if (plan == NULL) return 0;
    memset(plan, 0, sizeof(*plan));
    if (owned_input == NULL || string_count == NULL ||
        !pg_query_scalar_insert_certify(owned_input, &data.source) ||
        data.source.length != input_length) return 0;
    for (i = 0; i < data.source.relation_parts; ++i)
        if (!pg_query_mysql_identity_name(owned_input + data.source.relation[i].start,
                                          data.source.relation[i].length)) return 0;
    pos = data.source.columns_start;
    for (i = 0; i < data.source.columns; ++i)
        if (!pg_query_simple_insert_name(owned_input, data.source.length, &pos, &name) ||
            !pg_query_mysql_identity_name(owned_input + name.start, name.length) ||
            !pg_query_simple_insert_punctuation(owned_input, data.source.length, &pos,
                i + 1U == data.source.columns ? ')' : ',')) return 0;
    data.owner = owned_input;
    data.length = data.source.length;
    data.valid = PG_QUERY_MYSQL_OWNED_PLAN_VALID;
    memcpy(plan->opaque, &data, sizeof(data));
    *string_count = data.source.strings;
    return 1;
}

typedef enum PgQueryInsertConstructorMode
{
    PG_QUERY_INSERT_CONSTRUCTOR_NONE,
    PG_QUERY_INSERT_CONSTRUCTOR_SINGLE,
    PG_QUERY_INSERT_CONSTRUCTOR_BATCH
} PgQueryInsertConstructorMode;

static PgQueryInternalParsetreeAndError
pg_query_raw_parse_with_options(
	const char* input,
	int parser_options,
	bool preserve_identifier_spelling,
	PgQueryInsertConstructorMode constructor_mode,
	PgQueryNativeScalarInsertProof *native_proof,
    const PgQueryScalarInsertSource *owned_source,
    PgQueryNativeScalarInsertBatchProof *batch_proof)
{
	PgQueryInternalParsetreeAndError result = {0};
	MemoryContext parse_context = CurrentMemoryContext;

	char stderr_buffer[STDERR_BUFFER_LEN + 1] = {0};
#ifndef DEBUG
	int stderr_global;
	int stderr_pipe[2];
#endif

#ifndef DEBUG
	// Setup pipe for stderr redirection
	if (pipe(stderr_pipe) != 0) {
		PgQueryError* error = malloc(sizeof(PgQueryError));

		error->message = strdup("Failed to open pipe, too many open file descriptors")

		result.error = error;

		return result;
	}

	fcntl(stderr_pipe[0], F_SETFL, fcntl(stderr_pipe[0], F_GETFL) | O_NONBLOCK);

	// Redirect stderr to the pipe
	stderr_global = dup(STDERR_FILENO);
	dup2(stderr_pipe[1], STDERR_FILENO);
	close(stderr_pipe[1]);
#endif

	PG_TRY();
	{
		RawParseMode rawParseMode = RAW_PARSE_DEFAULT;
		switch (parser_options & PG_QUERY_PARSE_MODE_BITMASK)
		{
			case PG_QUERY_PARSE_TYPE_NAME:
				rawParseMode = RAW_PARSE_TYPE_NAME;
				break;
			case PG_QUERY_PARSE_PLPGSQL_EXPR:
				rawParseMode = RAW_PARSE_PLPGSQL_EXPR;
				break;
			case PG_QUERY_PARSE_PLPGSQL_ASSIGN1:
				rawParseMode = RAW_PARSE_PLPGSQL_ASSIGN1;
				break;
			case PG_QUERY_PARSE_PLPGSQL_ASSIGN2:
				rawParseMode = RAW_PARSE_PLPGSQL_ASSIGN2;
				break;
			case PG_QUERY_PARSE_PLPGSQL_ASSIGN3:
				rawParseMode = RAW_PARSE_PLPGSQL_ASSIGN3;
				break;
		}

		if ((parser_options & PG_QUERY_DISABLE_BACKSLASH_QUOTE) == PG_QUERY_DISABLE_BACKSLASH_QUOTE) {
			backslash_quote = BACKSLASH_QUOTE_OFF;
		} else {
			backslash_quote = BACKSLASH_QUOTE_SAFE_ENCODING;
		}
		standard_conforming_strings = !((parser_options & PG_QUERY_DISABLE_STANDARD_CONFORMING_STRINGS) == PG_QUERY_DISABLE_STANDARD_CONFORMING_STRINGS);
		escape_string_warning = !((parser_options & PG_QUERY_DISABLE_ESCAPE_STRING_WARNING) == PG_QUERY_DISABLE_ESCAPE_STRING_WARNING);

		/* Only private certified callers opt in. Each mode proves the entire
		 * immutable source before allocation; misses use the original grammar.
		 * Batch admission cannot expand any existing singleton caller. */
		if (constructor_mode != PG_QUERY_INSERT_CONSTRUCTOR_NONE &&
			parser_options == PG_QUERY_PARSE_DEFAULT && preserve_identifier_spelling)
		{
			if (constructor_mode == PG_QUERY_INSERT_CONSTRUCTOR_BATCH)
				result.tree = pg_query_try_scalar_insert_batch(input, batch_proof);
			else
			{
                /* Preserve historical simple-first provenance for its only
                 * possible descriptor shape. The complete owned proof already
                 * excludes that grammar for every other shape. */
                if (owned_source == NULL ||
                    (owned_source->relation_parts == 1U && owned_source->columns == 2U))
                    result.tree = pg_query_try_simple_insert(input);
                if (result.tree == NIL)
                    result.tree = owned_source != NULL ?
                        pg_query_scalar_insert_from_source(input, owned_source, native_proof, true) :
                        pg_query_try_scalar_insert(input, native_proof);
			}
		}
		if (result.tree == NIL)
			result.tree = raw_parser_with_options(
				input,
				rawParseMode,
				preserve_identifier_spelling);

		backslash_quote = BACKSLASH_QUOTE_SAFE_ENCODING;
		standard_conforming_strings = true;
		escape_string_warning = true;

#ifndef DEBUG
		// Save stderr for result
		read(stderr_pipe[0], stderr_buffer, STDERR_BUFFER_LEN);
#endif

		result.stderr_buffer = strdup(stderr_buffer);
	}
	PG_CATCH();
	{
		ErrorData* error_data;
		PgQueryError* error;

		MemoryContextSwitchTo(parse_context);
		error_data = CopyErrorData();

		// Note: This is intentionally malloc so exiting the memory context doesn't free this
		error = malloc(sizeof(PgQueryError));
		error->message   = strdup(error_data->message);
		error->filename  = strdup(error_data->filename);
		error->funcname  = strdup(error_data->funcname);
		error->context   = NULL;
		error->lineno    = error_data->lineno;
		error->cursorpos = error_data->cursorpos;

		result.error = error;
		FlushErrorState();
	}
	PG_END_TRY();

#ifndef DEBUG
	// Restore stderr, close pipe
	dup2(stderr_global, STDERR_FILENO);
	close(stderr_pipe[0]);
	close(stderr_global);
#endif

	return result;
}

PgQueryInternalParsetreeAndError pg_query_raw_parse(const char* input, int parser_options)
{
	return pg_query_raw_parse_with_options(input, parser_options, false, PG_QUERY_INSERT_CONSTRUCTOR_NONE, NULL, NULL, NULL);
}

PgQueryParseResult pg_query_parse(const char* input)
{
	return pg_query_parse_opts(input, PG_QUERY_PARSE_DEFAULT);
}

PgQueryParseResult pg_query_parse_opts(const char* input, int parser_options)
{
	MemoryContext ctx = NULL;
	PgQueryInternalParsetreeAndError parsetree_and_error;
	PgQueryParseResult result = {0};
	char *tree_json = NULL;

	ctx = pg_query_enter_memory_context();

	parsetree_and_error = pg_query_raw_parse(input, parser_options);

	// These are all malloc-ed and will survive exiting the memory context, the caller is responsible to free them now
	result.stderr_buffer = parsetree_and_error.stderr_buffer;
	result.error = parsetree_and_error.error;

	tree_json = pg_query_nodes_to_json(parsetree_and_error.tree);
	result.parse_tree = strdup(tree_json);
	pfree(tree_json);

	pg_query_exit_memory_context(ctx);

	return result;
}

PgQueryProtobufParseResult pg_query_parse_protobuf(const char* input)
{
	return pg_query_parse_protobuf_opts(input, PG_QUERY_PARSE_DEFAULT);
}

PgQueryProtobufParseResult pg_query_parse_protobuf_opts(const char* input, int parser_options)
{
	MemoryContext ctx = NULL;
	PgQueryInternalParsetreeAndError parsetree_and_error;
	PgQueryProtobufParseResult result = {0};

	ctx = pg_query_enter_memory_context();

	parsetree_and_error = pg_query_raw_parse(input, parser_options);

	// These are all malloc-ed and will survive exiting the memory context, the caller is responsible to free them now
	result.stderr_buffer = parsetree_and_error.stderr_buffer;
	result.error = parsetree_and_error.error;
	result.parse_tree = pg_query_nodes_to_protobuf(parsetree_and_error.tree);

	pg_query_exit_memory_context(ctx);

	return result;
}

PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling(
	const char* input,
	int parser_options)
{
	return pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
		input, parser_options, NULL, NULL);
}

PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
	const char* input,
	int parser_options,
	PgQueryProtobufObserver observer,
	void *context)
{
	MemoryContext ctx = NULL;
	PgQueryInternalParsetreeAndError parsetree_and_error;
	PgQueryProtobufParseResult result = {0};

	ctx = pg_query_enter_memory_context();

	parsetree_and_error = pg_query_raw_parse_with_options(
		input,
		parser_options,
		true,
		PG_QUERY_INSERT_CONSTRUCTOR_NONE,
		NULL, NULL, NULL);

	// These are all malloc-ed and will survive exiting the memory context, the caller is responsible to free them now
	result.stderr_buffer = parsetree_and_error.stderr_buffer;
	result.error = parsetree_and_error.error;
	/* Parse errors take precedence; never validate a partial/empty error tree. */
	result.parse_tree = pg_query_nodes_to_protobuf_observed(
		parsetree_and_error.tree,
		result.error == NULL ? observer : NULL,
		context);

	pg_query_exit_memory_context(ctx);

	return result;
}


PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified(
    const char *input, int parser_options,
    PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified)
{
    return pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native(
        input, parser_options, observer, context, statement_count, certified, NULL);
}

static PgQueryProtobufParseResult
pg_query_parse_certified_native_internal(
    const char *input, int parser_options,
    PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified,
    PgQueryNativeScalarInsertProof *native_proof,
    const PgQueryScalarInsertSource *owned_source)
{
    MemoryContext ctx;
    PgQueryInternalParsetreeAndError parsed;
    PgQueryProtobufParseResult result = {0};
    *statement_count = 0;
    *certified = 0;
    if (native_proof != NULL)
        memset(native_proof, 0, sizeof(*native_proof));
    ctx = pg_query_enter_memory_context();
    parsed = pg_query_raw_parse_with_options(input, parser_options, true, PG_QUERY_INSERT_CONSTRUCTOR_SINGLE, native_proof, owned_source, NULL);
    result.stderr_buffer = parsed.stderr_buffer;
    result.error = parsed.error;
    result.parse_tree = pg_query_nodes_to_protobuf_certified(parsed.tree,
        result.error == NULL ? observer : NULL, context,
        statement_count, certified);
    /* A recognizer hit is not enough: failure or a backend without the
     * canonical certified writer cannot mint wire provenance. */
    if (native_proof != NULL &&
        (result.error != NULL || result.parse_tree.data == NULL || !*certified))
        memset(native_proof, 0, sizeof(*native_proof));
    pg_query_exit_memory_context(ctx);
    return result;
}

PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native(
    const char *input, int parser_options,
    PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified,
    PgQueryNativeScalarInsertProof *native_proof)
{
    return pg_query_parse_certified_native_internal(input, parser_options,
        observer, context, statement_count, certified, native_proof, NULL);
}

PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan(
    const char *input, size_t input_length, int parser_options,
    PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified,
    PgQueryNativeScalarInsertProof *native_proof,
    PgQueryMysqlOwnedScalarInsertPlan *plan)
{
    PgQueryMysqlOwnedScalarInsertPlanData data = {0};
    const PgQueryScalarInsertSource *source = NULL;
    if (plan != NULL)
    {
        memcpy(&data, plan->opaque, sizeof(data));
        /* Consume even on mismatch, before parser allocation or any fallback. */
        memset(plan, 0, sizeof(*plan));
    }
    if (input != NULL && parser_options == PG_QUERY_PARSE_DEFAULT &&
        data.valid == PG_QUERY_MYSQL_OWNED_PLAN_VALID && data.owner == input &&
        data.length == input_length && data.source.length == input_length &&
        data.source.rows >= 32U && data.source.columns > 0U &&
        data.source.relation_parts > 0U && data.source.relation_parts <= 3U &&
        data.source.statement_start == 0U)
        source = &data.source;
    return pg_query_parse_certified_native_internal(input, parser_options,
        observer, context, statement_count, certified, native_proof, source);
}

PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_ordinary(
    const char *input, int parser_options,
    size_t *statement_count, int *certified)
{
    MemoryContext ctx;
    PgQueryInternalParsetreeAndError parsed;
    PgQueryProtobufParseResult result = {0};

    *statement_count = 0U;
    *certified = 0;
    ctx = pg_query_enter_memory_context();
    /* Keep constructor selection independent from validation certification. */
    parsed = pg_query_raw_parse_with_options(input, parser_options, true, PG_QUERY_INSERT_CONSTRUCTOR_NONE, NULL, NULL, NULL);
    result.stderr_buffer = parsed.stderr_buffer;
    result.error = parsed.error;
    if (result.error == NULL) {
        result.parse_tree = pg_query_nodes_to_protobuf_certified(
            parsed.tree, NULL, NULL, statement_count, certified);
    } else {
        /* Match ordinary parse-error serialization and error precedence. */
        result.parse_tree = pg_query_nodes_to_protobuf_observed(parsed.tree, NULL, NULL);
    }
    if (result.parse_tree.data == NULL) {
        *statement_count = 0U;
        *certified = 0;
    }
    pg_query_exit_memory_context(ctx);
    return result;
}

static PgQueryProtobufParseResult
pg_query_parse_certified_batch_internal(
    const char *input, int parser_options,
    size_t *statement_count, int *certified,
    PgQueryNativeScalarInsertBatchProof *native_proof)
{
    MemoryContext ctx;
    PgQueryInternalParsetreeAndError parsed;
    PgQueryProtobufParseResult result = {0};
    PgQueryNativeScalarInsertBatchProof candidate = {0};
    *statement_count = 0U;
    *certified = 0;
    if (native_proof != NULL)
        memset(native_proof, 0, sizeof(*native_proof));
    ctx = pg_query_enter_memory_context();
    parsed = pg_query_raw_parse_with_options(input, parser_options, true,
        PG_QUERY_INSERT_CONSTRUCTOR_BATCH, NULL, NULL,
        native_proof != NULL ? &candidate : NULL);
    result.stderr_buffer = parsed.stderr_buffer;
    result.error = parsed.error;
    if (result.error == NULL)
        result.parse_tree = pg_query_nodes_to_protobuf_certified(
            parsed.tree, NULL, NULL, statement_count, certified);
    else
        result.parse_tree = pg_query_nodes_to_protobuf_observed(parsed.tree, NULL, NULL);
    if (result.parse_tree.data == NULL)
    {
        *statement_count = 0U;
        *certified = 0;
    }
    /* A writer certificate alone also admits ordinary trees. Only the exact
     * successful native candidate may publish this narrower graph proof. */
    if (native_proof != NULL && result.error == NULL &&
        result.parse_tree.data != NULL && result.parse_tree.len != 0U && *certified &&
        candidate.statement_count >= 2U &&
        candidate.statement_count <= PG_QUERY_NATIVE_SCALAR_BATCH_MAX_STATEMENTS &&
        *statement_count == candidate.statement_count)
    {
        size_t count = candidate.statement_count;
        candidate.statement_count = 0U;
        *native_proof = candidate;
        native_proof->statement_count = count;
    }
    pg_query_exit_memory_context(ctx);
    return result;
}

/* The legacy entry requests no proof and retains its original behavior. */
PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_batch(
    const char *input, int parser_options,
    size_t *statement_count, int *certified)
{
    return pg_query_parse_certified_batch_internal(
        input, parser_options, statement_count, certified, NULL);
}

PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_batch_native(
    const char *input, int parser_options,
    size_t *statement_count, int *certified,
    PgQueryNativeScalarInsertBatchProof *native_proof)
{
    return pg_query_parse_certified_batch_internal(
        input, parser_options, statement_count, certified, native_proof);
}

void *pg_query_protobuf_alloc_output(size_t size)
{
    return malloc(size);
}

void pg_query_free_parse_result(PgQueryParseResult result)
{
	if (result.error) {
		pg_query_free_error(result.error);
	}

	free(result.parse_tree);
	free(result.stderr_buffer);
}

void pg_query_free_protobuf_parse_result(PgQueryProtobufParseResult result)
{
	if (result.error) {
		pg_query_free_error(result.error);
	}

	free(result.parse_tree.data);
	free(result.stderr_buffer);
}
