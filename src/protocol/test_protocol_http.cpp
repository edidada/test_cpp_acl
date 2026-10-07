// test_protocol_http.cpp
// Protocol-layer HTTP header API tests for the ACL library.
//
// No network is used: request headers are built via http_hdr_req_create() and
// raw-text parsing is exercised by appending a status/request line entry and
// calling the parse functions directly.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

extern "C" {
#include <lib_acl.h>
#include <http/lib_http_struct.h>
#include <http/lib_http.h>
#include <http/lib_http_util.h>
}

static const char *REQ_URL =
	"http://www.test.com/cgi-bin/test.cgi?name=acl&age=10";

// ---------------------------------------------------------------------------
// Request header tests
// ---------------------------------------------------------------------------
static void test_http_req(void)
{
	printf("\n==== HTTP request header tests ====\n");

	// new/free on an empty request header.
	HTTP_HDR_REQ *empty = http_hdr_req_new();
	assert(empty != NULL);
	http_hdr_req_free(empty);

	// Build a fully-formed request from a URL: this parses method, url,
	// url_path, host and query parameters internally.
	HTTP_HDR_REQ *req = http_hdr_req_create(REQ_URL, "GET", "HTTP/1.1");
	assert(req != NULL);

	printf("method=%s\n", http_hdr_req_method(req));
	assert(strcmp(http_hdr_req_method(req), "GET") == 0);

	printf("url=%s\n", http_hdr_req_url(req));
	assert(http_hdr_req_url(req) != NULL);

	printf("url_path=%s\n", http_hdr_req_url_path(req));
	assert(strcmp(http_hdr_req_url_path(req), "/cgi-bin/test.cgi") == 0);

	printf("host=%s\n", http_hdr_req_host(req));
	assert(strcmp(http_hdr_req_host(req), "www.test.com") == 0);

	// url parameters parsed into the params table.
	const char *name = http_hdr_req_param(req, "name");
	const char *age  = http_hdr_req_param(req, "age");
	printf("param name=%s age=%s\n", name ? name : "(null)",
		age ? age : "(null)");
	assert(name && strcmp(name, "acl") == 0);
	assert(age && strcmp(age, "10") == 0);

	http_hdr_req_free(req);

	// ---- parse a raw HTTP request string (manual feed) ----
	// Equivalent to reading these lines off the wire:
	//   GET /cgi-bin/test.cgi?name=acl&age=10 HTTP/1.1
	//   Host: www.test.com
	//   Cookie: sid=abc; uid=42
	//   Range: bytes=0-100
	//   X-Test: hello
	// The first entry (index 0) is treated as the request line by
	// http_hdr_req_line_parse().
	HTTP_HDR_REQ *raw = http_hdr_req_new();
	http_hdr_append_entry(&raw->hdr,
		http_hdr_entry_build("GET", "/cgi-bin/test.cgi?name=acl&age=10 HTTP/1.1"));
	http_hdr_put_str(&raw->hdr, "Host", "www.test.com");
	http_hdr_put_str(&raw->hdr, "Cookie", "sid=abc; uid=42");
	http_hdr_put_str(&raw->hdr, "Range", "bytes=0-100");
	http_hdr_put_str(&raw->hdr, "X-Test", "hello");

	int pr = http_hdr_req_parse(raw); // line_parse + cookies_parse
	printf("http_hdr_req_parse ret=%d\n", pr);

	printf("raw method=%s path=%s host=%s\n",
		http_hdr_req_method(raw), http_hdr_req_url_path(raw),
		http_hdr_req_host(raw));
	assert(strcmp(http_hdr_req_method(raw), "GET") == 0);
	assert(strcmp(http_hdr_req_url_path(raw), "/cgi-bin/test.cgi") == 0);

	// cookie extraction
	const char *sid = http_hdr_req_cookie_get(raw, "sid");
	printf("cookie sid=%s\n", sid ? sid : "(null)");
	assert(sid && strcmp(sid, "abc") == 0);

	// range parsing
	http_off_t from = -1, to = -1;
	int rr = http_hdr_req_range(raw, &from, &to);
	printf("range ret=%d from=%lld to=%lld\n",
		rr, (long long)from, (long long)to);
	assert(rr == 0 && from == 0 && to == 100);

	http_hdr_req_free(raw);
	printf("request header tests passed.\n");
}

// ---------------------------------------------------------------------------
// Generic header entry / build tests
// ---------------------------------------------------------------------------
static void test_http_hdr_common(void)
{
	printf("\n==== HTTP generic header tests ====\n");

	HTTP_HDR_REQ *req = http_hdr_req_create("http://a.com/x", "POST", "HTTP/1.1");
	assert(req != NULL);
	HTTP_HDR *hdr = &req->hdr;

	// put_str / put_int / put_fmt
	http_hdr_put_str(hdr, "Content-Type", "application/json");
	http_hdr_put_int(hdr, "X-Count", 42);
	http_hdr_put_fmt(hdr, "X-Fmt", "%s-%d", "tag", 7);

	// entry / entry_value
	HTTP_HDR_ENTRY *e = http_hdr_entry(hdr, "Content-Type");
	assert(e != NULL);
	printf("Content-Type=%s\n", http_hdr_entry_value(hdr, "Content-Type"));
	assert(strcmp(http_hdr_entry_value(hdr, "Content-Type"),
		"application/json") == 0);
	assert(strcmp(http_hdr_entry_value(hdr, "X-Count"), "42") == 0);
	assert(strcmp(http_hdr_entry_value(hdr, "X-Fmt"), "tag-7") == 0);

	// entry_replace
	int rt = http_hdr_entry_replace(hdr, "X-Count", "99", 1);
	printf("entry_replace ret=%d value=%s\n",
		rt, http_hdr_entry_value(hdr, "X-Count"));
	assert(rt == 0);
	assert(strcmp(http_hdr_entry_value(hdr, "X-Count"), "99") == 0);

	// http_hdr_build -> serialize whole header into a buffer
	ACL_VSTRING *buf = acl_vstring_alloc(512);
	http_hdr_build(hdr, buf);
	printf("built header:\n%s\n", acl_vstring_str(buf));
	assert(strstr(acl_vstring_str(buf), "Content-Type") != NULL);
	acl_vstring_free(buf);

	http_hdr_req_free(req);
	printf("generic header tests passed.\n");
}

// ---------------------------------------------------------------------------
// Response header tests
// ---------------------------------------------------------------------------
static void test_http_res(void)
{
	printf("\n==== HTTP response header tests ====\n");

	HTTP_HDR_RES *res = http_hdr_res_new();
	assert(res != NULL);

	// status-line parsing for 200 / 404 / 500
	assert(http_hdr_res_status_parse(res, "HTTP/1.1 200 OK") == 0);
	printf("status 200 -> %d\n", res->reply_status);
	assert(res->reply_status == 200);

	assert(http_hdr_res_status_parse(res, "HTTP/1.1 404 Not Found") == 0);
	assert(res->reply_status == 404);

	assert(http_hdr_res_status_parse(res, "HTTP/1.0 500 Server Error") == 0);
	assert(res->reply_status == 500);

	http_hdr_res_free(res);

	// Full parse path: append a status-line entry (index 0) plus a
	// Content-Range entry, then call http_hdr_res_parse().
	HTTP_HDR_RES *res2 = http_hdr_res_new();
	http_hdr_append_entry(&res2->hdr,
		http_hdr_entry_build("HTTP/1.1", "206 Partial Content"));
	http_hdr_put_str(&res2->hdr, "Content-Length", "101");
	http_hdr_put_str(&res2->hdr, "Content-Range", "bytes 0-100/200");
	int pr = http_hdr_res_parse(res2);
	printf("http_hdr_res_parse ret=%d reply_status=%d\n",
		pr, res2->reply_status);
	assert(pr == 0);
	assert(res2->reply_status == 206);

	http_off_t from = -1, to = -1, total = -1;
	int rr = http_hdr_res_range(res2, &from, &to, &total);
	printf("res range ret=%d from=%lld to=%lld total=%lld\n",
		rr, (long long)from, (long long)to, (long long)total);
	assert(rr == 0 && from == 0 && to == 100 && total == 200);

	http_hdr_res_free(res2);
	printf("response header tests passed.\n");
}

// ---------------------------------------------------------------------------
// http_status_line
// ---------------------------------------------------------------------------
static void test_http_status_line(void)
{
	printf("\n==== http_status_line tests ====\n");

	const char *s200 = http_status_line(200);
	const char *s404 = http_status_line(404);
	const char *s500 = http_status_line(500);
	printf("200: %s\n404: %s\n500: %s\n", s200, s404, s500);
	assert(s200 && strstr(s200, "200") != NULL);
	assert(s404 && strstr(s404, "404") != NULL);
	assert(s500 && strstr(s500, "500") != NULL);
}

// ---------------------------------------------------------------------------
// HTTP util (<http/lib_http_util.h>)
// ---------------------------------------------------------------------------
static void test_http_util(void)
{
	printf("\n==== HTTP util tests ====\n");

	HTTP_UTIL *u = http_util_req_new("http://www.test.com/index.html", "GET");
	assert(u != NULL);

	// configure request without sending it
	http_util_set_req_entry(u, "Connection", "keep-alive");
	http_util_set_req_entry(u, "User-Agent", "acl-test");
	http_util_set_req_cookie(u, "token", "xyz");
	http_util_set_req_keep_alive(u, 30);

	printf("util configured (no network send)\n");

	http_util_free(u);

	// dump_url against an unreachable host returns <0 without hanging
	// (connection to port 1 on loopback is refused immediately).
	int dr = http_util_dump_url("http://127.0.0.1:1/nope", "acl_dump.tmp");
	printf("http_util_dump_url(unreachable) ret=%d (expected <0)\n", dr);
	assert(dr < 0);

	printf("http util tests passed.\n");
}

int main(void)
{
	acl_lib_init();

	test_http_req();
	test_http_hdr_common();
	test_http_res();
	test_http_status_line();
	test_http_util();

	acl_lib_end();

	printf("\nALL PROTOCOL HTTP TESTS PASSED\n");
	return 0;
}
