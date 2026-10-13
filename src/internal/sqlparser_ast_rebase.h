#ifndef SQLPARSER_AST_REBASE_H
#define SQLPARSER_AST_REBASE_H

#include "sqlparser_internal.h"

#define SQLPARSER_AST_REBASE_INLINE_WRITES 64U

typedef struct {
    int32_t *slot;
    int32_t value;
} sqlparser_ast_rebase_write_t;

/* Transaction-local storage, never an AST or handle cache. Zero-initialize
 * before first use, and never copy or move an initialized plan by value:
 * items can point into its own inline_writes array. */
typedef struct {
    sqlparser_ast_rebase_write_t *items;
    size_t count;
    size_t capacity;
    sqlparser_ast_rebase_write_t inline_writes[SQLPARSER_AST_REBASE_INLINE_WRITES];
} sqlparser_ast_rebase_plan_t;

/* Called once for each reachable AConst owner slot, while all coordinates
 * and literal values still refer to the old SQL. under_type_name includes
 * all TypeName ancestry. The callback must not allocate or mutate the AST;
 * return 1 to continue, or 0 for a conservative eligibility miss. Caller
 * context may retain owner slots but must discard partial proof on failure. */
typedef int (*sqlparser_ast_rebase_literal_callback_t)(PgQuery__AConst **slot,
    int under_type_name, void *context);

/* Cheap range validation, available before token/source-identity proof. */
int sqlparser_ast_rebase_edits_valid(
    const sqlparser_surface_source_edits_t *edits,
    size_t old_length, size_t new_length);

/* Validate an independently owned, unshared PostgreSQL protobuf tree and
 * record only changed coordinates in one read-only walk. The caller proves
 * literal-token/source identity separately, using the required callback to
 * prove target ownership and semantic eligibility during that same walk.
 * All coordinates must still refer to the old SQL. Neither build nor the
 * callback may replace any literal payloads or change tree ownership.
 *
 * Return 1 for an eligible tree, 0 for a proof miss, or -1 for allocation
 * failure (including an unrepresentable write-array size). A failed build
 * releases partial storage and leaves the plan zeroed and the AST unchanged.
 * A new build releases any previous plan first. Unknown source metadata,
 * unsupported bounds, and excessive depth remain conservative proof misses.
 * This is not a validator for arbitrary pointers. */
int sqlparser_ast_rebase_plan_build(sqlparser_ast_rebase_plan_t *plan,
    PgQuery__ParseResult *ast, const sqlparser_surface_source_edits_t *edits,
    size_t old_length, size_t new_length,
    sqlparser_ast_rebase_literal_callback_t callback, void *callback_context);

/* Apply a successful plan before replacing/freeing any old literal payload.
 * The tree and its coordinates must remain unchanged since build. This is
 * an allocation-free, non-failing list of scalar writes, with no AST walk.
 * After payload replacement, only release may access the plan: its slots
 * can then refer to freed payloads. */
void sqlparser_ast_rebase_plan_apply(const sqlparser_ast_rebase_plan_t *plan);

/* Idempotent for a zero-initialized or built plan; never dereferences slots. */
void sqlparser_ast_rebase_plan_release(sqlparser_ast_rebase_plan_t *plan);

#endif
