/* Process-wide TLS key ownership and safe explicit/thread cleanup. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(SQLPARSER_TEST_NO_PTHREAD)
int main(void) { puts("SKIP: parser thread lifecycle requires POSIX pthreads"); return 0; }
#else
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/wait.h>
#include <unistd.h>
#include "sqlparser/sqlparser.h"
#include "pg_query.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
static atomic_size_t key_creates;
static atomic_int count_keys, fail_key, fail_association, fail_clear;
#ifdef SQLPARSER_THREAD_LIFECYCLE_WRAPPERS
int __real_pthread_key_create(pthread_key_t *, void (*)(void *));
int __real_pthread_setspecific(pthread_key_t, const void *);
int __wrap_pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    if (atomic_load(&count_keys)) atomic_fetch_add(&key_creates, 1);
    if (atomic_load(&fail_key)) return EAGAIN;
    return __real_pthread_key_create(key, destructor);
}
int __wrap_pthread_setspecific(pthread_key_t key, const void *value) {
    if ((value != NULL && atomic_load(&fail_association)) ||
        (value == NULL && atomic_load(&fail_clear))) return ENOMEM;
    return __real_pthread_setspecific(key, value);
}
#endif
static size_t keys_available(void) {
    pthread_key_t keys[4096];
    size_t count = 0;
    while (count < sizeof(keys)/sizeof(keys[0]) &&
           pthread_key_create(&keys[count], NULL) == 0) count++;
    CHECK(count < sizeof(keys)/sizeof(keys[0]));
    for (size_t i = 0; i < count; i++) CHECK(pthread_key_delete(keys[i]) == 0);
    return count;
}
static void parse_cycle(int explicit_cleanup) {
    sqlparser_handle_t *handle = NULL;
    sqlparser_error_t error;
    sqlparser_query_graph_view_t graph;
    char *output = NULL;
    const char *sql = "SELECT 'Case', 1 FROM t WHERE id = 2";
    sqlparser_status_t parse_status = sqlparser_parse(sql, &handle, &error);
    CHECK(parse_status == SQLPARSER_STATUS_OK);
    sqlparser_status_t graph_status = sqlparser_statement_query_graph(handle, 0, &graph, &error);
    if (graph_status != SQLPARSER_STATUS_OK) fprintf(stderr, "graph status=%d message=%s\n", graph_status, error.message);
    CHECK(graph_status == SQLPARSER_STATUS_OK);
    /* Returned handles must outlive parser context cleanup. */
    if (explicit_cleanup) { pg_query_exit(); pg_query_exit(); }
    CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(output, sql) == 0);
    sqlparser_string_free(output);
    sqlparser_handle_destroy(handle);
    handle = NULL;
    CHECK(sqlparser_parse("SELECT )", &handle, &error) == SQLPARSER_STATUS_PARSE_ERROR);
    CHECK(handle == NULL);
    if (explicit_cleanup) pg_query_exit();
}
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    size_t ready;
    int start;
} gate_t;
static void *worker(void *arg) {
    gate_t *gate = arg;
    if (gate != NULL) {
        CHECK(pthread_mutex_lock(&gate->mutex) == 0);
        gate->ready++;
        CHECK(pthread_cond_broadcast(&gate->cond) == 0);
        while (!gate->start) CHECK(pthread_cond_wait(&gate->cond, &gate->mutex) == 0);
        CHECK(pthread_mutex_unlock(&gate->mutex) == 0);
    }
    for (int i = 0; i < 3; i++) parse_cycle(i == 1);
    if (atomic_load(&fail_clear)) pg_query_exit();
    return NULL;
}
static void failure_child(int failure_kind) {
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        pthread_t thread;
        atomic_store(&fail_key, failure_kind == 0);
        atomic_store(&fail_association, failure_kind == 1);
        atomic_store(&fail_clear, failure_kind == 2);
        atomic_store(&count_keys, 1);
        pg_query_exit();
        for (int i = 0; i < 5; i++) parse_cycle(i & 1);
        CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);
        CHECK(pthread_join(thread, NULL) == 0);
        pg_query_exit(); pg_query_exit();
#ifdef SQLPARSER_THREAD_LIFECYCLE_WRAPPERS
        CHECK(atomic_load(&key_creates) == 1);
#endif
        exit(0);
    }
    int status;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
int main(void) {
    enum { CONCURRENT = 16, SEQUENTIAL = 1200 };
    pthread_t threads[CONCURRENT];
    gate_t gate = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0 };
    size_t before = keys_available();
    /* These children inherit an uninitialized once-control, so each forced
     * failure covers its first initialization and repeated later calls. */
    failure_child(0); failure_child(1); failure_child(2);
    atomic_store(&count_keys, 1);
    for (size_t i = 0; i < CONCURRENT; i++) CHECK(pthread_create(&threads[i], NULL, worker, &gate) == 0);
    CHECK(pthread_mutex_lock(&gate.mutex) == 0);
    while (gate.ready != CONCURRENT) CHECK(pthread_cond_wait(&gate.cond, &gate.mutex) == 0);
    gate.start = 1;
    CHECK(pthread_cond_broadcast(&gate.cond) == 0);
    CHECK(pthread_mutex_unlock(&gate.mutex) == 0);
    for (size_t i = 0; i < CONCURRENT; i++) CHECK(pthread_join(threads[i], NULL) == 0);
    for (size_t i = 0; i < SEQUENTIAL; i++) {
        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);
        CHECK(pthread_join(thread, NULL) == 0);
    }
    pg_query_exit();
    parse_cycle(1); parse_cycle(0); pg_query_exit(); pg_query_exit();
    atomic_store(&count_keys, 0);
    size_t after = keys_available();
    CHECK(before == after + 1);
#ifdef SQLPARSER_THREAD_LIFECYCLE_WRAPPERS
    CHECK(atomic_load(&key_creates) == 1);
#endif
    CHECK(pthread_mutex_destroy(&gate.mutex) == 0);
    CHECK(pthread_cond_destroy(&gate.cond) == 0);
    printf("TLS lifecycle passed: %d concurrent + %d sequential threads, one process key, explicit cleanup/reinit and forced key/association/clear failures\n", CONCURRENT, SEQUENTIAL);
    return 0;
}
#endif
