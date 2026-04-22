#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pg_query.h"
#include "sqlparser/sqlparser.h"

typedef enum {
	BENCH_MODE_NATIVE_PARSE = 0,
	BENCH_MODE_NATIVE_SUMMARY = 1,
	BENCH_MODE_NATIVE_DEPARSE = 2,
	BENCH_MODE_SQLPARSER_PARSE = 3,
	BENCH_MODE_SQLPARSER_PARSE_TREE_JSON = 4,
	BENCH_MODE_SQLPARSER_SUMMARY_JSON = 5,
	BENCH_MODE_SQLPARSER_DEPARSE = 6
} bench_mode_t;

typedef enum {
	WORKLOAD_SELECT_FILTER = 0,
	WORKLOAD_SELECT_JOIN = 1,
	WORKLOAD_INSERT_VALUES = 2,
	WORKLOAD_UPDATE_WHERE = 3,
	WORKLOAD_DELETE_WHERE = 4,
	WORKLOAD_CREATE_VIEW = 5,
	WORKLOAD_TRANSACTION = 6
} workload_kind_t;

typedef struct alloc_tracker alloc_tracker_t;

typedef struct {
	char *data;
	size_t len;
	size_t cap;
} sql_buffer_t;

typedef struct {
	bench_mode_t mode;
	workload_kind_t workload;
	size_t target_bytes;
	size_t iterations;
	size_t warmup_iterations;
	int print_header;
	const char *dump_sql_path;
} bench_options_t;

typedef struct {
	uint64_t latency_ns;
	uint64_t alloc_bytes;
	uint64_t peak_live_bytes;
	uint64_t retained_bytes;
} bench_sample_t;

typedef union {
	struct {
		uint64_t magic;
		size_t size;
		alloc_tracker_t *tracker;
	} meta;
	max_align_t alignment;
} alloc_header_t;

struct alloc_tracker {
	uint64_t alloc_calls;
	uint64_t free_calls;
	uint64_t total_alloc_bytes;
	uint64_t total_freed_bytes;
	uint64_t current_live_bytes;
	uint64_t peak_live_bytes;
	uint64_t retained_bytes;
	int closed;
};

typedef struct {
	PgQueryProtobufParseResult native_parse_result;
	PgQuerySummaryParseResult native_summary_result;
	PgQueryDeparseResult native_deparse_result;
	sqlparser_handle_t *handle;
	char *text_result;
} operation_state_t;

typedef struct {
	size_t sql_length_bytes;
	size_t total_operations;
	size_t ok_operations;
	size_t error_operations;
	double wall_total_s;
	double throughput_ops_s;
	double avg_us;
	double min_us;
	double p50_us;
	double p95_us;
	double p99_us;
	double max_us;
	double avg_alloc_bytes;
	double avg_peak_live_bytes;
	double avg_retained_bytes;
	uint64_t p50_alloc_bytes;
	uint64_t p95_alloc_bytes;
	uint64_t max_alloc_bytes;
	uint64_t p50_peak_live_bytes;
	uint64_t p95_peak_live_bytes;
	uint64_t max_peak_live_bytes;
	uint64_t p50_retained_bytes;
	uint64_t p95_retained_bytes;
	uint64_t max_retained_bytes;
	char error_message[256];
} bench_result_t;

static alloc_tracker_t *g_active_tracker = NULL;
static const uint64_t ALLOC_HEADER_MAGIC = UINT64_C(0x53514C5041525345);

extern __thread sig_atomic_t pg_query_initialized;

static void bench_pg_query_shutdown(void)
{
	if (pg_query_initialized != 0) {
		pg_query_exit();
		pg_query_initialized = 0;
	}
}

static void bench_pg_query_register_exit_once(void)
{
	(void)atexit(bench_pg_query_shutdown);
}

static void bench_pg_query_prepare(void)
{
	static pthread_once_t once_control = PTHREAD_ONCE_INIT;

	(void)pthread_once(&once_control, bench_pg_query_register_exit_once);
}

void *__real_malloc(size_t size);
void *__real_calloc(size_t nmemb, size_t size);
void *__real_realloc(void *ptr, size_t size);
void __real_free(void *ptr);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t nmemb, size_t size);
void *__wrap_realloc(void *ptr, size_t size);
void __wrap_free(void *ptr);
char *__wrap_strdup(const char *text);
char *__wrap_strndup(const char *text, size_t len);

static alloc_header_t *alloc_header_from_user(const void *ptr)
{
	return ((alloc_header_t *)ptr) - 1;
}

static void alloc_tracker_on_alloc(alloc_tracker_t *tracker, size_t size)
{
	if (tracker == NULL || tracker->closed) {
		return;
	}

	tracker->alloc_calls++;
	tracker->total_alloc_bytes += (uint64_t)size;
	tracker->current_live_bytes += (uint64_t)size;
	if (tracker->current_live_bytes > tracker->peak_live_bytes) {
		tracker->peak_live_bytes = tracker->current_live_bytes;
	}
}

static void alloc_tracker_on_free(alloc_tracker_t *tracker, size_t size)
{
	if (tracker == NULL || tracker->closed) {
		return;
	}

	tracker->free_calls++;
	tracker->total_freed_bytes += (uint64_t)size;
	if (tracker->current_live_bytes >= (uint64_t)size) {
		tracker->current_live_bytes -= (uint64_t)size;
	} else {
		tracker->current_live_bytes = 0U;
	}
}

static void alloc_tracker_begin(alloc_tracker_t *tracker)
{
	memset(tracker, 0, sizeof(*tracker));
	g_active_tracker = tracker;
}

static void alloc_tracker_end(alloc_tracker_t *tracker)
{
	if (tracker == NULL) {
		return;
	}

	if (g_active_tracker == tracker) {
		g_active_tracker = NULL;
	}
	tracker->retained_bytes = tracker->current_live_bytes;
	tracker->closed = 1;
}

void *__wrap_malloc(size_t size)
{
	alloc_header_t *header;
	size_t payload_size;
	size_t total_size;

	payload_size = size > 0U ? size : 1U;
	total_size = sizeof(*header) + payload_size;

	header = (alloc_header_t *)__real_malloc(total_size);
	if (header == NULL) {
		return NULL;
	}

	header->meta.magic = ALLOC_HEADER_MAGIC;
	header->meta.size = size;
	header->meta.tracker = g_active_tracker;
	alloc_tracker_on_alloc(g_active_tracker, size);
	return (void *)(header + 1);
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
	void *ptr;
	size_t total_size;

	if (nmemb != 0U && size > (SIZE_MAX / nmemb)) {
		errno = ENOMEM;
		return NULL;
	}

	total_size = nmemb * size;
	ptr = __wrap_malloc(total_size);
	if (ptr != NULL && total_size > 0U) {
		memset(ptr, 0, total_size);
	}

	return ptr;
}

void *__wrap_realloc(void *ptr, size_t size)
{
	alloc_header_t *old_header;
	alloc_header_t *new_header;
	alloc_tracker_t *old_tracker;
	size_t old_size;
	size_t payload_size;
	size_t total_size;

	if (ptr == NULL) {
		return __wrap_malloc(size);
	}
	if (size == 0U) {
		__wrap_free(ptr);
		return NULL;
	}

	old_header = alloc_header_from_user(ptr);
	if (old_header->meta.magic != ALLOC_HEADER_MAGIC) {
		return __real_realloc(ptr, size);
	}
	old_tracker = old_header->meta.tracker;
	old_size = old_header->meta.size;
	payload_size = size > 0U ? size : 1U;
	total_size = sizeof(*old_header) + payload_size;

	new_header = (alloc_header_t *)__real_realloc((void *)old_header, total_size);
	if (new_header == NULL) {
		return NULL;
	}

	alloc_tracker_on_free(old_tracker, old_size);
	new_header->meta.magic = ALLOC_HEADER_MAGIC;
	new_header->meta.size = size;
	new_header->meta.tracker = g_active_tracker;
	alloc_tracker_on_alloc(g_active_tracker, size);
	return (void *)(new_header + 1);
}

void __wrap_free(void *ptr)
{
	alloc_header_t *header;

	if (ptr == NULL) {
		return;
	}

	header = alloc_header_from_user(ptr);
	if (header->meta.magic != ALLOC_HEADER_MAGIC) {
		__real_free(ptr);
		return;
	}
	alloc_tracker_on_free(header->meta.tracker, header->meta.size);
	__real_free((void *)header);
}

char *__wrap_strdup(const char *text)
{
	char *copy;
	size_t len;

	if (text == NULL) {
		return NULL;
	}

	len = strlen(text) + 1U;
	copy = (char *)__wrap_malloc(len);
	if (copy == NULL) {
		return NULL;
	}

	memcpy(copy, text, len);
	return copy;
}

char *__wrap_strndup(const char *text, size_t len)
{
	char *copy;
	size_t copy_len;

	if (text == NULL) {
		return NULL;
	}

	copy_len = 0U;
	while (copy_len < len && text[copy_len] != '\0') {
		copy_len++;
	}

	copy = (char *)__wrap_malloc(copy_len + 1U);
	if (copy == NULL) {
		return NULL;
	}

	if (copy_len > 0U) {
		memcpy(copy, text, copy_len);
	}
	copy[copy_len] = '\0';
	return copy;
}

static void print_usage(const char *program)
{
	fprintf(stderr, "Usage: %s --mode MODE --workload WORKLOAD --length-bytes N [options]\n", program);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  --iterations N           measured iterations (default: 10000)\n");
	fprintf(stderr, "  --warmup N               warmup iterations (default: 1000)\n");
	fprintf(stderr, "  --dump-sql PATH          write the generated SQL to PATH\n");
	fprintf(stderr, "  --csv-header             print CSV header before the result row\n");
	fprintf(stderr, "Modes:\n");
	fprintf(stderr, "  native-parse\n");
	fprintf(stderr, "  native-summary\n");
	fprintf(stderr, "  native-deparse\n");
	fprintf(stderr, "  sqlparser-parse\n");
	fprintf(stderr, "  sqlparser-parse-tree-json\n");
	fprintf(stderr, "  sqlparser-summary-json\n");
	fprintf(stderr, "  sqlparser-deparse\n");
	fprintf(stderr, "Workloads:\n");
	fprintf(stderr, "  select-filter\n");
	fprintf(stderr, "  select-join\n");
	fprintf(stderr, "  insert-values\n");
	fprintf(stderr, "  update-where\n");
	fprintf(stderr, "  delete-where\n");
	fprintf(stderr, "  create-view\n");
	fprintf(stderr, "  transaction\n");
}

static const char *bench_mode_name(bench_mode_t mode)
{
	switch (mode) {
		case BENCH_MODE_NATIVE_PARSE:
			return "native-parse";
		case BENCH_MODE_NATIVE_SUMMARY:
			return "native-summary";
		case BENCH_MODE_NATIVE_DEPARSE:
			return "native-deparse";
		case BENCH_MODE_SQLPARSER_PARSE:
			return "sqlparser-parse";
		case BENCH_MODE_SQLPARSER_PARSE_TREE_JSON:
			return "sqlparser-parse-tree-json";
		case BENCH_MODE_SQLPARSER_SUMMARY_JSON:
			return "sqlparser-summary-json";
		case BENCH_MODE_SQLPARSER_DEPARSE:
			return "sqlparser-deparse";
		default:
			return "unknown";
	}
}

static const char *workload_name(workload_kind_t workload)
{
	switch (workload) {
		case WORKLOAD_SELECT_FILTER:
			return "select-filter";
		case WORKLOAD_SELECT_JOIN:
			return "select-join";
		case WORKLOAD_INSERT_VALUES:
			return "insert-values";
		case WORKLOAD_UPDATE_WHERE:
			return "update-where";
		case WORKLOAD_DELETE_WHERE:
			return "delete-where";
		case WORKLOAD_CREATE_VIEW:
			return "create-view";
		case WORKLOAD_TRANSACTION:
			return "transaction";
		default:
			return "unknown";
	}
}

static int parse_positive_size(const char *value, size_t *out_value)
{
	char *endptr;
	unsigned long long parsed;

	if (value == NULL || out_value == NULL) {
		return -1;
	}

	errno = 0;
	endptr = NULL;
	parsed = strtoull(value, &endptr, 10);
	if (errno != 0 || endptr == value || (endptr != NULL && *endptr != '\0') || parsed == 0ULL) {
		return -1;
	}

	*out_value = (size_t)parsed;
	return 0;
}

static int parse_mode(const char *value, bench_mode_t *out_mode)
{
	if (strcmp(value, "native-parse") == 0) {
		*out_mode = BENCH_MODE_NATIVE_PARSE;
		return 0;
	}
	if (strcmp(value, "native-summary") == 0) {
		*out_mode = BENCH_MODE_NATIVE_SUMMARY;
		return 0;
	}
	if (strcmp(value, "native-deparse") == 0) {
		*out_mode = BENCH_MODE_NATIVE_DEPARSE;
		return 0;
	}
	if (strcmp(value, "sqlparser-parse") == 0) {
		*out_mode = BENCH_MODE_SQLPARSER_PARSE;
		return 0;
	}
	if (strcmp(value, "sqlparser-parse-tree-json") == 0) {
		*out_mode = BENCH_MODE_SQLPARSER_PARSE_TREE_JSON;
		return 0;
	}
	if (strcmp(value, "sqlparser-summary-json") == 0) {
		*out_mode = BENCH_MODE_SQLPARSER_SUMMARY_JSON;
		return 0;
	}
	if (strcmp(value, "sqlparser-deparse") == 0) {
		*out_mode = BENCH_MODE_SQLPARSER_DEPARSE;
		return 0;
	}

	return -1;
}

static int parse_workload(const char *value, workload_kind_t *out_workload)
{
	if (strcmp(value, "select-filter") == 0) {
		*out_workload = WORKLOAD_SELECT_FILTER;
		return 0;
	}
	if (strcmp(value, "select-join") == 0) {
		*out_workload = WORKLOAD_SELECT_JOIN;
		return 0;
	}
	if (strcmp(value, "insert-values") == 0) {
		*out_workload = WORKLOAD_INSERT_VALUES;
		return 0;
	}
	if (strcmp(value, "update-where") == 0) {
		*out_workload = WORKLOAD_UPDATE_WHERE;
		return 0;
	}
	if (strcmp(value, "delete-where") == 0) {
		*out_workload = WORKLOAD_DELETE_WHERE;
		return 0;
	}
	if (strcmp(value, "create-view") == 0) {
		*out_workload = WORKLOAD_CREATE_VIEW;
		return 0;
	}
	if (strcmp(value, "transaction") == 0) {
		*out_workload = WORKLOAD_TRANSACTION;
		return 0;
	}

	return -1;
}

static int sql_buffer_init(sql_buffer_t *buffer, size_t initial_cap)
{
	if (buffer == NULL) {
		return -1;
	}

	buffer->data = (char *)malloc(initial_cap > 0U ? initial_cap : 64U);
	if (buffer->data == NULL) {
		return -1;
	}

	buffer->len = 0U;
	buffer->cap = initial_cap > 0U ? initial_cap : 64U;
	buffer->data[0] = '\0';
	return 0;
}

static void sql_buffer_free(sql_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}

	free(buffer->data);
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->cap = 0U;
}

static int sql_buffer_reserve(sql_buffer_t *buffer, size_t additional)
{
	char *next;
	size_t required;
	size_t next_cap;

	if (buffer == NULL) {
		return -1;
	}

	required = buffer->len + additional + 1U;
	if (required <= buffer->cap) {
		return 0;
	}

	next_cap = buffer->cap;
	while (next_cap < required) {
		next_cap *= 2U;
	}

	next = (char *)realloc(buffer->data, next_cap);
	if (next == NULL) {
		return -1;
	}

	buffer->data = next;
	buffer->cap = next_cap;
	return 0;
}

static int sql_buffer_append_n(sql_buffer_t *buffer, const char *text, size_t len)
{
	if (buffer == NULL || text == NULL) {
		return -1;
	}

	if (sql_buffer_reserve(buffer, len) != 0) {
		return -1;
	}

	memcpy(buffer->data + buffer->len, text, len);
	buffer->len += len;
	buffer->data[buffer->len] = '\0';
	return 0;
}

static int sql_buffer_append(sql_buffer_t *buffer, const char *text)
{
	return sql_buffer_append_n(buffer, text, strlen(text));
}

static int sql_buffer_append_repeat_char(sql_buffer_t *buffer, char ch, size_t count)
{
	size_t index;

	if (buffer == NULL) {
		return -1;
	}

	if (sql_buffer_reserve(buffer, count) != 0) {
		return -1;
	}

	for (index = 0; index < count; index++) {
		buffer->data[buffer->len + index] = ch;
	}
	buffer->len += count;
	buffer->data[buffer->len] = '\0';
	return 0;
}

static int write_text_file(const char *path, const char *content)
{
	FILE *output;
	size_t len;

	output = fopen(path, "wb");
	if (output == NULL) {
		return -1;
	}

	len = strlen(content);
	if (len > 0U && fwrite(content, 1U, len, output) != len) {
		fclose(output);
		return -1;
	}

	if (fclose(output) != 0) {
		return -1;
	}

	return 0;
}

static int finalize_with_literal_tail(
	sql_buffer_t *buffer,
	size_t target_bytes,
	const char *prefix,
	const char *suffix,
	char fill_char)
{
	size_t prefix_len;
	size_t suffix_len;
	size_t literal_len;

	prefix_len = strlen(prefix);
	suffix_len = strlen(suffix);
	if (buffer->len + prefix_len + suffix_len + 1U > target_bytes) {
		return -1;
	}

	literal_len = target_bytes - buffer->len - prefix_len - suffix_len;
	if (literal_len == 0U) {
		return -1;
	}

	if (sql_buffer_append(buffer, prefix) != 0) {
		return -1;
	}
	if (sql_buffer_append_repeat_char(buffer, fill_char, literal_len) != 0) {
		return -1;
	}
	if (sql_buffer_append(buffer, suffix) != 0) {
		return -1;
	}

	return buffer->len == target_bytes ? 0 : -1;
}

static char *generate_select_filter_sql(size_t target_bytes)
{
	sql_buffer_t buffer;
	size_t fragment_index;
	const char *base;
	const char *tail_prefix;
	const char *tail_suffix;
	char fragment[64];
	size_t min_tail_bytes;
	int written;

	base = "SELECT id FROM public.bench_t WHERE id = 1";
	tail_prefix = " OR note = '";
	tail_suffix = "'";
	min_tail_bytes = strlen(tail_prefix) + strlen(tail_suffix) + 1U;

	if (target_bytes < strlen(base) + min_tail_bytes) {
		return NULL;
	}

	if (sql_buffer_init(&buffer, target_bytes + 1U) != 0) {
		return NULL;
	}

	if (sql_buffer_append(&buffer, base) != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	fragment_index = 2U;
	for (;;) {
		written = snprintf(fragment, sizeof(fragment), " OR id = %zu", fragment_index);
		if (written <= 0 || (size_t)written >= sizeof(fragment)) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		if (buffer.len + (size_t)written + min_tail_bytes > target_bytes) {
			break;
		}
		if (sql_buffer_append(&buffer, fragment) != 0) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		fragment_index++;
	}

	if (finalize_with_literal_tail(&buffer, target_bytes, tail_prefix, tail_suffix, 'x') != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	return buffer.data;
}

static char *generate_select_join_sql(size_t target_bytes)
{
	sql_buffer_t buffer;
	size_t fragment_index;
	const char *base;
	const char *tail_prefix;
	const char *tail_suffix;
	char fragment[96];
	size_t min_tail_bytes;
	int written;

	base = "SELECT l.id, r.v FROM public.left_table l JOIN public.right_table r ON l.id = r.id WHERE l.flag = 'Y'";
	tail_prefix = " AND l.tail = '";
	tail_suffix = "'";
	min_tail_bytes = strlen(tail_prefix) + strlen(tail_suffix) + 1U;

	if (target_bytes < strlen(base) + min_tail_bytes) {
		return NULL;
	}

	if (sql_buffer_init(&buffer, target_bytes + 1U) != 0) {
		return NULL;
	}

	if (sql_buffer_append(&buffer, base) != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	fragment_index = 1U;
	for (;;) {
		written = snprintf(
			fragment,
			sizeof(fragment),
			" AND r.c%04zu = %zu",
			fragment_index,
			fragment_index);
		if (written <= 0 || (size_t)written >= sizeof(fragment)) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		if (buffer.len + (size_t)written + min_tail_bytes > target_bytes) {
			break;
		}
		if (sql_buffer_append(&buffer, fragment) != 0) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		fragment_index++;
	}

	if (finalize_with_literal_tail(&buffer, target_bytes, tail_prefix, tail_suffix, 'j') != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	return buffer.data;
}

static char *generate_insert_values_sql(size_t target_bytes)
{
	sql_buffer_t buffer;
	size_t row_index;
	const char *base;
	const char *tail_prefix;
	const char *tail_suffix;
	char fragment[96];
	size_t min_tail_bytes;
	int written;
	size_t padding_bytes;

	base = "INSERT INTO public.bench_t (id, name) VALUES (1, 'seed')";
	tail_prefix = ", (999999, '";
	tail_suffix = "')";
	min_tail_bytes = strlen(tail_prefix) + strlen(tail_suffix) + 1U;

	if (target_bytes < strlen(base)) {
		return NULL;
	}

	if (sql_buffer_init(&buffer, target_bytes + 1U) != 0) {
		return NULL;
	}

	if (sql_buffer_append(&buffer, base) != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	if (buffer.len == target_bytes) {
		return buffer.data;
	}

	if (buffer.len + min_tail_bytes > target_bytes) {
		padding_bytes = target_bytes - buffer.len;
		if (sql_buffer_append_repeat_char(&buffer, ' ', padding_bytes) != 0) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		if (buffer.len != target_bytes) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		return buffer.data;
	}

	row_index = 2U;
	for (;;) {
		written = snprintf(fragment, sizeof(fragment), ", (%zu, 'row%zu')", row_index, row_index);
		if (written <= 0 || (size_t)written >= sizeof(fragment)) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		if (buffer.len + (size_t)written + min_tail_bytes > target_bytes) {
			break;
		}
		if (sql_buffer_append(&buffer, fragment) != 0) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		row_index++;
	}

	if (finalize_with_literal_tail(&buffer, target_bytes, tail_prefix, tail_suffix, 'i') != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	return buffer.data;
}

static char *generate_update_where_sql(size_t target_bytes)
{
	sql_buffer_t buffer;
	size_t fragment_index;
	const char *base;
	const char *tail_prefix;
	const char *tail_suffix;
	char fragment[64];
	size_t min_tail_bytes;
	int written;

	base = "UPDATE public.bench_t SET name = 'seed' WHERE id = 1";
	tail_prefix = " OR note = '";
	tail_suffix = "'";
	min_tail_bytes = strlen(tail_prefix) + strlen(tail_suffix) + 1U;

	if (target_bytes < strlen(base) + min_tail_bytes) {
		return NULL;
	}

	if (sql_buffer_init(&buffer, target_bytes + 1U) != 0) {
		return NULL;
	}

	if (sql_buffer_append(&buffer, base) != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	fragment_index = 2U;
	for (;;) {
		written = snprintf(fragment, sizeof(fragment), " OR id = %zu", fragment_index);
		if (written <= 0 || (size_t)written >= sizeof(fragment)) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		if (buffer.len + (size_t)written + min_tail_bytes > target_bytes) {
			break;
		}
		if (sql_buffer_append(&buffer, fragment) != 0) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		fragment_index++;
	}

	if (finalize_with_literal_tail(&buffer, target_bytes, tail_prefix, tail_suffix, 'u') != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	return buffer.data;
}

static char *generate_delete_where_sql(size_t target_bytes)
{
	sql_buffer_t buffer;
	size_t fragment_index;
	const char *base;
	const char *tail_prefix;
	const char *tail_suffix;
	char fragment[64];
	size_t min_tail_bytes;
	int written;

	base = "DELETE FROM public.bench_t WHERE id = 1";
	tail_prefix = " OR note = '";
	tail_suffix = "'";
	min_tail_bytes = strlen(tail_prefix) + strlen(tail_suffix) + 1U;

	if (target_bytes < strlen(base) + min_tail_bytes) {
		return NULL;
	}

	if (sql_buffer_init(&buffer, target_bytes + 1U) != 0) {
		return NULL;
	}

	if (sql_buffer_append(&buffer, base) != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	fragment_index = 2U;
	for (;;) {
		written = snprintf(fragment, sizeof(fragment), " OR id = %zu", fragment_index);
		if (written <= 0 || (size_t)written >= sizeof(fragment)) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		if (buffer.len + (size_t)written + min_tail_bytes > target_bytes) {
			break;
		}
		if (sql_buffer_append(&buffer, fragment) != 0) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		fragment_index++;
	}

	if (finalize_with_literal_tail(&buffer, target_bytes, tail_prefix, tail_suffix, 'd') != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	return buffer.data;
}

static char *generate_create_view_sql(size_t target_bytes)
{
	sql_buffer_t buffer;
	size_t fragment_index;
	const char *base;
	const char *tail_prefix;
	const char *tail_suffix;
	char fragment[64];
	size_t min_tail_bytes;
	int written;

	base = "CREATE VIEW public.v_bench AS SELECT id FROM public.bench_t WHERE id = 1";
	tail_prefix = " OR note = '";
	tail_suffix = "'";
	min_tail_bytes = strlen(tail_prefix) + strlen(tail_suffix) + 1U;

	if (target_bytes < strlen(base) + min_tail_bytes) {
		return NULL;
	}

	if (sql_buffer_init(&buffer, target_bytes + 1U) != 0) {
		return NULL;
	}

	if (sql_buffer_append(&buffer, base) != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	fragment_index = 2U;
	for (;;) {
		written = snprintf(fragment, sizeof(fragment), " OR id = %zu", fragment_index);
		if (written <= 0 || (size_t)written >= sizeof(fragment)) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		if (buffer.len + (size_t)written + min_tail_bytes > target_bytes) {
			break;
		}
		if (sql_buffer_append(&buffer, fragment) != 0) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		fragment_index++;
	}

	if (finalize_with_literal_tail(&buffer, target_bytes, tail_prefix, tail_suffix, 'v') != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	return buffer.data;
}

static char *generate_transaction_sql(size_t target_bytes)
{
	sql_buffer_t buffer;
	size_t fragment_index;
	const char *base;
	const char *tail_prefix;
	const char *tail_suffix;
	char fragment[64];
	size_t min_tail_bytes;
	int written;

	base = "BEGIN; INSERT INTO public.bench_t (id, name) VALUES (1, 'seed'); UPDATE public.bench_t SET name = 'seed' WHERE id = 1";
	tail_prefix = " OR note = '";
	tail_suffix = "'; COMMIT";
	min_tail_bytes = strlen(tail_prefix) + strlen(tail_suffix) + 1U;

	if (target_bytes < strlen(base) + min_tail_bytes) {
		return NULL;
	}

	if (sql_buffer_init(&buffer, target_bytes + 1U) != 0) {
		return NULL;
	}

	if (sql_buffer_append(&buffer, base) != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	fragment_index = 2U;
	for (;;) {
		written = snprintf(fragment, sizeof(fragment), " OR id = %zu", fragment_index);
		if (written <= 0 || (size_t)written >= sizeof(fragment)) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		if (buffer.len + (size_t)written + min_tail_bytes > target_bytes) {
			break;
		}
		if (sql_buffer_append(&buffer, fragment) != 0) {
			sql_buffer_free(&buffer);
			return NULL;
		}
		fragment_index++;
	}

	if (finalize_with_literal_tail(&buffer, target_bytes, tail_prefix, tail_suffix, 't') != 0) {
		sql_buffer_free(&buffer);
		return NULL;
	}

	return buffer.data;
}

static char *generate_sql(workload_kind_t workload, size_t target_bytes)
{
	switch (workload) {
		case WORKLOAD_SELECT_FILTER:
			return generate_select_filter_sql(target_bytes);
		case WORKLOAD_SELECT_JOIN:
			return generate_select_join_sql(target_bytes);
		case WORKLOAD_INSERT_VALUES:
			return generate_insert_values_sql(target_bytes);
		case WORKLOAD_UPDATE_WHERE:
			return generate_update_where_sql(target_bytes);
		case WORKLOAD_DELETE_WHERE:
			return generate_delete_where_sql(target_bytes);
		case WORKLOAD_CREATE_VIEW:
			return generate_create_view_sql(target_bytes);
		case WORKLOAD_TRANSACTION:
			return generate_transaction_sql(target_bytes);
		default:
			return NULL;
	}
}

static uint64_t timespec_diff_ns(const struct timespec *start, const struct timespec *end)
{
	long long seconds;
	long long nanoseconds;

	seconds = (long long)(end->tv_sec - start->tv_sec);
	nanoseconds = (long long)(end->tv_nsec - start->tv_nsec);
	if (nanoseconds < 0LL) {
		seconds -= 1LL;
		nanoseconds += 1000000000LL;
	}
	return ((uint64_t)seconds * 1000000000ULL) + (uint64_t)nanoseconds;
}

static int record_pg_error(char *buffer, size_t buffer_size, const PgQueryError *error)
{
	if (buffer == NULL || buffer_size == 0U) {
		return -1;
	}

	if (error == NULL || error->message == NULL) {
		(void)snprintf(buffer, buffer_size, "unknown libpg_query error");
		return -1;
	}

	(void)snprintf(buffer, buffer_size, "%s", error->message);
	return -1;
}

static int compare_u64(const void *lhs, const void *rhs)
{
	const uint64_t *left;
	const uint64_t *right;

	left = (const uint64_t *)lhs;
	right = (const uint64_t *)rhs;
	if (*left < *right) {
		return -1;
	}
	if (*left > *right) {
		return 1;
	}
	return 0;
}

static uint64_t percentile_u64(const uint64_t *sorted, size_t count, size_t numerator, size_t denominator)
{
	size_t index;

	if (count == 0U || denominator == 0U) {
		return 0U;
	}

	index = ((count - 1U) * numerator) / denominator;
	return sorted[index];
}

static double percentile_us(const uint64_t *sorted, size_t count, size_t numerator, size_t denominator)
{
	return (double)percentile_u64(sorted, count, numerator, denominator) / 1000.0;
}

static int record_sqlparser_error(char *buffer, size_t buffer_size, const sqlparser_error_t *error)
{
	if (buffer == NULL || buffer_size == 0U) {
		return -1;
	}

	if (error == NULL || error->message[0] == '\0') {
		(void)snprintf(buffer, buffer_size, "unknown sqlparser error");
		return -1;
	}

	(void)snprintf(buffer, buffer_size, "%s", error->message);
	return -1;
}

static void operation_state_init(operation_state_t *state)
{
	memset(state, 0, sizeof(*state));
}

static int prepare_operation(
	bench_mode_t mode,
	const char *sql_text,
	operation_state_t *state,
	char *error_message,
	size_t error_message_size)
{
	sqlparser_error_t error;
	int status;

	switch (mode) {
		case BENCH_MODE_NATIVE_DEPARSE:
			bench_pg_query_prepare();
			state->native_parse_result = pg_query_parse_protobuf(sql_text);
			if (state->native_parse_result.error != NULL) {
				record_pg_error(error_message, error_message_size, state->native_parse_result.error);
				pg_query_free_protobuf_parse_result(state->native_parse_result);
				operation_state_init(state);
				return -1;
			}
			return 0;

		case BENCH_MODE_SQLPARSER_PARSE_TREE_JSON:
		case BENCH_MODE_SQLPARSER_SUMMARY_JSON:
		case BENCH_MODE_SQLPARSER_DEPARSE:
			memset(&error, 0, sizeof(error));
			status = sqlparser_parse(sql_text, &state->handle, &error);
			if (status != SQLPARSER_STATUS_OK) {
				record_sqlparser_error(error_message, error_message_size, &error);
				sqlparser_handle_destroy(state->handle);
				state->handle = NULL;
				return -1;
			}
			return 0;

		default:
			return 0;
	}
}

static int invoke_operation(
	bench_mode_t mode,
	const char *sql_text,
	operation_state_t *state,
	char *error_message,
	size_t error_message_size)
{
	sqlparser_error_t error;
	int status;

	switch (mode) {
		case BENCH_MODE_NATIVE_PARSE:
			{
				bench_pg_query_prepare();
				state->native_parse_result = pg_query_parse_protobuf(sql_text);
				if (state->native_parse_result.error != NULL) {
					record_pg_error(error_message, error_message_size, state->native_parse_result.error);
					return -1;
				}
				return 0;
			}

		case BENCH_MODE_NATIVE_SUMMARY:
			{
				bench_pg_query_prepare();
				state->native_summary_result = pg_query_summary(sql_text, 0, -1);
				if (state->native_summary_result.error != NULL) {
					record_pg_error(error_message, error_message_size, state->native_summary_result.error);
					return -1;
				}
				return 0;
			}

		case BENCH_MODE_NATIVE_DEPARSE:
			{
				bench_pg_query_prepare();
				state->native_deparse_result = pg_query_deparse_protobuf(state->native_parse_result.parse_tree);
				if (state->native_deparse_result.error != NULL) {
					record_pg_error(error_message, error_message_size, state->native_deparse_result.error);
					return -1;
				}
				return 0;
			}

		case BENCH_MODE_SQLPARSER_PARSE:
			{
				memset(&error, 0, sizeof(error));
				status = sqlparser_parse(sql_text, &state->handle, &error);
				if (status != SQLPARSER_STATUS_OK) {
					record_sqlparser_error(error_message, error_message_size, &error);
					return -1;
				}
				return 0;
			}

		case BENCH_MODE_SQLPARSER_PARSE_TREE_JSON:
			{
				memset(&error, 0, sizeof(error));
				status = sqlparser_export_parse_tree_json(state->handle, 0, &state->text_result, &error);
				if (status != SQLPARSER_STATUS_OK) {
					record_sqlparser_error(error_message, error_message_size, &error);
					return -1;
				}
				return 0;
			}

		case BENCH_MODE_SQLPARSER_SUMMARY_JSON:
			{
				memset(&error, 0, sizeof(error));
				status = sqlparser_export_summary_json(state->handle, 0, &state->text_result, &error);
				if (status != SQLPARSER_STATUS_OK) {
					record_sqlparser_error(error_message, error_message_size, &error);
					return -1;
				}
				return 0;
			}

		case BENCH_MODE_SQLPARSER_DEPARSE:
			{
				memset(&error, 0, sizeof(error));
				status = sqlparser_deparse(state->handle, &state->text_result, &error);
				if (status != SQLPARSER_STATUS_OK) {
					record_sqlparser_error(error_message, error_message_size, &error);
					return -1;
				}
				return 0;
			}

		default:
			(void)snprintf(error_message, error_message_size, "unsupported mode");
			return -1;
	}
}

static void cleanup_operation(bench_mode_t mode, operation_state_t *state)
{
	if (state == NULL) {
		return;
	}

	switch (mode) {
		case BENCH_MODE_NATIVE_PARSE:
			if (state->native_parse_result.parse_tree.data != NULL ||
			    state->native_parse_result.stderr_buffer != NULL ||
			    state->native_parse_result.error != NULL) {
				pg_query_free_protobuf_parse_result(state->native_parse_result);
			}
			break;

		case BENCH_MODE_NATIVE_SUMMARY:
			if (state->native_summary_result.summary.data != NULL ||
			    state->native_summary_result.stderr_buffer != NULL ||
			    state->native_summary_result.error != NULL) {
				pg_query_free_summary_parse_result(state->native_summary_result);
			}
			break;

		case BENCH_MODE_NATIVE_DEPARSE:
			if (state->native_deparse_result.query != NULL || state->native_deparse_result.error != NULL) {
				pg_query_free_deparse_result(state->native_deparse_result);
			}
			if (state->native_parse_result.parse_tree.data != NULL ||
			    state->native_parse_result.stderr_buffer != NULL ||
			    state->native_parse_result.error != NULL) {
				pg_query_free_protobuf_parse_result(state->native_parse_result);
			}
			break;

		case BENCH_MODE_SQLPARSER_PARSE:
			sqlparser_handle_destroy(state->handle);
			break;

		case BENCH_MODE_SQLPARSER_PARSE_TREE_JSON:
		case BENCH_MODE_SQLPARSER_SUMMARY_JSON:
		case BENCH_MODE_SQLPARSER_DEPARSE:
			sqlparser_string_free(state->text_result);
			sqlparser_handle_destroy(state->handle);
			break;

		default:
			break;
	}

	operation_state_init(state);
}

static int execute_iteration(
	const bench_options_t *options,
	const char *sql_text,
	bench_sample_t *sample,
	char *error_message,
	size_t error_message_size)
{
	operation_state_t state;
	alloc_tracker_t tracker;
	struct timespec start_time;
	struct timespec end_time;
	int timed;
	int invoke_status;

	operation_state_init(&state);
	timed = sample != NULL;

	if (prepare_operation(options->mode, sql_text, &state, error_message, error_message_size) != 0) {
		return -1;
	}

	if (timed && clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
		cleanup_operation(options->mode, &state);
		(void)snprintf(error_message, error_message_size, "clock_gettime start failed");
		return -1;
	}

	if (timed) {
		alloc_tracker_begin(&tracker);
	}

	invoke_status = invoke_operation(options->mode, sql_text, &state, error_message, error_message_size);

	if (timed) {
		alloc_tracker_end(&tracker);
		if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
			cleanup_operation(options->mode, &state);
			(void)snprintf(error_message, error_message_size, "clock_gettime end failed");
			return -1;
		}
	}

	cleanup_operation(options->mode, &state);
	if (invoke_status != 0) {
		return -1;
	}

	if (timed) {
		sample->latency_ns = timespec_diff_ns(&start_time, &end_time);
		sample->alloc_bytes = tracker.total_alloc_bytes;
		sample->peak_live_bytes = tracker.peak_live_bytes;
		sample->retained_bytes = tracker.retained_bytes;
	}

	return 0;
}

static int run_benchmark(const bench_options_t *options, const char *sql_text, bench_result_t *result)
{
	bench_sample_t *samples;
	uint64_t *latency_ns_sorted;
	uint64_t *alloc_bytes_sorted;
	uint64_t *peak_live_bytes_sorted;
	uint64_t *retained_bytes_sorted;
	struct timespec wall_start;
	struct timespec wall_end;
	unsigned long long latency_sum_ns;
	unsigned long long alloc_sum_bytes;
	unsigned long long peak_sum_bytes;
	unsigned long long retained_sum_bytes;
	size_t index;

	if (options == NULL || sql_text == NULL || result == NULL) {
		return -1;
	}

	memset(result, 0, sizeof(*result));
	result->sql_length_bytes = strlen(sql_text);

	samples = (bench_sample_t *)calloc(options->iterations, sizeof(*samples));
	latency_ns_sorted = (uint64_t *)calloc(options->iterations, sizeof(*latency_ns_sorted));
	alloc_bytes_sorted = (uint64_t *)calloc(options->iterations, sizeof(*alloc_bytes_sorted));
	peak_live_bytes_sorted = (uint64_t *)calloc(options->iterations, sizeof(*peak_live_bytes_sorted));
	retained_bytes_sorted = (uint64_t *)calloc(options->iterations, sizeof(*retained_bytes_sorted));
	if (samples == NULL || latency_ns_sorted == NULL || alloc_bytes_sorted == NULL ||
	    peak_live_bytes_sorted == NULL || retained_bytes_sorted == NULL) {
		free(samples);
		free(latency_ns_sorted);
		free(alloc_bytes_sorted);
		free(peak_live_bytes_sorted);
		free(retained_bytes_sorted);
		return -1;
	}

	for (index = 0; index < options->warmup_iterations; index++) {
		if (execute_iteration(options, sql_text, NULL, result->error_message, sizeof(result->error_message)) != 0) {
			result->error_operations = 1U;
			free(samples);
			free(latency_ns_sorted);
			free(alloc_bytes_sorted);
			free(peak_live_bytes_sorted);
			free(retained_bytes_sorted);
			return -1;
		}
	}

	if (clock_gettime(CLOCK_MONOTONIC, &wall_start) != 0) {
		(void)snprintf(result->error_message, sizeof(result->error_message), "clock_gettime wall start failed");
		result->error_operations = 1U;
		free(samples);
		free(latency_ns_sorted);
		free(alloc_bytes_sorted);
		free(peak_live_bytes_sorted);
		free(retained_bytes_sorted);
		return -1;
	}

	for (index = 0; index < options->iterations; index++) {
		if (execute_iteration(options, sql_text, &samples[index], result->error_message, sizeof(result->error_message)) != 0) {
			result->error_operations = 1U;
			free(samples);
			free(latency_ns_sorted);
			free(alloc_bytes_sorted);
			free(peak_live_bytes_sorted);
			free(retained_bytes_sorted);
			return -1;
		}
	}

	if (clock_gettime(CLOCK_MONOTONIC, &wall_end) != 0) {
		(void)snprintf(result->error_message, sizeof(result->error_message), "clock_gettime wall end failed");
		result->error_operations = 1U;
		free(samples);
		free(latency_ns_sorted);
		free(alloc_bytes_sorted);
		free(peak_live_bytes_sorted);
		free(retained_bytes_sorted);
		return -1;
	}

	result->total_operations = options->iterations;
	result->ok_operations = options->iterations;
	result->error_operations = 0U;
	result->wall_total_s = (double)timespec_diff_ns(&wall_start, &wall_end) / 1000000000.0;
	if (result->wall_total_s > 0.0) {
		result->throughput_ops_s = (double)result->ok_operations / result->wall_total_s;
	}

	latency_sum_ns = 0ULL;
	alloc_sum_bytes = 0ULL;
	peak_sum_bytes = 0ULL;
	retained_sum_bytes = 0ULL;
	for (index = 0; index < options->iterations; index++) {
		latency_ns_sorted[index] = samples[index].latency_ns;
		alloc_bytes_sorted[index] = samples[index].alloc_bytes;
		peak_live_bytes_sorted[index] = samples[index].peak_live_bytes;
		retained_bytes_sorted[index] = samples[index].retained_bytes;

		latency_sum_ns += samples[index].latency_ns;
		alloc_sum_bytes += samples[index].alloc_bytes;
		peak_sum_bytes += samples[index].peak_live_bytes;
		retained_sum_bytes += samples[index].retained_bytes;
	}

	qsort(latency_ns_sorted, options->iterations, sizeof(*latency_ns_sorted), compare_u64);
	qsort(alloc_bytes_sorted, options->iterations, sizeof(*alloc_bytes_sorted), compare_u64);
	qsort(peak_live_bytes_sorted, options->iterations, sizeof(*peak_live_bytes_sorted), compare_u64);
	qsort(retained_bytes_sorted, options->iterations, sizeof(*retained_bytes_sorted), compare_u64);

	if (options->iterations > 0U) {
		result->avg_us = ((double)latency_sum_ns / (double)options->iterations) / 1000.0;
		result->min_us = (double)latency_ns_sorted[0] / 1000.0;
		result->p50_us = percentile_us(latency_ns_sorted, options->iterations, 50U, 100U);
		result->p95_us = percentile_us(latency_ns_sorted, options->iterations, 95U, 100U);
		result->p99_us = percentile_us(latency_ns_sorted, options->iterations, 99U, 100U);
		result->max_us = (double)latency_ns_sorted[options->iterations - 1U] / 1000.0;

		result->avg_alloc_bytes = (double)alloc_sum_bytes / (double)options->iterations;
		result->avg_peak_live_bytes = (double)peak_sum_bytes / (double)options->iterations;
		result->avg_retained_bytes = (double)retained_sum_bytes / (double)options->iterations;

		result->p50_alloc_bytes = percentile_u64(alloc_bytes_sorted, options->iterations, 50U, 100U);
		result->p95_alloc_bytes = percentile_u64(alloc_bytes_sorted, options->iterations, 95U, 100U);
		result->max_alloc_bytes = alloc_bytes_sorted[options->iterations - 1U];

		result->p50_peak_live_bytes = percentile_u64(peak_live_bytes_sorted, options->iterations, 50U, 100U);
		result->p95_peak_live_bytes = percentile_u64(peak_live_bytes_sorted, options->iterations, 95U, 100U);
		result->max_peak_live_bytes = peak_live_bytes_sorted[options->iterations - 1U];

		result->p50_retained_bytes = percentile_u64(retained_bytes_sorted, options->iterations, 50U, 100U);
		result->p95_retained_bytes = percentile_u64(retained_bytes_sorted, options->iterations, 95U, 100U);
		result->max_retained_bytes = retained_bytes_sorted[options->iterations - 1U];
	}

	free(samples);
	free(latency_ns_sorted);
	free(alloc_bytes_sorted);
	free(peak_live_bytes_sorted);
	free(retained_bytes_sorted);
	return 0;
}

static void print_csv_header(void)
{
	printf(
		"mode,workload,target_sql_bytes,actual_sql_bytes,iterations,warmup,total_operations,ok_operations,error_operations,"
		"wall_total_s,throughput_ops_s,avg_us,min_us,p50_us,p95_us,p99_us,max_us,"
		"avg_alloc_bytes,p50_alloc_bytes,p95_alloc_bytes,max_alloc_bytes,"
		"avg_peak_live_bytes,p50_peak_live_bytes,p95_peak_live_bytes,max_peak_live_bytes,"
		"avg_retained_bytes,p50_retained_bytes,p95_retained_bytes,max_retained_bytes,error_message\n");
}

static void print_csv_row(const bench_options_t *options, const bench_result_t *result)
{
	printf(
		"%s,%s,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.9f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
		"%.2f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
		"%.2f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
		"%.2f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",\"%s\"\n",
		bench_mode_name(options->mode),
		workload_name(options->workload),
		options->target_bytes,
		result->sql_length_bytes,
		options->iterations,
		options->warmup_iterations,
		result->total_operations,
		result->ok_operations,
		result->error_operations,
		result->wall_total_s,
		result->throughput_ops_s,
		result->avg_us,
		result->min_us,
		result->p50_us,
		result->p95_us,
		result->p99_us,
		result->max_us,
		result->avg_alloc_bytes,
		result->p50_alloc_bytes,
		result->p95_alloc_bytes,
		result->max_alloc_bytes,
		result->avg_peak_live_bytes,
		result->p50_peak_live_bytes,
		result->p95_peak_live_bytes,
		result->max_peak_live_bytes,
		result->avg_retained_bytes,
		result->p50_retained_bytes,
		result->p95_retained_bytes,
		result->max_retained_bytes,
		result->error_message);
}

int main(int argc, char **argv)
{
	bench_options_t options;
	bench_result_t result;
	char *sql_text;
	int index;
	int have_mode;
	int have_workload;
	int have_length;

	memset(&options, 0, sizeof(options));
	options.iterations = 10000U;
	options.warmup_iterations = 1000U;
	options.print_header = 0;
	options.dump_sql_path = NULL;

	have_mode = 0;
	have_workload = 0;
	have_length = 0;

	for (index = 1; index < argc; index++) {
		if (strcmp(argv[index], "--mode") == 0) {
			if (index + 1 >= argc || parse_mode(argv[index + 1], &options.mode) != 0) {
				print_usage(argv[0]);
				return 1;
			}
			have_mode = 1;
			index++;
		} else if (strcmp(argv[index], "--workload") == 0) {
			if (index + 1 >= argc || parse_workload(argv[index + 1], &options.workload) != 0) {
				print_usage(argv[0]);
				return 1;
			}
			have_workload = 1;
			index++;
		} else if (strcmp(argv[index], "--length-bytes") == 0) {
			if (index + 1 >= argc || parse_positive_size(argv[index + 1], &options.target_bytes) != 0) {
				print_usage(argv[0]);
				return 1;
			}
			have_length = 1;
			index++;
		} else if (strcmp(argv[index], "--iterations") == 0) {
			if (index + 1 >= argc || parse_positive_size(argv[index + 1], &options.iterations) != 0) {
				print_usage(argv[0]);
				return 1;
			}
			index++;
		} else if (strcmp(argv[index], "--warmup") == 0) {
			if (index + 1 >= argc || parse_positive_size(argv[index + 1], &options.warmup_iterations) != 0) {
				print_usage(argv[0]);
				return 1;
			}
			index++;
		} else if (strcmp(argv[index], "--dump-sql") == 0) {
			if (index + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			options.dump_sql_path = argv[index + 1];
			index++;
		} else if (strcmp(argv[index], "--csv-header") == 0) {
			options.print_header = 1;
		} else {
			print_usage(argv[0]);
			return 1;
		}
	}

	if (!have_mode || !have_workload || !have_length) {
		print_usage(argv[0]);
		return 1;
	}

	sql_text = generate_sql(options.workload, options.target_bytes);
	if (sql_text == NULL) {
		fprintf(stderr, "failed to generate SQL for workload %s at %zu bytes\n", workload_name(options.workload), options.target_bytes);
		return 1;
	}

	if (options.dump_sql_path != NULL && write_text_file(options.dump_sql_path, sql_text) != 0) {
		fprintf(stderr, "failed to write generated SQL to %s\n", options.dump_sql_path);
		free(sql_text);
		return 1;
	}

	if (options.print_header) {
		print_csv_header();
	}

	if (run_benchmark(&options, sql_text, &result) != 0) {
		if (result.error_message[0] == '\0') {
			(void)snprintf(result.error_message, sizeof(result.error_message), "benchmark execution failed");
		}
		print_csv_row(&options, &result);
		free(sql_text);
		return 1;
	}

	print_csv_row(&options, &result);
	free(sql_text);
	return 0;
}
