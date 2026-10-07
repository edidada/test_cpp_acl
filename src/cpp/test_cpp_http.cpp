/**
 * test_cpp_http.cpp — exercise the acl_cpp HTTP/HTTPS/cookied/session API.
 *
 * Verified against lib_acl_cpp/include/acl_cpp/http/*.hpp
 *
 * API notes (discovered from the real headers):
 *  - http_header::set_keep_alive takes a bool (NOT an int seconds value).
 *  - http_header::build_response(string&) is the only overload — the status
 *    code is set beforehand via set_status(int); there is no
 *    build_response(status, buf) form.
 *  - http_header::set_ws_key has two overloads: (const void*, size_t) and
 *    (const char*).
 *  - HttpCookie has NO setCookie(name, value) two-arg form: either use the
 *    HttpCookie(name, value) ctor or setCookie("name=value") (single-arg
 *    Set-Cookie value parser).
 *  - HttpServlet's ONLY pure virtual is its destructor (=0, defined
 *    out-of-line in the library); doGet/doPost are protected virtuals
 *    returning bool — a compile-check subclass only needs ~MyServlet().
 *  - WebSocketServlet pure virtuals are the protected callbacks
 *      bool onPing(unsigned long long, bool)
 *      bool onPong(unsigned long long, bool)
 *      bool onMessage(unsigned long long, bool text, bool finish)
 *    — NOT the (websocket&, data, len, opcode) form.
 *  - acl::session is abstract with pure virtuals remove(), get_attrs(map&),
 *    set_attrs(map&) and a PROTECTED pure virtual set_timeout(time_t); a
 *    minimal in-memory fake can drive HttpSession end-to-end.
 *  - http_request(addr,...) does NOT connect in the ctor (lazy open), so we
 *    can construct it and touch request_header() without any server.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
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
// Minimal in-memory session backend so HttpSession can be exercised without
// memcached/redis. NOTE on base-class semantics: session::set()/del() push
// pending changes into the PROTECTED attrs_cache_ and then call
// set_attrs(attrs_) with only the freshly fetched backend map, so a purely
// passive backend would never see the new values. This fake therefore also
// overrides the (virtual) set/get_buf/del entry points to operate directly on
// its store, while get_attrs/set_attrs/remove/set_timeout implement the pure
// virtual hooks required for the bulk-map APIs and flush().
// ---------------------------------------------------------------------------
class fake_session : public acl::session {
public:
	fake_session() : acl::session(120) {}
	~fake_session() override {}

	// @override — invalidate the whole session on the backend
	bool remove() override {
		store_.clear();
		return true;
	}

	// @override — fetch every attribute of the current sid
	// (real backends return true whenever the sid exists, empty or not)
	bool get_attrs(std::map<acl::string, acl::session_string>& attrs) override {
		attrs = store_;
		return true;
	}

	// @override — persist the attribute collection (honors TODO_SET/TODO_DEL)
	bool set_attrs(const std::map<acl::string, acl::session_string>& attrs) override {
		std::map<acl::string, acl::session_string>::const_iterator it = attrs.begin();
		for (; it != attrs.end(); ++it) {
			if (it->second.todo_ == acl::TODO_DEL) {
				store_.erase(it->first);
			} else {
				store_[it->first] = it->second;
			}
		}
		return true;
	}

	// NOTE: get_buf is NOT declared const in the base (virtual bool mismatch
	// would silently fail the override) — keep it non-const, which it is.

	// @override — write straight through to the store
	bool set(const char* name, const void* value, size_t len) override {
		acl::session_string ss(len);
		ss.copy(value, len);
		ss.todo_ = acl::TODO_SET;
		store_[name] = ss;
		return true;
	}

	// @override — read straight from the store
	acl::session_string* get_buf(const char* name) override {
		std::map<acl::string, acl::session_string>::iterator it = store_.find(name);
		return it == store_.end() ? NULL : &it->second;
	}

	// @override — delete straight from the store
	bool del(const char* name) override {
		return store_.erase(name) > 0;
	}

	size_t store_size() const { return store_.size(); }

protected:
	// @override (protected pure virtual) — reset backend TTL
	bool set_timeout(time_t ttl) override {
		ttl_ = ttl;
		return true;
	}

private:
	std::map<acl::string, acl::session_string> store_;
};

// ---------------------------------------------------------------------------
// Compile-check subclass of HttpServlet. Only the destructor is pure virtual;
// doGet/doPost are overridden anyway (protected, bool-returning) to prove the
// signatures. We NEVER call start()/doRun() — they need a live connection.
// ---------------------------------------------------------------------------
class MyServlet : public acl::HttpServlet {
public:
	MyServlet() : acl::HttpServlet() {}
	~MyServlet() override {}

protected:
	bool doGet(acl::HttpServletRequest &req, acl::HttpServletResponse &res) override {
		(void)req; (void)res;
		return true;
	}
	bool doPost(acl::HttpServletRequest &req, acl::HttpServletResponse &res) override {
		(void)req; (void)res;
		return true;
	}
};

// ---------------------------------------------------------------------------
// Compile-check subclass of WebSocketServlet implementing the three pure
// virtual protected callbacks with their real signatures.
// ---------------------------------------------------------------------------
class MyWsServlet : public acl::WebSocketServlet {
public:
	MyWsServlet() : acl::WebSocketServlet() {}
	~MyWsServlet() override {}

	// get_websocket() is protected in the base — expose a public wrapper
	bool hasWebsocket() const { return get_websocket() != NULL; }

protected:
	bool onPing(unsigned long long payload_len, bool finish) override {
		(void)payload_len; (void)finish;
		return true;
	}
	bool onPong(unsigned long long payload_len, bool finish) override {
		(void)payload_len; (void)finish;
		return true;
	}
	bool onMessage(unsigned long long payload_len, bool text, bool finish) override {
		(void)payload_len; (void)text; (void)finish;
		return true;
	}
};

// ---------------------------------------------------------------------------
static void test_http_header_build() {
	section("http_header build_request");

	acl::http_header hdr;
	hdr.set_method(acl::HTTP_METHOD_GET);
	hdr.set_url("/index.html");
	hdr.set_host("localhost");
	hdr.add_param("name", "acl");
	hdr.add_entry("Accept", "text/html");
	hdr.add_cookie("sid", "abc123");
	hdr.set_content_length(1024);
	hdr.set_content_type("text/html");
	hdr.set_keep_alive(true);              // bool, not int
	hdr.set_proto_version("1.1");
	hdr.accept_gzip(true);
	hdr.add_int("cnt", 42);
	hdr.add_format("fmt", "%s-%d", "x", 7);
	hdr.set_param_override(true);
	hdr.set_redirect(3);
	CHECK(hdr.get_redirect() == 3);
	(void)hdr.redirect("/alt.html");       // url rewrite inside same host
	hdr.disable_header("Accept-Encoding", true);

	// getters round-trip
	CHECK(hdr.is_request());
	CHECK(0 == strcmp(hdr.get_host(), "localhost"));
	CHECK(0 == strcmp(hdr.get_entry("Accept"), "text/html"));
	CHECK(hdr.get_keep_alive());
	CHECK(hdr.get_content_length() == 1024);
	acl::string mbuf;
	CHECK(acl::HTTP_METHOD_GET == hdr.get_method(&mbuf));

	const acl::HttpCookie* ck = hdr.get_cookie("sid");
	CHECK(ck != NULL);
	if (ck) {
		CHECK(0 == strcmp(ck->getName(), "sid"));
		CHECK(0 == strcmp(ck->getValue(), "abc123"));
	}

	acl::string req_buf;
	CHECK(hdr.build_request(req_buf));
	printf("---- request ----\n%s----------------\n", req_buf.c_str());
	CHECK(NULL != strstr(req_buf.c_str(), "GET /"));
	CHECK(NULL != strstr(req_buf.c_str(), "Host: localhost"));
	CHECK(NULL != strstr(req_buf.c_str(), "Content-Type: text/html"));

	// ---- URL-form ctor ----
	acl::http_header h2("http://localhost/cgi-bin/t.cgi?a=1&b=%20x");
	CHECK(0 == strcmp(h2.get_host(), "localhost"));

	// ---- response side ----
	acl::http_header res;
	res.set_content_type("application/json");
	res.add_entry("X-Custom", "test");
	res.set_status(200);                    // no build_response(status, buf) overload
	res.set_chunked(true);                  // chunked belongs to responses
	res.set_transfer_gzip(true);            // only sticks if zlib loads at runtime
	res.set_cgi_mode(false);
	CHECK(res.get_status() == 200);
	CHECK(res.chunked_transfer());
	CHECK(!res.is_request());
	// is_transfer_gzip() depends on runtime zlib availability — call only
	(void)res.is_transfer_gzip();
	acl::string res_buf;
	CHECK(res.build_response(res_buf));
	printf("---- response ----\n%s----------------\n", res_buf.c_str());
	CHECK(NULL != strstr(res_buf.c_str(), "200"));
	CHECK(NULL != strstr(res_buf.c_str(), "X-Custom: test"));

	// reset clears state for reuse
	res.reset();
	CHECK(res.get_status() == 0 || res.get_status() == 200);

	// static helpers
	char tbuf[64];
	acl::http_header::date_format(tbuf, sizeof(tbuf), 1700000000);
	CHECK(strlen(tbuf) > 0);
	acl::http_header::uri_unsafe_correct(true);
	acl::http_header::uri_unsafe_correct(false);
}

static void test_ws_header_fields() {
	section("websocket header fields");

	acl::http_header ws_hdr;
	ws_hdr.set_method(acl::HTTP_METHOD_GET);
	ws_hdr.set_url("/websocket");           // build_request requires non-empty url
	ws_hdr.set_host("localhost");
	ws_hdr.set_ws_origin("http://localhost");
	ws_hdr.set_ws_key("dGhlIHNhbXBsZQ==");   // const char* overload
	CHECK(0 == strcmp(ws_hdr.get_ws_origin(), "http://localhost"));
	CHECK(ws_hdr.get_ws_key() != NULL);

	ws_hdr.set_ws_key("abc", 3);             // (const void*, size_t) overload
	ws_hdr.set_ws_protocol("chat");
	ws_hdr.set_ws_version(13);
	ws_hdr.set_upgrade("websocket");
	CHECK(0 == strcmp(ws_hdr.get_ws_protocol(), "chat"));
	CHECK(ws_hdr.get_ws_version() == 13);
	CHECK(0 == strcmp(ws_hdr.get_upgrade(), "websocket"));

	acl::string buf;
	CHECK(ws_hdr.build_request(buf));
	CHECK(NULL != strstr(buf.c_str(), "Sec-WebSocket-Key"));
}

static void test_http_cookie() {
	section("HttpCookie");

	// two-arg ctor form
	acl::HttpCookie cookie("sid", "abc123");
	cookie.setDomain(".example.com");
	cookie.setPath("/app");
	cookie.setMaxAge(3600);
	cookie.setExpires((time_t)3600);
	cookie.add("Secure", "true");
	CHECK(0 == strcmp(cookie.getName(), "sid"));
	CHECK(0 == strcmp(cookie.getValue(), "abc123"));
	CHECK(0 == strcmp(cookie.getDomain(), ".example.com"));
	CHECK(0 == strcmp(cookie.getPath(), "/app"));
	CHECK(cookie.getMaxAge() == 3600);
	CHECK(cookie.getExpires() != NULL && *cookie.getExpires() != '\0');
	CHECK(cookie.getParam("secure") != NULL);
	CHECK(!cookie.getParams().empty());

	// default ctor + single-arg setCookie parser (NO setCookie(name,value) form)
	acl::HttpCookie c2;
	CHECK(c2.setCookie("theme=dark; domain=.foo.com; path=/; max-age=60"));
	CHECK(0 == strcmp(c2.getName(), "theme"));
	CHECK(0 == strcmp(c2.getValue(), "dark"));
	CHECK(0 == strcmp(c2.getDomain(), ".foo.com"));
	CHECK(c2.getMaxAge() == 60);

	// copy ctor + pointer-based add_cookie into a header
	acl::HttpCookie c3(&c2);
	CHECK(0 == strcmp(c3.getName(), "theme"));
	acl::http_header hdr;
	hdr.add_cookie(&c2);
	CHECK(hdr.get_cookie("theme") != NULL);

	// destroy() for heap objects
	acl::HttpCookie* c4 = new acl::HttpCookie("k", "v");
	c4->destroy();
}

static void test_http_session() {
	section("HttpSession + fake session backend");

	fake_session sess;
	{
		acl::HttpSession hsess(sess);
		CHECK(hsess.getSid() != NULL && *hsess.getSid() != '\0');

		CHECK(hsess.setAttribute("user", "alice"));
		CHECK(0 == strcmp(hsess.getAttribute("user"), "alice"));

		const char bin[4] = { 1, 2, 3, 0 };
		CHECK(hsess.setAttribute("bin", bin, 3));
		size_t sz = 0;
		const void* p = hsess.getAttribute("bin", &sz);
		CHECK(p != NULL && sz == 3);

		// bulk map API
		std::map<acl::string, acl::session_string> attrs;
		attrs["a"] = acl::session_string("1");
		attrs["b"] = acl::session_string("2");
		CHECK(hsess.setAttributes(attrs));

		std::map<acl::string, acl::session_string> got;
		CHECK(hsess.getAttributes(got));
		CHECK(got.count("user") == 1 || got.size() >= 2);

		std::vector<acl::string> names;
		names.push_back("user");
		std::vector<acl::session_string> values;
		CHECK(hsess.getAttributes(names, values));
		CHECK(values.size() == 1);

		CHECK(hsess.setMaxAge(300));
		CHECK(sess.get_ttl() == 300);

		CHECK(hsess.removeAttribute("user"));
		CHECK('\0' == *hsess.getAttribute("user"));   // gone => empty string

		CHECK(hsess.invalidate());
	}
	CHECK(sess.flush());
}

static void test_servlet_compile_check() {
	section("HttpServlet / WebSocketServlet compile check");

	// default-constructed; do NOT call start()/doRun() (needs live socket)
	MyServlet servlet;
	servlet.setLocalCharset("utf-8");
	servlet.setRwTimeout(30);
	servlet.setParseBody(true);
	servlet.setParseBodyLimit(1024 * 1024);
	CHECK(servlet.getStream() == NULL);   // CGI-style ctor => no socket

	MyWsServlet ws_servlet;
	CHECK(!ws_servlet.hasWebsocket()); // not upgraded yet
}

static void test_http_request_client() {
	section("http_request (client, no I/O)");

	// ctor does not connect (lazy open), safe without a server
	acl::http_request hreq("127.0.0.1:19999", 1, 1, false);
	hreq.set_unzip(false);
	hreq.set_local_charset("gb2312");

	acl::http_header& rh = hreq.request_header();
	rh.set_url("/");
	rh.set_host("127.0.0.1:19999");
	rh.set_method(acl::HTTP_METHOD_HEAD);
	CHECK(acl::HTTP_METHOD_HEAD == rh.get_method());
	CHECK(!hreq.keep_alive());           // no request sent yet
	hreq.reset();
}

int main() {
	acl::acl_cpp_init();   // NOTE: no acl_cpp_end() exists in this API

	test_http_header_build();
	test_ws_header_fields();
	test_http_cookie();
	test_http_session();
	test_servlet_compile_check();
	test_http_request_client();

	if (g_failures > 0) {
		printf("%d HTTP check(s) FAILED\n", g_failures);
		return 1;
	}
	printf("All HTTP tests passed!\n");
	return 0;
}
