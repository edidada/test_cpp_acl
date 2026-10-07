/*
 * test_c_event.cpp
 *
 * C event-driven self test for the ACL core library (lib_acl).
 *
 * NOTE: the reference doc's event surface does not fully match this ACL
 * version, so the verified real symbols are used:
 *   - acl_event_new(const char*, int)  -> acl_event_new(int mode, int use_thr,
 *                                               int delay_sec, int delay_usec)
 *   - acl_event_name()                  -> acl_event_mode() (no name accessor)
 *   - acl_event_exit_loop()             -> acl_event_loop() runs ONE pass in
 *                                          this version, so the loop is driven
 *                                          by a bounded while() plus a timer.
 *   - acl_event_request_timer(ev,cb,a,d)-> request_timer(ev,cb,a,delay_us,keep)
 *   - acl_astream_open/acl_astream_close-> acl_aio_create/acl_aio_open/acl_aio_free
 *
 * All tests are guaranteed to terminate (bounded loop iteration + real timers).
 */

#if defined(_WIN32) || defined(_WIN64)
/* MinGW-w64 defines struct timezone/timespec under these include guards;
 * pre-defining them lets ACL's own definitions win without a redefinition
 * clash, so this file compiles out-of-the-box under MinGW. */
# define _TIMEZONE_DEFINED
# define _TIMESPEC_DEFINED
#endif

#include <lib_acl.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

/* ---- timer plumbing -------------------------------------------------- */
static volatile int g_timer_fired = 0;
static volatile int g_timer_count = 0;

static void my_timer_cb(int event_type, ACL_EVENT *event, void *context)
{
	(void) event_type;
	(void) event;
	int *arg = (int *) context;

	g_timer_fired = 1;
	g_timer_count++;
	if (arg != NULL) {
		(*arg)++;
	}
	printf("  timer fired (arg=%d, total=%d)\n",
		arg ? *arg : -1, g_timer_count);
}

/* ---- read-event plumbing -------------------------------------------- */
static volatile int    g_read_fired = 0;
static char            g_read_buf[64];
static int             g_read_len   = 0;

static void my_read_cb(int event_type, ACL_EVENT *event,
	ACL_VSTREAM *stream, void *context)
{
	(void) context;

	if (event_type & (ACL_EVENT_READ | ACL_EVENT_XCPT | ACL_EVENT_RW_TIMEOUT)) {
		memset(g_read_buf, 0, sizeof(g_read_buf));
		g_read_len = acl_vstream_read(stream, g_read_buf, sizeof(g_read_buf) - 1);
		printf("  read event: type=0x%x len=%d data=%.*s\n",
			event_type, g_read_len, g_read_len > 0 ? g_read_len : 0, g_read_buf);
		g_read_fired = 1;
		/* stop watching so the loop can drain and terminate */
		acl_event_disable_read(event, stream);
	}
}

/* ---- event create/free/mode ----------------------------------------- */
static void test_event_create(const char *label, int mode)
{
	printf("--- event create: %s ---\n", label);

	ACL_EVENT *event = acl_event_new(mode, 0, 1, 0);
	if (event == NULL) {
		printf("  acl_event_new(%s) not available on this platform - skip\n",
			label);
		return;
	}

	assert(event != NULL);
	printf("  created, actual mode=%d (requested %d), use_thread=%d\n",
		acl_event_mode(event), mode, acl_event_use_thread(event));

	/* create/select-specific constructors also exist */
	ACL_EVENT *ev2 = acl_event_new_select(1, 0);
	assert(ev2 != NULL);
	printf("  acl_event_new_select ok, mode=%d\n", acl_event_mode(ev2));
	acl_event_free(ev2);

	acl_event_free(event);
	printf("  freed ok\n");
}

/* ---- timer: fire once then bounded loop exits ----------------------- */
static void test_timer(void)
{
	printf("--- timer (100ms, one-shot) ---\n");

	ACL_EVENT *event = acl_event_new_select(1, 0);
	assert(event != NULL);

	int my_counter = 0;
	g_timer_fired = 0;
	g_timer_count = 0;

	/* delay is in microseconds; 100000 us = 100ms; keep=0 -> fire once */
	acl_int64 when = acl_event_request_timer(event, my_timer_cb, &my_counter,
		(acl_int64) 100 * 1000, 0);
	printf("  timer scheduled, when=%lld us\n", (long long) when);

	/* acl_event_loop() processes a single pass; iterate until the timer
	 * fires (bounded so we can never hang). */
	int guard = 0;
	while (!g_timer_fired && guard++ < 10000) {
		acl_event_loop(event);
	}

	assert(g_timer_fired == 1);
	assert(my_counter == 1);
	printf("  timer loop finished after %d passes\n", guard);

	/* cancel any (already-fired) timer; safe on one-shot timers */
	acl_event_cancel_timer(event, my_timer_cb, &my_counter);

	acl_event_free(event);
}

/* ---- enable_read / disable_read on a socketpair --------------------- */
static void test_read_event(void)
{
	printf("--- enable_read / disable_read (socketpair) ---\n");

	ACL_SOCKET sv[2];
	if (acl_sane_socketpair(AF_INET, SOCK_STREAM, 0, sv) != 0) {
		printf("  acl_sane_socketpair failed - skip read-event test\n");
		return;
	}

	ACL_VSTREAM *rd = acl_vstream_fdopen(sv[0], O_RDWR, 1024, 5,
		ACL_VSTREAM_TYPE_SOCK);
	if (rd == NULL) {
		printf("  fdopen failed - skip\n");
		acl_socket_close(sv[0]);
		acl_socket_close(sv[1]);
		return;
	}

	ACL_EVENT *event = acl_event_new_select(1, 0);
	assert(event != NULL);

	/* stage data so the read event is immediately ready (no blocking) */
	const char *hello = "read-me";
	/* write directly on the peer socket (returns 0 on success) */
	assert(acl_socket_write(sv[1], hello, strlen(hello), 5, NULL, NULL) == 0);

	g_read_fired = 0;
	acl_event_enable_read(event, rd, 5, my_read_cb, NULL);

	/* drain bounded; the read callback disables the watch and sets flag */
	int guard = 0;
	while (!g_read_fired && guard++ < 10000) {
		acl_event_loop(event);
	}

	assert(g_read_fired == 1);
	assert(g_read_len == (int) strlen(hello));
	assert(memcmp(g_read_buf, hello, strlen(hello)) == 0);
	printf("  read-event delivered %d bytes\n", g_read_len);

	/* explicit disable (idempotent after the callback already disabled) */
	acl_event_disable_read(event, rd);

	acl_event_free(event);
	acl_vstream_fclose(rd);        /* closes sv[0] */
	acl_socket_close(sv[1]);
	printf("  read-event teardown ok\n");
}

/* ---- astream (via ACL_AIO) open / close ----------------------------- */
static void test_astream(void)
{
	printf("--- astream open/close (via acl_aio) ---\n");

	ACL_AIO *aio = acl_aio_create(ACL_EVENT_SELECT);
	assert(aio != NULL);

	/* an event object is embedded in the aio */
	ACL_EVENT *event = acl_aio_event(aio);
	assert(event != NULL);
	printf("  aio event mode=%d\n", acl_event_mode(event));

	/* open an asynchronous stream over a socketpair-backed vstream */
	ACL_SOCKET sv[2];
	if (acl_sane_socketpair(AF_INET, SOCK_STREAM, 0, sv) != 0) {
		printf("  acl_sane_socketpair failed - skip astream test\n");
		acl_aio_free(aio);
		return;
	}

	ACL_VSTREAM *stm = acl_vstream_fdopen(sv[0], O_RDWR, 1024, 5,
		ACL_VSTREAM_TYPE_SOCK);
	if (stm == NULL) {
		printf("  fdopen failed - skip astream test\n");
		acl_socket_close(sv[0]);
		acl_socket_close(sv[1]);
		acl_aio_free(aio);
		return;
	}

	ACL_ASTREAM *as = acl_aio_open(aio, stm);
	assert(as != NULL);
	printf("  acl_aio_open ok, refer=%d\n", acl_aio_refer_value(as));

	acl_aio_free(aio);           /* frees aio + its embedded event + astreams */
	acl_vstream_fclose(stm);     /* closes sv[0] */
	acl_socket_close(sv[1]);
	printf("  astream close ok\n");
}

int main(void)
{
	acl_lib_init();

	printf("==== ACL C event tests ====\n");

	test_event_create("select", ACL_EVENT_SELECT);
	test_event_create("poll",   ACL_EVENT_POLL);
	test_timer();
	test_read_event();
	test_astream();

	acl_lib_end();

	printf("All event tests passed!\n");
	return 0;
}
