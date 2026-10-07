/**
 * test_cpp_modules.cpp — compile-check + safe runtime exercise of the
 * remaining acl_cpp modules: mqtt, beanstalk, smtp, mime, aliyun OSS,
 * hsocket, SSL/TLS config (openssl_conf), serialize/deserialize, IPC/RPC,
 * charset_conv.
 *
 * Verified against the real headers:
 *  - mqtt_client(addr, conn_timeout=10, rw_timeout=10) extends connect_client;
 *    mqtt_connect::set_cid/set_keep_alive/set_username/set_passwd,
 *    mqtt_publish::set_topic/set_payload(len,data),
 *    mqtt_subscribe::add_topic(topic, mqtt_qos_t{MQTT_QOS0..2}).
 *  - beanstalk(addr, conn_timeout, bool retry=true) — NOT (addr,timo,retrytime);
 *    use(tube)/watch(tube)/put(data,len,pri,delay,ttr)/reserve(string&,timo).
 *    beanstalk_pool has a NO-ARG ctor; clients come from
 *    pool.peek(addr, clean_watch=true, conn_timeout) / pool.put(cli,...).
 *  - smtp_client(addr, conn_timeout=60, rw_timeout=60);
 *    mail_message::set_from(from,name)/add_to(rcpt)/set_subject/
 *    set_body(const mail_body&)/add_attachment(path,ctype)/get_to();
 *    mail_body(charset="utf-8", encoding="base64")::set_plain(data,len)
 *    (no format()/set_to()/get_subject() on this version);
 *    smtp_client::set_ssl(sslbase_conf*).
 *  - mime streaming parse: update_begin(path_or_NULL) -> update(data,len)
 *    -> update_end(); get_plain_body()/get_html_body() return mime_body*,
 *    get_attachments()/get_images() return const std::list<...>;
 *    mime_node::get_name(), get_ctype_s() (NOT get_ctype() — that's the int).
 *    LIB BUG worked around below: get_html_body()/get_plain_body() delete the
 *    cached m_pBody but return NULL WITHOUT nulling it when the part is
 *    absent -> reset()/dtor double-free; call get_html_body() BEFORE
 *    get_plain_body() to stay safe. (mqtt_header also has no default ctor.)
 *  - aliyun OSSClient(keyId,keySecret) / OSSClient(endPoint,keyId,keySecret)
 *    exists in acl_cpp/aliyun/oss/OSSClient.hpp BUT the CMake build of
 *    lib_acl_cpp does NOT compile src/aliyun (see lib_acl_cpp/CMakeLists.txt
 *    source list) -> constructing would fail at link; header-level API
 *    presence is checked with sizeof only.
 *  - hsclient(addr, cache_enable=true, retry_enable=true) — timeouts are NOT
 *    ctor parameters.
 *  - sslbase_conf is abstract; openssl_conf(server_side=false, timeout=30)
 *    is concrete. The TLS version constants (tls_ver_1_0 .. tls_ver_1_3) live
 *    at namespace scope: acl::tls_ver_1_2, NOT sslbase_conf::tls_ver_1_2.
 *    Human-readable name via static acl::sslbase_conf::version_s(int) (there
 *    is no ver2str). NOTE: there is no acl_cpp_end() API; acl::acl_cpp_init()
 *    needs no counterpart.
 *  - serialize/deserialize (acl_cpp/serialize/serialize.hpp, NOT pulled in by
 *    lib_acl.hpp) are plain templates over user-provided overloads
 *    `acl::json_node& gson(acl::json&, T&)` (packing) and
 *    `std::pair<bool,std::string> gson(acl::json_node&, T*)` (unpacking).
 *    There is NO DEFINE_GOSONG macro in this version — gsoner.hpp is a
 *    code-generator class; the test writes the two gson() overloads by hand.
 *  - ipc: rpc_request is abstract (rpc_run()/rpc_onover() pure virtual,
 *    public ctor); rpc_service(nthread, ipc_keep=true) left as compile-only.
 *  - charset_conv extends pipe_stream: update_begin(from,to) ->
 *    update(in,len,string* out)* -> update_finish(string* out).
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <list>
#include <utility>

#include <lib_acl.hpp>
#include <acl_cpp/serialize/serialize.hpp>        // not in lib_acl.hpp
#include <acl_cpp/aliyun/oss/OSSClient.hpp>       // not in lib_acl.hpp

static int g_failures = 0;

#define CHECK(expr) \
	do { \
		if (!(expr)) { \
			g_failures++; \
			printf("  [FAIL] %s (line %d)\n", #expr, __LINE__); \
		} \
	} while (0)

//////////////////////////////////////////////////////////////////////
// MQTT
//////////////////////////////////////////////////////////////////////

static void test_mqtt(void) {
	printf("== mqtt ==\n");

	acl::mqtt_client mqtt("127.0.0.1:1883", 1, 1);
	acl::connect_client* base = &mqtt;
	base->set_timeout(1, 1);
	(void)base->alive();

	// CONNECT packet object
	acl::mqtt_connect conn;
	conn.set_cid("qoder-test-client");
	conn.set_keep_alive(30);
	conn.set_username("user");
	conn.set_passwd("secret");
	conn.set_will_qos(acl::MQTT_QOS1);
	conn.set_will_topic("will/topic");
	conn.set_will_msg("bye");

	// PUBLISH packet object (chained setters)
	acl::mqtt_publish pub;
	pub.set_topic("test/hello").set_payload(5, "hello");
	CHECK(strcmp(pub.get_topic(), "test/hello") == 0);
	CHECK(pub.get_payload_len() == 5);
	printf("  publish topic=%s len=%u\n",
		pub.get_topic(), pub.get_payload_len());

	// SUBSCRIBE packet object
	acl::mqtt_subscribe sub;
	sub.add_topic("test/hello", acl::MQTT_QOS1);
	CHECK(!sub.get_topics().empty());
	CHECK(sub.get_qoses().front() == acl::MQTT_QOS1);

	// other packet types exist and are constructible
	acl::mqtt_pingreq ping;
	acl::mqtt_disconnect disc;
	printf("  qos desc: %s\n", acl::mqtt_qos_desc(acl::MQTT_QOS1));

	// opening against a closed port must fail fast (no broker running)
	bool opened = base->open();
	printf("  mqtt_client open -> %s\n", opened ? "ok" : "refused");
	CHECK(!opened);
}

//////////////////////////////////////////////////////////////////////
// Beanstalk
//////////////////////////////////////////////////////////////////////

static void test_beanstalk(void) {
	printf("== beanstalk ==\n");

	// ctor is (addr, conn_timeout, bool retry) — retry=false: fail fast
	acl::beanstalk bs("127.0.0.1:11300", 1, false);

	// producer-side commands (return 0/false: no beanstalkd listening)
	bs.use("tube-a");
	unsigned watched = bs.watch("tube-b");
	printf("  watch -> %u (0 = offline, as expected)\n", watched);
	CHECK(watched == 0);

	unsigned long long id = bs.put("job-data", 8);
	printf("  put -> id %llu (0 = offline, as expected)\n", id);
	CHECK(id == 0);

	acl::string buf;
	unsigned long long rid = bs.reserve(buf, 1);
	printf("  reserve -> %llu (0 = offline)\n", rid);
	CHECK(rid == 0);

	// beanstalk_pool pools beanstalk* clients; its ctor takes no addr,
	// peek(addr, clean_watch, conn_timeout) does. Without a running server
	// peek() may still hand back a (lazy/unverified) client object, so we
	// simply put it back with keep=false instead of asserting NULL.
	acl::beanstalk_pool pool;
	acl::beanstalk* bc = pool.peek("127.0.0.1:11300", true, 1);
	printf("  beanstalk_pool peek -> %p\n", bc);
	if (bc) {
		pool.put(bc, true, false);	// keep=false: destroy unusable conn
		printf("  beanstalk_pool put(keep=false) ok\n");
	}
}

//////////////////////////////////////////////////////////////////////
// SMTP client + mail message
//////////////////////////////////////////////////////////////////////

static void test_smtp(void) {
	printf("== smtp / mail_message ==\n");

	acl::smtp_client client("127.0.0.1:25", 1, 1);

	acl::mail_message msg;
	msg.set_from("sender@example.com", "Sender");
	msg.add_to("rcpt@example.com");		// no set_to in this version
	msg.set_subject("test subject");

	acl::mail_body body("utf-8", "base64");
	body.set_plain("hello mime body\n", strlen("hello mime body\n"));
	msg.set_body(body);

	CHECK(msg.get_to().size() == 1);	// no get_subject(); check recipient list

	// attach a file path that likely doesn't exist — API presence only
	msg.add_attachment("nonexistent.pdf", "application/pdf");

	// demonstrate set_ssl() accepts an sslbase_conf* (see SSL section)
	acl::openssl_conf conf;
	client.set_ssl(&conf);
	client.set_ssl(NULL);
	printf("  smtp mail_message wired; no network send attempted\n");
}

//////////////////////////////////////////////////////////////////////
// MIME parsing
//////////////////////////////////////////////////////////////////////

static void test_mime(void) {
	printf("== mime ==\n");

	static const char raw_mime[] =
		"From: sender@example.com\r\n"
		"To: rcpt@example.com\r\n"
		"Subject: hi\r\n"
		"MIME-Version: 1.0\r\n"
		"Content-Type: text/plain; charset=utf-8\r\n"
		"\r\n"
		"Hello, MIME!\r\n";

	acl::mime m;
	m.update_begin(NULL);            // parse from memory, not from a file
	bool done = m.update(raw_mime, sizeof(raw_mime) - 1);
	m.update_end();
	printf("  update -> %s (true only for complete multipart)\n",
		done ? "true" : "false");

	// NOTE: call get_html_body() BEFORE get_plain_body(). There is a bug in
	// this acl version: get_html_body/get_plain_body `delete m_pBody;` but
	// return NULL WITHOUT nulling m_pBody when no html/plain node exists,
	// so a later reset()/dtor double-frees the dangling m_pBody. Invoking
	// the html getter first (m_pBody==NULL then) avoids the dangling state.
	acl::mime_body* html = m.get_html_body();
	printf("  html body: %s (expected none for text/plain)\n",
		html ? "found" : "none");

	acl::mime_body* plain = m.get_plain_body();
	printf("  plain body: %s\n", plain ? "found" : "none");
	if (plain) {
		const char* ct = plain->get_ctype_s();   // string form of content type
		printf("  body ctype=%s, node name=%s\n",
			ct ? ct : "(null)", plain->get_name());
		CHECK(ct != NULL);
	}

	const std::list<acl::mime_attach*>& atts = m.get_attachments();
	const std::list<acl::mime_image*>& imgs = m.get_images();
	printf("  attachments=%lu, images=%lu\n",
		(unsigned long)atts.size(), (unsigned long)imgs.size());
	CHECK(atts.empty());
	CHECK(imgs.empty());

	m.reset();	// object reusable for the next mail
}

//////////////////////////////////////////////////////////////////////
// Aliyun OSS — header presence only: lib_acl_cpp CMake does NOT compile
// src/aliyun/oss into acl_cpp_static, so constructing OSSClient would be a
// link error. sizeof() proves the class is declared and complete.
//////////////////////////////////////////////////////////////////////

static void test_oss(void) {
	printf("== aliyun OSS (compile-only, sources not in CMake build) ==\n");

	CHECK(sizeof(acl::OSSClient) > 0);
	acl::OSSClient* p = NULL;
	(void)p;
	// (would be: acl::OSSClient oss("endPoint", "keyId", "keySecret");)
	printf("  acl::OSSClient declared, sizeof=%zu\n", sizeof(acl::OSSClient));
}

//////////////////////////////////////////////////////////////////////
// HandlerSocket
//////////////////////////////////////////////////////////////////////

static void test_hsocket(void) {
	printf("== hsocket ==\n");

	// ctor: (addr, cache_enable, retry_enable) — no timeouts here
	acl::hsclient client("127.0.0.1:9900", true, false);
	client.debug_enable(true);
	printf("  hsclient constructed for %s\n", "127.0.0.1:9900");

	// hspool composes one rw client plus optional rd client
	acl::hspool pool("127.0.0.1:9900");
	printf("  hspool constructed\n");
}

//////////////////////////////////////////////////////////////////////
// SSL/TLS configuration
//////////////////////////////////////////////////////////////////////

static void test_ssl_conf(void) {
	printf("== sslbase_conf / openssl_conf ==\n");

	// sslbase_conf is abstract; openssl_conf is the concrete subclass
	acl::openssl_conf conf;

	// version constants are namespace-scope enumerators
	bool v = conf.set_version(acl::tls_ver_1_0, acl::tls_ver_1_2);
	printf("  set_version(tls1.0..1.2) -> %s\n", v ? "ok" : "unsupported");

	// bogus paths: methods must return false, not crash
	bool ca = conf.load_ca("no-such-ca.pem", NULL);
	bool cert = conf.add_cert("no-such.crt", "no-such.key", NULL);
	bool key = conf.set_key("no-such.key", NULL);
	printf("  load_ca=%d add_cert=%d set_key=%d (all expected false)\n",
		(int)ca, (int)cert, (int)key);
	CHECK(!ca);
	CHECK(!cert);
	CHECK(!key);

	// sslbase_conf::version_s for readable diagnostics (static member)
	printf("  version str: %s\n", acl::sslbase_conf::version_s(acl::tls_ver_1_2));
}

//////////////////////////////////////////////////////////////////////
// serialize / deserialize — no DEFINE_GOSONG macro exists in this version;
// provide the two gson() overloads in namespace acl by hand.
//////////////////////////////////////////////////////////////////////

namespace acl {

struct demo_obj {
	std::string name;
	long long   age = 0;
};

// packing: used by acl::serialize(o, buf) -> gson(json&, o)
static inline acl::json_node& gson(acl::json& json, acl::demo_obj& o) {
	acl::json_node& node = json.get_root();
	node.add_text("name", o.name.c_str());
	node.add_number("age", o.age);
	return node;
}

// unpacking: used by acl::deserialize(json, &o, &err) -> gson(node, ptr)
static inline std::pair<bool, std::string>
gson(acl::json_node& node, acl::demo_obj* o) {
	acl::json_node* p = node["name"];
	if (p == NULL || p->get_string() == NULL)
		return std::make_pair(false, "field name missing");
	o->name = p->get_string();

	p = node["age"];
	if (p == NULL || p->get_int64() == NULL)
		return std::make_pair(false, "field age missing");
	o->age = *p->get_int64();

	return std::make_pair(true, std::string());
}

} // namespace acl

static void test_serialize(void) {
	printf("== serialize / deserialize ==\n");

	acl::demo_obj src;
	src.name = "connector";
	src.age  = 42;

	acl::string buf;
	acl::serialize(src, buf);           // object -> json text
	printf("  serialized: %s\n", buf.c_str());
	CHECK(buf.length() > 0);

	acl::json j;
	j.update(buf.c_str());
	CHECK(j.finish());

	acl::demo_obj dst;
	acl::demo_obj* dstp = &dst;		// deserialize takes T& -> pass ptr lvalue
	acl::string err;
	bool ok = acl::deserialize(j, dstp, &err);	// json text -> object
	printf("  deserialize ok=%d name=%s age=%lld\n",
		(int)ok, dst.name.c_str(), dst.age);
	CHECK(ok);
	CHECK(dst.name == "connector");
	CHECK(dst.age == 42);

	// bad payload path: missing field must report an error, not crash
	acl::json j2;
	j2.update("{\"name\":\"x\"}");
	acl::demo_obj bad;
	acl::demo_obj* badp = &bad;
	acl::string err2;
	bool ok2 = acl::deserialize(j2, badp, &err2);
	printf("  deserialize(missing age) ok=%d err=%s\n",
		(int)ok2, err2.c_str());
	CHECK(!ok2);
}

//////////////////////////////////////////////////////////////////////
// IPC / RPC — abstract classes, compile-only + trivial subclass
//////////////////////////////////////////////////////////////////////

class demo_rpc_request : public acl::rpc_request {
public:
	// @override — runs in a worker process/thread when dispatched
	void rpc_run(void) override {
		ran_ = true;
	}
	// @override — completion callback in the main process
	void rpc_onover(void) override {
		printf("  rpc request finished (ran=%d)\n", (int)ran_);
	}

private:
	bool ran_ = false;
};

static void test_ipc_rpc(void) {
	printf("== ipc / rpc (compile-only) ==\n");

	demo_rpc_request req;		// concrete subclass of abstract rpc_request
	req.rpc_run();			// invoke directly (no IPC transport started)
	req.rpc_onover();

	// rpc_service (nthread, ipc_keep) and ipc_client exist but are not
	// instantiated — running one would spawn worker threads/processes.
	CHECK(sizeof(acl::rpc_service) > 0);
	CHECK(sizeof(acl::ipc_client) > 0);
	CHECK(sizeof(acl::ipc_request) > 0);
	printf("  rpc_service/ipc_client/ipc_request present, sizeof ok\n");
}

//////////////////////////////////////////////////////////////////////
// charset conversion
//////////////////////////////////////////////////////////////////////

static void test_charset_conv(void) {
	printf("== charset_conv ==\n");

	acl::charset_conv conv;		// concrete pipe_stream subclass

	bool begun = conv.update_begin("utf-8", "gbk");
	printf("  update_begin(utf-8 -> gbk) -> %s\n", begun ? "ok" : "fail");

	if (begun) {
		acl::string out;
		const char* in = "hello";
		bool r = conv.update(in, strlen(in), &out);
		conv.update_finish(&out);
		printf("  update/finish ok=%d out=[%s] len=%zu\n",
			(int)r, out.c_str(), out.length());
		CHECK(r);
		// pure ASCII survives utf-8 -> gbk unchanged
		CHECK(out.length() == strlen(in));
		CHECK(strcmp(out.c_str(), "hello") == 0);
	}
}

int main() {
	setvbuf(stdout, NULL, _IONBF, 0);	// unbuffered: locate crashes precisely
	acl::acl_cpp_init();

	printf("acl_cpp version: %s\n", acl::acl_cpp_verbose());

	test_mqtt();
	test_beanstalk();
	test_smtp();
	test_mime();
	test_oss();
	test_hsocket();
	test_ssl_conf();
	test_serialize();
	test_ipc_rpc();
	test_charset_conv();

	if (g_failures == 0) {
		printf("\nAll modules tests passed!\n");
		return 0;
	}
	printf("\n%d module test(s) FAILED\n", g_failures);
	return 1;
}
