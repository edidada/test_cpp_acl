/*
 * test_c_thread.cpp
 *
 * C threading self test for the ACL core library (lib_acl).
 *
 * NOTE: the reference doc's high-level ACL_THREAD/ACL_THREAD_MUTEX object API
 * does not exist in this ACL version; the verified cross-platform pthread-style
 * wrappers are used instead:
 *   - acl_thread_create/join  -> acl_pthread_create / acl_pthread_join
 *   - acl_thread_self         -> acl_pthread_self / acl_main_thread_self
 *   - acl_thread_mutex_*      -> acl_pthread_mutex_* (+ acl_thread_mutex_lock)
 *   - acl_thread_rwlock_*     -> acl_pthread_rwlock_*
 *   - acl_thread_sem_*        -> acl_sem_* (Windows build; guarded below)
 *   - acl_thread_cond_*       -> acl_pthread_cond_* / acl_thread_cond_create
 *   - acl_thread_pool_run/free/threads_count/task_qlen
 *                            -> acl_pthread_pool_add_one / _destroy / _size / _qlen
 */

#if defined(_WIN32) || defined(_WIN64)
/* MinGW-w64 defines struct timezone/timespec under these include guards;
 * pre-defining them lets ACL's own definitions win without a redefinition
 * clash, so this file compiles out-of-the-box under MinGW. */
# define _TIMEZONE_DEFINED
# define _TIMESPEC_DEFINED
#endif

#include <lib_acl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/*
 * Many assertions below wrap side-effecting ACL calls, e.g.
 *   assert(acl_pthread_mutex_init(&m, NULL) == 0);
 * In a Release build NDEBUG turns assert() into a no-op AND the enclosed
 * expression is never evaluated, so the mutex/cond/semaphore would stay
 * uninitialised and the later real calls crash or hang. Force assert() to be
 * live so those setup calls always run while still validating their result.
 */
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <assert.h>

/* ---- plain thread: create + join + self ------------------------------ */
static volatile int g_thread_ran = 0;

static void *worker_inc(void *arg)
{
	int *p = (int *) arg;
	unsigned long self = (unsigned long) acl_pthread_self();

	if (p != NULL) {
		*p += 1;
	}
	g_thread_ran = 1;
	printf("  worker running, self id=%lu, counter now=%d\n",
		self, p ? *p : -1);
	return NULL;
}

static void test_thread(void)
{
	printf("--- thread create/join/self ---\n");

	unsigned long main_id = acl_main_thread_self();
	printf("  main thread id=%lu\n", main_id);

	int shared = 0;
	acl_pthread_t th;
	int rc = acl_pthread_create(&th, NULL, worker_inc, &shared);
	assert(rc == 0);
	assert(shared == 0 || shared == 1); /* may or may not have run yet */

	rc = acl_pthread_join(th, NULL);
	assert(rc == 0);

	assert(g_thread_ran == 1);
	assert(shared == 1);
	printf("  joined, shared=%d\n", shared);

	/* spawn several threads that each increment under a lock-free volatile
	 * (single-writer style) is racy, so just verify join returns. */
	g_thread_ran = 0;
	acl_pthread_t th2;
	rc = acl_pthread_create(&th2, NULL, worker_inc, NULL);
	assert(rc == 0);
	acl_pthread_join(th2, NULL);
	assert(g_thread_ran == 1);
	printf("  second thread ok\n");
}

/* ---- thread pool ----------------------------------------------------- */
#define POOL_TASKS 50

typedef struct {
	int                 count;
	acl_pthread_mutex_t lock;
} pool_ctx_t;

static void pool_job(void *arg)
{
	pool_ctx_t *ctx = (pool_ctx_t *) arg;

	acl_pthread_mutex_lock(&ctx->lock);
	ctx->count++;
	acl_pthread_mutex_unlock(&ctx->lock);
}

static void test_thread_pool(void)
{
	printf("--- thread pool ---\n");

	acl_pthread_pool_t *pool = acl_thread_pool_create(8, 1);
	assert(pool != NULL);
	printf("  created pool, threads_limit=%d, size=%d\n",
		acl_pthread_pool_limit(pool), acl_pthread_pool_size(pool));

	pool_ctx_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	acl_pthread_mutex_init(&ctx.lock, NULL);

	for (int i = 0; i < POOL_TASKS; i++) {
		acl_pthread_pool_add_one(pool, pool_job, &ctx);
	}

	printf("  queued=%d immediately after submit\n",
		acl_pthread_pool_qlen(pool));

	/* wait (bounded) until all jobs have run */
	int guard = 0;
	while (ctx.count < POOL_TASKS && guard++ < 5000) {
		acl_doze(2);
	}
	printf("  finished %d/%d tasks after %d polls\n",
		ctx.count, POOL_TASKS, guard);
	assert(ctx.count == POOL_TASKS);

	acl_pthread_mutex_destroy(&ctx.lock);

	int rc = acl_pthread_pool_destroy(pool);
	printf("  destroy returned %d\n", rc);
	printf("  thread pool ok\n");
}

/* ---- mutex ----------------------------------------------------------- */
static void test_mutex(void)
{
	printf("--- mutex ---\n");

	acl_pthread_mutex_t m;
	assert(acl_pthread_mutex_init(&m, NULL) == 0);

	/* recursive lock is not guaranteed here; just exercise lock/unlock */
	acl_pthread_mutex_lock(&m);
	printf("  locked\n");
	acl_pthread_mutex_unlock(&m);
	printf("  unlocked\n");

	/* trylock on an unlocked mutex should succeed (0) */
	int rc = acl_pthread_mutex_trylock(&m);
	printf("  trylock -> %d\n", rc);
	assert(rc == 0);
	acl_pthread_mutex_unlock(&m);

	/* cross-platform convenience wrappers */
	acl_thread_mutex_lock(&m);
	acl_thread_mutex_unlock(&m);
	printf("  acl_thread_mutex_lock/unlock ok\n");

	assert(acl_pthread_mutex_destroy(&m) == 0);
	printf("  mutex ok\n");
}

/* ---- rwlock ----------------------------------------------------------
 * 注: ACL 在 Windows 上通过 acl_pthread_rwlock.h 定义 ACL_HAVE_NO_RWLOCK,
 * 但 acl_pthread_rwlock.c 源文件里对该宏的检查发生在 include 该头之前,
 * 导致 Windows 上 rwlock_* 函数虽然被声明却没有实际实现. 这里在 Windows 上跳过。
 * ---------------------------------------------------------- */
#if !defined(_WIN32) && !defined(_WIN64)
static void test_rwlock(void)
{
	printf("--- rwlock ---\n");

	acl_pthread_rwlock_t rw = NULL;
	assert(acl_pthread_rwlock_init(&rw, NULL) == 0);

	/* read lock */
	assert(acl_pthread_rwlock_rdlock(&rw) == 0);
	printf("  rdlock ok\n");
	assert(acl_pthread_rwlock_unlock(&rw) == 0);

	/* read trylock */
	assert(acl_pthread_rwlock_tryrdlock(&rw) == 0);
	acl_pthread_rwlock_unlock(&rw);

	/* write lock */
	assert(acl_pthread_rwlock_wrlock(&rw) == 0);
	printf("  wrlock ok\n");
	assert(acl_pthread_rwlock_unlock(&rw) == 0);

	/* write trylock */
	assert(acl_pthread_rwlock_trywrlock(&rw) == 0);
	acl_pthread_rwlock_unlock(&rw);

	assert(acl_pthread_rwlock_destroy(&rw) == 0);
	printf("  rwlock ok\n");
}
#endif

/* ---- condition variable --------------------------------------------- */
typedef struct {
	int                 ready;
	acl_pthread_mutex_t *mutex;
	acl_pthread_cond_t  *cond;
} cond_ctx_t;

static void *signaler(void *arg)
{
	cond_ctx_t *c = (cond_ctx_t *) arg;

	acl_doze(30);                       /* let the waiter block first */
	acl_pthread_mutex_lock(c->mutex);
	c->ready = 1;
	acl_pthread_cond_signal(c->cond);
	acl_pthread_mutex_unlock(c->mutex);
	return NULL;
}

static void *broadcaster(void *arg)
{
	cond_ctx_t *c = (cond_ctx_t *) arg;

	acl_doze(30);
	acl_pthread_mutex_lock(c->mutex);
	c->ready = 1;
	acl_pthread_cond_broadcast(c->cond);
	acl_pthread_mutex_unlock(c->mutex);
	return NULL;
}

static void test_cond_wait(acl_pthread_cond_t *cond, acl_pthread_mutex_t *mutex,
	void *(*fn)(void *))
{
	acl_pthread_mutex_lock(mutex);

	cond_ctx_t ctx;
	ctx.ready = 0;
	ctx.mutex = mutex;
	ctx.cond  = cond;

	acl_pthread_t th;
	assert(acl_pthread_create(&th, NULL, fn, &ctx) == 0);

	/* predicate loop -> cannot miss the signal and cannot hang */
	while (ctx.ready == 0) {
		acl_pthread_cond_wait(cond, mutex);
	}

	acl_pthread_join(th, NULL);
	acl_pthread_mutex_unlock(mutex);
	assert(ctx.ready == 1);
}

static void test_cond(void)
{
	printf("--- condition variable ---\n");

	acl_pthread_mutex_t mutex;
	acl_pthread_cond_t  cond;
	assert(acl_pthread_mutex_init(&mutex, NULL) == 0);
	assert(acl_pthread_cond_init(&cond, NULL) == 0);

	/* signal path */
	test_cond_wait(&cond, &mutex, signaler);
	printf("  cond_wait + signal ok\n");

	/* broadcast path */
	test_cond_wait(&cond, &mutex, broadcaster);
	printf("  cond_wait + broadcast ok\n");

	/* timedwait: absolute deadline ~ now+1s; a signaler wakes it earlier */
	{
		acl_pthread_mutex_lock(&mutex);

		cond_ctx_t ctx;
		ctx.ready = 0;
		ctx.mutex = &mutex;
		ctx.cond  = &cond;

		acl_pthread_t th;
		assert(acl_pthread_create(&th, NULL, signaler, &ctx) == 0);

		struct timespec ts;
		ts.tv_sec  = time(NULL) + 1;
		ts.tv_nsec = 0;

		int rc = 0;
		while (ctx.ready == 0 && rc == 0) {
			rc = acl_pthread_cond_timedwait(&cond, &mutex, &ts);
		}

		acl_pthread_join(th, NULL);
		acl_pthread_mutex_unlock(&mutex);
		assert(ctx.ready == 1);
		printf("  cond_timedwait ok (last rc=%d)\n", rc);
	}

	assert(acl_pthread_cond_destroy(&cond) == 0);
	assert(acl_pthread_mutex_destroy(&mutex) == 0);

	/* heap-allocated cond via acl_thread_cond_create() (Windows impl:
	 * acl_pthread_cond_destroy() frees the dynamic cond itself). */
#if defined(_WIN32) || defined(_WIN64)
	acl_pthread_cond_t *hc = acl_thread_cond_create();
	assert(hc != NULL);
	printf("  acl_thread_cond_create ok\n");
	acl_pthread_cond_destroy(hc);   /* also releases the dynamic object */
#endif
	printf("  condition variable ok\n");
}

/* ---- semaphore (Windows/ACL_SEM implementation) --------------------- */
#if defined(_WIN32) || defined(_WIN64)
static void test_semaphore(void)
{
	printf("--- semaphore ---\n");

	ACL_SEM *sem = acl_sem_create(1);
	assert(sem != NULL);

	/* consume the available resource */
	assert(acl_sem_wait(sem) == 0);
	printf("  after wait, value=%u\n", acl_sem_value(sem));

	/* nothing available -> try_wait should fail */
	assert(acl_sem_try_wait(sem) != 0);
	printf("  try_wait on empty -> failed (ok)\n");

	/* timed wait should also time out quickly */
	assert(acl_sem_wait_timeout(sem, 50) != 0);
	printf("  wait_timeout(50ms) on empty -> timed out (ok)\n");

	/* produce one resource -> now try_wait succeeds */
	assert(acl_sem_post(sem) == 0);
	printf("  after post, value=%u\n", acl_sem_value(sem));
	assert(acl_sem_try_wait(sem) == 0);

	acl_sem_destroy(sem);
	printf("  semaphore ok\n");
}
#endif

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	acl_lib_init();

	printf("==== ACL C thread tests ====\n");

	test_thread();
	test_thread_pool();
	test_mutex();
#if !defined(_WIN32) && !defined(_WIN64)
	test_rwlock();
#else
	printf("--- rwlock: skipped on Windows (ACL compile-order bug) ---\n");
#endif
	test_cond();
#if defined(_WIN32) || defined(_WIN64)
	test_semaphore();
#else
	printf("--- semaphore: ACL_SEM wrapper is Windows-only, skipped ---\n");
#endif

	acl_lib_end();

	printf("All thread tests passed!\n");
	return 0;
}
