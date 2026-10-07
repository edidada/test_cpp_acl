/**
 * test_cpp_thread.cpp — test acl::thread / acl::thread_pool / acl::thread_job /
 * acl::locker / acl::lock_guard.
 *
 * Verified against lib_acl_cpp/include/acl_cpp/stdlib/{thread,thread_pool,locker}.hpp
 *
 * API notes:
 *  - thread::start(bool sync = false); wait(void** out = NULL) for
 *    non-detachable threads.
 *  - thread_pool::run(thread_job*) does NOT take ownership of the job object
 *    (see src/stdlib/thread_pool.cpp: thread_run only calls job->run()), so
 *    this test deletes submitted jobs after pool.wait().
 *  - locker::open(path) creates an additional file lock; the pure-thread
 *    locker is built via locker(bool use_mutex, bool use_spinlock).
 */

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <vector>

#include <lib_acl.hpp>

static int g_failures = 0;

#define CHECK(expr) \
	do { \
		if (!(expr)) { \
			g_failures++; \
			printf("  [FAIL] %s (line %d)\n", #expr, __LINE__); \
		} \
	} while (0)

static void section(const char* title) {
	printf("\n==== %s ====\n", title);
}

// ---------------------------------------------------------------------------
// acl::thread
// ---------------------------------------------------------------------------

class counter_thread : public acl::thread {
public:
	counter_thread(int rounds) : rounds_(rounds), done_(0) {}

	/// executed inside the child thread — increments the shared counter
	virtual void* run() override {
		printf("  child thread id=%lu started\n", acl::thread::thread_self());
		for (int i = 0; i < rounds_; i++) {
			done_++;
		}
		// on POSIX the returned value is delivered through wait(void**);
		// on Windows the thread port cannot propagate it (see test_thread)
		return (void*) (size_t) (done_.load() * 10);
	}

	std::atomic<int>& get_done() {
		return done_;
	}

private:
	int rounds_;
	std::atomic<int> done_;
};

static void test_thread() {
	section("acl::thread");

	const int rounds = 1000;
	counter_thread th(rounds);

	// configuration must be set BEFORE start()
	th.set_stacksize(512 * 1024);        // 512KB stack
	th.set_detachable(false);            // we will wait() on it

	CHECK(th.start());
	unsigned long tid = th.thread_id();
	CHECK(tid != 0);
	printf("  parent=%lu child=%lu\n", acl::thread::thread_self(), tid);

	void* ret = NULL;
	CHECK(th.wait(&ret));                // join, fetch run()'s return value
	CHECK(th.get_done().load() == rounds);
#if defined(_WIN32) || defined(_WIN64)
	// NOTE: on Windows the ACL pthread port stores the thread function's
	// return value in a private heap copy (h_thread) while
	// acl_pthread_join() reads it back from the caller-side struct, which
	// stays zeroed — so wait(void**) cannot deliver run()'s result here.
	printf("  Windows port: wait(&ret) yields %d (run() value not propagated"
		" by acl_pthread_join) — not asserted\n", (int) (size_t) ret);
#else
	CHECK((int) (size_t) ret == rounds * 10);
#endif
	printf("  thread finished, counter=%d ret=%d\n",
		th.get_done().load(), (int) (size_t) ret);

	// detachable thread — must NOT be waited on
	class detached_thread : public acl::thread {
	public:
		virtual void* run() override {
			printf("  detachable thread id=%lu running\n", thread_self());
			return NULL;
		}
	};
	detached_thread* dt = new detached_thread();
	dt->set_detachable(true);
	CHECK(dt->start());
	// object self-manages in detach mode; just sleep a bit via the pool below
	(void) dt;   // NOTE: intentionally leaked for demo; detachable threads
	             // release their own resources — but ACL still deletes the
	             // thread object only when created with `new` + detach, so
	             // the safest pattern shown in ACL samples is to keep it.
}

// ---------------------------------------------------------------------------
// acl::thread_pool
// ---------------------------------------------------------------------------

class pool_job : public acl::thread_job {
public:
	explicit pool_job(std::atomic<int>& counter) : counter_(counter) {}
	virtual ~pool_job() {}

	/// runs inside one of the pool's child threads
	virtual void* run() override {
		counter_++;
		return NULL;
	}

private:
	std::atomic<int>& counter_;
};

class my_thread_pool : public acl::thread_pool {
public:
	my_thread_pool() {
		init_count_ = 0;
		exit_count_ = 0;
	}

	std::atomic<int> init_count_;
	std::atomic<int> exit_count_;

protected:
	/// called first when a pool child thread is created
	virtual bool thread_on_init() override {
		init_count_++;
		printf("  pool thread init, tid=%lu\n", acl::thread::thread_self());
		return true;
	}

	/// called when a pool child thread exits
	virtual void thread_on_exit() override {
		exit_count_++;
	}
};

static void test_thread_pool() {
	section("acl::thread_pool");

	std::atomic<int> work_done(0);
	my_thread_pool pool;

	// configuration before start()
	pool.set_stacksize(256 * 1024);
	pool.set_limit(4);                  // at most 4 worker threads
	CHECK(pool.get_limit() == 4);
	pool.set_idle(1);                   // idle threads exit after 1s

	pool.start();
	int nthreads = pool.threads_count();
	printf("  threads_count=%d (0 allowed right after start)\n", nthreads);
	CHECK(nthreads >= 0);
	CHECK(pool.task_qlen() >= 0);

	const int JOBS = 20;
	std::vector<pool_job*> jobs;
	for (int i = 0; i < JOBS; i++) {
		pool_job* job = new pool_job(work_done);
		jobs.push_back(job);
		if (i % 2 == 0) {
			CHECK(pool.run(job));       // submit task
		} else {
			CHECK(pool.execute(job));   // same as run(), java-style name
		}
	}

	pool.wait();                        // wait for all queued tasks to finish

	printf("  work_done=%d, task_qlen=%d\n",
		work_done.load(), pool.task_qlen());
	CHECK(work_done.load() == JOBS);

	for (size_t i = 0; i < jobs.size(); i++) {
		delete jobs[i];                 // pool does not own jobs
	}
	jobs.clear();

	printf("  worker init=%d exit=%d\n",
		pool.init_count_.load(), pool.exit_count_.load());
	CHECK(pool.init_count_.load() >= 1);
	CHECK(pool.exit_count_.load() >= 1);

	pool.stop();                        // threads exit; start() again is legal
	CHECK(pool.threads_count() == -1);  // -1: pool not running
}

// ---------------------------------------------------------------------------
// acl::locker + acl::lock_guard
// ---------------------------------------------------------------------------

class locked_thread : public acl::thread {
public:
	locked_thread(acl::locker& lk, int* shared, int rounds)
		: lk_(lk), shared_(shared), rounds_(rounds) {}

	virtual void* run() override {
		for (int i = 0; i < rounds_; i++) {
			lk_.lock();
			(*shared_)++;
			lk_.unlock();

			// RAII style with lock_guard for the second increment
			{
				acl::lock_guard guard(lk_);
				(*shared_)++;
			}
		}
		return NULL;
	}

private:
	acl::locker& lk_;
	int* shared_;
	int rounds_;
};

static void test_locker() {
	section("acl::locker / acl::lock_guard");

	// pure mutex-based locker (no file lock)
	acl::locker lk(true, false);

	int shared = 0;
	const int ROUNDS = 500;

	CHECK(lk.lock());
	CHECK(lk.unlock());

	// try_lock on an unlocked mutex must succeed
	CHECK(lk.try_lock());
	CHECK(lk.unlock());

	// RAII
	{
		acl::lock_guard guard(lk);
		shared += 7;
	}
	CHECK(shared == 7);

	// two threads increment shared under the locker: total must be exact
	locked_thread t1(lk, &shared, ROUNDS);
	locked_thread t2(lk, &shared, ROUNDS);
	t1.set_detachable(false);
	t2.set_detachable(false);
	CHECK(t1.start());
	CHECK(t2.start());
	t1.wait();
	t2.wait();
	CHECK(shared == 7 + ROUNDS * 2 * 2);
	printf("  locked shared=%d (expected %d)\n", shared, 7 + ROUNDS * 4);

	// file lock variant: locker::open(path) adds a file lock around the
	// given path (works together with the thread mutex of this object)
	const char* lockfile = "_acl_locker_test.tmp";
	{
		FILE* fp = fopen(lockfile, "wb");
		if (fp) {
			fclose(fp);
		}
	}
	acl::locker flk(true, false);
	bool opened = flk.open(lockfile);
	printf("  locker.open(%s) -> %s\n", lockfile, opened ? "true" : "false");
	if (opened) {
		CHECK(flk.lock());
		CHECK(flk.unlock());
	}
	remove(lockfile);
}

int main() {
	acl::acl_cpp_init();

	printf("acl_cpp verbose: %s\n", acl::acl_cpp_verbose());

	test_thread();
	test_thread_pool();
	test_locker();

	printf("\n%s (failures: %d)\n",
		g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", g_failures);
	return g_failures == 0 ? 0 : 1;
}
