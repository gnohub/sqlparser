#ifndef POSTGRES_DEPARSE_H
#define POSTGRES_DEPARSE_H

#include <stdbool.h>
#include <sys/types.h>

#define POSTGRES_DEPARSE_GENERATED_IDENTIFIER_LOCATION (-2)
#define POSTGRES_DEPARSE_SEMANTIC_IDENTIFIER_LOCATION (-3)
#define POSTGRES_DEPARSE_GENERATED_STYLE_BASE (-16)
#define POSTGRES_DEPARSE_GENERATED_SPELLING_LAST (-1610612737)
#define POSTGRES_DEPARSE_MERGE_ACTION_WHERE_LOCATION (-1610612736)
#define POSTGRES_DEPARSE_SQL_VALUE_CASE_BASE (-1073741840)
#define POSTGRES_DEPARSE_SQL_VALUE_CASE_LAST (-1073872911)
#define POSTGRES_DEPARSE_IDENTIFIER_STYLE_BITS 3U
#define POSTGRES_DEPARSE_IDENTIFIER_STYLE_MASK 7U
#define POSTGRES_DEPARSE_IDENTIFIER_STYLE_UNQUOTED 1U
#define POSTGRES_DEPARSE_IDENTIFIER_STYLE_DOUBLE_QUOTED 2U
#define POSTGRES_DEPARSE_IDENTIFIER_STYLE_BACKTICK_QUOTED 3U
#define POSTGRES_DEPARSE_IDENTIFIER_STYLE_BRACKET_QUOTED 4U
#define POSTGRES_DEPARSE_WINDOW_NAME_COMPONENT ((size_t)-1)
#define POSTGRES_DEPARSE_INDEX_ACCESS_METHOD_COMPONENT ((size_t)-2)
#define POSTGRES_DEPARSE_SQL_VALUE_FUNCTION_COMPONENT ((size_t)-3)

typedef enum PostgresDeparseSourceTokenKind {
    POSTGRES_DEPARSE_SOURCE_TOKEN_NONE = 0,
    POSTGRES_DEPARSE_SOURCE_TOKEN_IDENTIFIER_MATCH,
    POSTGRES_DEPARSE_SOURCE_TOKEN_IDENTIFIER_MISMATCH,
    POSTGRES_DEPARSE_SOURCE_TOKEN_STRING
} PostgresDeparseSourceTokenKind;

typedef struct PostgresDeparseComment {
    int match_location;          // Insert comment before a node, once we find a node whose location field is equal-or-higher than this location
    int newlines_before_comment; // Insert newlines before inserting the comment (set to non-zero if the source comment was separated from the prior token by at least one newline)
    int newlines_after_comment;  // Insert newlines after inserting the comment (set to non-zero if the source comment was separated from the next token by at least one newline)
    char *str;                   // The actual comment string, including comment start/end tokens, and newline characters in comment (if any)
} PostgresDeparseComment;

typedef bool (*PostgresDeparseIdentifierResolver)(
    void *context,
    const char *identifier,
    int location,
    size_t component_index,
    bool search_forward,
    const char **resolved,
    size_t *resolved_length);

typedef bool (*PostgresDeparseKeywordMatcher)(
    void *context,
    const char *identifier,
    const char *keyword,
    int location,
    bool search_forward);

typedef PostgresDeparseSourceTokenKind (*PostgresDeparseSourceTokenProbe)(
    void *context,
    const char *identifier,
    int location,
    bool search_forward);

typedef void (*PostgresDeparseGeneratedIdentifierReader)(
    void *context,
    size_t identifier_index,
    const char *identifier);

typedef bool (*PostgresDeparseGeneratedIdentifierProbe)(
    void *context,
    const char *identifier);

typedef struct PostgresDeparseOpts {
    PostgresDeparseComment **comments;
    size_t comment_count;

    // Pretty print options
    bool pretty_print;
    int indent_size;           // Indentation size (Default 4 spaces)
    int max_line_length;       // Restricts the line length of certain lists of items (Default 80 characters)
    bool trailing_newline;     // Whether to add a trailing newline at the end of the output (Default off)
    bool commas_start_of_line; // Place separating commas at start of line (Default off)

    // Optional source-spelling resolver for identifiers.
    PostgresDeparseIdentifierResolver identifier_resolver;
    PostgresDeparseKeywordMatcher keyword_matcher;
    PostgresDeparseSourceTokenProbe source_token_probe;
    const char *generated_identifier_prefix;
    size_t generated_identifier_prefix_length;
    PostgresDeparseGeneratedIdentifierReader generated_identifier_reader;
    PostgresDeparseGeneratedIdentifierProbe generated_identifier_probe;
    void *identifier_resolver_context;
} PostgresDeparseOpts;

/* Forward declarations to allow referencing the structs in this include file without needing Postgres includes */
struct StringInfoData;
typedef struct StringInfoData *StringInfo;
struct RawStmt;

extern void deparseRawStmt(StringInfo str, struct RawStmt *raw_stmt);
extern void deparseRawStmtOpts(StringInfo str, struct RawStmt *raw_stmt, PostgresDeparseOpts opts);
extern int postgres_deparse_keyword_category(const char *word, size_t length);

#endif
