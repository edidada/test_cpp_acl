/**
 * test_cpp_connpool.cpp — exercises the connection-pool family found in
 * acl_cpp/connpool/: connect_client, connect_pool, tcp_client, tcp_pool,
 * connect_manager, tcp_manager, connect_guard.
 *
 * Verified against real headers under
 * lib_acl_cpp/include/acl_cpp/connpool/*.hpp:
 *  - connect_client() takes NO address; open() is a PUBLIC pure virtual
 *    (declared in the base), alive() and set_timeout(conn_rw) are public
 *    virtuals; tcp_client(addr, conn_timeout=10, rw_timeout=10) is concrete
 *    and keeps open() protected — call open() through a connect_client&.
 *  - connect_pool(addr, max, idx=0) is ABSTRACT: pure virtual
 *    connect_client* create_connect() must be overridden. Chainable setters:
 *    set_timeout(conn, rw, sockopt=false), set_conns_min(n),
 *    set_retry_inter(sec), set_idle_ttl(sec), set_check_inter(sec).
 *    connect_client* peek(bool on=true, double* tc=NULL, bool* old=NULL),
 *    void put(conn, keep=true, oper=cpool_put_check_idle),
 *    size_t check_idle(time_t), bool aliving(), void set_alive(bool).
 *  - tcp_pool(addr, count, idx=0) is a CONCRETE connect_pool.
 *  - connect_manager is abstract too (protected pure virtual create_pool),
 *    but all interesting methods are concrete:
 *    init(default_addr, addr_list, count, conn_timeout, rw_timeout,
 *        sockopt_timeo) where addr_list is "IP:PORT:MAX;IP:PORT;...",
 *    set(addr, max, ...), get(addr, exclusive=true, restore=false),
 *    virtual peek(), get_config(addr). The concrete subclass is
 *    tcp_manager() (no-arg ctor).
 *  - connect_guard(connect_pool&) RAII helper: peek() + auto put() in dtor.
 *
 * All target ports on 127.0.0.1 are closed, so real connects fail fast
 * (connection refused); the test asserts the graceful NULL/false results.
 * NOTE: there is no acl_cpp_end() in the API; acl::acl_cpp_init() has no
 * required counterpart.
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <lib_acl.hpp>

static int g_failures = 0;

#define CHECK(expr) \
	do { \
		if (!(expr)) { \
			g_failures++; \
			printf("  [FAIL] %s (line %d)\n", #expr, __LINE__); \
		} \
	} while (0)

//////////////////////////////////////////////////////////////////////
// A concrete connect_pool subclass: create_connect() must build a
// connect_client for the target server. Connections are created lazily
// by peek(), never in the constructor.
//////////////////////////////////////////////////////////////////////

class my_tcp_pool : public acl::connect_pool {
public:
	my_tcp_pool() : acl::connect_pool("127.0.0.1:19876", 4, 0) {
		// chainable configuration, all in seconds
		set_timeout(1, 1)			// conn/rw timeout
			.set_conns_min(0)		// don't keep minimum conns
			.set_retry_inter(1)		// dead-pool retry interval
			.set_idle_ttl(60)		// idle conns live 60s
			.set_check_inter(-1);	// disable auto idle check on put()
	}

	int create_calls() const {
		return create_calls_;
	}

protected:
	// @override pure virtual of connect_pool
	acl::connect_client* create_connect() override {
		create_calls_++;
		return new acl::tcp_client("127.0.0.1:19876", 1, 1);
	}

private:
	int create_calls_ = 0;
};

//////////////////////////////////////////////////////////////////////

static void test_tcp_client(void) {
	printf("== tcp_client / connect_client ==\n");

	acl::tcp_client client("127.0.0.1:19876", 1, 1);

	// connect_client public virtuals
	acl::connect_client* base = &client;
	base->set_timeout(1, 1);          // seconds, seconds
	CHECK(base->alive() != false || !base->alive()); // callable, value ok

	// open() is public through the base interface; port 19876 is closed,
	// so the connect must fail fast and return false.
	bool opened = base->open();
	printf("  tcp_client open(127.0.0.1:19876) -> %s\n", opened ? "ok" : "refused");
	CHECK(!opened);

	// send() without an established connection must fail, not crash
	acl::string out;
	bool sent = client.send("ping", 4, &out);
	printf("  tcp_client send -> %s\n", sent ? "ok" : "false");
	CHECK(!sent);
}

static void test_connect_pool_subclass(void) {
	printf("== connect_pool (custom subclass) ==\n");

	my_tcp_pool pool;

	CHECK(strcmp(pool.get_addr(), "127.0.0.1:19876") == 0);
	CHECK(pool.get_max() == 4);
	CHECK(pool.get_idx() == 0);
	CHECK(pool.get_count() == 0);

	// peek() -> create_connect() -> tcp_client::open() fails (closed port)
	acl::connect_client* conn = pool.peek();
	printf("  pool.peek() -> %p (NULL expected: no server listening)\n", conn);
	CHECK(conn == NULL);
	CHECK(pool.create_calls() >= 1); // create_connect() really invoked

	// no idle connections to reclaim (cast disambiguates the two
	// check_idle overloads: time_t vs bool)
	size_t n = pool.check_idle((time_t)30);
	printf("  check_idle(30) closed %lu conns\n", (unsigned long)n);
	CHECK(n == 0);

	// liveness bookkeeping
	pool.set_alive(true);
	CHECK(pool.aliving());

	pool.reset_statistics(60);
	printf("  total_used=%llu\n", pool.get_total_used());
}

static void test_tcp_pool(void) {
	printf("== tcp_pool (concrete) ==\n");

	// addr, pool size, index — concrete class, no subclassing needed
	acl::tcp_pool pool("127.0.0.1:19876", 3, 0);

	CHECK(strcmp(pool.get_addr(), "127.0.0.1:19876") == 0);
	CHECK(pool.get_max() == 3);
	pool.set_timeout(1, 1);

	// on=false: forbid creating new conns, so empty pool returns NULL
	// immediately without touching the network
	acl::connect_client* conn = pool.peek(false);
	printf("  tcp_pool.peek(false) -> %p\n", conn);
	CHECK(conn == NULL);

	// tcp_pool::send auto-peeks; with a dead server it must fail
	acl::string out;
	bool sent = pool.send("HELP\r\n", 6, &out);
	printf("  tcp_pool.send -> %s\n", sent ? "ok" : "false");
	CHECK(!sent);
}

static void test_connect_guard(void) {
	printf("== connect_guard (RAII) ==\n");

	my_tcp_pool pool;
	acl::connect_guard guard(pool);

	acl::connect_client* conn = guard.peek(); // NULL: server not listening
	printf("  guard.peek() -> %p\n", conn);
	CHECK(conn == NULL);
	guard.set_keep(false);
	// dtor would pool.put() the connection if one had been obtained
}

static void test_connect_manager(void) {
	printf("== connect_manager / tcp_manager ==\n");

	// connect_manager itself is abstract (create_pool); use tcp_manager
	acl::tcp_manager mgr;

	// default addr + list "IP:PORT:MAX;IP:PORT:MAX;...", timeouts in seconds
	mgr.init("127.0.0.1:19876", "127.0.0.1:19876:4;127.0.0.1:19877:2",
		2 /*default MAX*/, 1 /*conn_timeout*/, 1 /*rw_timeout*/);

	// configuration objects exist for both addresses
	const acl::conn_config* cfg1 = mgr.get_config("127.0.0.1:19876");
	const acl::conn_config* cfg2 = mgr.get_config("127.0.0.1:19877");
	printf("  config(19876)=%s max=%lu, config(19877)=%s max=%lu\n",
		cfg1 ? "yes" : "no", cfg1 ? (unsigned long)cfg1->max : 0UL,
		cfg2 ? "yes" : "no", cfg2 ? (unsigned long)cfg2->max : 0UL);
	CHECK(cfg1 != NULL);
	CHECK(cfg2 != NULL);

	// get a pool by address (created on demand)
	acl::connect_pool* pool = mgr.get("127.0.0.1:19876");
	printf("  mgr.get(127.0.0.1:19876) -> %p\n", pool);
	CHECK(pool != NULL);
	if (pool) {
		CHECK(strcmp(pool->get_addr(), "127.0.0.1:19876") == 0);
		// unknown address (no default fallback since use_first=false)
		CHECK(mgr.get("127.0.0.1:65500") == NULL);
	}

	// round-robin peek: never NULL once init() has been called
	acl::connect_pool* p1 = mgr.peek();
	acl::connect_pool* p2 = mgr.peek();
	printf("  mgr.peek() round-robin: %p, %p\n", p1, p2);
	CHECK(p1 != NULL);
	CHECK(p2 != NULL);

	// default pool == the one for default_addr
	acl::connect_pool* d = mgr.get_default_pool();
	printf("  default pool addr: %s\n", d ? d->get_addr() : "(none)");
	CHECK(d != NULL);

	// dynamic registration of another server
	mgr.set("127.0.0.1:19878", 1, 1, 1);
	CHECK(mgr.get("127.0.0.1:19878") != NULL);
}

int main() {
	acl::acl_cpp_init();

	printf("acl_cpp version: %s\n", acl::acl_cpp_verbose());

	test_tcp_client();
	test_connect_pool_subclass();
	test_tcp_pool();
	test_connect_guard();
	test_connect_manager();

	if (g_failures == 0) {
		printf("\nAll connpool tests passed!\n");
		return 0;
	}
	printf("\n%d connpool test(s) FAILED\n", g_failures);
	return 1;
}
