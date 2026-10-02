#include "pg_query.h"
#include "pg_query_internal.h"

#include <mb/pg_wchar.h>
#include <utils/memutils.h>
#include <utils/memdebug.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include <signal.h>

const char* progname = "pg_query";

__thread sig_atomic_t pg_query_initialized = 0;

#ifdef HAVE_PTHREAD
static pthread_key_t pg_query_thread_exit_key;
static pthread_once_t pg_query_thread_exit_once = PTHREAD_ONCE_INIT;
static int pg_query_thread_exit_key_error;
static __thread bool pg_query_thread_exit_registered;
static void pg_query_thread_exit(void *key);

static void pg_query_create_thread_exit_key(void)
{
	pg_query_thread_exit_key_error =
		pthread_key_create(&pg_query_thread_exit_key, pg_query_thread_exit);
}
#endif

void pg_query_init(void)
{
	if (pg_query_initialized != 0) return;
	MemoryContextInit();
	SetDatabaseEncoding(PG_UTF8);
	pg_query_initialized = 1;

#ifdef HAVE_PTHREAD
	/* A key is a process-wide resource, not a per-thread resource. */
	pg_query_thread_exit_registered =
		pthread_once(&pg_query_thread_exit_once, pg_query_create_thread_exit_key) == 0 &&
		pg_query_thread_exit_key_error == 0 &&
		pthread_setspecific(pg_query_thread_exit_key, TopMemoryContext) == 0;
#endif
}

void pg_query_free_top_memory_context(MemoryContext context)
{
	Assert(MemoryContextIsValid(context));

	/*
	 * After this, no memory contexts are valid anymore, so ensure that
	 * the current context is the top-level context.
	 */
	Assert(TopMemoryContext == CurrentMemoryContext);

	MemoryContextDeleteChildren(context);

	/* TopMemoryContext uses ALLOCSET_DEFAULT_SIZES, so delete_context puts
	 * its header/keeper block on the aset freelist rather than freeing it.
	 * Drain that list afterwards, including TopMemoryContext itself. A
	 * separate free(context) would leave a dangling entry on the freelist
	 * and make a later initialization reuse freed storage. */
	context->methods->delete_context(context);

	VALGRIND_DESTROY_MEMPOOL(context);
	AllocSetDeleteFreeList(context);

	/* Reset pointers */
	TopMemoryContext = NULL;
	CurrentMemoryContext = NULL;
	ErrorContext = NULL;
	pg_query_initialized = 0;
}

#ifdef HAVE_PTHREAD
static void pg_query_thread_exit(void *key)
{
	/* Explicit cleanup may have already released the saved pointer. The
	 * current thread-local context is the authoritative owner. */
	(void)key;
	pg_query_exit();
}
#endif

void pg_query_exit(void)
{
#ifdef HAVE_PTHREAD
	if (pg_query_thread_exit_registered) {
		(void)pthread_setspecific(pg_query_thread_exit_key, NULL);
		pg_query_thread_exit_registered = false;
	}
#endif
	if (TopMemoryContext != NULL)
		pg_query_free_top_memory_context(TopMemoryContext);
	pg_query_initialized = 0;
}

MemoryContext pg_query_enter_memory_context()
{
	MemoryContext ctx = NULL;

	pg_query_init();

	Assert(CurrentMemoryContext == TopMemoryContext);
	ctx = AllocSetContextCreate(TopMemoryContext,
								"pg_query",
								ALLOCSET_DEFAULT_SIZES);
	MemoryContextSwitchTo(ctx);

	return ctx;
}

void pg_query_exit_memory_context(MemoryContext ctx)
{
	// Return to previous PostgreSQL memory context
	MemoryContextSwitchTo(TopMemoryContext);

	MemoryContextDelete(ctx);
	ctx = NULL;

#ifdef HAVE_PTHREAD
	/* Resource pressure can prevent installing a thread destructor. In that
	 * case retain no top-level context between public calls. All returned
	 * results use malloc-owned storage, independent of these contexts. */
	if (!pg_query_thread_exit_registered)
		pg_query_exit();
#endif
}

void pg_query_free_error(PgQueryError *error)
{
	free(error->message);
	free(error->funcname);
	free(error->filename);

	if (error->context) {
		free(error->context);
	}

	free(error);
}
