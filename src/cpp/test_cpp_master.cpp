/**
 * test_cpp_master.cpp — exercise the acl_cpp master_* server frameworks.
 *
 * Verified against lib_acl_cpp/include/acl_cpp/master/*.hpp
 *
 * API notes (discovered from the real headers):
 *  - master_base's ctor/dtor are PROTECTED => every demo must be a subclass.
 *  - master_threads: the only pure virtual is
 *      protected: virtual bool thread_on_read(socket_stream*) = 0;
 *    thread_on_accept/thread_on_close/thread_on_init/thread_on_exit all have
 *    default (non-pure) implementations, and thread_on_accept returns bool.
 *  - master_aio: pure virtual bool on_accept(aio_socket_stream*) = 0 (protected).
 *  - master_proc: pure virtual void on_accept(socket_stream*) = 0 (protected).
 *  - master_udp: pure virtual void on_read(socket_stream*) = 0 (protected).
 *  - master_trigger: pure virtual void on_trigger() = 0 (protected).
 *  - daemon_mode() is a public const getter on master_base.
 *  - set_cfg_int/str/bool/int64 are public and take NUL-terminated static
 *    tables of master_int_tbl/master_str_tbl/... (see master_conf.hpp).
 *  - run_daemon()/run_alone() BLOCK (and need config files / master process),
 *    so we only instantiate the objects and touch non-blocking accessors.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
// master_threads: thread pool framework
// ---------------------------------------------------------------------------
class MyMasterThreads : public acl::master_threads {
public:
	MyMasterThreads() : master_threads() {}
	~MyMasterThreads() override {}

	bool called_read() const { return called_; }

private:
	// @override — the ONLY pure virtual of master_threads
	bool thread_on_read(acl::socket_stream *conn) override {
		(void)conn;
		called_ = true;
		return true;   // keep the (hypothetical) connection alive
	}
	// below: optional overrides with the real signatures
	bool thread_on_accept(acl::socket_stream *conn) override {
		(void)conn;
		return true;   // NOTE: returns bool, not void
	}
	void thread_on_close(acl::socket_stream *conn) override { (void)conn; }
	void thread_on_init() override {}
	void thread_on_exit() override {}

	bool called_ = false;
};

// ---------------------------------------------------------------------------
// master_aio: single-threaded async IO framework
// ---------------------------------------------------------------------------
class MyMasterAio : public acl::master_aio {
public:
	MyMasterAio() : master_aio() {}
	~MyMasterAio() override {}

private:
	bool on_accept(acl::aio_socket_stream *conn) override {
		(void)conn;
		return true;   // continue accepting
	}
};

// ---------------------------------------------------------------------------
// master_proc: classic multi-process framework
// ---------------------------------------------------------------------------
class MyMasterProc : public acl::master_proc {
public:
	MyMasterProc() : master_proc() {}
	~MyMasterProc() override {}

private:
	void on_accept(acl::socket_stream *conn) override {
		(void)conn;    // framework closes the stream after we return
	}
};

// ---------------------------------------------------------------------------
// master_udp: UDP datagram framework
// ---------------------------------------------------------------------------
class MyMasterUdp : public acl::master_udp {
public:
	MyMasterUdp() : master_udp() {}
	~MyMasterUdp() override {}

	// get_conf_path()/get_sstreams() are PROTECTED in master_udp — expose them
	const char* confPath() const { return get_conf_path(); }
	size_t nstreams() const { return get_sstreams().size(); }

private:
	void on_read(acl::socket_stream *conn) override { (void)conn; }
	void proc_on_bind(acl::socket_stream& ss) override { (void)ss; }
	void proc_on_unbind(acl::socket_stream& ss) override { (void)ss; }
	void thread_on_init() override {}
};

// ---------------------------------------------------------------------------
// master_trigger: periodic timer framework
// ---------------------------------------------------------------------------
class MyMasterTrigger : public acl::master_trigger {
public:
	MyMasterTrigger() : master_trigger() {}
	~MyMasterTrigger() override {}

private:
	void on_trigger() override {}
};

// ---------------------------------------------------------------------------
// static configuration tables — the {NULL,...} sentinel terminates each table
// ---------------------------------------------------------------------------
static int  g_cfg_max_threads = 0;
static char* g_cfg_my_name    = NULL;
static int  g_cfg_debug       = 0;

static acl::master_int_tbl g_int_tbl[] = {
	{ "max_threads", 4, &g_cfg_max_threads, 1, 128 },
	{ NULL, 0, NULL, 0, 0 }
};

static acl::master_str_tbl g_str_tbl[] = {
	{ "my_name", "default", &g_cfg_my_name },
	{ NULL, NULL, NULL }
};

static acl::master_bool_tbl g_bool_tbl[] = {
	{ "my_debug", 0, &g_cfg_debug },
	{ NULL, 0, NULL }
};

int main() {
	acl::acl_cpp_init();   // NOTE: no acl_cpp_end() exists in this API

	section("master_threads");
	MyMasterThreads mt;
	// NOT calling run_daemon/run_alone — both block
	CHECK(!mt.daemon_mode());               // const getter, false when standalone
	CHECK(mt.get_conf_path() == NULL);      // no config file set
	CHECK(mt.task_qlen() == 0);             // safe: returns 0 when no pool
	(void)mt.threads_pool();                // don't assert value pre-run
	// public master_base cfg API: set_cfg_* only REGISTERS the table; the
	// defaults are pushed into the targets by load_int()/load_str() which are
	// no-ops until a config file has been loaded (cfg_loaded_), so the targets
	// keep their initial values in standalone mode.
	CHECK(&mt.set_cfg_int(g_int_tbl) == &mt);
	CHECK(g_cfg_max_threads == 0);
	CHECK(!mt.proc_set_timer(NULL));        // refuses without a running engine

	section("master_aio");
	MyMasterAio aio;
	CHECK(!aio.daemon_mode());
	CHECK(aio.get_conf_path() == NULL);
	CHECK(&aio.set_cfg_str(g_str_tbl) == &aio);
	CHECK(g_cfg_my_name == NULL);           // defaults not applied pre-config
	// NOTE: stop()/get_handle() call acl_assert(handle_) — must NOT be used
	// before run_alone()/run_daemon() created the engine.

	section("master_proc");
	MyMasterProc mp;
	CHECK(!mp.daemon_mode());
	CHECK(mp.get_conf_path() == NULL);
	CHECK(&mp.set_cfg_bool(g_bool_tbl) == &mp);

	section("master_udp");
	MyMasterUdp mu;
	CHECK(!mu.daemon_mode());
	CHECK(mu.confPath() == NULL);
	CHECK(mu.nstreams() == 0);             // nothing bound yet
	mu.lock();
	mu.unlock();

	section("master_trigger");
	MyMasterTrigger tr;
	CHECK(!tr.daemon_mode());
	CHECK(tr.get_conf_path() == NULL);

	section("master log switches");
	acl::master_log_enable(true);
	CHECK(acl::master_log_enabled());
	acl::master_log_enable(false);
	CHECK(!acl::master_log_enabled());

	if (g_failures > 0) {
		printf("%d Master check(s) FAILED\n", g_failures);
		return 1;
	}
	printf("All Master tests passed!\n");
	return 0;
}
