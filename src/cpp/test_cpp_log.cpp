/**
 * test_cpp_log.cpp — test acl::log static logging and the logger macros.
 *
 * Verified against lib_acl_cpp/include/acl_cpp/stdlib/log.hpp
 *
 * API notes:
 *  - acl::log is a fully static class: open()/close()/msg1()/warn1()/error1().
 *    fatal1() is deliberately NOT tested — it aborts the process.
 *  - The logger()/logger_warn()/logger_error() macros expand (on MSVC>=2008
 *    and GCC) to msg4()/warn4()/error4() with __FILE__/__LINE__/__FUNCTION__
 *    injected; they require at least one format argument on MSVC's classic
 *    preprocessor, so every call below passes formatted args.
 *  - There is NO acl::logger_stream (ostream-style logger) in this version of
 *    the ACL C++ API; the closest facility is acl::log + the macros, plus
 *    acl::stdout_stream (see stream/stdout_stream.hpp) for raw stdout writes.
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

const char* kLogFile = "_acl_cpp_test.log";

static void test_log_basic() {
	section("acl::log open / msg / warn / error / close");

	// recipient can be a local file; multiple recipients may be joined with
	// '|', e.g. "file.log|UDP:127.0.0.1:12345|TCP:..|UNIX:.."
	acl::log::open(kLogFile, "test_cpp_log", NULL);
	printf("  log opened -> %s\n", kLogFile);

	// direct static interfaces (msg1/warn1/error1 take printf-style args)
	acl::log::msg1("msg1: hello %s, num=%d", "acl", 1);
	acl::log::warn1("warn1: value too big: %u", 100u);
	acl::log::error1("error1: something failed: %s", "connection reset");

	// msg4/warn4/error4 variants with file/line/function info
	acl::log::msg4(__FILE__, __LINE__, __FUNCTION__, "msg4: %s", "ok");
	acl::log::warn4(__FILE__, __LINE__, __FUNCTION__, "warn4: %d", 42);
	acl::log::error4(__FILE__, __LINE__, __FUNCTION__, "error4: %s", "bad");

	acl::log::close();
	printf("  log closed\n");

	// the log file must exist and contain the written lines
	FILE* fp = fopen(kLogFile, "rb");
	CHECK(fp != NULL);
	if (fp != NULL) {
		char buf[4096];
		size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
		fclose(fp);
		buf[n] = 0;
		printf("  log file size=%u\n", (unsigned) n);
		CHECK(n > 0);
		CHECK(strstr(buf, "msg1: hello acl") != NULL);
		CHECK(strstr(buf, "warn1: value too big") != NULL);
		CHECK(strstr(buf, "error1: something failed") != NULL);
		remove(kLogFile);
	}
}

static void test_log_macros() {
	section("logger / logger_warn / logger_error macros");

	acl::log::open(kLogFile, "test_cpp_log_macro", NULL);

	// macros defined by log.hpp — logger maps to acl::log::msg4 on modern
	// compilers (injecting __FILE__, __LINE__, __FUNCTION__ automatically)
	logger("logger macro: %s at %d", "hello", 1);
	logger_warn("logger_warn macro: %s", "careful");
	logger_error("logger_error macro: %s", "ouch");

	acl::log::close();

	FILE* fp = fopen(kLogFile, "rb");
	CHECK(fp != NULL);
	if (fp != NULL) {
		char buf[4096];
		size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
		fclose(fp);
		buf[n] = 0;
		CHECK(strstr(buf, "logger macro: hello") != NULL);
		CHECK(strstr(buf, "logger_warn macro: careful") != NULL);
		CHECK(strstr(buf, "logger_error macro: ouch") != NULL);
		remove(kLogFile);
	}

	// NOTE: do NOT call logger_fatal()/acl::log::fatal1() here — the fatal
	// level aborts() the process by design.
}

static void test_log_debug_sections() {
	section("acl::log debug sections (msg3/msg6)");

	// configure debug sections: record section 101 with level < 2 only
	acl::log::debug_init("101:2; 102:3");
	acl::log::open(kLogFile, "test_cpp_log_debug", "101:2; 102:3");

	// section/level filtered message: this one (level 1 < 2) is logged
	acl::log::msg3(101, 1, "msg3 section 101 level 1: %s", "visible");
	// this one (level 5 >= 2) is filtered out
	acl::log::msg3(101, 5, "msg3 section 101 level 5: %s", "hidden");

	// logger_debug macro variant (msg6)
	logger_debug(102, 2, "logger_debug 102/2: %s", "visible");

	acl::log::close();

	FILE* fp = fopen(kLogFile, "rb");
	if (fp != NULL) {
		char buf[4096];
		size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
		fclose(fp);
		buf[n] = 0;
		printf("  debug log size=%u\n", (unsigned) n);
		CHECK(strstr(buf, "level 1: visible") != NULL);
		CHECK(strstr(buf, "level 5: hidden") == NULL);
		CHECK(strstr(buf, "logger_debug 102/2: visible") != NULL);
		remove(kLogFile);
	} else {
		CHECK(false);
	}
}

static void test_log_stdout() {
	section("acl::log stdout_open");

	// with no log file opened, stdout output can be enabled; messages then go
	// to the console instead of being dropped
	acl::log::stdout_open(true);
	acl::log::msg1("stdout msg1: %s (goes to console)", "watch me");
	acl::log::warn1("stdout warn1: %s", "watch me");
	acl::log::stdout_open(false);

	// calling log functions with the logger closed must not crash
	acl::log::error1("after stdout_open(false): %s", "still safe");
}

int main() {
	acl::acl_cpp_init();

	printf("acl_cpp verbose: %s\n", acl::acl_cpp_verbose());

	test_log_basic();
	test_log_macros();
	test_log_debug_sections();
	test_log_stdout();

	acl::log::close();   // ensure closed; NOTE: no acl_cpp_end() exists

	printf("\n%s (failures: %d)\n",
		g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", g_failures);
	return g_failures == 0 ? 0 : 1;
}
