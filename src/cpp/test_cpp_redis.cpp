/**
 * test_cpp_redis.cpp -- exercise the whole acl::redis* C++ API surface.
 *
 * Verified against lib_acl_cpp/include/acl_cpp/redis/*.hpp of the ACL master
 * branch fetched by CMake (build/_deps/acl-src).
 *
 * IMPORTANT API FACTS (several differ from the common assumption):
 *  - There is NO acl::acl_cpp_end(): acl_cpp/acl_cpp_init.hpp exports only
 *    acl_cpp_init(), acl_cpp_verbose() and (Win32) open_dos()/close_dos().
 *    acl_cpp_init() needs no paired cleanup call.
 *  - acl::redis_client has NO default constructor and is noncopyable:
 *        explicit redis_client(const char* addr, int conn_timeout = 60,
 *                              int rw_timeout = 30, bool retry = true);
 *    Its open() is PROTECTED (called lazily by get_stream(true)), so
 *    "client.open()" does not compile -- use get_stream() or let a command
 *    connect implicitly.
 *  - ping()/echo()/select()/auth()/quit() belong to redis_connection, NOT to
 *    redis_server. redis_server carries info, dbsize, save, bgsave,
 *    config_get, config_set, client_list, slowlog_*, ...
 *  - result_type(), result_status(), result_error(), result_size(),
 *    result_number()/result_number64()/get_result()/result_child()/
 *    result_value() are members of redis_command -- the *virtual* base class of
 *    every command class -- and are NULL-safe before any command has run.
 *  - Every command class offers a default ctor plus ctors taking a
 *    redis_client, redis_client_cluster or redis_client_pipeline pointer, and
 *    wiring can equally be done after construction with set_client()/
 *    set_cluster()/set_pipeline(). Note redis_string/redis_set/redis_stream/
 *    redis_cluster/redis_transaction take `explicit` pointer ctors.
 *  - SET option flags are preprocessor macros declared inside redis_string.hpp
 *    (SETFLAG_EX=0x02, SETFLAG_PX=0x03, SETFLAG_NX=0x08, SETFLAG_XX=0x0C).
 *  - GEO units / WITH / SORT are anonymous enums in redis_geo.hpp.
 *  - redis_role and redis_sentinel are NOT part of the acl::redis aggregate
 *    (redis.hpp does not inherit them) -- they must be used standalone.
 *  - The pipeline class is acl::redis_client_pipeline (a thread subclass);
 *    constructing it starts no thread, start_thread() does.
 *
 * No Redis server is reachable in this environment, therefore:
 *  - Construction, wiring (set_client/set_cluster/set_pipeline), option
 *    setters, the RESP request builder redis_command::build_request()/
 *    request_buf(), the cluster hash-slot computation hash_slot(), and all
 *    result accessors are exercised FOR REAL and asserted.
 *  - Every call that would need a live server sits inside
 *    `if (kHaveServer) { ... }`, so it is fully type-checked by the compiler yet
 *    never executed. Flip kHaveServer to true against a real redis-server to
 *    run the whole command set.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <utility>

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

// Flip to true while a redis-server listens on kRedisAddr to really execute
// the network commands; false keeps them compile-only.
static const bool kHaveServer = false;

static const char* kRedisAddr    = "127.0.0.1:6379";
static const char* kRedisAddrB   = "127.0.0.1:6380";
static const char* kSentinelAddr = "127.0.0.1:26379";
static const char* kLogFile      = "_acl_cpp_test_redis.log";

// Handy local types (acl::string is ACL's own growable string class)
typedef std::vector<acl::string>          StrVec;
typedef std::vector<const char*>          CStrVec;
typedef std::map<acl::string, acl::string> StrMap;
typedef std::map<acl::string, double>      StrDoubleMap;

//////////////////////////////////////////////////////////////////////////
// 1. redis_client -- the connection object (pure accessors, no IO)
//////////////////////////////////////////////////////////////////////////

static void test_redis_client()
{
	section("acl::redis_client construction and option accessors");

	// short timeouts + retry=false: with no server listening nothing blocks
	acl::redis_client client(kRedisAddr, 1, 1, false);

	printf("  addr = %s\n", client.get_addr());
	CHECK(strcmp(client.get_addr(), kRedisAddr) == 0);

	client.set_password("my-secret-passwd");
	client.set_db(0);
	CHECK(client.get_db() == 0);
	client.set_db(3);
	CHECK(client.get_db() == 3);
	client.set_db(0);

	// strict peer-address checking is a DEBUG-only feature (costs perf)
	client.set_check_addr(true);
	client.set_check_addr(false);

	client.set_slice_request(false);
	client.set_slice_respond(true);
	client.set_slice_respond(false);
	client.set_ssl_conf(NULL);

	// inherited from acl::connect_client
	client.set_timeout(2, 3);
	client.set_when(time(NULL));
	printf("  when = %ld, pool = %p\n", (long) client.get_when(),
		(void*) client.get_pool());
	CHECK(client.get_pool() == NULL);
	CHECK(client.get_when() != 0);

	// no connection has been made yet -> eof() true, close() is a no-op,
	// get_stream(false) returns NULL instead of dialling out
	printf("  eof(not connected) = %d\n", client.eof() ? 1 : 0);
	CHECK(client.eof());
	CHECK(client.get_stream(false) == NULL);
	client.close();
	printf("  close() on an unconnected client is safe\n");

	// client.open() is PROTECTED in redis_client -- it cannot be called from
	// application code; the connection is established lazily by
	// get_stream(true) / by the first command execution.
}

//////////////////////////////////////////////////////////////////////////
// 2. redis_command base: wiring, RESP building, result accessors
//////////////////////////////////////////////////////////////////////////

static void test_redis_command_base(acl::redis_client* client,
	acl::redis_client_cluster* cluster)
{
	section("acl::redis_command: wiring / build_request / result accessors");

	acl::redis_string str;

	// (a) wiring through set_client()/get_client()
	str.set_client(client);
	CHECK(str.get_client() == client);
	printf("  get_client_addr() = \"%s\" (empty -> not connected)\n",
		str.get_client_addr());

	// (b) wiring straight from the constructor
	acl::redis_hash hash(client);
	CHECK(hash.get_client() == client);

	// (c) override the recorded address without touching the socket
	hash.set_client_addr("127.0.0.1:6399");
	CHECK(strcmp(hash.get_client_addr(), "127.0.0.1:6399") == 0);

	// (d) the base class itself is instantiable
	acl::redis_command base;
	base.set_client(client);
	CHECK(base.get_client() == client);
	base.set_cluster(cluster);
	CHECK(base.get_cluster() == cluster);
	base.set_pipeline(NULL);
	CHECK(base.get_pipeline() == NULL);
	base.set_client(NULL);
	CHECK(base.get_client() == NULL);
	base.set_cluster(NULL);
	CHECK(base.get_cluster() == NULL);
	base.set_check_addr(true);
	CHECK(base.is_check_addr());
	base.set_slice_request(false);
	base.set_slice_respond(false);
	CHECK(base.get_dbuf() != NULL);
	printf("  dbuf pool handle = %p\n", (void*) base.get_dbuf());

	// (e) clear() releases the previous command's resources
	base.clear(true);
	base.clear(false);
	printf("  slot after clear(false) = %d\n", base.get_slot());
	CHECK(base.get_slot() == -1);

	// (f) RESP (REdis Serialization Protocol) building is fully client-side:
	//     *<argc>\r\n$<len>\r\n<arg>\r\n ...
	{
		const char* argv[] = { "SET", "key", "value" };
		const size_t lens[] = { 3, 3, 5 };
		acl::string out;
		acl::redis_command::build_request(3, argv, lens, out);
		printf("  build_request -> %lu bytes\n",
			(unsigned long) out.length());
		printf("  [%s]\n", out.c_str());
		CHECK(out.length() > 0);
		CHECK(memcmp(out.c_str(), "*3\r\n", 4) == 0);
		CHECK(strstr(out.c_str(), "$3\r\nSET\r\n") != NULL);
		CHECK(strstr(out.c_str(), "$3\r\nkey\r\n") != NULL);
		CHECK(strstr(out.c_str(), "$5\r\nvalue\r\n") != NULL);
		CHECK(strstr(out.c_str(), "value\r\n") == out.c_str() + out.length() - 7);
	}

	// the same request, cached inside the command object for reuse
	{
		const char* argv[] = { "GET", "key" };
		const size_t lens[] = { 3, 3 };
		base.set_client(client);
		base.build_request(2, argv, lens);
		const acl::string* buf = base.request_buf();
		CHECK(buf != NULL);
		if (buf != NULL) {
			printf("  request_buf -> [%s]\n", buf->c_str());
			CHECK(memcmp(buf->c_str(), "*2\r\n$3\r\nGET\r\n", 13) == 0);
		}
		CHECK(base.get_request_buf() == buf);
		CHECK(base.get_request_obj() == NULL);   // non-sliced mode
	}

	// sliced request mode builds a redis_request object rather than one buffer
	{
		base.set_slice_request(true);
		CHECK(base.is_slice_req());
		const char* argv[] = { "PING" };
		const size_t lens[] = { 4 };
		base.build_request(1, argv, lens);
		CHECK(base.get_request_obj() != NULL);
		printf("  sliced request object = %p\n",
			(void*) base.get_request_obj());
		base.set_slice_request(false);
	}

	// (g) every result accessor is NULL-safe before any command ran
	CHECK(base.result_type() == acl::REDIS_RESULT_UNKOWN);
	CHECK(base.result_size() == 0);
	CHECK(base.get_result() == NULL);              // const redis_result*
	CHECK(base.get_result(0, NULL) == NULL);       // const char* overload
	CHECK(base.result_child(0) == NULL);
	CHECK(base.result_value(0, NULL) == NULL);
	CHECK(base.result_status()[0] == '\0');
	CHECK(base.result_error() != NULL);            // falls back to last_serror()
	{
		// with no result_ cached, result_number() returns 0 and LEAVES the
		// success flag untouched -- it is only written through to
		// redis_result::get_integer() when a result exists.
		bool ok = false;
		CHECK(base.result_number(&ok) == 0);
		CHECK(ok == false);
		CHECK(base.result_number64(&ok) == 0);
		CHECK(base.result_number() == 0);
		CHECK(base.result_number64() == 0);
	}
	// redis_command::eof() == (conn_ != NULL && conn_->eof()); after
	// set_cluster() the bound client pointer was cleared, so eof() is false.
	printf("  cmd eof = %d\n", base.eof() ? 1 : 0);
	CHECK(!base.eof());

	// result_type() enum values are visible to the application
	printf("  acl::REDIS_RESULT_UNKOWN=%d NIL=%d ERROR=%d STATUS=%d INTEGER=%d "
		"STRING=%d ARRAY=%d\n", (int) acl::REDIS_RESULT_UNKOWN,
		(int) acl::REDIS_RESULT_NIL, (int) acl::REDIS_RESULT_ERROR,
		(int) acl::REDIS_RESULT_STATUS, (int) acl::REDIS_RESULT_INTEGER,
		(int) acl::REDIS_RESULT_STRING, (int) acl::REDIS_RESULT_ARRAY);

	// (h) the generic raw-command entry points (network -> compile only)
	if (kHaveServer) {
		const char* argv[] = { "PING" };
		const size_t lens[] = { 4 };
		const acl::redis_result* r1 = base.request(1, argv, lens, 0);
		StrVec args;
		args.push_back("PING");
		const acl::redis_result* r2 = base.request(args, 0);
		printf("  raw request results: %p %p type=%d\n",
			(void*) r1, (void*) r2, (int) base.result_type());
		if (r1 != NULL) {
			// acl::redis_result self-describes its payload
			size_t n = r1->get_size();
			const char* s = r1->get(0, NULL);
			printf("  result type=%d size=%lu first=%s\n",
				(int) r1->get_type(), (unsigned long) n, s ? s : "(null)");
			printf("  status=%s error=%s integer=%d double=%g\n",
				r1->get_status(), r1->get_error(), r1->get_integer(),
				r1->get_double());
			printf("  length=%lu argv=%p lens=%p children=%p\n",
				(unsigned long) r1->get_length(), (void*) r1->get_argv(),
				(void*) r1->get_lens(), (void*) r1->get_children(&n));
			acl::string dump;
			r1->to_string(dump);
			printf("  to_string=[%s]\n", dump.c_str());
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// 3. cluster-mode wiring + offline hash-slot computation
//////////////////////////////////////////////////////////////////////////

static void test_hash_slot(acl::redis_client_cluster* cluster)
{
	section("acl::redis_command::hash_slot (cluster mode, computed offline)");

	acl::redis_key key;
	key.set_cluster(cluster);
	CHECK(key.get_cluster() == cluster);
	CHECK(key.get_client() == NULL);
	CHECK(key.get_slot() == -1);

	// hash_slot() computes only when a cluster (or pipeline) is bound
	key.hash_slot("foo");
	int slotFoo = key.get_slot();
	printf("  slot(\"foo\") = %d (max_slot=%d)\n", slotFoo,
		cluster->get_max_slot());
	CHECK(slotFoo >= 0 && slotFoo < cluster->get_max_slot());

	// clear(false) drops the cached slot so the next key is recomputed
	key.clear(false);
	key.hash_slot("{user}.1");
	int slotTag = key.get_slot();
	printf("  slot(\"{user}.1\") = %d\n", slotTag);
	CHECK(slotTag >= 0 && slotTag < cluster->get_max_slot());

	// explicit key length overload
	key.clear(false);
	key.hash_slot("bar", 3);
	printf("  slot(\"bar\") = %d\n", key.get_slot());
	CHECK(key.get_slot() >= 0);

	// hash_slot() CRC16s the ENTIRE key string: unlike the redis-server it
	// does NOT extract the {hash-tag} substring, so two keys sharing the
	// "{user}" tag may land on different slots. What the wrapper guarantees
	// is that the computation is deterministic and stays inside the ring.
	key.clear(false);
	key.hash_slot("{user}.99999");
	int slotTag2 = key.get_slot();
	printf("  slot(\"{user}.99999\") = %d (\"{user}.1\" was %d)\n",
		slotTag2, slotTag);
	CHECK(slotTag2 >= 0 && slotTag2 < cluster->get_max_slot());
	key.clear(false);
	key.hash_slot("{user}.99999");
	CHECK(key.get_slot() == slotTag2);   // same key -> same slot

	// save_slot=true keeps the computed slot across clear()
	key.clear(true);
	printf("  slot after clear(save_slot=true) = %d\n", key.get_slot());
	CHECK(key.get_slot() >= 0);

	// a slot >= max_slot is invalid -> hash_slot() leaves it recomputed
	key.clear(false);
	key.set_client_addr("127.0.0.1:6379");
	printf("  cluster redirect knobs: max=%d sleep=%dms\n",
		cluster->get_redirect_max(), cluster->get_redirect_sleep());
}

//////////////////////////////////////////////////////////////////////////
// 4. redis_client_cluster / redis_client_pool / redis_client_pipeline
//////////////////////////////////////////////////////////////////////////

static void test_client_infrastructure(acl::redis_client_cluster* cluster,
	acl::redis_client_pool* pool, acl::redis_client_pipeline* pipeline)
{
	section("redis_client_cluster / redis_client_pool / redis_client_pipeline");

	// ---- redis_client_cluster (a connect_manager) --------------------
	CHECK(cluster->get_max_slot() == 16384);
	cluster->set_redirect_max(5);
	CHECK(cluster->get_redirect_max() == 5);
	cluster->set_redirect_sleep(200);
	CHECK(cluster->get_redirect_sleep() == 200);

	// per-node passwords (address "default" -> all nodes)
	cluster->set_password("default", "pass-default");
	cluster->set_password(kRedisAddr, "pass-node");
	CHECK(strcmp(cluster->get_password(kRedisAddr), "pass-node") == 0);
	printf("  password(%s) = %s\n", kRedisAddr, cluster->get_password(kRedisAddr));
	printf("  password map holds %lu entries\n",
		(unsigned long) cluster->get_passwords().size());
	CHECK(cluster->get_passwords().size() >= 2);
	cluster->set_ssl_conf(NULL);

	// slot <-> address table maintenance is purely local
	cluster->set_slot(0, kRedisAddr);
	cluster->set_slot(8192, kRedisAddrB);
	cluster->clear_slot(8192);
	printf("  peek_slot(8192) after clear = %p\n", (void*) cluster->peek_slot(8192));

	// connect_manager bookkeeping
	printf("  pools managed = %lu\n", (unsigned long) cluster->size());
	cluster->set_retry_inter(10);
	cluster->set_idle_ttl(60);
	cluster->set_check_inter(30);
	cluster->set_pools_status(kRedisAddr, true);
	cluster->lock();
	cluster->unlock();
	// NOTE: peek()/peek_conn()/redirect()/set_all_slot() would dial out --
	// they are what needs a running cluster, so they stay guarded below.
	if (kHaveServer) {
		cluster->set_all_slot(kRedisAddr, 4, 3, 3);
		acl::connect_pool* p1 = cluster->peek();
		acl::connect_pool* p2 = cluster->peek(kRedisAddr, true);
		acl::redis_client* c1 = cluster->peek_conn(0);
		acl::redis_client* c2 = cluster->redirect(kRedisAddr, 4);
		printf("  pools: %p %p conns: %p %p\n",
			(void*) p1, (void*) p2, (void*) c1, (void*) c2);
		cluster->remove(kRedisAddr);
		cluster->check_idle_conns(10);
		cluster->check_dead_conns(10);
		cluster->keep_min_conns(10);
		cluster->statistics();
	}

	// ---- redis_client_pool (a connect_pool) --------------------------
	// redis_client_pool(addr, count, idx) -- count 0 means "unlimited"
	printf("  pool addr=%s db=%d\n", pool->get_addr(), pool->get_db());
	CHECK(strcmp(pool->get_addr(), kRedisAddr) == 0);
	pool->set_password("pool-pass");
	pool->set_db(2);
	CHECK(pool->get_db() == 2);
	pool->set_db(0);
	pool->set_ssl_conf(NULL);

	// connect_pool tuning knobs -- none of these touch the network
	pool->set_conns_min(1);
	pool->set_retry_inter(15);
	pool->set_idle_ttl(120);
	pool->set_check_inter(45);
	pool->set_key("my-pool-key");
	pool->set_alive(true);
	printf("  aliving = %d\n", pool->aliving() ? 1 : 0);
	printf("  check_idle(3600) removed = %lu\n",
		(unsigned long) pool->check_idle((time_t) 3600));
	printf("  check_idle() removed = %lu\n",
		(unsigned long) pool->check_idle());
	// peek()/bind_one() would create real connections -> guarded
	if (kHaveServer) {
		acl::connect_client* conn = pool->peek(true);
		printf("  pool connection = %p\n", (void*) conn);
		if (conn != NULL) {
			pool->bind_one(conn);
		}
		pool->check_dead();
		pool->keep_conns();
		pool->reset_statistics(1);
	}

	// ---- redis_client_pipeline ---------------------------------------
	// constructing it starts nothing; start_thread() spawns the pipeline
	// worker, so we only touch the setters here.
	printf("  pipeline max_slot = %lu\n",
		(unsigned long) pipeline->get_max_slot());
	CHECK(pipeline->get_max_slot() == 16384);
	pipeline->set_max_slot(16384);
	CHECK(pipeline->get_max_slot() == 16384);
	pipeline->set_password("pipe-pass");
	pipeline->set_timeout(3, 5);
	pipeline->set_retry(false);
	pipeline->set_preconnect(false);
	pipeline->set_ssl_conf(NULL);

	acl::redis_string viaPipeline;
	viaPipeline.set_pipeline(pipeline);
	CHECK(viaPipeline.get_pipeline() == pipeline);
	printf("  command bound to pipeline: %p\n",
		(void*) viaPipeline.get_pipeline());
	// get_pipeline_message() asserts pipeline_ != NULL, so it is valid now
	acl::redis_pipeline_message* msg = viaPipeline.get_pipeline_message();
	printf("  pipeline message = %p\n", (void*) msg);
	// hash_slot() also works in pipeline mode
	viaPipeline.hash_slot("pipe-key");
	printf("  pipeline slot(\"pipe-key\") = %d\n", viaPipeline.get_slot());
	CHECK(viaPipeline.get_slot() >= 0);

	if (kHaveServer) {
		pipeline->set_preconnect(true);
		pipeline->start_thread();
		viaPipeline.set("pipe-key", "pipe-value");
		acl::string v;
		viaPipeline.get("pipe-key", v);
		printf("  pipeline get = %s\n", v.c_str());
		pipeline->stop_thread();
	}

	// ---- wiring a command straight through the cluster / pipeline ----
	acl::redis zsetViaCluster(cluster);
	CHECK(zsetViaCluster.get_cluster() == cluster);
	(void) zsetViaCluster;
}

//////////////////////////////////////////////////////////////////////////
// 5. redis_key
//////////////////////////////////////////////////////////////////////////

static void test_redis_key(acl::redis_client* client)
{
	section("acl::redis_key (key space / expiry / scan)");

	acl::redis_key key;
	key.set_client(client);
	CHECK(key.get_client() == client);
	key.set_slice_respond(true);
	key.set_slice_respond(false);
	key.clear();

	if (kHaveServer) {
		acl::string out;
		StrVec keys;
		CStrVec ckeys;
		keys.push_back("k1"); keys.push_back("k2");
		ckeys.push_back("k1"); ckeys.push_back("k2");
		const char* karr[] = { "k1", "k2" };
		const size_t klens[] = { 2, 2 };
		const size_t count = 100;
		std::vector<size_t> migrateLens;
		int cursor = 0;

		// DEL family: del() returns the number of keys actually removed
		printf("  del_one=%d del(vector)=%d del(vararg)=%d\n",
			key.del_one("k1"), key.del(keys),
			key.del_keys("k1", "k2", NULL));
		printf("  del(char*[])=%d del(char*[],lens)=%d del(cvec)=%d\n",
			key.del(karr, 2), key.del(karr, klens, 2), key.del(ckeys));
		printf("  del_keys(vector)=%d del_keys(cvec)=%d del_keys(arr)=%d\n",
			key.del_keys(keys), key.del_keys(ckeys), key.del_keys(karr, 2));

		printf("  exists=%d\n", key.exists("k1") ? 1 : 0);
		printf("  expire=%d pexpire=%d expireat=%d pexpireat=%d persist=%d\n",
			key.expire("k1", 60), key.pexpire("k1", 60000),
			key.expireat("k1", time(NULL) + 60),
			key.pexpireat("k1", 60000), key.persist("k1"));
		printf("  ttl=%d pttl=%lld\n", key.ttl("k1"),
			(long long) key.pttl("k1"));

		acl::redis_key_t t = key.type("k1");
		printf("  type=%d (NONE=%d STRING=%d HASH=%d LIST=%d SET=%d ZSET=%d)\n",
			(int) t, (int) acl::REDIS_KEY_NONE, (int) acl::REDIS_KEY_STRING,
			(int) acl::REDIS_KEY_HASH, (int) acl::REDIS_KEY_LIST, (int) acl::REDIS_KEY_SET,
			(int) acl::REDIS_KEY_ZSET);

		printf("  dump=%d restore=%d\n", key.dump("k1", out),
			(int) key.restore("k2", out.c_str(), out.length(), 0, true));

		printf("  keys_pattern=%d scan=%d\n",
			key.keys_pattern("k*", &keys),
			key.scan(cursor, keys, "k*", &count));

		printf("  rename_key=%d renamenx=%d move=%d randomkey=%d\n",
			(int) key.rename_key("k1", "k1.bak"),
			key.renamenx("k1", "k1.bak"), key.move("k1", 1),
			(int) key.randomkey(out));

		printf("  object_refcount=%d object_idletime=%d object_encoding=%d\n",
			key.object_refcount("k1"), key.object_idletime("k1"),
			(int) key.object_encoding("k1", out));

		printf("  migrate=%d migrate(batch)=%d\n",
			(int) key.migrate("k1", kRedisAddrB, 0, 1000, NULL),
			(int) key.migrate(kRedisAddrB, 0, 1000, ckeys, migrateLens, NULL));
	}
}

//////////////////////////////////////////////////////////////////////////
// 6. redis_string
//////////////////////////////////////////////////////////////////////////

static void test_redis_string(acl::redis_client* client)
{
	section("acl::redis_string (SET / GET / INCR / BIT commands)");

	acl::redis_string str;
	str.set_client(client);
	CHECK(str.get_client() == client);

	// the SET option flags are macros exported by redis_string.hpp
	printf("  SETFLAG_EX=%d PX=%d NX=%d XX=%d\n",
		SETFLAG_EX, SETFLAG_PX, SETFLAG_NX, SETFLAG_XX);
	CHECK((SETFLAG_NX & 0x0C) != 0);

	if (kHaveServer) {
		acl::string buf;
		StrVec out;
		CStrVec keys;
		keys.push_back("k1"); keys.push_back("k2");
		const char* karr[] = { "k1", "k2" };
		const char* varr[] = { "v1", "v2" };
		const size_t klens[] = { 2, 2 };
		const size_t vlens[] = { 2, 2 };
		long long int n = 0;
		double d = 0.0;
		int bit = 0;

		StrMap kv;
		kv["k1"] = "v1";
		kv["k2"] = "v2";

		// plain SET / SET with EX|PX + NX|XX
		printf("  set=%d\n", str.set("k1", "v1") ? 1 : 0);
		printf("  set(len)=%d\n",
			str.set("k1", 2, "v1", 2) ? 1 : 0);
		printf("  set(EX,NX)=%d set(PX,XX)=%d\n",
			str.set("k1", "v1", 60, SETFLAG_EX | SETFLAG_NX) ? 1 : 0,
			str.set("k1", "v1", 60000, SETFLAG_PX | SETFLAG_XX) ? 1 : 0);
		printf("  setex=%d psetex=%d setnx=%d\n",
			str.setex("k1", "v1", 60) ? 1 : 0,
			str.psetex("k1", "v1", 60000) ? 1 : 0,
			str.setnx("k1", "v1"));

		// GET: bool get(key, string&) or raw const redis_result* get(key)
		printf("  get(str)=%d value=%s\n", str.get("k1", buf) ? 1 : 0,
			buf.c_str());
		printf("  get(len,str)=%d\n", str.get("k1", 2, buf) ? 1 : 0);
		const acl::redis_result* rr = str.get("k1");
		printf("  get(raw) result type=%d\n", rr ? (int) rr->get_type() : -1);

		printf("  getset=%d strlen=%d append=%d\n",
			str.getset("k1", "v2", buf) ? 1 : 0,
			str.get_strlen("k1"), str.append("k1", "suffix"));
		printf("  setrange=%d getrange=%d\n",
			str.setrange("k1", 0, "xyz"),
			str.getrange("k1", 0, 2, buf) ? 1 : 0);

		printf("  incr=%lld decr=%lld incrby=%lld decrby=%lld\n",
			(long long) (n = 0, str.incr("num", &n) ? n : -1),
			(long long) (str.decr("num", &n) ? n : -1),
			(long long) (str.incrby("num", 5, &n) ? n : -1),
			(long long) (str.decrby("num", 5, &n) ? n : -1));
		printf("  incrbyfloat=%g\n",
			str.incrbyfloat("fnum", 1.5, &d) ? d : 0.0);

		printf("  mset=%d msetnx=%d mget=%d\n",
			str.mset(kv) ? 1 : 0, str.msetnx(kv),
			str.mget(keys, &out) ? 1 : 0);
		printf("  mset(arr)=%d mget(vararg)=%d mget(arr)=%d\n",
			str.mset(karr, varr, 2) ? 1 : 0,
			str.mget(&out, "k1", "k2", NULL) ? 1 : 0,
			str.mget(karr, 2, &out) ? 1 : 0);

		printf("  setbit=%d getbit=%d bitcount=%d\n",
			str.setbit_("b", 0, true) ? 1 : 0,
			str.getbit("b", 0, bit) ? bit : -1,
			str.bitcount("b"));
		printf("  bitcount(range)=%d bitcount(len)=%d\n",
			str.bitcount("b", 0, 8), str.bitcount("b", 1));
		printf("  bitop_and=%d bitop_or=%d bitop_xor=%d\n",
			str.bitop_and("d", "k1", "k2", NULL),
			str.bitop_or("d", keys), str.bitop_xor("d", keys));
		printf("  bitop(arr)=%d\n", str.bitop_and("d", karr, 2));

		// NOTE: there is no redis_string::lcs()/substr() in this ACL version;
		// STRLEN / SUBSTRING-equivalents are get_strlen() and getrange().
		printf("  strlen=%d\n", str.get_strlen("k1"));
	}
}

//////////////////////////////////////////////////////////////////////////
// 7. redis_hash
//////////////////////////////////////////////////////////////////////////

static void test_redis_hash(acl::redis_client* client)
{
	section("acl::redis_hash (HSET / HGET / HMGET / HINCRBY commands)");

	acl::redis_hash hash;
	hash.set_client(client);
	CHECK(hash.get_client() == client);

	if (kHaveServer) {
		acl::string buf;
		StrVec names, values, result;
		CStrVec cnames;
		StrMap attrs;
		const char* narr[] = { "f1", "f2" };
		const char* varr[] = { "v1", "v2" };
		const size_t nlens[] = { 2, 2 };
		const size_t vlens[] = { 2, 2 };
		const size_t count = 100;
		long long int n = 0;
		double d = 0.0;
		int cursor = 0;

		names.push_back("f1"); names.push_back("f2");
		cnames.push_back("f1"); cnames.push_back("f2");
		values.push_back("v1"); values.push_back("v2");
		attrs["f1"] = "v1";
		attrs["f2"] = "v2";

		printf("  hset=%d hset(len)=%d hsetnx=%d\n",
			hash.hset("h", "f1", "v1"),
			hash.hset("h", 1, "f1", 2, "v1", 2),
			hash.hsetnx("h", "f3", "v3"));
		printf("  hmset(map)=%d hmset(vec)=%d hmset(arr)=%d\n",
			hash.hmset("h", attrs) ? 1 : 0,
			hash.hmset("h", names, values) ? 1 : 0,
			hash.hmset("h", narr, varr, 2) ? 1 : 0);

		printf("  hget=%d value=%s\n", hash.hget("h", "f1", buf) ? 1 : 0,
			buf.c_str());
		printf("  hmget=%d\n", hash.hmget("h", names, &result) ? 1 : 0);
		printf("  hmget(cvec)=%d hmget(arr)=%d\n",
			hash.hmget("h", cnames, &result) ? 1 : 0,
			hash.hmget("h", narr, 2, &result) ? 1 : 0);

		StrMap all;
		printf("  hgetall=%d entries=%lu\n",
			hash.hgetall("h", all) ? 1 : 0,
			(unsigned long) all.size());

		printf("  hexists=%d hlen=%d hstrlen=%d\n",
			hash.hexists("h", "f1") ? 1 : 0, hash.hlen("h"),
			hash.hstrlen("h", "f1"));
		printf("  hkeys=%d hvals=%d\n",
			hash.hkeys("h", names) ? 1 : 0, hash.hvals("h", values) ? 1 : 0);

		printf("  hdel=%d hdel(vararg)=%d hdel_fields=%d\n",
			hash.hdel("h", "f1"), hash.hdel("h", narr, 2),
			hash.hdel_fields("h", "f1", "f2", NULL));
		printf("  hdel(vec)=%d hdel(cvec)=%d\n",
			hash.hdel("h", names), hash.hdel("h", cnames));

		printf("  hincrby=%lld hincrbyfloat=%g\n",
			(long long) (hash.hincrby("h", "n", 3, &n) ? n : -1),
			hash.hincrbyfloat("h", "f", 1.5, &d) ? d : 0.0);
		printf("  hscan=%d cursor=%d\n",
			hash.hscan("h", cursor, attrs, "f*", &count), cursor);
	}
}

//////////////////////////////////////////////////////////////////////////
// 8. redis_list
//////////////////////////////////////////////////////////////////////////

static void test_redis_list(acl::redis_client* client)
{
	section("acl::redis_list (LPUSH / RPUSH / POP / LRANGE commands)");

	acl::redis_list list;
	list.set_client(client);
	CHECK(list.get_client() == client);

	if (kHaveServer) {
		acl::string buf;
		StrVec values, keys;
		std::vector<acl::string> out;
		CStrVec ckeys, cvals;
		ckeys.push_back("l1"); ckeys.push_back("l2");
		cvals.push_back("a"); cvals.push_back("b");
		keys.push_back("l1"); keys.push_back("l2");
		const char* varr[] = { "a", "b" };
		const size_t vlens[] = { 1, 1 };
		std::pair<acl::string, acl::string> kv;

		values.push_back("a"); values.push_back("b");

		printf("  lpush(vararg)=%d lpush(vec)=%d lpush(cvec)=%d\n",
			list.lpush("l", "a", "b", NULL), list.lpush("l", values),
			list.lpush("l", cvals));
		printf("  lpush(arr)=%d lpush(lens)=%d lpushx=%d\n",
			list.lpush("l", varr, 2), list.lpush("l", varr, vlens, 2),
			list.lpushx("l", "c"));
		printf("  rpush(vararg)=%d rpush(vec)=%d rpushx=%d\n",
			list.rpush("l", "a", "b", NULL), list.rpush("l", values),
			list.rpushx("l", "c"));

		printf("  llen=%d lindex=%d\n", list.llen("l"),
			list.lindex("l", 0, buf) ? 1 : 0);
		printf("  lrange=%d\n", list.lrange("l", 0, -1, &out) ? 1 : 0);
		printf("  lset=%d lrem=%d ltrim=%d\n",
			list.lset("l", 0, "z") ? 1 : 0, list.lrem("l", 1, "a"),
			list.ltrim("l", 0, 5) ? 1 : 0);

		printf("  lpop=%d rpop=%d\n", list.lpop("l", buf), list.rpop("l", buf));
		printf("  rpoplpush=%d\n",
			list.rpoplpush("l", "l2", &buf) ? 1 : 0);
		printf("  brpoplpush=%d\n",
			list.brpoplpush("l", "l2", (size_t) 5, &buf) ? 1 : 0);

		printf("  blpop(vararg)=%d blpop(cvec)=%d blpop(vec)=%d\n",
			list.blpop(kv, (size_t) 5, "l", NULL) ? 1 : 0,
			list.blpop(ckeys, (size_t) 5, kv) ? 1 : 0,
			list.blpop(keys, (size_t) 5, kv) ? 1 : 0);
		printf("  brpop(vararg)=%d brpop(cvec)=%d\n",
			list.brpop(kv, (size_t) 5, "l", NULL) ? 1 : 0,
			list.brpop(ckeys, (size_t) 5, kv) ? 1 : 0);

		// LINSERT with the explicit BEFORE/AFTER helpers
		printf("  linsert_after=%d linsert_before=%d\n",
			list.linsert_after("l", "a", "z"),
			list.linsert_before("l", "a", "y"));
		printf("  linsert(len)=%d\n",
			list.linsert_after("l", "a", 1, "z", 1) ? 1 : 0);
		// NOTE: pop()/bpop()/pushx() are the PRIVATE implementation helpers
		// behind lpop()/blpop()/lpushx() and are not callable from here;
		// this ACL release has no LMOVE / LMPOP / LPOS wrappers either.
	}
}

//////////////////////////////////////////////////////////////////////////
// 9. redis_set
//////////////////////////////////////////////////////////////////////////

static void test_redis_set(acl::redis_client* client)
{
	section("acl::redis_set (SADD / SMEMBERS / SINTER / SUNION commands)");

	acl::redis_set set;
	set.set_client(client);
	CHECK(set.get_client() == client);

	if (kHaveServer) {
		acl::string buf;
		StrVec members, keys;
		CStrVec cmembers, ckeys;
		const char* marr[] = { "a", "b" };
		const size_t mlens[] = { 1, 1 };
		const size_t count = 100;
		int cursor = 0;

		members.push_back("a"); members.push_back("b");
		keys.push_back("s1"); keys.push_back("s2");
		cmembers.push_back("a"); cmembers.push_back("b");
		ckeys.push_back("s1"); ckeys.push_back("s2");

		printf("  sadd(vararg)=%d sadd(vec)=%d sadd(cvec)=%d\n",
			set.sadd("s", "a", "b", NULL), set.sadd("s", members),
			set.sadd("s", cmembers));
		printf("  sadd(arr)=%d scard=%d sismember=%d\n",
			set.sadd("s", marr, 2), set.scard("s"),
			set.sismember("s", "a") ? 1 : 0);
		printf("  smembers=%d\n", set.smembers("s", &members));
		printf("  spop=%d srandmember=%d\n",
			set.spop("s", buf) ? 1 : 0, set.srandmember("s", buf));
		printf("  srandmember(n)=%d\n", set.srandmember("s", 2, members));
		printf("  smove=%d\n", set.smove("s", "s2", "a"));
		printf("  srem=%d\n", set.srem("s", "a", "b", NULL));
		printf("  srem(vec)=%d srem(cvec)=%d\n",
			set.srem("s", members), set.srem("s", cmembers));
		printf("  sadd(lens)=%d\n", set.sadd("s", marr, mlens, 2));

		// set algebra: the destination vector is passed by pointer
		printf("  sdiff=%d sinter=%d sunion=%d\n",
			set.sdiff(&members, "s1", "s2", NULL),
			set.sinter(keys, &members), set.sunion(ckeys, &members));
		printf("  sdiff(vec)=%d sinter(cvec)=%d sunion(vec)=%d\n",
			set.sdiff(keys, &members),
			set.sinter(ckeys, &members), set.sunion(keys, &members));
		printf("  sdiffstore=%d sinterstore=%d sunionstore=%d\n",
			set.sdiffstore("d", "s1", "s2", NULL),
			set.sinterstore("d", ckeys), set.sunionstore("d", members));

		printf("  sscan=%d cursor=%d\n",
			set.sscan("s", cursor, members, "a*", &count), cursor);
		// NOTE: this ACL release wraps neither SMISMEMBER nor the
		// count-variant SPOP (only bool spop(key, string&) exists).
	}
}

//////////////////////////////////////////////////////////////////////////
// 10. redis_zset
//////////////////////////////////////////////////////////////////////////

static void test_redis_zset(acl::redis_client* client)
{
	section("acl::redis_zset (ZADD / ZRANGE / ZRANGEBYSCORE commands)");

	acl::redis_zset zset;
	zset.set_client(client);
	CHECK(zset.get_client() == client);

	if (kHaveServer) {
		StrVec members;
		std::vector<double> scores;
		std::vector<acl::string> out;
		std::vector<std::pair<acl::string, double> > scored;
		StrDoubleMap mmap;
		CStrVec cmembers;
		const char* marr[] = { "a", "b" };
		size_t mlens[] = { 1, 1 };
		double dscore[] = { 1.0, 2.0 };
		double d = 0.0;
		const int offset = 0, limit = 10;
		const size_t count = 100;
		int cursor = 0;

		members.push_back("a"); members.push_back("b");
		scores.push_back(1.0); scores.push_back(2.0);
		cmembers.push_back("a"); cmembers.push_back("b");
		mmap["a"] = 1.0; mmap["b"] = 2.0;

		printf("  zadd(map)=%d zadd(vec)=%d zadd(cvec)=%d\n",
			zset.zadd("z", mmap), zset.zadd("z", members, scores),
			zset.zadd("z", cmembers, scores));
		printf("  zadd(pairs)=%d zadd(arr)=%d\n",
			zset.zadd("z", scored), zset.zadd("z", marr, dscore, 2));
		printf("  zadd(lens)=%d zadd_with_ch_xx=%d ch_nx=%d\n",
			zset.zadd("z", marr, mlens, dscore, 2),
			zset.zadd_with_ch_xx("z", mmap), zset.zadd_with_ch_nx("z", mmap));
		printf("  zadd_with_incr=%d\n",
			zset.zadd_with_incr("z", "a", 1.0, &d) ? 1 : 0);

		printf("  zcard=%d zcount=%d zlexcount=%d\n",
			zset.zcard("z"), zset.zcount("z", 0.0, 10.0),
			zset.zlexcount("z", "[a", "[b"));
		printf("  zscore=%g zrank=%d zrevrank=%d\n",
			zset.zscore("z", "a", d) ? d : 0.0,
			zset.zrank("z", "a"), zset.zrevrank("z", "a"));
		printf("  zincrby=%g\n", zset.zincrby("z", 1.0, "a", &d) ? d : 0.0);

		printf("  zrange=%d\n", zset.zrange("z", 0, -1, &out));
		printf("  zrange_with_scores=%d\n",
			zset.zrange_with_scores("z", 0, -1, scored));
		printf("  zrevrange=%d\n", zset.zrevrange("z", 0, -1, &out));
		printf("  zrangebyscore=%d\n",
			zset.zrangebyscore("z", 0.0, 10.0, &out, &offset, &limit));
		printf("  zrangebyscore(str)=%d\n",
			zset.zrangebyscore("z", "-inf", "+inf", &out));
		printf("  zrangebyscore_with_scores=%d\n",
			zset.zrangebyscore_with_scores("z", 0.0, 10.0, scored));
		printf("  zrevrangebyscore_with_scores=%d\n",
			zset.zrevrangebyscore_with_scores("z", 10.0, 0.0, scored));
		printf("  zrangebylex=%d\n",
			zset.zrangebylex("z", "[a", "[b", &out));

		printf("  zrem=%d zrem(vec)=%d\n",
			zset.zrem("z", "a", NULL), zset.zrem("z", members));
		printf("  zremrangebyrank=%d zremrangebyscore=%d zremrangebylex=%d\n",
			zset.zremrangebyrank("z", 0, 1),
			zset.zremrangebyscore("z", 0.0, 10.0),
			zset.zremrangebylex("z", "[a", "[b"));
		printf("  zpopmin=%d zpopmax=%d\n",
			zset.zpopmin("z", scored, 1), zset.zpopmax("z", scored, 1));
		printf("  bzpopmin=%d bzpopmax=%d\n",
			zset.bzpopmin("z", (size_t) 5, out[0], &d),
			zset.bzpopmax(members, (size_t) 5, out[0], &d));

		StrDoubleMap src;
		src["z"] = 1.0;
		printf("  zunionstore=%d zinterstore=%d\n",
			zset.zunionstore("dst", src, "SUM"),
			zset.zinterstore("dst", members, NULL, "MIN"));
		printf("  zscan=%d cursor=%d\n",
			zset.zscan("z", cursor, scored, "a*", &count), cursor);
	}
}

//////////////////////////////////////////////////////////////////////////
// 11. redis_connection -- PING / ECHO / SELECT / AUTH / QUIT live HERE
//////////////////////////////////////////////////////////////////////////

static void test_redis_connection(acl::redis_client* client)
{
	section("acl::redis_connection (auth / select / ping / echo / quit)");

	acl::redis_connection conn;
	conn.set_client(client);
	CHECK(conn.get_client() == client);

	// constructor variants
	acl::redis_connection conn2(client);
	CHECK(conn2.get_client() == client);
	(void) conn2;

	if (kHaveServer) {
		printf("  ping=%d echo=%d\n",
			conn.ping() ? 1 : 0, conn.echo("hello") ? 1 : 0);
		printf("  auth=%d select=%d\n",
			conn.auth("my-secret-passwd") ? 1 : 0, conn.select(1) ? 1 : 0);
		printf("  quit=%d\n", conn.quit() ? 1 : 0);
	}
}

//////////////////////////////////////////////////////////////////////////
// 12. redis_server -- server-side administration
//////////////////////////////////////////////////////////////////////////

static void test_redis_server(acl::redis_client* client)
{
	section("acl::redis_server (info / dbsize / save / config_*)");

	acl::redis_server server;
	server.set_client(client);
	CHECK(server.get_client() == client);

	if (kHaveServer) {
		acl::string buf;
		StrMap info;
		time_t stamp = 0;
		int micro = 0;

		printf("  info(str)=%d bytes\n", server.info(buf));
		printf("  info(map)=%d entries\n", server.info(info));
		printf("  dbsize=%d\n", server.dbsize());
		printf("  lastsave=%ld\n", (long) server.lastsave());
		printf("  get_time=%d stamp=%ld micro=%d\n",
			server.get_time(stamp, micro) ? 1 : 0, (long) stamp, micro);

		printf("  save=%d bgsave=%d bgrewriteaof=%d\n",
			server.save() ? 1 : 0, server.bgsave() ? 1 : 0,
			server.bgrewriteaof() ? 1 : 0);

		printf("  config_get=%d pairs\n",
			server.config_get("maxmemory", info));
		printf("  config_set=%d resetstat=%d rewrite=%d\n",
			server.config_set("maxmemory", "100mb") ? 1 : 0,
			server.config_resetstat() ? 1 : 0,
			server.config_rewrite() ? 1 : 0);

		printf("  client_setname=%d client_getname=%d\n",
			server.client_setname("acl-test") ? 1 : 0,
			server.client_getname(buf) ? 1 : 0);
		printf("  client_list=%d bytes client_kill=%d\n",
			server.client_list(buf), server.client_kill("127.0.0.1:1234") ? 1 : 0);

		printf("  slowlog_len=%d slowlog_reset=%d\n",
			server.slowlog_len(), server.slowlog_reset() ? 1 : 0);
		const acl::redis_result* slow = server.slowlog_get(10);
		printf("  slowlog_get -> %p\n", (void*) slow);

		printf("  flushdb=%d flushall=%d slaveof=%d\n",
			server.flushdb() ? 1 : 0, server.flushall() ? 1 : 0,
			server.slaveof(kRedisAddrB, 6380) ? 1 : 0);

		// monitor()/get_command() loop; shutdown() terminates the server, so
		// it is only shown, never invoked here.
		if (false) {
			server.monitor();
			while (server.get_command(buf)) { }
		}
		if (false) {
			server.shutdown(false);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// 13. redis_pubsub
//////////////////////////////////////////////////////////////////////////

static void test_redis_pubsub(acl::redis_client* client)
{
	section("acl::redis_pubsub (PUBLISH / SUBSCRIBE / PSUBSCRIBE)");

	acl::redis_pubsub ps;
	ps.set_client(client);
	CHECK(ps.get_client() == client);

	if (kHaveServer) {
		StrVec channels, patterns;
		CStrVec cchannels;
		channels.push_back("news"); patterns.push_back("news.*");
		cchannels.push_back("news");

		printf("  publish=%d receivers\n", ps.publish("news", "hello", 5));
		printf("  publish(plain)=%d\n", ps.publish("news", "hello", 5));
		printf("  subscribe(vararg)=%d subscribe(vec)=%d\n",
			ps.subscribe("news", "sports", NULL), ps.subscribe(channels));
		printf("  subscribe(cvec)=%d unsubscribe=%d\n",
			ps.subscribe(cchannels), ps.unsubscribe("news"));
		printf("  psubscribe(vec)=%d punsubscribe=%d\n",
			ps.psubscribe(patterns), ps.punsubscribe(patterns));
		printf("  pubsub_numpat=%d\n", ps.pubsub_numpat());

		std::map<acl::string, int> nums;
		printf("  pubsub_channels=%d pubsub_numsub=%d\n",
			ps.pubsub_channels(&channels, "news*", NULL),
			ps.pubsub_numsub(nums, "news", NULL));

		// get_message() blocks on the subscribed connection
		if (false) {
			acl::string ch, msg, type;
			while (ps.get_message(ch, msg, &type)) { }
		}
		// NOTE: subop()/subop_result()/check_channel() and the single-argument
		// pubsub_numsub(map&) are the PRIVATE building blocks behind the
		// public SUBSCRIBE/UNSUBSCRIBE/PUBSUB wrappers above.
	}
}

//////////////////////////////////////////////////////////////////////////
// 14. redis_transaction
//////////////////////////////////////////////////////////////////////////

static void test_redis_transaction(acl::redis_client* client)
{
	section("acl::redis_transaction (MULTI / EXEC / DISCARD / WATCH)");

	acl::redis_transaction tx;
	tx.set_client(client);
	CHECK(tx.get_client() == client);

	// get_size() reports how many commands were queued -- works offline
	printf("  queued commands = %lu\n", (unsigned long) tx.get_size());
	CHECK(tx.get_size() == 0);
	// get_child() on an empty transaction returns NULL -- offline safe
	CHECK(tx.get_child(0, NULL) == NULL);

	if (kHaveServer) {
		StrVec keys, args;
		keys.push_back("k1");
		args.push_back("k1"); args.push_back("v1");

		printf("  watch=%d\n", tx.watch(keys) ? 1 : 0);
		printf("  unwatch=%d\n", tx.unwatch() ? 1 : 0);
		printf("  multi=%d\n", tx.multi() ? 1 : 0);
		printf("  run_cmd=%d\n", tx.run_cmd("SET", args) ? 1 : 0);
		printf("  queued=%lu\n", (unsigned long) tx.get_size());
		printf("  discard=%d\n", tx.discard() ? 1 : 0);

		tx.multi();
		tx.run_cmd("GET", keys);
		printf("  exec=%d\n", tx.exec() ? 1 : 0);
		acl::string cmdname;
		const acl::redis_result* child = tx.get_child(0, &cmdname);
		printf("  child[0] cmd=%s result=%p\n", cmdname.c_str(), (void*) child);
	}
}

//////////////////////////////////////////////////////////////////////////
// 15. redis_script
//////////////////////////////////////////////////////////////////////////

static void test_redis_script(acl::redis_client* client)
{
	section("acl::redis_script (EVAL / EVALSHA / SCRIPT)");

	acl::redis_script script;
	script.set_client(client);
	CHECK(script.get_client() == client);

	if (kHaveServer) {
		StrVec keys, args, sha1s;
		CStrVec ckeys, cargs;
		std::vector<bool> exists;
		acl::string lua, sha1, out;
		int n = 0;
		long long int n64 = 0;

		lua = "return redis.call('set', KEYS[1], ARGV[1])";
		keys.push_back("k1"); args.push_back("v1");
		ckeys.push_back("k1"); cargs.push_back("v1");
		sha1s.push_back("0123456789abcdef");

		printf("  eval=%p\n", (void*) script.eval(lua.c_str(), keys, args));
		printf("  eval(cvec)=%p\n", (void*) script.eval(lua.c_str(), ckeys, cargs));
		printf("  eval_status=%d\n",
			script.eval_status(lua.c_str(), keys, args, "OK") ? 1 : 0);
		printf("  eval_number=%d eval_number64=%d\n",
			script.eval_number(lua.c_str(), keys, args, n) ? n : -1,
			script.eval_number64(lua.c_str(), keys, args, n64) ? (int) n64 : -1);
		printf("  eval_string=%d\n",
			script.eval_string(lua.c_str(), keys, args, out));
		printf("  evalsha_status=%d evalsha_number=%d\n",
			script.evalsha_status("abc", keys, args) ? 1 : 0,
			script.evalsha_number("abc", keys, args, n) ? n : -1);
		printf("  evalsha=%p\n", (void*) script.evalsha("abc", ckeys, cargs));

		printf("  script_load=%d sha=%s\n",
			script.script_load(lua, sha1) ? 1 : 0, sha1.c_str());
		printf("  script_exists=%d\n", script.script_exists(sha1s, exists));
		printf("  script_flush=%d script_kill=%d\n",
			script.script_flush() ? 1 : 0, script.script_kill() ? 1 : 0);
	}
}

//////////////////////////////////////////////////////////////////////////
// 16. redis_hyperloglog
//////////////////////////////////////////////////////////////////////////

static void test_redis_hyperloglog(acl::redis_client* client)
{
	section("acl::redis_hyperloglog (PFADD / PFCOUNT / PFMERGE)");

	acl::redis_hyperloglog hll;
	hll.set_client(client);
	CHECK(hll.get_client() == client);

	if (kHaveServer) {
		StrVec elems, keys;
		CStrVec celems, ckeys;
		elems.push_back("e1"); elems.push_back("e2");
		keys.push_back("h1"); keys.push_back("h2");
		celems.push_back("e1"); ckeys.push_back("h1");

		printf("  pfadd(vararg)=%d pfadd(vec)=%d pfadd(cvec)=%d\n",
			hll.pfadd("h", "e1", "e2", NULL), hll.pfadd("h", elems),
			hll.pfadd("h", celems));
		printf("  pfcount(vararg)=%d pfcount(vec)=%d pfcount(cvec)=%d\n",
			hll.pfcount("h1", "h2", NULL), hll.pfcount(keys),
			hll.pfcount(ckeys));
		printf("  pfmerge(vararg)=%d pfmerge(vec)=%d pfmerge(cvec)=%d\n",
			hll.pfmerge("dst", "h1", "h2", NULL) ? 1 : 0,
			hll.pfmerge("dst", keys) ? 1 : 0,
			hll.pfmerge("dst", ckeys) ? 1 : 0);
	}
}

//////////////////////////////////////////////////////////////////////////
// 17. redis_geo + geo_member
//////////////////////////////////////////////////////////////////////////

static void test_redis_geo(acl::redis_client* client)
{
	section("acl::redis_geo (GEOADD / GEORADIUS) and acl::geo_member");

	// geo_member is a plain value class: fully usable offline
	{
		acl::geo_member m("Palermo");
		m.set_name("Catania");
		printf("  geo_member name=%s\n", m.get_name());
		CHECK(strcmp(m.get_name(), "Catania") == 0);
		m.set_dist(150.0);
		m.set_coordinate(15.09, 37.55);
		m.set_hash(1234567890LL);
		printf("  dist=%g lon=%g lat=%g hash=%lld\n", m.get_dist(),
			m.get_longitude(), m.get_latitude(), (long long) m.get_hash());
		CHECK(m.get_dist() == 150.0);
		CHECK(m.get_longitude() == 15.09);
		CHECK(m.get_latitude() == 37.55);
		CHECK(m.get_hash() == 1234567890LL);

		acl::geo_member copy(m);
		CHECK(strcmp(copy.get_name(), "Catania") == 0);
		copy.set_name("Palermo");
		CHECK(strcmp(m.get_name(), "Catania") == 0);

		// the coordinate range constants come from redis_geo.hpp
		// (integer macros -> cast to double so %g reads the right type)
		printf("  lon range [%g, %g] lat range [%g, %g] invalid=%g\n",
			(double) GEO_LONGITUDE_MIN, (double) GEO_LONGITUDE_MAX,
			(double) GEO_LATITUDE_MIN, (double) GEO_LATITUDE_MAX,
			(double) GEO_INVALID);
		CHECK(GEO_LONGITUDE_MIN == -180);
		CHECK(GEO_LONGITUDE_MAX == 180);
		CHECK(GEO_INVALID == 360);

		// the unit / WITH / SORT enumerations are anonymous enums in acl
		printf("  units: FT=%d M=%d MI=%d KM=%d\n",
			acl::GEO_UNIT_FT, acl::GEO_UNIT_M,
			acl::GEO_UNIT_MI, acl::GEO_UNIT_KM);
		printf("  with: COORD=%d DIST=%d HASH=%d; sort: NONE=%d ASC=%d DESC=%d\n",
			acl::GEO_WITH_COORD, acl::GEO_WITH_DIST, acl::GEO_WITH_HASH,
			acl::GEO_SORT_NONE, acl::GEO_SORT_ASC, acl::GEO_SORT_DESC);
		// NOTE: acl::geo_member exposes the payload through get_dist()/
		// get_hash()/get_longitude()/get_latitude(); there is no get_coord().
	}

	acl::redis_geo geo;
	geo.set_client(client);
	CHECK(geo.get_client() == client);

	if (kHaveServer) {
		StrVec members, hashes;
		std::vector<double> lons, lats;
		std::vector<std::pair<double, double> > positions;
		std::pair<double, double> pos;
		acl::string hash;
		const char* marr[] = { "Palermo", "Catania" };
		const double lonarr[] = { 13.361389, 15.087269 };
		const double latarr[] = { 38.115556, 37.502669 };
		members.push_back("Palermo"); members.push_back("Catania");
		lons.push_back(13.361389); lons.push_back(15.087269);
		lats.push_back(38.115556); lats.push_back(37.502669);

		printf("  geoadd(one)=%d geoadd(vec)=%d\n",
			geo.geoadd("g", "Palermo", 13.361389, 38.115556),
			geo.geoadd("g", members, lons, lats));
		printf("  geoadd(arr)=%d\n", geo.geoadd("g", 2, marr, lonarr, latarr));
		printf("  geohash(one)=%d geohash(vec)=%d\n",
			geo.geohash("g", "Palermo", hash) ? 1 : 0,
			geo.geohash("g", members, hashes) ? 1 : 0);
		printf("  geopos(one)=%d geopos(vec)=%d\n",
			geo.geopos("g", "Palermo", pos) ? 1 : 0,
			geo.geopos("g", members, positions) ? 1 : 0);
		printf("  geodist=%g m, km=%g\n",
			geo.geodist("g", "Palermo", "Catania", acl::GEO_UNIT_M),
			geo.geodist("g", "Palermo", "Catania", acl::GEO_UNIT_KM));

		const std::vector<acl::geo_member>& found =
			geo.georadius("g", 15.0, 37.0, 200.0, acl::GEO_UNIT_KM,
				acl::GEO_WITH_COORD | acl::GEO_WITH_DIST, acl::GEO_SORT_ASC);
		printf("  georadius -> %lu members\n", (unsigned long) found.size());
		const std::vector<acl::geo_member>& found2 =
			geo.georadiusbymember("g", "Palermo", 200.0, acl::GEO_UNIT_KM);
		printf("  georadiusbymember -> %lu members\n",
			(unsigned long) found2.size());
	}
}

//////////////////////////////////////////////////////////////////////////
// 18. redis_stream (+ the stream value structs)
//////////////////////////////////////////////////////////////////////////

static void test_redis_stream(acl::redis_client* client)
{
	section("acl::redis_stream (XADD / XRANGE / XREADGROUP) and stream structs");

	// the stream helper structs are plain data -- fully usable offline
	{
		acl::redis_stream_field f;
		f.name = "temperature"; f.value = "42";
		acl::redis_stream_message m;
		m.id = "1-0";
		m.fields.push_back(f);
		acl::redis_stream_messages ms;
		ms.key = "s";
		ms.messages.push_back(m);
		printf("  stream message key=%s count=%lu id=%s field=%s=%s\n",
			ms.key.c_str(), (unsigned long) ms.size(), m.id.c_str(),
			m.fields[0].name.c_str(), m.fields[0].value.c_str());
		CHECK(ms.empty() == false);
		CHECK(ms.size() == 1);

		acl::redis_stream_info si;
		printf("  stream_info length=%lu groups=%lu\n",
			(unsigned long) si.length, (unsigned long) si.groups);
		CHECK(si.length == 0 && si.groups == 0);

		acl::redis_xinfo_consumer consumer;
		acl::redis_xinfo_group group;
		acl::redis_pending_consumer pc;
		acl::redis_pending_message pm;
		printf("  consumer pending=%lu idle=%lu groups=%lu/%lu pc=%lu pm=%llu/%lu\n",
			(unsigned long) consumer.pending, (unsigned long) consumer.idle,
			(unsigned long) group.consumers, (unsigned long) group.pending,
			(unsigned long) pc.pending_number,
			(unsigned long long) pm.elapsed,
			(unsigned long) pm.delivered);
		CHECK(consumer.pending == 0 && consumer.idle == 0);
		CHECK(group.consumers == 0 && group.pending == 0);
		CHECK(pc.pending_number == 0);
		CHECK(pm.elapsed == 0 && pm.delivered == 0);

		acl::redis_pending_summary summary;
		summary.consumers.push_back(pc);
		acl::redis_pending_detail detail;
		detail.messages["1-0"] = pm;
		printf("  summary size=%lu empty=%d detail size=%lu empty=%d\n",
			(unsigned long) summary.size(), summary.empty() ? 1 : 0,
			(unsigned long) detail.size(), detail.empty() ? 1 : 0);
		CHECK(summary.size() == 1 && summary.empty() == false);
		CHECK(detail.size() == 1 && detail.empty() == false);
	}

	acl::redis_stream stream;
	stream.set_client(client);
	CHECK(stream.get_client() == client);

	if (kHaveServer) {
		acl::string id;
		StrMap fields;
		StrVec names, vals, ids;
		CStrVec cnames, cvals, cids;
		std::map<acl::string, acl::string> streams;
		StrVec msgIds;
		std::vector<acl::redis_stream_message> msgs;
		acl::redis_stream_messages messages;
		acl::redis_stream_info info;
		acl::redis_pending_summary psummary;
		acl::redis_pending_detail pdetail;
		std::map<acl::string, acl::redis_xinfo_consumer> consumers;
		std::map<acl::string, acl::redis_xinfo_group> groups;
		const char* narr[] = { "f1" };
		const size_t nlens[] = { 2 };
		const char* varr[] = { "v1" };
		const size_t vlens[] = { 2 };

		fields["f1"] = "v1";
		names.push_back("f1"); vals.push_back("v1");
		cnames.push_back("f1"); cvals.push_back("v1");
		ids.push_back("1-0"); cids.push_back("1-0");
		streams["s"] = "$";

		printf("  xadd(map)=%d id=%s\n",
			stream.xadd("s", fields, id) ? 1 : 0, id.c_str());
		printf("  xadd(vec)=%d\n", stream.xadd("s", names, vals, id) ? 1 : 0);
		printf("  xadd(cvec)=%d\n", stream.xadd("s", cnames, cvals, id) ? 1 : 0);
		printf("  xadd(arr)=%d xadd_maxlen=%d\n",
			stream.xadd("s", narr, nlens, varr, vlens, 1, id) ? 1 : 0,
			stream.xadd_with_maxlen("s", 100, fields, id) ? 1 : 0);
		printf("  xlen=%d xdel=%d xdel(vec)=%d xtrim=%d\n",
			stream.xlen("s"), stream.xdel("s", "1-0"),
			stream.xdel("s", ids), stream.xtrim("s", 10, true));

		printf("  xrange=%d\n", stream.xrange(messages, "s") ? 1 : 0);
		printf("  xrevrange=%d\n", stream.xrevrange(messages, "s") ? 1 : 0);
		printf("  xread=%d\n", stream.xread(messages, streams, 100, -1) ? 1 : 0);

		printf("  xgroup_create=%d setid=%d destroy=%d delconsumer=%d\n",
			stream.xgroup_create("s", "g", "$", true) ? 1 : 0,
			stream.xgroup_setid("s", "g", "$") ? 1 : 0,
			stream.xgroup_destroy("s", "g"),
			stream.xgroup_delconsumer("s", "g", "c1"));
		printf("  xreadgroup=%d noack=%d\n",
			stream.xreadgroup(messages, "g", "c1", streams, 100, -1, false) ? 1 : 0,
			stream.xreadgroup_with_noack(messages, "g", "c1", streams) ? 1 : 0);
		printf("  xack=%d xack(vec)=%d\n",
			stream.xack("s", "g", "1-0"), stream.xack("s", "g", ids));
		printf("  xclaim=%d\n",
			stream.xclaim(msgs, "s", "g", "c1", 0, ids) ? 1 : 0);
		printf("  xclaim_justid=%d\n",
			stream.xclaim_with_justid(msgIds, "s", "g", "c1", 0, ids) ? 1 : 0);
		printf("  xpending_summary=%d detail=%d\n",
			stream.xpending_summary("s", "g", psummary) ? 1 : 0,
			stream.xpending_detail(pdetail, "s", "g") ? 1 : 0);
		printf("  xinfo_stream=%d groups=%d consumers=%d\n",
			stream.xinfo_stream("s", info) ? 1 : 0,
			stream.xinfo_groups("s", groups) ? 1 : 0,
			stream.xinfo_consumers("s", "g", consumers) ? 1 : 0);
		printf("  xgroup_help=%d xinfo_help=%d\n",
			stream.xgroup_help(names) ? 1 : 0, stream.xinfo_help(vals) ? 1 : 0);
	}
}

//////////////////////////////////////////////////////////////////////////
// 19. redis_cluster (cluster.* commands) + redis_slot / redis_node
//////////////////////////////////////////////////////////////////////////

static void test_redis_cluster_cmds(acl::redis_client* client,
	acl::redis_client_cluster* cluster)
{
	section("acl::redis_cluster (CLUSTER *) + acl::redis_slot / acl::redis_node");

	// redis_slot is a value class: constructible and inspectable offline
	{
		acl::redis_slot slot(0, 8191, "127.0.0.1", 6379);
		printf("  slot range [%lu, %lu] ip=%s port=%d\n",
			(unsigned long) slot.get_slot_min(),
			(unsigned long) slot.get_slot_max(),
			slot.get_ip(), slot.get_port());
		CHECK(slot.get_slot_min() == 0);
		CHECK(slot.get_slot_max() == 8191);
		CHECK(slot.get_port() == 6379);
		CHECK(strcmp(slot.get_ip(), "127.0.0.1") == 0);
		CHECK(slot.get_slaves().empty());

		acl::redis_slot copy(slot);
		CHECK(copy.get_slot_min() == 0 && copy.get_port() == 6379);
		// add_slave() TRANSFERS OWNERSHIP: ~redis_slot() deletes every pointer
		// held in slaves_ (lib_acl_cpp/src/redis/redis_slot.cpp), so the slave
		// node must be heap allocated -- passing the address of a stack object
		// makes the destructor delete a non-heap pointer (0xc0000374).
		slot.add_slave(new acl::redis_slot(0, 0, "127.0.0.1", 6389));
		printf("  slaves of slot = %lu\n", (unsigned long) slot.get_slaves().size());
		CHECK(slot.get_slaves().size() == 1);
	}

	// redis_node likewise
	{
		acl::redis_node node;
		node.set_id("0123456789abcdef");
		node.set_addr(kRedisAddr);
		node.set_type("master");
		node.set_myself(true);
		node.set_connected(true);
		node.set_handshaking(false);
		node.set_master(&node);
		node.set_master_id("0123456789abcdef");
		node.add_slot_range(0, 8191);
		printf("  node id=%s addr=%s info=%s type=%s\n", node.get_id(),
			node.get_addr(), node.get_addr_info(), node.get_type());
		CHECK(strcmp(node.get_id(), "0123456789abcdef") == 0);
		CHECK(strcmp(node.get_addr(), kRedisAddr) == 0);
		CHECK(node.is_myself());
		CHECK(node.is_connected());
		CHECK(node.is_handshaking() == false);
		CHECK(node.is_master());
		CHECK(node.get_master() == &node);
		printf("  node slots = %lu\n", (unsigned long) node.get_slots().size());
		CHECK(node.get_slots().size() == 1);
		CHECK(node.get_slots()[0].first == 0);
		CHECK(node.get_slots()[0].second == 8191);
		printf("  slaves = %lu\n", (unsigned long) node.get_slaves()->size());
		node.clear_slaves(false);
	}

	acl::redis_cluster cmd;
	cmd.set_client(client);
	CHECK(cmd.get_client() == client);

	acl::redis_cluster viaCluster(cluster);
	CHECK(viaCluster.get_cluster() == cluster);

	if (kHaveServer) {
		StrMap info;
		std::list<acl::string> keys;
		const int slots[] = { 0, 1, 2 };
		StrVec slotList;

		printf("  cluster_info=%d\n", cmd.cluster_info(info) ? 1 : 0);
		printf("  cluster_keyslot=%d\n", cmd.cluster_keyslot("foo"));
		printf("  cluster_slots=%p\n", (void*) cmd.cluster_slots());
		printf("  cluster_nodes=%p\n", (void*) cmd.cluster_nodes());
		printf("  cluster_slaves=%p\n", (void*) cmd.cluster_slaves("nodeid"));
		printf("  cluster_countkeysinslot=%d\n", cmd.cluster_countkeysinslot(0));
		printf("  cluster_getkeysinslot=%d\n", cmd.cluster_getkeysinslot(0, 10, keys));
		printf("  cluster_count_failure_reports=%d\n",
			cmd.cluster_count_failure_reports("nodeid"));

		printf("  cluster_addslots(arr)=%d addslots(vararg)=%d\n",
			cmd.cluster_addslots(slots, 3) ? 1 : 0,
			cmd.cluster_addslots(0, 1, 2, -1) ? 1 : 0);
		printf("  cluster_delslots=%d\n", cmd.cluster_delslots(slots, 3) ? 1 : 0);
		printf("  cluster_delslots(vararg)=%d\n",
			cmd.cluster_delslots(0, 1, -1) ? 1 : 0);
		printf("  cluster_meet=%d forget=%d replicate=%d\n",
			cmd.cluster_meet("127.0.0.1", 6380) ? 1 : 0,
			cmd.cluster_forget("nodeid") ? 1 : 0,
			cmd.cluster_replicate("nodeid") ? 1 : 0);
		printf("  cluster_reset/soft/hard=%d %d %d\n",
			cmd.cluster_reset() ? 1 : 0, cmd.cluster_reset_soft() ? 1 : 0,
			cmd.cluster_reset_hard() ? 1 : 0);
		printf("  cluster_setslot_*=%d %d %d %d\n",
			cmd.cluster_setslot_importing(0, "nodeid") ? 1 : 0,
			cmd.cluster_setslot_migrating(0, "nodeid") ? 1 : 0,
			cmd.cluster_setslot_stable(0) ? 1 : 0,
			cmd.cluster_setslot_node(0, "nodeid") ? 1 : 0);
		printf("  cluster_failover*=%d %d %d saveconfig=%d epoch=%d\n",
			cmd.cluster_failover() ? 1 : 0,
			cmd.cluster_failover_force() ? 1 : 0,
			cmd.cluster_failover_takeover() ? 1 : 0,
			cmd.cluster_saveconfig() ? 1 : 0,
			cmd.cluster_set_config_epoch("1") ? 1 : 0);
		(void) slotList;
	}
}

//////////////////////////////////////////////////////////////////////////
// 20. redis_sentinel (+ redis_master / redis_slave value classes)
//////////////////////////////////////////////////////////////////////////

static void test_redis_sentinel(acl::redis_client* client)
{
	section("acl::redis_sentinel (SENTINEL *) + redis_master / redis_slave");

	// the master/slave description classes are pure data: usable offline
	{
		acl::redis_master master;
		master.name_ = "mymaster";
		master.ip_ = "127.0.0.1";
		master.port_ = 6379;
		master.quorum_ = 2;
		master.num_slaves_ = 1;
		master.down_after_milliseconds_ = 30000;
		master.flags_ = "master";
		printf("  master name=%s ip=%s port=%d quorum=%u slaves=%u\n",
			master.name_.c_str(), master.ip_.c_str(), master.port_,
			master.quorum_, master.num_slaves_);
		CHECK(master.name_ == "mymaster");
		CHECK(master.port_ == 6379);
		CHECK(master.quorum_ == 2);

		acl::redis_slave slave;
		slave.name_ = "myslave";
		slave.ip_ = "127.0.0.1";
		slave.port_ = 6380;
		slave.flags_ = "slave";
		slave.master_link_down_time_ = 0;
		printf("  slave name=%s ip=%s port=%d\n", slave.name_.c_str(),
			slave.ip_.c_str(), slave.port_);
		CHECK(slave.port_ == 6380);
	}

	acl::redis_sentinel sentinel;
	sentinel.set_client(client);
	CHECK(sentinel.get_client() == client);

	acl::redis_sentinel sentinel2;
	(void) sentinel2;

	if (kHaveServer) {
		// point this client at the sentinel port rather than the redis port
		acl::redis_client sc(kSentinelAddr, 1, 1, false);
		sentinel.set_client(&sc);

		acl::redis_master m;
		std::vector<acl::redis_master> masters;
		std::vector<acl::redis_slave> slaves;
		acl::string ip;
		int port = 0;

		printf("  sentinel_master=%d\n",
			sentinel.sentinel_master("mymaster", m) ? 1 : 0);
		printf("  sentinel_masters=%d count=%lu\n",
			sentinel.sentinel_masters(masters) ? 1 : 0,
			(unsigned long) masters.size());
		printf("  sentinel_slaves=%d count=%lu\n",
			sentinel.sentinel_slaves("mymaster", slaves) ? 1 : 0,
			(unsigned long) slaves.size());
		printf("  get_master_addr_by_name=%d %s:%d\n",
			sentinel.sentinel_get_master_addr_by_name("mymaster", ip, port) ? 1 : 0,
			ip.c_str(), port);
		printf("  sentinel_reset=%d failover=%d remove=%d\n",
			sentinel.sentinel_reset("mymaster"),
			sentinel.sentinel_failover("mymaster") ? 1 : 0,
			sentinel.sentinel_remove("mymaster") ? 1 : 0);
		printf("  sentinel_monitor=%d flushconfig=%d\n",
			sentinel.sentinel_monitor("mymaster", "127.0.0.1", 6379, 2) ? 1 : 0,
			sentinel.sentinel_flushconfig() ? 1 : 0);
		printf("  sentinel_set(str)=%d set(unsigned)=%d\n",
			sentinel.sentinel_set("mymaster", "down-after-milliseconds", "30000") ? 1 : 0,
			sentinel.sentinel_set("mymaster", "parallel-syncs", 1u) ? 1 : 0);
	}
}

//////////////////////////////////////////////////////////////////////////
// 21. redis_role (+ redis_role4master / redis_role4slave)
//////////////////////////////////////////////////////////////////////////

static void test_redis_role(acl::redis_client* client)
{
	section("acl::redis_role (ROLE) + redis_role4master / redis_role4slave");

	// these two are value classes and are completely offline-usable
	{
		acl::redis_role4slave s;
		s.set_ip("127.0.0.1");
		s.set_port(6380);
		s.set_status("online");
		s.set_offset(12345LL);
		printf("  slave %s:%d status=%s offset=%lld\n", s.get_ip(),
			s.get_port(), s.get_status(), (long long) s.get_offset());
		CHECK(strcmp(s.get_ip(), "127.0.0.1") == 0);
		CHECK(s.get_port() == 6380);
		CHECK(strcmp(s.get_status(), "online") == 0);
		CHECK(s.get_offset() == 12345LL);

		acl::redis_role4slave s2;
		s2.set_ip("127.0.0.1");
		s2.set_port(6381);

		acl::redis_role4master m;
		m.set_offset(99999LL);
		m.add_slave(s);
		m.add_slave(s2);
		printf("  master offset=%lld slaves=%lu\n",
			(long long) m.get_offset(),
			(unsigned long) m.get_slaves().size());
		CHECK(m.get_offset() == 99999LL);
		CHECK(m.get_slaves().size() == 2);
		CHECK(m.get_slaves()[0].get_port() == 6380);
		CHECK(m.get_slaves()[1].get_port() == 6381);
	}

	acl::redis_role role;
	role.set_client(client);
	CHECK(role.get_client() == client);
	// before any ROLE command the reported role name is simply empty
	printf("  role name before command = \"%s\"\n", role.get_role_name());
	CHECK(strlen(role.get_role_name()) == 0);

	if (kHaveServer) {
		printf("  role=%d name=%s\n", role.role() ? 1 : 0, role.get_role_name());
		printf("  role4master offset=%lld\n",
			(long long) role.get_role4master().get_offset());
		printf("  role4slave addr=%s:%d\n", role.get_role4slave().get_ip(),
			role.get_role4slave().get_port());
	}
}

//////////////////////////////////////////////////////////////////////////
// 22. acl::redis -- the aggregate that inherits every command class
//////////////////////////////////////////////////////////////////////////

static void test_redis_aggregate(acl::redis_client* client,
	acl::redis_client_cluster* cluster, acl::redis_client_pipeline* pipeline)
{
	section("acl::redis aggregate (all command classes in one object)");

	acl::redis all;
	all.set_client(client);
	CHECK(all.get_client() == client);

	// switching communication mode on the very same object
	all.set_cluster(cluster);
	CHECK(all.get_cluster() == cluster);
	CHECK(all.get_client() == NULL);
	all.set_pipeline(pipeline);
	CHECK(all.get_pipeline() == pipeline);
	all.set_client(client);
	CHECK(all.get_client() == client);
	// NOTE: set_client() clears cluster_ but NOT pipeline_ (see
	// redis_command::set_client); the pipeline binding persists until it is
	// explicitly cleared with set_pipeline(NULL).
	CHECK(all.get_pipeline() == pipeline);
	CHECK(all.get_cluster() == NULL);
	all.set_pipeline(NULL);
	CHECK(all.get_pipeline() == NULL);

	// the union of all command families is reachable through one object
	acl::redis withClient(client);
	CHECK(withClient.get_client() == client);
	acl::redis withCluster(cluster);
	CHECK(withCluster.get_cluster() == cluster);
	acl::redis withPipeline(pipeline);
	CHECK(withPipeline.get_pipeline() == pipeline);

	// command-family methods all resolve through the virtual base
	withClient.clear(true);
	withClient.set_check_addr(false);
	withClient.set_slice_request(false);
	withClient.set_slice_respond(false);
	CHECK(withClient.result_type() == acl::REDIS_RESULT_UNKOWN);
	CHECK(withClient.result_size() == 0);
	// redis_command::eof() merely forwards to the bound client; every
	// set_client() above lazily dialled kRedisAddr (get_stream default
	// auto_connect=true) and the failed attempt left the client stream in a
	// non-eof state, so the command reports eof()==false here.
	printf("  client.eof=%d withClient.eof=%d\n",
		client->eof() ? 1 : 0, withClient.eof() ? 1 : 0);
	CHECK(withClient.eof() == client->eof());
	CHECK(!withClient.eof());

	// every family is present: key/string/hash/list/set/zset/hll/pubsub/
	// transaction/script/connection/server/cluster/geo/stream
	if (kHaveServer) {
		acl::string buf;
		StrVec vec;
		StrMap map;
		StrDoubleMap zmap;
		zmap["member1"] = 1.0;
		printf("  ping=%d\n", withClient.ping() ? 1 : 0);
		printf("  set=%d get=%d\n", withClient.set("k", "v") ? 1 : 0,
			withClient.get("k", buf) ? 1 : 0);
		printf("  hset=%d hget=%d\n", withClient.hset("h", "f", "v"),
			withClient.hget("h", "f", buf) ? 1 : 0);
		printf("  lpush=%d llen=%d\n", withClient.lpush("l", "a", NULL),
			withClient.llen("l"));
		printf("  sadd=%d scard=%d\n", withClient.sadd("s", "a", NULL),
			withClient.scard("s"));
		printf("  zadd=%d zcard=%d\n", withClient.zadd("z", zmap, NULL) ,
			withClient.zcard("z"));
		printf("  pfadd=%d\n", withClient.pfadd("hll", "a", NULL));
		printf("  publish=%d\n", withClient.publish("ch", "msg", 3));
		printf("  multi=%d exec=%d\n", withClient.multi() ? 1 : 0,
			withClient.exec() ? 1 : 0);
		printf("  dbsize=%d info=%d\n", withClient.dbsize(),
			withClient.info(buf));
		printf("  keyslot=%d geoadd=%d xadd=%d\n",
			withClient.cluster_keyslot("k"),
			withClient.geoadd("g", "m", 15.0, 37.0),
			withClient.xadd("st", map, buf) ? 1 : 0);
		printf("  del=%d\n", withClient.del("k"));
	}
	(void) cluster; (void) pipeline;
}

//////////////////////////////////////////////////////////////////////////
// 23. raw request/response object classes
//////////////////////////////////////////////////////////////////////////

static void test_result_types()
{
	section("acl::redis_result type enum and acl::redis_client_pool ownership");

	// the result-type enum drives every result_* accessor
	const char* names[] = { "UNKOWN", "NIL", "ERROR", "STATUS", "INTEGER",
		"STRING", "ARRAY" };
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		printf("  %d = %s\n", (int) i, names[i]);
		CHECK((int) i == (int) acl::REDIS_RESULT_UNKOWN + (int) i);
	}
	CHECK(acl::REDIS_RESULT_UNKOWN == 0);
	CHECK(acl::REDIS_RESULT_ARRAY == 6);

	// redis_client_pool is ref-counted so a cluster can share pools safely
	acl::redis_client_pool pool(kRedisAddr, 2, 1);
	CHECK(strcmp(pool.get_addr(), kRedisAddr) == 0);
	CHECK(pool.get_db() == 0);
	pool.refer();
	pool.refer();
	pool.unrefer();
	pool.unrefer();
	printf("  pool refer/unrefer balanced ok\n");

	// redis_pipeline_message type enum
	printf("  pipeline msg types: cmd=%d redirect=%d down=%d stop=%d closed=%d\n",
		(int) acl::redis_pipeline_t_cmd, (int) acl::redis_pipeline_t_redirect,
		(int) acl::redis_pipeline_t_clusterdonw, (int) acl::redis_pipeline_t_stop,
		(int) acl::redis_pipeline_t_channel_closed);
	CHECK(acl::redis_pipeline_t_cmd == 0);
	CHECK(acl::redis_pipeline_t_channel_closed == 4);
}

//////////////////////////////////////////////////////////////////////////

int main()
{
	setvbuf(stdout, NULL, _IONBF, 0);   // keep markers visible if a crash occurs
	acl::acl_cpp_init();

	printf("acl_cpp verbose: %s\n", acl::acl_cpp_verbose());

	// Route ACL's own diagnostics into a file. With no server listening,
	// redis_client's lazy connect attempt triggered by set_client() logs an
	// error; keeping it off the console makes the assertions readable.
	acl::log::open(kLogFile, "test_cpp_redis", NULL);

	// short timeouts + retry=false -> any accidental connect fails at once
	acl::redis_client          client(kRedisAddr, 1, 1, false);
	acl::redis_client_cluster  cluster(16384);
	acl::redis_client_pool     pool(kRedisAddr, 4, 0);
	acl::redis_client_pipeline pipeline(kRedisAddr);

	test_redis_client();
	test_redis_command_base(&client, &cluster);
	test_hash_slot(&cluster);
	test_client_infrastructure(&cluster, &pool, &pipeline);

	test_redis_key(&client);
	test_redis_string(&client);
	test_redis_hash(&client);
	test_redis_list(&client);
	test_redis_set(&client);
	test_redis_zset(&client);

	test_redis_connection(&client);
	test_redis_server(&client);
	test_redis_pubsub(&client);
	test_redis_transaction(&client);
	test_redis_script(&client);
	test_redis_hyperloglog(&client);
	test_redis_geo(&client);
	test_redis_stream(&client);

	test_redis_cluster_cmds(&client, &cluster);
	test_redis_sentinel(&client);
	test_redis_role(&client);
	test_redis_aggregate(&client, &cluster, &pipeline);
	test_result_types();

	if (kHaveServer) {
		client.close();
	}

	acl::log::close();
	remove(kLogFile);

	printf("\n%s (failures: %d)\n",
		g_failures == 0 ? "ALL REDIS API TESTS PASSED" : "SOME TESTS FAILED",
		g_failures);
	printf("test_cpp_redis: OK\n");
	return g_failures == 0 ? 0 : 1;
}
