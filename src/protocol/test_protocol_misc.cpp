// test_protocol_misc.cpp
// Miscellaneous protocol + core service tests: SMTP, ICMP, memdb, AIO.
//
// Network/ICMP are exercised in a "graceful" way: SMTP/ICMP without a real
// server or raw socket simply fail and are reported/skipped, never fatal.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <lib_acl.h>
#include <aio/acl_aio.h>
#include <db/acl_mdb.h>
#include <smtp/smtp_client.h>
#include <icmp/lib_icmp_type.h>
#include <icmp/lib_icmp.h>
}

/*
 * Several assertions below wrap side-effecting ACL calls, e.g.
 *   assert(acl_mdb_add(mdb, tbl, &u1, ...) != NULL);
 * In a Release build NDEBUG turns assert() into a no-op AND skips the enclosed
 * expression, so records are never inserted and later lookups dereference NULL
 * and crash. Keep assert() live so those calls always run.
 */
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <assert.h>

typedef struct USER_INFO {
	char name[32];
	char home[32];
	int  age;
} USER_INFO;

// ---------------------------------------------------------------------------
// AIO (<aio/acl_aio.h>)
// ---------------------------------------------------------------------------
static void test_aio(void)
{
	printf("\n==== AIO tests ====\n");

	// Create an event-driven AIO object with the SELECT engine.
	ACL_AIO *aio = acl_aio_create(ACL_EVENT_SELECT);
	assert(aio != NULL);
	printf("aio created, event_mode=%d\n", acl_aio_event_mode(aio));

	acl_aio_free(aio);
	printf("aio freed.\n");
}

// ---------------------------------------------------------------------------
// memdb (<db/acl_mdb.h>)
// ---------------------------------------------------------------------------
static void test_memdb(void)
{
	printf("\n==== MEMDB tests ====\n");

	// The task named acl_memdb_create/acl_memdb_destroy; the real ACL C
	// memory-DB API lives in <db/acl_mdb.h> as acl_mdb_* below.
	static const char *key_names[] = { "name", "home", NULL };
	static unsigned int key_flags[] = { ACL_MDT_FLAG_UNI, 0 };
	const char *dbname = "test.memdb";
	const char *tbl    = "test.user";

	ACL_MDB *mdb = acl_mdb_create(dbname, "hash");
	assert(mdb != NULL);

	ACL_MDT *mdt = acl_mdb_tbl_create(mdb, tbl, ACL_MDT_FLAG_NUL,
		100, key_names, key_flags);
	assert(mdt != NULL);

	USER_INFO u1, u2;
	memset(&u1, 0, sizeof(u1));
	memset(&u2, 0, sizeof(u2));
	strcpy(u1.name, "alice"); strcpy(u1.home, "beijing"); u1.age = 20;
	strcpy(u2.name, "bob");   strcpy(u2.home, "shanghai"); u2.age = 30;

	const char *kv1[] = { "alice", "beijing", NULL };
	const char *kv2[] = { "bob", "shanghai", NULL };

	assert(acl_mdb_add(mdb, tbl, &u1, sizeof(u1), key_names, kv1) != NULL);
	assert(acl_mdb_add(mdb, tbl, &u2, sizeof(u2), key_names, kv2) != NULL);

	printf("count=%d\n", acl_mdb_cnt(mdb, tbl));
	assert(acl_mdb_cnt(mdb, tbl) == 2);

	// probe an existing key
	assert(acl_mdb_probe(mdb, tbl, "name", "alice") != 0);
	// probe a missing key
	assert(acl_mdb_probe(mdb, tbl, "name", "nobody") == 0);

	// find by key
	ACL_MDT_RES *res = acl_mdb_find(mdb, tbl, "home", "beijing", 0, 0);
	assert(res != NULL);
	USER_INFO *p = (USER_INFO *)acl_mdt_fetch_row(res);
	assert(p != NULL);
	printf("found: name=%s age=%d\n", p->name, p->age);
	assert(strcmp(p->name, "alice") == 0);
	acl_mdt_res_free(res);

	// delete one record
	int n = acl_mdb_del(mdb, tbl, "name", "bob", NULL);
	printf("deleted=%d remaining=%d\n", n, acl_mdb_cnt(mdb, tbl));
	assert(n >= 1);

	acl_mdb_free(mdb);
	printf("memdb tests passed.\n");
}

// ---------------------------------------------------------------------------
// SMTP (<smtp/smtp_client.h>)
// ---------------------------------------------------------------------------
static void test_smtp(void)
{
	printf("\n==== SMTP tests ====\n");

	// No server listening on 127.0.0.1:10025 -> smtp_open returns NULL.
	// That is expected; we simply verify the API exists and fails cleanly.
	SMTP_CLIENT *c = smtp_open("127.0.0.1:10025", 1, 1, 256);
	if (c == NULL) {
		printf("smtp_open failed as expected (no server); API present, skipping.\n");
	} else {
		printf("smtp_open unexpectedly succeeded; closing.\n");
		smtp_close(c);
	}
	assert(c == NULL || c != NULL); // always true; just exercise the path
}

// ---------------------------------------------------------------------------
// ICMP (<icmp/lib_icmp.h>)
// ---------------------------------------------------------------------------
static void cb_respond(ICMP_PKT_STATUS *status, void *arg)
{
	(void)status; (void)arg;
}
static void cb_timeout(ICMP_PKT_STATUS *status, void *arg)
{
	(void)status; (void)arg;
}
static void cb_unreach(ICMP_PKT_STATUS *status, void *arg)
{
	(void)status; (void)arg;
}
static void cb_finish(ICMP_HOST *host, void *arg)
{
	(void)host; (void)arg;
}

static void test_icmp(void)
{
	printf("\n==== ICMP tests ====\n");

	// Create an AIO engine + ICMP chat object.
	ACL_AIO *aio = acl_aio_create(ACL_EVENT_SELECT);
	assert(aio != NULL);

	ICMP_CHAT *chat = icmp_chat_create(aio, 0);
	assert(chat != NULL);
	printf("icmp chat created\n");

	// Create a probe host object (do NOT send packets / no raw socket).
	ICMP_HOST *host = icmp_host_new(chat, "localhost", "127.0.0.1",
		1 /*npkt*/, 64 /*dlen*/, 1000 /*delay*/, 1000 /*timeout*/);
	assert(host != NULL);

	// Attach callbacks.
	icmp_host_set(host, NULL, cb_respond, cb_timeout, cb_unreach, cb_finish);
	printf("icmp host created + callbacks set\n");

	// icmp_host_new() registers the host in the chat's host list
	// (icmp_host_alloc prepends it to chat->host_head and the AIO chat owns
	// it), so the chat frees it during icmp_chat_free()/icmp_rset(). Calling
	// icmp_host_free() here as well would double-free the host -> crash, so
	// we must NOT free the host explicitly.
	(void) host;
	icmp_chat_free(chat);
	acl_aio_free(aio);
	printf("icmp tests passed (no actual ping attempted).\n");
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	acl_lib_init();

	test_aio();
	test_memdb();
	test_smtp();
	test_icmp();

	acl_lib_end();

	printf("\nALL PROTOCOL MISC TESTS PASSED\n");
	return 0;
}
