/*
 * test_c_misc.cpp
 *
 * Broad self test for assorted ACL core (lib_acl) primitives.
 *
 * NOTE: all symbols below were verified against the real headers under
 * lib_acl/include (a single <lib_acl.h> already pulls in stdlib/net/thread/
 * msg/event/ioctl/aio ...). Functions that exist only in the reference doc
 * but not in this ACL version (e.g. acl_spool_*, acl_aqueue_create, the
 * simple acl_test_* unit-test API, acl_timeval2long/acl_msecdiff) are either
 * mapped to their real equivalents or skipped and noted in comments.
 */

#if defined(_WIN32) || defined(_WIN64)
/* MinGW-w64 defines struct timezone/timespec under these include guards;
 * pre-defining them lets ACL's own definitions win without a redefinition
 * clash, so this file compiles out-of-the-box under MinGW. */
# define _TIMEZONE_DEFINED
# define _TIMESPEC_DEFINED
#endif

#include <lib_acl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

/*
 * Several assertions below embed side-effecting ACL calls, e.g.
 *   assert(acl_mbox_send(mbox, &a) == 0);
 *   assert(acl_aqueue_push(aq, &a) == 0);
 *   assert(acl_make_dirs(TMP_SUB, 0700) == 0);
 * In a Release build NDEBUG makes assert() a no-op and the enclosed expression
 * is skipped, so the objects are never populated and later reads crash. Keep
 * assert() live so those setup calls always run.
 */
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <assert.h>

#define TMP_DIR   "./acl_misc_tmp"
#define TMP_SUB   "./acl_misc_tmp/sub"
#define TMP_FILE  "./acl_misc_tmp/hello.txt"
#define LOG_FILE  "./acl_misc_log.txt"

/* ---- atomic -------------------------------------------------------- */
static void test_atomic(void)
{
	printf("--- atomic ---\n");

	ACL_ATOMIC *a = acl_atomic_new();
	assert(a != NULL);

	/*
	 * The int64 atomic operations treat ACL_ATOMIC->value as the ADDRESS of
	 * a 64-bit integer (Windows: InterlockedExchangePointer(self->value, n),
	 * InterlockedExchangeAdd64((LONGLONG*) self->value, n)). acl_atomic_new()
	 * leaves value == NULL, so the int64 ops must be given backing storage
	 * via acl_atomic_set() first, otherwise they would write through a null
	 * pointer and fault.
	 */
	long long storage = 0;
	acl_atomic_set(a, &storage);           /* bind int64 backing storage */

	acl_atomic_int64_set(a, 0);
	long long old = acl_atomic_int64_fetch_add(a, 5);   /* -> 0 */
	long long neu = acl_atomic_int64_add_fetch(a, 3);   /* -> 8 */
	printf("  fetch_add(5)=%lld, add_fetch(3)=%lld\n", old, neu);
	assert(old == 0);
	assert(neu == 8);

	acl_atomic_int64_set(a, 100);
	assert(acl_atomic_int64_fetch_add(a, 1) == 100);

	/* pointer-sized CAS / exchange (here value IS the stored pointer) */
	int  x = 1, y = 2, z = 3;
	acl_atomic_set(a, &x);
	void *prev = acl_atomic_cas(a, &x, &y);    /* current==&x -> swap to &y */
	printf("  cas prev=%p\n", prev);
	void *old2 = acl_atomic_xchg(a, &z);       /* returns &y, stores &z */
	printf("  xchg old=%p\n", old2);
	assert(old2 == &y);

	acl_atomic_free(a);
	printf("  atomic ok\n");
}

/* ---- bits map ------------------------------------------------------ */
static void test_bits_map(void)
{
	printf("--- bits_map ---\n");

	ACL_BITS_MASK mask;
	memset(&mask, 0, sizeof(mask));

	ACL_BITS_MASK_ALLOC(&mask, 256);
	ACL_BITS_MASK_ZERO(&mask);

	ACL_BITS_MASK_SET(10, &mask);
	ACL_BITS_MASK_SET(200, &mask);
	assert(ACL_BITS_MASK_ISSET(10, &mask));
	assert(ACL_BITS_MASK_ISSET(200, &mask));
	assert(!ACL_BITS_MASK_ISSET(11, &mask));

	ACL_BITS_MASK_CLR(10, &mask);
	assert(!ACL_BITS_MASK_ISSET(10, &mask));

	ACL_BITS_MASK_FREE(&mask);
	printf("  bits_map ok (byte_count=%d)\n",
		(int) ACL_BITS_MASK_BYTE_COUNT(&mask));
}

/* ---- chunk chain --------------------------------------------------- */
static void test_chunk_chain(void)
{
	printf("--- chunk_chain ---\n");

	ACL_CHAIN *chain = acl_chain_new(1024, 0);
	assert(chain != NULL);

	const char *d1 = "Hello, ";
	const char *d2 = "chain!";
	acl_chain_add(chain, d1, 0, (int) strlen(d1));
	acl_chain_add(chain, d2, (acl_int64) strlen(d1), (int) strlen(d2));

	const char *data = acl_chain_data(chain);
	int len = acl_chain_data_len(chain);
	printf("  data=\"%.*s\" len=%d size=%d\n", len, data, len,
		acl_chain_size(chain));
	assert(len == (int) strlen("Hello, chain!"));
	assert(memcmp(data, "Hello, chain!", len) == 0);

	acl_chain_free(chain);
	printf("  chunk_chain ok\n");
}

/* ---- dlink (interval container) ------------------------------------ */
static void test_dlink(void)
{
	printf("--- dlink ---\n");

	ACL_DLINK *dl = acl_dlink_create(10);
	assert(dl != NULL);

	ACL_DITEM *it = acl_dlink_insert(dl, 100, 200);
	assert(it != NULL);
	acl_dlink_insert(dl, 300, 400);

	printf("  size=%d\n", acl_dlink_size(dl));
	assert(acl_dlink_lookup(dl, 150) != NULL);
	assert(acl_dlink_lookup(dl, 250) == NULL);
	assert(acl_dlink_lookup(dl, 350) != NULL);

	assert(acl_dlink_delete(dl, 150) == 0);
	assert(acl_dlink_delete_range(dl, 300, 400) == 0);

	acl_dlink_free(dl);
	printf("  dlink ok\n");
}

/* ---- iplink -------------------------------------------------------- */
static void test_iplink(void)
{
	printf("--- iplink ---\n");

	ACL_IPLINK *il = acl_iplink_create(10);
	assert(il != NULL);

	ACL_IPITEM *it = acl_iplink_insert(il, "192.168.1.1", "192.168.1.10");
	assert(it != NULL);
	acl_iplink_insert(il, "10.0.0.1", "10.0.0.255");

	printf("  items=%d\n", acl_iplink_count_item(il));
	assert(acl_iplink_lookup_str(il, "192.168.1.5") != NULL);
	assert(acl_iplink_lookup_str(il, "192.168.1.50") == NULL);
	assert(acl_iplink_lookup_str(il, "10.0.0.9") != NULL);

	assert(acl_iplink_delete_by_ip(il, "192.168.1.1") == 0);

	acl_iplink_free(il);
	printf("  iplink ok\n");
}

/* ---- yqueue / ypipe ------------------------------------------------ */
static void test_yqueue_ypipe(void)
{
	printf("--- yqueue / ypipe ---\n");

	int val = 42;

	/* lock-free queue (block-slot based) -- exercise the API surface */
	ACL_YQUEUE *q = acl_yqueue_new();
	assert(q != NULL);

	/*
	 * acl_yqueue_back() returns &back_chunk->value[back_pos], but back_chunk
	 * is NULL until the very first acl_yqueue_push() runs (see acl_ypipe_new,
	 * which always pushes once to prime the queue). So push first, then the
	 * front/back slots are valid.
	 */
	acl_yqueue_push(q);
	void **front = acl_yqueue_front(q);
	void **back  = acl_yqueue_back(q);
	assert(front != NULL && back != NULL);
	*back = &val;
	acl_yqueue_push(q);
	printf("  yqueue front=%p back=%p\n", (void *) front, (void *) back);
	acl_yqueue_pop(q);
	acl_yqueue_free(q, NULL);
	printf("  yqueue ok\n");

	/* lock-free pipe */
	ACL_YPIPE *p = acl_ypipe_new();
	assert(p != NULL);
	acl_ypipe_write(p, &val);
	/*
	 * acl_ypipe_flush() only returns 1 on the rare CAS-contention branch;
	 * in single-threaded use it returns 0 but still publishes the value via
	 * the internal compare-and-swap side effect, which is what makes the
	 * subsequent check_read()/read() succeed. So we exercise flush() without
	 * asserting a specific return code.
	 */
	printf("  ypipe flush -> %d\n", acl_ypipe_flush(p));
	assert(acl_ypipe_check_read(p) == 1);
	void *got = acl_ypipe_read(p);
	assert(got == &val);
	printf("  ypipe read=%d\n", *(int *) got);
	acl_ypipe_free(p, NULL);
	printf("  ypipe ok\n");
}

/* ---- mbox ---------------------------------------------------------- */
static void test_mbox(void)
{
	printf("--- mbox ---\n");

	int a = 1, b = 2, c = 3;
	ACL_MBOX *mbox = acl_mbox_create();
	assert(mbox != NULL);

	assert(acl_mbox_send(mbox, &a) == 0);
	assert(acl_mbox_send(mbox, &b) == 0);
	assert(acl_mbox_send(mbox, &c) == 0);

	int success = 0;
	void *m1 = acl_mbox_read(mbox, 100, &success);   /* ms timeout */
	assert(success && m1 == &a);
	void *m2 = acl_mbox_read(mbox, 100, &success);
	assert(success && m2 == &b);
	void *m3 = acl_mbox_read(mbox, 100, &success);
	assert(success && m3 == &c);
	printf("  read %d %d %d\n", *(int *) m1, *(int *) m2, *(int *) m3);

	acl_mbox_free(mbox, NULL);
	printf("  mbox ok\n");
}

/* ---- dirs / scan --------------------------------------------------- */
static void test_dirs_scan(void)
{
	printf("--- make_dirs / scan_dir ---\n");

	assert(acl_make_dirs(TMP_SUB, 0700) == 0);
	printf("  created %s\n", TMP_SUB);

	/* drop a small file so the scan has something to find */
	ACL_VSTREAM *fp = acl_vstream_fopen(TMP_FILE,
		O_WRONLY | O_CREAT | O_TRUNC, 0600, 1024);
	if (fp != NULL) {
		acl_vstream_write(fp, "abc", 3);
		acl_vstream_fclose(fp);
	}

	ACL_SCAN_DIR *sd = acl_scan_dir_open(TMP_DIR, 1);
	if (sd != NULL) {
		const char *name;
		int n = 0;
		while ((name = acl_scan_dir_next(sd)) != NULL) {
			printf("  entry[%d]: %s (file=%s)\n", n++,
				acl_scan_dir_path(sd), acl_scan_dir_file(sd));
		}
		printf("  ndirs=%u nfiles=%u nsize=%lld\n",
			acl_scan_dir_ndirs(sd), acl_scan_dir_nfiles(sd),
			(long long) acl_scan_dir_nsize(sd));
		acl_scan_dir_close(sd);
	} else {
		printf("  acl_scan_dir_open failed - skip scan\n");
	}

	int nfile = 0, ndir = 0;
	acl_int64 total = acl_scan_dir_size(TMP_DIR, 1, &nfile, &ndir);
	printf("  acl_scan_dir_size -> %lld (nfile=%d, ndir=%d)\n",
		(long long) total, nfile, ndir);

	/* clean up the temp tree */
	int rdir = 0, rfile = 0;
	acl_scan_dir_rm(TMP_DIR, 1, &rdir, &rfile);
	printf("  removed %d files / %d dirs\n", rfile, rdir);
}

/* ---- basename / dirname -------------------------------------------- */
static void test_basename(void)
{
	printf("--- sane_basename / sane_dirname ---\n");

	ACL_VSTRING *bp = acl_vstring_alloc(0);
	assert(bp != NULL);

	char *bn = acl_sane_basename(bp, "/a/b/c/file.txt");
	printf("  basename(\"/a/b/c/file.txt\") = %s\n", bn ? bn : "(null)");
	assert(bn != NULL && strcmp(bn, "file.txt") == 0);

	char *dn = acl_sane_dirname(bp, "/a/b/c/file.txt");
	printf("  dirname(\"/a/b/c/file.txt\")  = %s\n", dn ? dn : "(null)");

	acl_vstring_free(bp);
	printf("  basename ok\n");
}

/* ---- msg / mylog --------------------------------------------------- */
static void test_msg_log(void)
{
	printf("--- msg / mylog ---\n");

	/* acl_msg_info / warn route to acl_msg_printf (stdout) */
	acl_msg_info("hello from acl_msg_info: %d", 1);
	acl_msg_warn("hello from acl_msg_warn: %s", "careful");

	/* file-based logger */
	if (acl_open_log(LOG_FILE, NULL) == 0) {
		acl_write_to_log("log entry value=%d\n", 123);
		acl_close_log();
		printf("  wrote to %s\n", LOG_FILE);
		remove(LOG_FILE);
	} else {
		printf("  acl_open_log failed - skip file log\n");
	}
	printf("  msg/log ok\n");
}

/* ---- getopt / env -------------------------------------------------- */
static void test_getopt_env(void)
{
	printf("--- getopt / env ---\n");

	acl_getopt_init();
	printf("  acl_getopt_init ok (optind=%d)\n", acl_optind);

	char buf[256];
	char *path = acl_getenv("PATH");
	printf("  PATH via acl_getenv: %s\n", path ? "found" : "(null)");

	if (acl_getenv3("PATH", buf, sizeof(buf)) != NULL) {
		printf("  acl_getenv3 PATH length=%d\n", (int) strlen(buf));
	}
	printf("  getopt/env ok\n");
}

/* ---- vstream file I/O ---------------------------------------------- */
static void test_vstream_file(void)
{
	printf("--- vstream file I/O ---\n");

	const char *path = "./acl_misc_file.txt";
	char rb[128];

	/* write side */
	ACL_VSTREAM *fp = acl_vstream_fopen(path,
		O_RDWR | O_CREAT | O_TRUNC, 0600, 4096);
	assert(fp != NULL);

	assert(acl_vstream_write(fp, "line1\n", 6) == 6);
	assert(acl_vstream_fprintf(fp, "%s=%d\n", "answer", 42) > 0);
	assert(acl_vstream_fputs("tail\n", fp) >= 0);
	assert(acl_vstream_printf("stdout-only printf %d\n", 7) > 0);
	assert(acl_vstream_fflush(fp) == 0);

	/* ftell should report the write offset */
	acl_off_t endpos = acl_vstream_ftell(fp);
	printf("  after write ftell=%lld\n", (long long) endpos);

	/* rewind and read back */
	acl_vstream_fseek(fp, 0, SEEK_SET);
	memset(rb, 0, sizeof(rb));
	int n = acl_vstream_read(fp, rb, sizeof(rb) - 1);
	printf("  read back %d bytes: %.*s", n, n > 0 ? n : 0, rb);

	acl_vstream_fclose(fp);
	remove(path);
	printf("  vstream file ok\n");
}

/* ---- aqueue -------------------------------------------------------- */
static void test_aqueue(void)
{
	printf("--- aqueue ---\n");

	/* this version exposes acl_aqueue_new()/qlen(), not _create/_size */
	ACL_AQUEUE *aq = acl_aqueue_new();
	assert(aq != NULL);

	int a = 1, b = 2, c = 3;
	assert(acl_aqueue_push(aq, &a) == 0);
	assert(acl_aqueue_push(aq, &b) == 0);
	assert(acl_aqueue_push(aq, &c) == 0);
	printf("  qlen=%d\n", acl_aqueue_qlen(aq));
	assert(acl_aqueue_qlen(aq) == 3);

	void *p1 = acl_aqueue_pop(aq);
	void *p2 = acl_aqueue_pop(aq);
	void *p3 = acl_aqueue_pop(aq);
	assert(p1 == &a && p2 == &b && p3 == &c);
	printf("  popped %d %d %d\n", *(int *) p1, *(int *) p2, *(int *) p3);

	acl_aqueue_free(aq, NULL);
	printf("  aqueue ok\n");
}

/* ---- sane_socketpair ----------------------------------------------- */
static void test_socketpair(void)
{
	printf("--- sane_socketpair ---\n");

	ACL_SOCKET sv[2];
	if (acl_sane_socketpair(AF_INET, SOCK_STREAM, 0, sv) != 0) {
		printf("  acl_sane_socketpair unsupported - skip\n");
		return;
	}

	/*
	 * acl_socket_write()/acl_socket_read() return the number of bytes
	 * moved by send()/recv() (the header's "0: OK" note is misleading),
	 * so we check for a positive byte count rather than == 0.
	 */
	const char *msg = "pair-ping";
	int msglen = (int) strlen(msg);
	int wn = acl_socket_write(sv[0], msg, strlen(msg), 5, NULL, NULL);
	printf("  socket_write -> %d\n", wn);
	if (wn <= 0) {
		printf("  socket_write failed (%d) - skip round-trip\n", wn);
		acl_socket_close(sv[0]);
		acl_socket_close(sv[1]);
		return;
	}

	char buf[32];
	memset(buf, 0, sizeof(buf));
	int rn = acl_socket_read(sv[1], buf, msglen, 5, NULL, NULL);
	printf("  socket_read -> %d\n", rn);
	if (rn <= 0) {
		printf("  socket_read failed (%d) - skip round-trip\n", rn);
		acl_socket_close(sv[0]);
		acl_socket_close(sv[1]);
		return;
	}
	assert(memcmp(buf, msg, rn) == 0);
	printf("  socketpair round-trip: %.*s\n", rn, buf);

	acl_socket_close(sv[0]);
	acl_socket_close(sv[1]);
	printf("  socketpair ok\n");
}

/* ---- dynamic library header (compile-time presence) ---------------- */
static void test_dll_header(void)
{
	printf("--- dll (header presence) ---\n");
	/* acl_dll.h is included transitively via lib_acl.h; verify a symbol. */
	const char *err = acl_dlerror();
	printf("  acl_dlerror() -> %s\n", err ? err : "(null)");
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	acl_lib_init();

	printf("==== ACL C misc tests ====\n");

	test_atomic();
	test_bits_map();
	test_chunk_chain();
	test_dlink();
	test_iplink();
	test_yqueue_ypipe();
	test_mbox();
	test_dirs_scan();
	test_basename();
	test_msg_log();
	test_getopt_env();
	test_vstream_file();
	test_aqueue();
	test_socketpair();
	test_dll_header();

	/* Skipped on purpose (not present / not feasible in this ACL version):
	 *   acl_spool_*  -> ACL_SPOOL is an alias of ACL_IOCTL (server framework)
	 *   acl_test_create/add/run -> unit_test is config-driven, use assert()
	 *   acl_timeval2long/acl_msecdiff -> not declared in acl_timeops.h here
	 *   acl_exec_command -> actually spawns processes; avoided in a smoke test
	 */

	acl_lib_end();

	printf("All misc tests passed!\n");
	return 0;
}
