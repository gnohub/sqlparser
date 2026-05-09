#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pg_query.h"

typedef enum {
	BASELINE_MODE_SINGLE_SUCCESS = 0,
	BASELINE_MODE_THREAD_INIT = 1,
	BASELINE_MODE_CONCURRENT_SUCCESS = 2,
	BASELINE_MODE_CONCURRENT_ERROR = 3
} baseline_mode_t;

typedef struct alloc_tracker alloc_tracker_t;

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
	uint64_t latency_ns;
	uint64_t alloc_bytes;
	uint64_t peak_live_bytes;
	uint64_t retained_bytes;
	int ok;
	int actual_parse_error;
	int bad_error_message;
} baseline_sample_t;

typedef struct {
	baseline_mode_t mode;
	size_t target_bytes;
	size_t iterations;
	size_t warmup;
	size_t threads;
	int print_header;
} baseline_options_t;

typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	size_t ready;
	size_t threads;
	int go;
} start_gate_t;

typedef struct {
	const char *sql;
	int expect_error;
	size_t iterations;
	start_gate_t *gate;
	baseline_sample_t *samples;
} worker_args_t;

static __thread alloc_tracker_t *g_active_tracker = NULL;
static const uint64_t ALLOC_HEADER_MAGIC = UINT64_C(0x4C50475142415345);

extern __thread sig_atomic_t pg_query_initialized;

static void baseline_pg_query_shutdown(void)
{
	if (pg_query_initialized != 0) {
		pg_query_exit();
		pg_query_initialized = 0;
	}
}

static void baseline_pg_query_register_exit_once(void)
{
	(void)atexit(baseline_pg_query_shutdown);
}

static void baseline_pg_query_prepare(void)
{
	static pthread_once_t once_control = PTHREAD_ONCE_INIT;

	(void)pthread_once(&once_control, baseline_pg_query_register_exit_once);
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
	fprintf(stderr, "Usage: %s --mode MODE --length-bytes N [options]\n", program);
	fprintf(stderr, "Modes: single-success, thread-init, concurrent-success, concurrent-error\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  --iterations N   measured iterations or thread samples\n");
	fprintf(stderr, "  --warmup N       warmup iterations for single-success\n");
	fprintf(stderr, "  --threads N      worker threads for concurrent modes\n");
	fprintf(stderr, "  --csv-header     print CSV header\n");
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

static int parse_mode(const char *value, baseline_mode_t *out_mode)
{
	if (strcmp(value, "single-success") == 0) {
		*out_mode = BASELINE_MODE_SINGLE_SUCCESS;
		return 0;
	}
	if (strcmp(value, "thread-init") == 0) {
		*out_mode = BASELINE_MODE_THREAD_INIT;
		return 0;
	}
	if (strcmp(value, "concurrent-success") == 0) {
		*out_mode = BASELINE_MODE_CONCURRENT_SUCCESS;
		return 0;
	}
	if (strcmp(value, "concurrent-error") == 0) {
		*out_mode = BASELINE_MODE_CONCURRENT_ERROR;
		return 0;
	}
	return -1;
}

static const char *mode_name(baseline_mode_t mode)
{
	switch (mode) {
		case BASELINE_MODE_SINGLE_SUCCESS:
			return "single-success";
		case BASELINE_MODE_THREAD_INIT:
			return "thread-init";
		case BASELINE_MODE_CONCURRENT_SUCCESS:
			return "concurrent-success";
		case BASELINE_MODE_CONCURRENT_ERROR:
			return "concurrent-error";
		default:
			return "unknown";
	}
}

static int parse_args(int argc, char **argv, baseline_options_t *options)
{
	int index;

	memset(options, 0, sizeof(*options));
	options->mode = BASELINE_MODE_SINGLE_SUCCESS;
	options->target_bytes = 1024U;
	options->iterations = 1000U;
	options->warmup = 100U;
	options->threads = 1U;

	for (index = 1; index < argc; index++) {
		if (strcmp(argv[index], "--mode") == 0 && index + 1 < argc) {
			if (parse_mode(argv[++index], &options->mode) != 0) {
				return -1;
			}
		} else if (strcmp(argv[index], "--length-bytes") == 0 && index + 1 < argc) {
			if (parse_positive_size(argv[++index], &options->target_bytes) != 0) {
				return -1;
			}
		} else if (strcmp(argv[index], "--iterations") == 0 && index + 1 < argc) {
			if (parse_positive_size(argv[++index], &options->iterations) != 0) {
				return -1;
			}
		} else if (strcmp(argv[index], "--warmup") == 0 && index + 1 < argc) {
			if (parse_positive_size(argv[++index], &options->warmup) != 0) {
				return -1;
			}
		} else if (strcmp(argv[index], "--threads") == 0 && index + 1 < argc) {
			if (parse_positive_size(argv[++index], &options->threads) != 0) {
				return -1;
			}
		} else if (strcmp(argv[index], "--csv-header") == 0) {
			options->print_header = 1;
		} else if (strcmp(argv[index], "--help") == 0) {
			print_usage(argv[0]);
			exit(0);
		} else {
			return -1;
		}
	}

	return 0;
}

static char *generate_insert_values_sql(size_t target_bytes)
{
	char *buffer;
	size_t len;
	size_t cap;
	size_t row_index;
	const char *base = "INSERT INTO bench_t (id, name) VALUES (1, 'seed')";

	cap = target_bytes + 128U;
	buffer = (char *)malloc(cap);
	if (buffer == NULL) {
		return NULL;
	}

	len = strlen(base);
	if (target_bytes < len) {
		free(buffer);
		return NULL;
	}
	memcpy(buffer, base, len + 1U);

	row_index = 2U;
	while (len + 32U < target_bytes) {
		int written;

		written = snprintf(buffer + len, cap - len, ", (%zu, 'row%zu')", row_index, row_index);
		if (written <= 0 || (size_t)written >= cap - len) {
			free(buffer);
			return NULL;
		}
		if (len + (size_t)written > target_bytes) {
			break;
		}
		len += (size_t)written;
		row_index++;
	}

	if (len < target_bytes) {
		size_t fill_len;
		const char *prefix = ", (999999, '";
		const char *suffix = "')";
		size_t prefix_len = strlen(prefix);
		size_t suffix_len = strlen(suffix);

		if (len + prefix_len + suffix_len + 1U <= target_bytes) {
			memcpy(buffer + len, prefix, prefix_len);
			len += prefix_len;
			fill_len = target_bytes - len - suffix_len;
			memset(buffer + len, 'x', fill_len);
			len += fill_len;
			memcpy(buffer + len, suffix, suffix_len);
			len += suffix_len;
		} else {
			memset(buffer + len, ' ', target_bytes - len);
			len = target_bytes;
		}
	}

	buffer[len] = '\0';
	return buffer;
}

static char *generate_invalid_sql(size_t target_bytes)
{
	char *buffer;
	size_t len;
	const char *base = "SELECT FROM bench_t WHERE id =";

	len = strlen(base);
	if (target_bytes < len) {
		target_bytes = len;
	}

	buffer = (char *)malloc(target_bytes + 1U);
	if (buffer == NULL) {
		return NULL;
	}
	memcpy(buffer, base, len);
	if (len < target_bytes) {
		memset(buffer + len, ' ', target_bytes - len);
	}
	buffer[target_bytes] = '\0';
	return buffer;
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static int error_message_is_expected(const PgQueryError *error)
{
	if (error == NULL || error->message == NULL) {
		return 0;
	}
	return strstr(error->message, "syntax error") != NULL;
}

static baseline_sample_t parse_once(const char *sql, int expect_error)
{
	PgQueryProtobufParseResult result;
	alloc_tracker_t tracker;
	baseline_sample_t sample;
	uint64_t start_ns;
	uint64_t end_ns;

	memset(&sample, 0, sizeof(sample));
	alloc_tracker_begin(&tracker);
	start_ns = now_ns();
	result = pg_query_parse_protobuf(sql);
	if (result.error != NULL) {
		sample.actual_parse_error = 1;
	}
	if (expect_error) {
		sample.ok = sample.actual_parse_error;
		sample.bad_error_message = sample.actual_parse_error && !error_message_is_expected(result.error);
		if (sample.bad_error_message) {
			sample.ok = 0;
		}
	} else {
		sample.ok = !sample.actual_parse_error;
	}
	pg_query_free_protobuf_parse_result(result);
	end_ns = now_ns();
	alloc_tracker_end(&tracker);

	sample.latency_ns = end_ns - start_ns;
	sample.alloc_bytes = tracker.total_alloc_bytes;
	sample.peak_live_bytes = tracker.peak_live_bytes;
	sample.retained_bytes = tracker.retained_bytes;

	return sample;
}

static int compare_sample_latency(const void *lhs, const void *rhs)
{
	const baseline_sample_t *left = (const baseline_sample_t *)lhs;
	const baseline_sample_t *right = (const baseline_sample_t *)rhs;

	if (left->latency_ns < right->latency_ns) {
		return -1;
	}
	if (left->latency_ns > right->latency_ns) {
		return 1;
	}
	return 0;
}

static int compare_u64(const void *lhs, const void *rhs)
{
	const uint64_t *left = (const uint64_t *)lhs;
	const uint64_t *right = (const uint64_t *)rhs;

	if (*left < *right) {
		return -1;
	}
	if (*left > *right) {
		return 1;
	}
	return 0;
}

static size_t percentile_index(size_t count, size_t numerator, size_t denominator)
{
	if (count == 0U || denominator == 0U) {
		return 0U;
	}
	return ((count - 1U) * numerator) / denominator;
}

static double ns_to_ms(uint64_t value)
{
	return (double)value / 1000000.0;
}

static void print_header(void)
{
	printf(
		"mode,target_sql_bytes,actual_sql_bytes,threads,iterations,warmup,total_operations,"
		"ok_operations,unexpected_operations,actual_parse_errors,bad_error_messages,"
		"wall_total_s,throughput_ops_s,avg_ms,min_ms,p50_ms,p95_ms,p99_ms,max_ms,"
		"avg_alloc_bytes,p50_alloc_bytes,p95_alloc_bytes,max_alloc_bytes,"
		"avg_peak_live_bytes,p50_peak_live_bytes,p95_peak_live_bytes,max_peak_live_bytes,"
		"avg_retained_bytes,p50_retained_bytes,p95_retained_bytes,max_retained_bytes\n");
}

static int print_summary_row(
	const char *row_mode,
	size_t target_bytes,
	size_t actual_bytes,
	size_t threads,
	size_t iterations,
	size_t warmup,
	const baseline_sample_t *samples,
	size_t count,
	uint64_t wall_ns)
{
	baseline_sample_t *latency_sorted;
	uint64_t *alloc_values;
	uint64_t *peak_values;
	uint64_t *retained_values;
	size_t index;
	size_t ok_count;
	size_t unexpected_count;
	size_t parse_error_count;
	size_t bad_error_count;
	long double total_latency_ns;
	long double total_alloc_bytes;
	long double total_peak_live_bytes;
	long double total_retained_bytes;
	uint64_t min_latency;
	uint64_t max_latency;
	uint64_t p50_alloc_bytes;
	uint64_t p95_alloc_bytes;
	uint64_t max_alloc_bytes;
	uint64_t p50_peak_live_bytes;
	uint64_t p95_peak_live_bytes;
	uint64_t max_peak_live_bytes;
	uint64_t p50_retained_bytes;
	uint64_t p95_retained_bytes;
	uint64_t max_retained_bytes;

	if (count == 0U) {
		return -1;
	}

	latency_sorted = (baseline_sample_t *)malloc(sizeof(*latency_sorted) * count);
	alloc_values = (uint64_t *)malloc(sizeof(*alloc_values) * count);
	peak_values = (uint64_t *)malloc(sizeof(*peak_values) * count);
	retained_values = (uint64_t *)malloc(sizeof(*retained_values) * count);
	if (latency_sorted == NULL || alloc_values == NULL || peak_values == NULL || retained_values == NULL) {
		free(latency_sorted);
		free(alloc_values);
		free(peak_values);
		free(retained_values);
		return -1;
	}

	memcpy(latency_sorted, samples, sizeof(*latency_sorted) * count);
	qsort(latency_sorted, count, sizeof(*latency_sorted), compare_sample_latency);

	ok_count = 0U;
	unexpected_count = 0U;
	parse_error_count = 0U;
	bad_error_count = 0U;
	total_latency_ns = 0.0L;
	total_alloc_bytes = 0.0L;
	total_peak_live_bytes = 0.0L;
	total_retained_bytes = 0.0L;
	min_latency = latency_sorted[0].latency_ns;
	max_latency = latency_sorted[count - 1U].latency_ns;

	for (index = 0U; index < count; index++) {
		if (samples[index].ok) {
			ok_count++;
		} else {
			unexpected_count++;
		}
		if (samples[index].actual_parse_error) {
			parse_error_count++;
		}
		if (samples[index].bad_error_message) {
			bad_error_count++;
		}
		total_latency_ns += (long double)samples[index].latency_ns;
		total_alloc_bytes += (long double)samples[index].alloc_bytes;
		total_peak_live_bytes += (long double)samples[index].peak_live_bytes;
		total_retained_bytes += (long double)samples[index].retained_bytes;
		alloc_values[index] = samples[index].alloc_bytes;
		peak_values[index] = samples[index].peak_live_bytes;
		retained_values[index] = samples[index].retained_bytes;
	}

	qsort(alloc_values, count, sizeof(*alloc_values), compare_u64);
	qsort(peak_values, count, sizeof(*peak_values), compare_u64);
	qsort(retained_values, count, sizeof(*retained_values), compare_u64);
	p50_alloc_bytes = alloc_values[percentile_index(count, 50U, 100U)];
	p95_alloc_bytes = alloc_values[percentile_index(count, 95U, 100U)];
	max_alloc_bytes = alloc_values[count - 1U];
	p50_peak_live_bytes = peak_values[percentile_index(count, 50U, 100U)];
	p95_peak_live_bytes = peak_values[percentile_index(count, 95U, 100U)];
	max_peak_live_bytes = peak_values[count - 1U];
	p50_retained_bytes = retained_values[percentile_index(count, 50U, 100U)];
	p95_retained_bytes = retained_values[percentile_index(count, 95U, 100U)];
	max_retained_bytes = retained_values[count - 1U];

	printf(
		"%s,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,"
		"%.9f,%.4f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
		"%.4Lf,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
		"%.4Lf,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
		"%.4Lf,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
		row_mode,
		target_bytes,
		actual_bytes,
		threads,
		iterations,
		warmup,
		count,
		ok_count,
		unexpected_count,
		parse_error_count,
		bad_error_count,
		(double)wall_ns / 1000000000.0,
		wall_ns > 0U ? ((double)count * 1000000000.0) / (double)wall_ns : 0.0,
		ns_to_ms((uint64_t)(total_latency_ns / (long double)count)),
		ns_to_ms(min_latency),
		ns_to_ms(latency_sorted[percentile_index(count, 50U, 100U)].latency_ns),
		ns_to_ms(latency_sorted[percentile_index(count, 95U, 100U)].latency_ns),
		ns_to_ms(latency_sorted[percentile_index(count, 99U, 100U)].latency_ns),
		ns_to_ms(max_latency),
		total_alloc_bytes / (long double)count,
		p50_alloc_bytes,
		p95_alloc_bytes,
		max_alloc_bytes,
		total_peak_live_bytes / (long double)count,
		p50_peak_live_bytes,
		p95_peak_live_bytes,
		max_peak_live_bytes,
		total_retained_bytes / (long double)count,
		p50_retained_bytes,
		p95_retained_bytes,
		max_retained_bytes);

	free(latency_sorted);
	free(alloc_values);
	free(peak_values);
	free(retained_values);
	return unexpected_count == 0U ? 0 : 1;
}

static int run_single_success(const baseline_options_t *options, const char *sql)
{
	baseline_sample_t *samples;
	size_t index;
	uint64_t start_ns;
	uint64_t end_ns;
	int status;

	for (index = 0U; index < options->warmup; index++) {
		baseline_sample_t sample = parse_once(sql, 0);
		if (!sample.ok) {
			return 1;
		}
	}

	samples = (baseline_sample_t *)calloc(options->iterations, sizeof(*samples));
	if (samples == NULL) {
		return 1;
	}

	start_ns = now_ns();
	for (index = 0U; index < options->iterations; index++) {
		samples[index] = parse_once(sql, 0);
	}
	end_ns = now_ns();

	status = print_summary_row(
		mode_name(options->mode),
		options->target_bytes,
		strlen(sql),
		1U,
		options->iterations,
		options->warmup,
		samples,
		options->iterations,
		end_ns - start_ns);
	free(samples);
	return status;
}

static void *thread_init_worker(void *arg)
{
	worker_args_t *worker = (worker_args_t *)arg;

	worker->samples[0] = parse_once(worker->sql, 0);
	worker->samples[1] = parse_once(worker->sql, 0);
	return NULL;
}

static int run_thread_init(const baseline_options_t *options, const char *sql)
{
	baseline_sample_t *first_samples;
	baseline_sample_t *second_samples;
	size_t index;
	int status;

	first_samples = (baseline_sample_t *)calloc(options->iterations, sizeof(*first_samples));
	second_samples = (baseline_sample_t *)calloc(options->iterations, sizeof(*second_samples));
	if (first_samples == NULL || second_samples == NULL) {
		free(first_samples);
		free(second_samples);
		return 1;
	}

	for (index = 0U; index < options->iterations; index++) {
		pthread_t thread;
		baseline_sample_t samples[2];
		worker_args_t args;

		memset(samples, 0, sizeof(samples));
		args.sql = sql;
		args.expect_error = 0;
		args.iterations = 2U;
		args.gate = NULL;
		args.samples = samples;
		if (pthread_create(&thread, NULL, thread_init_worker, &args) != 0) {
			free(first_samples);
			free(second_samples);
			return 1;
		}
		if (pthread_join(thread, NULL) != 0) {
			free(first_samples);
			free(second_samples);
			return 1;
		}
		first_samples[index] = samples[0];
		second_samples[index] = samples[1];
	}

	status = print_summary_row(
		"thread-first-success",
		options->target_bytes,
		strlen(sql),
		1U,
		options->iterations,
		0U,
		first_samples,
		options->iterations,
		0U);
	status |= print_summary_row(
		"thread-second-success",
		options->target_bytes,
		strlen(sql),
		1U,
		options->iterations,
		0U,
		second_samples,
		options->iterations,
		0U);

	free(first_samples);
	free(second_samples);
	return status;
}

static int start_gate_init(start_gate_t *gate, size_t threads)
{
	if (pthread_mutex_init(&gate->mutex, NULL) != 0) {
		return -1;
	}
	if (pthread_cond_init(&gate->cond, NULL) != 0) {
		pthread_mutex_destroy(&gate->mutex);
		return -1;
	}
	gate->ready = 0U;
	gate->threads = threads;
	gate->go = 0;
	return 0;
}

static void start_gate_destroy(start_gate_t *gate)
{
	pthread_cond_destroy(&gate->cond);
	pthread_mutex_destroy(&gate->mutex);
}

static int start_gate_wait_ready(start_gate_t *gate)
{
	if (pthread_mutex_lock(&gate->mutex) != 0) {
		return -1;
	}
	while (gate->ready < gate->threads) {
		if (pthread_cond_wait(&gate->cond, &gate->mutex) != 0) {
			pthread_mutex_unlock(&gate->mutex);
			return -1;
		}
	}
	pthread_mutex_unlock(&gate->mutex);
	return 0;
}

static int start_gate_release(start_gate_t *gate)
{
	if (pthread_mutex_lock(&gate->mutex) != 0) {
		return -1;
	}
	gate->go = 1;
	pthread_cond_broadcast(&gate->cond);
	pthread_mutex_unlock(&gate->mutex);
	return 0;
}

static int start_gate_worker_wait(start_gate_t *gate)
{
	if (pthread_mutex_lock(&gate->mutex) != 0) {
		return -1;
	}
	gate->ready++;
	pthread_cond_broadcast(&gate->cond);
	while (!gate->go) {
		if (pthread_cond_wait(&gate->cond, &gate->mutex) != 0) {
			pthread_mutex_unlock(&gate->mutex);
			return -1;
		}
	}
	pthread_mutex_unlock(&gate->mutex);
	return 0;
}

static void *concurrent_worker(void *arg)
{
	worker_args_t *worker = (worker_args_t *)arg;
	size_t index;

	if (start_gate_worker_wait(worker->gate) != 0) {
		return NULL;
	}

	for (index = 0U; index < worker->iterations; index++) {
		worker->samples[index] = parse_once(worker->sql, worker->expect_error);
	}
	return NULL;
}

static int run_concurrent(const baseline_options_t *options, const char *sql, int expect_error)
{
	pthread_t *threads;
	worker_args_t *args;
	baseline_sample_t *samples;
	start_gate_t gate;
	size_t total_operations;
	size_t index;
	int status;
	uint64_t start_ns;
	uint64_t end_ns;

	total_operations = options->threads * options->iterations;
	threads = (pthread_t *)calloc(options->threads, sizeof(*threads));
	args = (worker_args_t *)calloc(options->threads, sizeof(*args));
	samples = (baseline_sample_t *)calloc(total_operations, sizeof(*samples));
	if (threads == NULL || args == NULL || samples == NULL) {
		free(threads);
		free(args);
		free(samples);
		return 1;
	}
	if (start_gate_init(&gate, options->threads) != 0) {
		free(threads);
		free(args);
		free(samples);
		return 1;
	}

	for (index = 0U; index < options->threads; index++) {
		args[index].sql = sql;
		args[index].expect_error = expect_error;
		args[index].iterations = options->iterations;
		args[index].gate = &gate;
		args[index].samples = samples + (index * options->iterations);
		if (pthread_create(&threads[index], NULL, concurrent_worker, &args[index]) != 0) {
			start_gate_destroy(&gate);
			free(threads);
			free(args);
			free(samples);
			return 1;
		}
	}

	if (start_gate_wait_ready(&gate) != 0) {
		start_gate_destroy(&gate);
		free(threads);
		free(args);
		free(samples);
		return 1;
	}
	start_ns = now_ns();
	(void)start_gate_release(&gate);
	for (index = 0U; index < options->threads; index++) {
		(void)pthread_join(threads[index], NULL);
	}
	end_ns = now_ns();

	status = print_summary_row(
		mode_name(options->mode),
		options->target_bytes,
		strlen(sql),
		options->threads,
		options->iterations,
		0U,
		samples,
		total_operations,
		end_ns - start_ns);

	start_gate_destroy(&gate);
	free(threads);
	free(args);
	free(samples);
	return status;
}

int main(int argc, char **argv)
{
	baseline_options_t options;
	char *sql;
	int status;

	if (parse_args(argc, argv, &options) != 0) {
		print_usage(argv[0]);
		return 2;
	}

	baseline_pg_query_prepare();
	if (options.mode == BASELINE_MODE_CONCURRENT_ERROR) {
		sql = generate_invalid_sql(options.target_bytes);
	} else {
		sql = generate_insert_values_sql(options.target_bytes);
	}
	if (sql == NULL) {
		fprintf(stderr, "failed to generate SQL with target length %zu\n", options.target_bytes);
		return 2;
	}

	if (options.print_header) {
		print_header();
	}

	switch (options.mode) {
		case BASELINE_MODE_SINGLE_SUCCESS:
			status = run_single_success(&options, sql);
			break;
		case BASELINE_MODE_THREAD_INIT:
			status = run_thread_init(&options, sql);
			break;
		case BASELINE_MODE_CONCURRENT_SUCCESS:
			status = run_concurrent(&options, sql, 0);
			break;
		case BASELINE_MODE_CONCURRENT_ERROR:
			status = run_concurrent(&options, sql, 1);
			break;
		default:
			status = 2;
			break;
	}

	free(sql);
	return status;
}
