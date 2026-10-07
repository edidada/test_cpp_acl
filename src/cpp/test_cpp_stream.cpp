/**
 * test_cpp_stream.cpp — test acl::fstream / acl::ifstream / acl::ofstream /
 * acl::socket_stream / acl::server_socket.
 *
 * Verified against headers under lib_acl_cpp/include/acl_cpp/stream/
 *
 * API notes:
 *  - fstream::read()/write()/format()/puts() come from istream/ostream bases.
 *  - socket_stream::open(addr, conn_timeout, rw_timeout, unit) — the timeout
 *    args have no defaults in the header, so they must be passed explicitly.
 *  - server_socket::set_tcp_defer_accept() is documented as Linux-only, so it
 *    is only exercised on non-Windows platforms here.
 *  - no acl_cpp_end() exists in the library; acl_cpp_init() needs no paired
 *    deinit call.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
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

const char* kFileA = "_acl_fstream_a.txt";
const char* kFileB = "_acl_fstream_b.txt";
const char* kEchoAddr = "127.0.0.1:19889";

// ---------------------------------------------------------------------------
// acl::fstream
// ---------------------------------------------------------------------------
static void test_fstream() {
	section("acl::fstream");

	// drop leftovers from a previous run — on Windows rename() refuses to
	// overwrite an existing target, so kFileB must not pre-exist
	::remove(kFileA);
	::remove(kFileB);

	// create() + write/format/puts
	acl::fstream fw;
	CHECK(fw.create(kFileA));
	CHECK(fw.format("line1 %d\n", 100) > 0);          // ostream::format()
	CHECK(fw.puts("line2 text\n") > 0);               // ostream::puts()
	char buf[] = "raw-bytes\n";
	CHECK(fw.write(buf, sizeof(buf)) == (int) sizeof(buf));
	long long pos = fw.ftell();
	CHECK(pos > 0);
	long long sz = fw.fsize();
	CHECK(sz == pos);
	printf("  wrote file, ftell=%lld fsize=%lld\n", pos, sz);

	// lock()/unlock()
	CHECK(fw.lock(true));        // exclusive lock
	CHECK(fw.unlock());

	// rewind, read back
	CHECK(fw.fseek(0, SEEK_SET) == 0);
	CHECK(fw.ftell() == 0);
	char rb[128];
	memset(rb, 0, sizeof(rb));
	int n = fw.read(rb, 5, true);                    // read "line1"
	CHECK(n == 5 && memcmp(rb, "line1", 5) == 0);

	// tell position after read
	printf("  after read(5): ftell=%lld\n", (long long) fw.ftell());

	// file_path()
	CHECK(fw.file_path() != NULL);

	// rename() — on Windows the file handle must be closed first
	fw.close();
	CHECK(fw.rename(kFileA, kFileB));

	// reopen the renamed file for reading via open_trunc? no — that truncates;
	// use read-only open with documented flag values (O_RDONLY == 0x0000)
	acl::fstream fr;
	CHECK(fr.open(kFileB, 0x0000 /*O_RDONLY*/, 0700));
	char all[256];
	memset(all, 0, sizeof(all));
	int total = fr.read(all, sizeof(all) - 1, false);
	CHECK(total > 0);
	CHECK(strstr(all, "line1 100") != NULL);
	CHECK(strstr(all, "line2 text") != NULL);
	fr.close();

	// open_trunc() recreates the file and clears content
	acl::fstream ft;
	CHECK(ft.open_trunc(kFileB));
	CHECK(ft.fsize() == 0);
	ft.puts("truncated content\n");
	ft.close();

	// remove() deletes the file — it must still own the stream, because
	// close() destroys it and file_path() then returns NULL; on Windows
	// remove() closes the file handle internally before unlinking.
	acl::fstream fd;
	CHECK(fd.open(kFileB, 0x0000, 0700));
	CHECK(fd.remove());

	// static fsize on a missing file returns -1
	CHECK(acl::fstream::fsize(kFileB) == -1);
}

// ---------------------------------------------------------------------------
// acl::ifstream / acl::ofstream
// ---------------------------------------------------------------------------
static void test_if_ofstream() {
	section("acl::ifstream / acl::ofstream");

	acl::ofstream ow;
	CHECK(ow.open_write("_acl_ofs.txt"));          // truncating write mode
	ow.format("ofs-%s\n", "hello");
	ow.puts("second line\n");
	ow.close();

	acl::ofstream ap;
	CHECK(ap.open_append("_acl_ofs.txt"));        // append mode
	ap.puts("third line\n");
	ap.close();

	acl::ifstream rd;
	CHECK(rd.open_read("_acl_ofs.txt"));
	char buf[256];
	memset(buf, 0, sizeof(buf));
	// file holds "ofs-hello\n" + "second line\n" + "third line\n" = 34 LF
	// bytes; acl::ofstream writes text mode on Windows so each \n becomes
	// \r\n -> 37 bytes on disk; the read strips CR again.
	int n = rd.read(buf, sizeof(buf) - 1, false);
	CHECK(n >= 34);
	CHECK(strstr(buf, "ofs-hello") != NULL);
	CHECK(strstr(buf, "third line") != NULL);

	// gets() a single line
	acl::ifstream rd2;
	CHECK(rd2.open_read("_acl_ofs.txt"));
	acl::string line;
	CHECK(rd2.gets(line));
	CHECK(strcmp(line.c_str(), "ofs-hello") == 0);
	rd2.close();

	// istream::read(string&) reads up to the string's capacity
	acl::ifstream rd3;
	CHECK(rd3.open_read("_acl_ofs.txt"));
	acl::string all;
	CHECK(rd3.read(all, false));
	CHECK(all.length() > 0);
	printf("  read(string&) length=%u\n", (unsigned) all.length());
	rd3.close();

	// remove test files — close every writer/reader first: on Windows an
	// unlink fails while another handle still holds the file open (rd above
	// has not gone out of scope yet)
	rd.close();
	acl::fstream fm;
	if (fm.open("_acl_ofs.txt", 0x0000, 0700)) {
		if (!fm.remove()) {
			printf("  acl fstream::remove() failed for _acl_ofs.txt\n");
		}
	}
	if (acl::fstream::fsize("_acl_ofs.txt") != -1) {
		::remove("_acl_ofs.txt");   // stdio fallback so reruns stay clean
	}
}

// ---------------------------------------------------------------------------
// acl::server_socket + acl::socket_stream localhost echo
// ---------------------------------------------------------------------------

/**
 * Server thread: listen on 127.0.0.1:19889, accept one connection with a
 * timeout (ms), read one request line and echo it back, then close.
 */
class echo_server : public acl::thread {
public:
	echo_server() : ok_(false), opened_(false) {}

protected:
	virtual void* run() override {
		acl::server_socket ss;
		opened_ = ss.open(kEchoAddr);
		if (!opened_) {
			printf("  server_socket.open(%s) failed: %s\n",
				kEchoAddr, acl::last_serror());
			return NULL;
		}
		printf("  server listening on %s\n", kEchoAddr);

		// deferred accept is Linux-only per server_socket.hpp docs
#if !defined(_WIN32) && !defined(_WIN64)
		ss.set_tcp_defer_accept(3);
#endif

		acl::socket_stream* in = ss.accept(5000 /*ms*/);
		if (in == NULL) {
			printf("  accept timeout\n");
			ss.close();
			return NULL;
		}

		char req[64];
		memset(req, 0, sizeof(req));
		// client sends exactly 17 bytes
		int n = in->read(req, 17, true);
		if (n == 17) {
			in->write(req, n);       // echo back
			ok_ = true;
		} else {
			printf("  server read error, n=%d\n", n);
		}

		delete in;
		ss.close();
		return NULL;
	}

public:
	bool ok_;
	bool opened_;
};

static void test_socket_echo() {
	section("acl::server_socket + acl::socket_stream echo");

	echo_server srv;
	srv.set_detachable(false);
	CHECK(srv.start());

	// give the listener a moment to bind (acl C++ headers have no sleep()
	// wrapper, so use the C++11 one)
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	const char* msg = "ping-pong-acl!!\n";      // exactly 17 bytes
	acl::socket_stream cs;
	CHECK(cs.open(kEchoAddr, 3 /*conn, s*/, 3 /*rw, s*/));
	printf("  client connected, peer=%s local=%s\n",
		cs.get_peer(), cs.get_local());

	CHECK(cs.alive());
	cs.set_tcp_nodelay(true);
	CHECK(cs.get_tcp_nodelay());
	cs.set_tcp_non_blocking(false);

	// send request
	CHECK(cs.write(msg, 17, true) == 17);

	// read echo
	char resp[32];
	memset(resp, 0, sizeof(resp));
	int n = cs.read(resp, 17, true);
	CHECK(n == 17);
	CHECK(memcmp(resp, msg, 17) == 0);
	printf("  echo verified: %.*s", n, resp);

	// socket handle is valid
#if defined(_WIN32) || defined(_WIN64)
	CHECK(cs.sock_handle() != INVALID_SOCKET);
#else
	CHECK(cs.sock_handle() >= 0);
#endif

	srv.wait();               // let the server thread finish its side
	CHECK(srv.opened_);
	CHECK(srv.ok_);

	cs.shutdown_read();
	cs.shutdown_write();
}

int main() {
	acl::acl_cpp_init();

	printf("acl_cpp verbose: %s\n", acl::acl_cpp_verbose());

	test_fstream();
	test_if_ofstream();
	test_socket_echo();

	// cleanup leftovers if any
	acl::fstream fm;
	if (fm.open(kFileA, 0x0000, 0700)) { fm.remove(); }

	printf("\n%s (failures: %d)\n",
		g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", g_failures);
	return g_failures == 0 ? 0 : 1;
}
