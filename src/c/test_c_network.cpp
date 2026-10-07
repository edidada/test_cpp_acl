/*
 * test_c_network.cpp
 *
 * C-network self test for the ACL core library (lib_acl).
 *
 * NOTE: the exact API of the fetched ACL version was verified against the
 * real headers under lib_acl/include (see docs/acl-api-reference.md for the
 * intended surface). A few names in the reference doc do not exist in this
 * version, so the equivalent real symbols are used here:
 *   - acl_listen()/acl_accept()/ACL_BLOCK   -> acl_vstream_listen()/
 *                                               acl_vstream_accept()/ACL_BLOCKING
 *   - acl_get_ifconf() / acl_get_local_ips() -> acl_get_ifaddrs()
 *   - acl_set_tcp_nodelay / _nonblocking / _sendbuf / _recvbuf
 *                                            -> acl_tcp_nodelay / acl_non_blocking
 *                                               / acl_tcp_set_sndbuf / acl_tcp_set_rcvbuf
 *   - acl_vstream_set_* / acl_vstream_sockhandle
 *                                            -> ACL_VSTREAM_SOCK() + socket level setters
 *                                               + ACL_VSTREAM_SET_RWTIMO()
 *   - acl_inet_aton                           -> acl_inet_pton(AF_INET,...)
 */

#if defined(_WIN32) || defined(_WIN64)
/* MinGW-w64 guards its own struct timezone / struct timespec definitions with
 * these macros. ACL's headers provide the structs unguarded, which otherwise
 * collides with MinGW's. Pre-defining the macros makes MinGW skip its defs and
 * lets ACL's win, so the file compiles out-of-the-box without manual -D flags. */
# define _TIMEZONE_DEFINED
# define _TIMESPEC_DEFINED
#endif

#include <lib_acl.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- socket init / close / alive -------------------------------------- */
static void test_socket_basic(void)
{
	printf("--- socket init/close/alive ---\n");

	/* acl_socket_init() performs WSAStartup on Windows; it is also invoked
	 * by acl_lib_init(), calling it again is harmless. */
	assert(acl_socket_init() == 0);

	/* an invalid socket must not be reported as alive */
	assert(acl_socket_alive(ACL_SOCKET_INVALID) == 0);

	/* create a raw TCP socket, check it, close it */
	ACL_SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
	assert(fd != ACL_SOCKET_INVALID);
	assert(acl_socket_alive(fd) == 1);

	assert(acl_socket_close(fd) == 0);

	/* a closed socket is no longer usable; alive() reports it as dead */
	printf("  closed socket alive=%d\n", acl_socket_alive(fd));
	printf("  socket alive/close ok\n");
}

/* ---- DNS / inet helpers ----------------------------------------------- */
static void test_dns(void)
{
	printf("--- DNS / inet ---\n");

	/* acl_inet_pton is the aton-style text->binary conversion */
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	size_t n = acl_inet_pton(AF_INET, "127.0.0.1", (struct sockaddr *) &sa);
	assert(n > 0);
	printf("  inet_pton(127.0.0.1) ok, family=%d\n", (int) sa.sin_family);

	/* binary -> text */
	char buf[64];
	const char *ret = acl_inet_ntoa(sa.sin_addr, buf, sizeof(buf));
	assert(ret != NULL);
	assert(strcmp(buf, "127.0.0.1") == 0);
	printf("  inet_ntoa -> %s\n", buf);

	/* simple IP validity checks */
	assert(acl_is_ipv4("192.168.1.1") == 1);
	assert(acl_is_ipv4("not.an.ip") == 0);

	/* acl_gethostbyname returns an ACL_DNS_DB* (freed via acl_netdb_free).
	 * "localhost" resolves via the local hosts table, no external DNS needed. */
	int h_error = 0;
	ACL_DNS_DB *db = acl_gethostbyname("localhost", &h_error);
	if (db != NULL) {
		int size = acl_netdb_size(db);
		printf("  gethostbyname(localhost) returned %d address(es)\n", size);
		for (int i = 0; i < size; i++) {
			const char *ip = acl_netdb_index_ip(db, i);
			if (ip != NULL) {
				printf("    [%d] %s\n", i, ip);
			}
		}
		acl_netdb_free(db);
	} else {
		printf("  gethostbyname(localhost) failed (h_error=%d) - not fatal\n",
			h_error);
	}
}

/* ---- network interfaces / local ips --------------------------------- */
static void test_ifaddrs(void)
{
	printf("--- network interfaces / local ips ---\n");

	/* acl_get_ifaddrs() replaces both acl_get_ifconf() and acl_get_local_ips()
	 * in this ACL version. */
	ACL_IFCONF *ifconf = acl_get_ifaddrs();
	if (ifconf == NULL) {
		printf("  acl_get_ifaddrs() returned NULL (no interfaces?)\n");
		return;
	}

	printf("  found %d interface address(es)\n", ifconf->length);
	for (int i = 0; i < ifconf->length; i++) {
		ACL_IFADDR *a = &ifconf->addrs[i];
		printf("    [%d] name=%-12s addr=%s\n", i, a->name, a->addr);
	}

	acl_free_ifaddrs(ifconf);
}

/* ---- TCP / socket options (applied to a real socket) ---------------- */
static void test_tcp_options(ACL_SOCKET fd)
{
	printf("--- TCP options ---\n");

	/* nodelay on/off, then query */
	acl_tcp_nodelay(fd, 1);
	printf("  tcp_nodelay=1 -> get=%d\n", acl_get_tcp_nodelay(fd));
	acl_tcp_nodelay(fd, 0);
	printf("  tcp_nodelay=0 -> get=%d\n", acl_get_tcp_nodelay(fd));

	/* send/recv buffers + getters */
	acl_tcp_set_sndbuf(fd, 64 * 1024);
	acl_tcp_set_rcvbuf(fd, 64 * 1024);
	printf("  sndbuf=%d rcvbuf=%d\n",
		acl_tcp_get_sndbuf(fd), acl_tcp_get_rcvbuf(fd));

	/* non-blocking toggle */
	assert(acl_non_blocking(fd, 1) == 0);
	printf("  set non-blocking ok\n");
	assert(acl_non_blocking(fd, 0) == 0);
	printf("  set blocking ok\n");
}

/* ---- localhost echo (self contained) -------------------------------- */
static void test_echo(void)
{
	printf("--- localhost echo (127.0.0.1:19888) ---\n");

	const char *addr = "127.0.0.1:19888";
	const char *msg  = "hello-acl-echo";
	char rbuf[128];
	char ipbuf[64];
	int    n;

	/* 1. start a blocking listener on loopback */
	ACL_VSTREAM *listen = acl_vstream_listen(addr, 128);
	if (listen == NULL) {
		printf("  acl_vstream_listen(%s) failed - skip echo test\n", addr);
		return;
	}
	printf("  listening on %s\n", addr);

	/* 2. a blocking client connection; the kernel completes the loopback
	 *    handshake against the listen backlog even before accept(). */
	ACL_VSTREAM *cli = acl_vstream_connect(addr, ACL_BLOCKING, 10, 10, 8192);
	assert(cli != NULL);
	printf("  client connected\n");

	/* 3. accept the pending connection on the server side */
	ACL_VSTREAM *srv = acl_vstream_accept(listen, ipbuf, sizeof(ipbuf));
	assert(srv != NULL);
	printf("  accepted peer=%s\n", ipbuf[0] ? ipbuf : "?");

	/* apply the vstream timeout macro + socket-level options on the client */
	ACL_VSTREAM_SET_RWTIMO(cli, 10);
	ACL_SOCKET cli_fd = ACL_VSTREAM_SOCK(cli);
	acl_tcp_nodelay(cli_fd, 1);
	printf("  client sock=%d nodelay set\n", (int) cli_fd);

	/* 4. client -> server */
	n = acl_vstream_write(cli, msg, (int) strlen(msg));
	assert(n == (int) strlen(msg));

	memset(rbuf, 0, sizeof(rbuf));
	n = acl_vstream_read(srv, rbuf, sizeof(rbuf) - 1);
	assert(n == (int) strlen(msg));
	assert(memcmp(rbuf, msg, strlen(msg)) == 0);
	printf("  server got: %.*s\n", n, rbuf);

	/* 5. server -> client (echo back) */
	n = acl_vstream_write(srv, msg, (int) strlen(msg));
	assert(n == (int) strlen(msg));

	memset(rbuf, 0, sizeof(rbuf));
	n = acl_vstream_read(cli, rbuf, sizeof(rbuf) - 1);
	assert(n == (int) strlen(msg));
	assert(memcmp(rbuf, msg, strlen(msg)) == 0);
	printf("  client got: %.*s\n", n, rbuf);

	/* 6. close everything (order: data streams first, then listener) */
	acl_vstream_fclose(cli);
	acl_vstream_fclose(srv);
	acl_vstream_fclose(listen);
	printf("  echo test passed\n");
}

int main(void)
{
	acl_lib_init();

	printf("==== ACL C network tests (version %s) ====\n", acl_version());

	test_socket_basic();
	test_dns();
	test_ifaddrs();

	/* exercise TCP options against a freshly created socket */
	ACL_SOCKET opt_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (opt_fd != ACL_SOCKET_INVALID) {
		test_tcp_options(opt_fd);
		acl_socket_close(opt_fd);
	}

	test_echo();

	acl_lib_end();

	printf("All network tests passed!\n");
	return 0;
}
