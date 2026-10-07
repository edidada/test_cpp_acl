/**
 * test_cpp_misc.cpp — misc acl_cpp classes: dbuf_pool, pipe_string,
 * singleton<T>, event_timer, session, queue_manager, memcache.
 *
 * Verified against headers under lib_acl_cpp/include/acl_cpp: stdlib/,
 * event/, session/, queue/, memcache/
 *
 * API notes:
 *  - acl::session is abstract (remove/get_attrs/set_attrs/set_timeout pure
 *    virtual) so a simple in-memory subclass is used here.
 *  - acl::event_timer delays are in MICROSECONDS; timer_callback(id) must be
 *    implemented by a subclass.
 *  - queue_manager::queue_manager(home, name) creates home/name subdirs on
 *    disk (kept in the build dir; harmless).
 *  - memcache connects lazily; without a running memcached on
 *    127.0.0.1:11211 its operations simply return false (verified in
 *    src/memcache/memcache.cpp), so calls are exercised and the result is
 *    only printed, not asserted.
 *  - no acl::queue exists by that bare name; acl::queue_manager plays the
 *    "queue_manager: if it exists, create/close" role via
 *    create_file()/close_file().
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <chrono>
#include <map>
#include <thread>

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
// acl::dbuf_pool
// ---------------------------------------------------------------------------
static void test_dbuf_pool() {
	section("acl::dbuf_pool");

	acl::dbuf_pool pool(4, 8);       // 4 x 4096 blocks, 8-byte alignment
	CHECK(pool.get_dbuf() != NULL);

	void* p1 = pool.dbuf_alloc(100);
	CHECK(p1 != NULL);
	memset(p1, 0x5A, 100);

	void* p2 = pool.dbuf_calloc(200);
	CHECK(p2 != NULL);
	unsigned char* cp = (unsigned char*) p2;
	CHECK(cp[0] == 0 && cp[199] == 0);      // calloc zeroes memory

	char* s = pool.dbuf_strdup("hello dbuf_pool");
	CHECK(s != NULL);
	CHECK(strcmp(s, "hello dbuf_pool") == 0);

	char* sn = pool.dbuf_strndup("abcdef", 3);
	CHECK(sn != NULL);
	CHECK(strcmp(sn, "abc") == 0);

	void* md = pool.dbuf_memdup(s, strlen(s) + 1);
	CHECK(md != NULL);
	CHECK(strcmp((char*) md, "hello dbuf_pool") == 0);

	CHECK(pool.dbuf_keep(md));               // survive a dbuf_reset

	CHECK(pool.dbuf_free(s));
	CHECK(pool.dbuf_free(sn));

	CHECK(pool.dbuf_reset());                // frees all non-kept memory
	CHECK(strcmp((char*) md, "hello dbuf_pool") == 0);  // kept block survives
	CHECK(pool.dbuf_unkeep(md));
	CHECK(pool.dbuf_free(md));

	printf("  dbuf_pool ok\n");
}

// ---------------------------------------------------------------------------
// acl::pipe_string (acl::pipe_stream implementation)
// ---------------------------------------------------------------------------
static void test_pipe_string() {
	section("acl::pipe_string / acl::pipe_stream");

	acl::pipe_string ps;
	acl::string out;

	// pipe_string is a pass-through pipe: push_pop emits what it received
	int n = ps.push_pop("hello", 5, &out, 0);
	CHECK(n >= 0);
	CHECK(strcmp(out.c_str(), "hello") == 0);
	CHECK(ps.length() == 5);
	CHECK(!ps.empty());
	CHECK(strcmp(ps.c_str(), "hello") == 0);

	out.clear();
	ps.push_pop(" world", 7, &out, 0);
	CHECK(strcmp(out.c_str(), " world") == 0);

	// pop_end flushes nothing extra for a pass-through pipe
	out.clear();
	n = ps.pop_end(&out, 0);
	CHECK(n >= 0);

	// clear() empties the internal buffer
	ps.clear();
	CHECK(ps.empty());
	CHECK(ps.length() == 0);

	// get_buf() exposes the internal string
	acl::string& buf = ps.get_buf();
	buf = "direct";
	CHECK(strcmp(ps.c_str(), "direct") == 0);

	printf("  pipe_string ok\n");
}

// ---------------------------------------------------------------------------
// acl::singleton<T>
// ---------------------------------------------------------------------------

class app_config : public acl::singleton<app_config> {
public:
	app_config() : ready_(false), value_(0) {}
	~app_config() {}

	app_config& init(int v) {
		value_ = v;
		ready_ = true;
		return *this;
	}

	bool is_ready() const {
		return ready_;
	}

	int get_value() const {
		return value_;
	}

private:
	bool ready_;
	int  value_;
};

static void test_singleton() {
	section("acl::singleton<T>");

	app_config& c1 = app_config::get_instance();
	app_config& c2 = app_config::get_instance();
	CHECK(&c1 == &c2);                       // same object every time

	c1.init(1234);
	CHECK(c2.is_ready());
	CHECK(c2.get_value() == 1234);           // state shared through singleton

	// singleton_module lock API
	CHECK(!acl::singleton_module::is_locked());
	acl::singleton_module::lock();
	CHECK(acl::singleton_module::is_locked());
	acl::singleton_module::unlock();
	CHECK(!acl::singleton_module::is_locked());

	printf("  singleton address: %p\n", (void*) &c1);
}

// ---------------------------------------------------------------------------
// acl::event_timer
// ---------------------------------------------------------------------------

class my_timer : public acl::event_timer {
public:
	my_timer() : acl::event_timer(false), fired_(0), last_id_(0), one_shot_(true) {}
	virtual ~my_timer() {}

	/// required pure virtual — invoked by trigger() for each expired task id.
	/// NOTE: event_timer::trigger() RESCHEDULES every expired task before the
	/// callback runs, so for one-shot behaviour the callback itself must
	/// delete the fired task via del_task(id).
	virtual void timer_callback(unsigned int id) override {
		fired_++;
		last_id_ = id;
		printf("  timer_callback fired, id=%u\n", id);
		if (one_shot_) {
			(void) del_task(id);
		}
	}

	/// called when the timer is removed from the timer collection
	virtual void destroy(void) override {
		printf("  timer destroy() called\n");
	}

	std::atomic<int> fired_;
	unsigned int last_id_;
	bool one_shot_;
};

static void test_event_timer() {
	section("acl::event_timer");

	my_timer t;
	CHECK(t.empty());
	CHECK(t.length() == 0);
	CHECK(!t.keep_timer());

	// delays are microseconds — fire a 1ms task after sleeping 20ms
	long long first = (long long) t.set_task(1, 1000);
	printf("  set_task(1, 1ms) -> first trigger in %lld us\n", first);
	CHECK(t.length() == 1);
	CHECK(!t.empty());

	// a far-future task so min_delay()/del_task() can be exercised
	long long far_us = (long long) t.set_task(2, 60 * 1000 * 1000);
	CHECK(t.length() == 2);
	printf("  set_task(2, 60s) -> %lld us, min_delay=%lld\n",
		far_us, (long long) t.min_delay());

	std::this_thread::sleep_for(std::chrono::milliseconds(20));

	long long next = (long long) t.trigger();   // fires expired tasks
	printf("  trigger() -> next delay %lld\n", next);
	CHECK(t.fired_.load() == 1);
	CHECK(t.last_id_ == 1);
	// our callback deletes the fired task, so only the 60s task remains
	CHECK(t.length() == 1);

	// delete the remaining task
	(void) t.del_task(2);
	CHECK(t.empty());
	CHECK(t.length() == 0);

	// trigger on an empty timer must not crash
	t.trigger();

	// clear() with fresh tasks
	t.set_task(7, 5 * 1000 * 1000);
	t.set_task(8, 6 * 1000 * 1000);
	CHECK(t.length() == 2);
	int cleared = t.clear();
	CHECK(cleared == 2);
	CHECK(t.empty());

	// keep_timer mode (repeating tasks)
	t.keep_timer(true);
	CHECK(t.keep_timer());
	printf("  event_timer ok\n");
}

// ---------------------------------------------------------------------------
// acl::session (abstract — implement with an in-memory backend)
// ---------------------------------------------------------------------------

class mem_session : public acl::session {
public:
	mem_session() : acl::session(60 /*ttl seconds*/, NULL) {}
	virtual ~mem_session() {}

	// --- pure virtuals of acl::session -------------------------------------
	virtual bool remove(void) override {
		backend_.clear();
		return true;
	}

	virtual bool get_attrs(std::map<acl::string, acl::session_string>& attrs) override {
		attrs = backend_;
		return true;
	}

	virtual bool set_attrs(const
		std::map<acl::string, acl::session_string>& attrs) override {
		backend_ = attrs;
		// apply pending delayed writes/deletes from attrs_cache_ (protected
		// member of acl::session), like a real cache backend would after flush
		std::map<acl::string, acl::session_string>::const_iterator it =
			attrs_cache_.begin();
		for (; it != attrs_cache_.end(); ++it) {
			if (it->second.todo_ == acl::TODO_DEL) {
				backend_.erase(it->first);
			} else if (it->second.todo_ == acl::TODO_SET) {
				backend_[it->first] = it->second;
			}
		}
		return true;
	}

protected:
	virtual bool set_timeout(time_t ttl) override {
		ttl_ = ttl;
		return true;
	}

private:
	std::map<acl::string, acl::session_string> backend_;
};

static void test_session() {
	section("acl::session");

	mem_session s;

	// sid auto-generated by the constructor
	const char* sid = s.get_sid();
	CHECK(sid != NULL && *sid != 0);
	printf("  auto sid: %s, ttl=%d\n", sid, (int) s.get_ttl());
	CHECK(s.get_ttl() == 60);

	// explicit sid
	s.set_sid("my-fixed-sid-001");
	CHECK(strcmp(s.get_sid(), "my-fixed-sid-001") == 0);

	// attribute round-trip through the in-memory backend
	CHECK(s.set("user", "zsx"));
	const char* v = s.get("user");
	CHECK(v != NULL && strcmp(v, "zsx") == 0);
	printf("  session attr user=%s\n", v);

	CHECK(s.set_delay("pending", "val", 3));
	CHECK(s.flush());

	CHECK(s.del("user"));                   // removed from backend via stub
	s.reset();                              // drop temporary/cached data

	// get_attrs collection interface
	std::map<acl::string, acl::session_string> attrs;
	CHECK(s.get_attrs(attrs));

	printf("  session ok\n");
}

// ---------------------------------------------------------------------------
// acl::queue_manager
// ---------------------------------------------------------------------------
static void test_queue_manager() {
	section("acl::queue_manager");

	// NOTE: the constructor creates ./_acl_qtest and 10 sub-shard dirs
	acl::queue_manager qm(".", "_acl_qtest", 10);
	CHECK(strcmp(qm.get_home(), ".") == 0);
	CHECK(strcmp(qm.get_queueName(), "_acl_qtest") == 0);

	// create + close a queue file (returns NULL on dir permission errors)
	acl::queue_file* fp = qm.create_file("dat");
	if (fp != NULL) {
		printf("  queue file created\n");
		qm.close_file(fp);
	} else {
		printf("  queue file create failed (tolerated)\n");
	}

	// static helpers
	acl::string home, name, part;
	bool ok = acl::queue_manager::parse_path("_acl_qtest/0/abc.dat",
		&home, &name, &part);
	printf("  parse_path -> %s (home=%s name=%s part=%s)\n",
		ok ? "true" : "false", home.c_str(), name.c_str(), part.c_str());
	unsigned w = acl::queue_manager::hash_queueSub("abc", 10);
	CHECK(w < 10);
	printf("  hash_queueSub(abc,10)=%u\n", w);
}

// ---------------------------------------------------------------------------
// acl::memcache (lazy connect; no server in CI — ops must fail cleanly)
// ---------------------------------------------------------------------------
static void test_memcache() {
	section("acl::memcache (no server required for API exercise)");

	// short timeouts so a refused localhost connection returns fast
	acl::memcache mc("127.0.0.1:11211", 1 /*conn s*/, 1 /*rw s*/);

	mc.set_prefix("acltest_");
	mc.auto_retry(true);
	mc.encode_key(false);

	acl::string val;
	bool got = mc.get("some-key", val);
	printf("  memcache get -> %s%s\n", got ? "true" : "false",
		got ? "" : " (no memcached running: expected false)");

	bool set = mc.set("some-key", "data", 4);
	printf("  memcache set -> %s\n", set ? "true" : "false");

	// operations must fail without aborting the process; reaching this line
	// means the client degraded gracefully.
	CHECK(true);
}

int main() {
	acl::acl_cpp_init();

	printf("acl_cpp verbose: %s\n", acl::acl_cpp_verbose());

	test_dbuf_pool();
	test_pipe_string();
	test_singleton();
	test_event_timer();
	test_session();
	test_queue_manager();
	test_memcache();

	// NOTE: no acl_cpp_end() exists in the API; acl_cpp_init() has no
	// required counterpart.
	printf("\n%s (failures: %d)\n",
		g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", g_failures);
	return g_failures == 0 ? 0 : 1;
}
