/* Test-only copies of the real planner. Header first makes the capability
 * substitution local to reads, without changing the dialect ops layout.
 * Every Oracle owner has native scalar validation disabled. Reference handles
 * retain their exact registered owner, so only ASCII admission is disabled. */
#include "../../src/dialect/sqlparser_dialect_internal.h"
#if SQLPARSER_ASCII_REFERENCE
#define plain_ascii_string_fragments plain_scalar_native_validation
#define sqlparser_apply_patch sqlparser_apply_patch_ascii_reference
#else
#define sqlparser_apply_patch sqlparser_apply_patch_ascii_probe
#endif
#include "../../src/core/sqlparser_patch.c"
#if !SQLPARSER_ASCII_REFERENCE
sqlparser_status_t sqlparser_test_ascii_validate(sqlparser_handle_t *h,
    const char *sql, sqlparser_selector_kind_t selector, int *ordinals, sqlparser_error_t *e)
{
    return sqlparser_patch_validate_expression_sql(h, 0U, sql, NULL, selector, 0, ordinals, e);
}
#endif
