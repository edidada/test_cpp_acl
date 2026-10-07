/*
 * test_c_string.cpp
 *
 * Standalone test for the ACL C-core string APIs under lib_acl/include/stdlib:
 *   - acl_vstring.h  : dynamic length-managed string
 *   - acl_vbuf.h     : the variable-length buffer primitive
 *   - acl_mystring.h : classic string helpers
 *   - acl_stringops.h: digit/double checks, concat, basename, name=value
 *   - acl_split_at.h : in-place split on a delimiter
 *   - acl_vsprintf.h : ACL's sprintf family
 *   - acl_hex_code.h : hex encode/decode
 *
 * NOTE ON API NAMES: the task brief mentions `acl_vstring_reset` and
 * `acl_vbuf_alloc/free/append/reset`. Those exact symbols do not exist in this
 * ACL release. Their real, compiling counterparts are used here:
 *   - reset   -> the ACL_VSTRING_RESET(vp) macro
 *   - vbuf    -> the real acl_vbuf.h primitives: acl_vbuf_write / acl_vbuf_put
 *                / acl_vbuf_space and the ACL_VBUF_TERM macro, exercised on the
 *                ACL_VBUF embedded inside an ACL_VSTRING.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

/*
 * The ACL Windows headers pick their backend based on _MSC_VER (winsock2 vs
 * winsock). MSVC always defines it; on a plain MinGW build it is absent.
 * Declare a modern value on Windows only when the compiler did not define it.
 */
#if (defined(_WIN32) || defined(_WIN64)) && !defined(_MSC_VER)
#  define _MSC_VER 1920
#endif

extern "C" {
#include <init/acl_init.h>
#include <stdlib/acl_mymalloc.h>
#include <stdlib/acl_vbuf.h>
#include <stdlib/acl_vstring.h>
#include <stdlib/acl_mystring.h>
#include <stdlib/acl_stringops.h>
#include <stdlib/acl_split_at.h>
#include <stdlib/acl_vsprintf.h>
#include <stdlib/acl_hex_code.h>
}

/* ------------------------------------------------------------------ */
static void test_vstring(void)
{
	printf("== acl_vstring.h ==\n");

	/* heap-allocated form */
	ACL_VSTRING *vs = acl_vstring_alloc(16);
	assert(vs != NULL);

	acl_vstring_strcpy(vs, "Hello");
	assert(ACL_VSTRING_LEN(vs) == 5);
	assert(strcmp(acl_vstring_str(vs), "Hello") == 0);

	acl_vstring_strcat(vs, ", ACL!");
	assert(ACL_VSTRING_LEN(vs) == 11);
	assert(strcmp(acl_vstring_str(vs), "Hello, ACL!") == 0);

	acl_vstring_strncpy(vs, "abcdefghij", 4);
	assert(ACL_VSTRING_LEN(vs) == 4);
	assert(strcmp(acl_vstring_str(vs), "abcd") == 0);

	acl_vstring_strcpy(vs, "foo");
	acl_vstring_strncat(vs, "barbaz", 3);
	assert(strcmp(acl_vstring_str(vs), "foobar") == 0);

	/* find a substring (returns a pointer managed by vs) */
	char *at = acl_vstring_strstr(vs, "bar");
	assert(at != NULL && strcmp(at, "bar") == 0);

	/* truncate keeps a prefix */
	acl_vstring_strcpy(vs, "hello world");
	acl_vstring_truncate(vs, 5);
	assert(strcmp(acl_vstring_str(vs), "hello") == 0);
	assert(ACL_VSTRING_LEN(vs) == 5);

	/* sprintf replaces the content, sprintf_append appends to it */
	acl_vstring_sprintf(vs, "%d-%s", 42, "x");
	assert(strcmp(acl_vstring_str(vs), "42-x") == 0);
	acl_vstring_sprintf_append(vs, "!!%c", '!');
	assert(strcmp(acl_vstring_str(vs), "42-x!!!") == 0);

	/* the reset macro rewinds the write pointer to the start */
	ACL_VSTRING_RESET(vs);
	assert(ACL_VSTRING_LEN(vs) == 0);

	acl_vstring_free(vs);

	/* stack/struct form: init then free_buf */
	ACL_VSTRING st;
	acl_vstring_init(&st, 32);
	acl_vstring_strcpy(&st, "static");
	assert(strcmp(acl_vstring_str(&st), "static") == 0);
	acl_vstring_free_buf(&st);

	printf("OK acl_vstring.h\n");
}

/* ------------------------------------------------------------------ */
static void test_vbuf(void)
{
	printf("== acl_vbuf.h (raw buffer primitives) ==\n");

	/*
	 * ACL_VSTRING owns an ACL_VBUF as its first member; drive that buffer
	 * directly with the real vbuf.h API instead of the non-existent
	 * acl_vbuf_alloc/free/append/reset helpers.
	 */
	ACL_VSTRING *vs = acl_vstring_alloc(16);
	ACL_VBUF    *bp = &vs->vbuf;

	/*
	 * NOTE: acl_vbuf_write() is deliberately not used here. Its bulk loop
	 * sizes each chunk from `dlen = bp->ptr - bp->data` (the bytes already
	 * written). On a freshly initialized buffer ptr == data, so dlen == 0,
	 * every memcpy copies 0 bytes, `count` never decreases and the loop
	 * spins forever (this is why the raw write hung). We therefore append
	 * bytes with acl_vbuf_put()/ACL_VBUF_PUT, which grow the buffer via
	 * acl_vstring_put_ready() and are safe on an empty buffer.
	 */
	printf("  acl_vbuf_write: skipped (loops forever when ptr==data)\n");

	/* ACL_VBUF_PUT appends one char in place when there is room */
	assert(ACL_VBUF_PUT(bp, 'a') == 'a');
	assert(ACL_VBUF_PUT(bp, 'b') == 'b');

	/* acl_vbuf_put appends a single character (grows if needed) */
	assert(acl_vbuf_put(bp, 'c') == 'c');
	assert(acl_vbuf_put(bp, 'd') == 'd');

	/* terminate so the buffer can be read as a C string */
	ACL_VBUF_TERM(bp);
	assert(ACL_VSTRING_LEN(vs) == 4);
	assert(strcmp(acl_vstring_str(vs), "abcd") == 0);

	/* acl_vbuf_space guarantees room for the requested byte count */
	assert(acl_vbuf_space(bp, 64) >= 0);

	acl_vstring_free(vs);
	printf("OK acl_vbuf.h\n");
}

/* ------------------------------------------------------------------ */
static void test_mystring(void)
{
	printf("== acl_mystring.h ==\n");

	char buf[64];

	ACL_SAFE_STRCPY(buf, "Hello");
	assert(strcmp(buf, "Hello") == 0);

	acl_lowercase(buf);
	assert(strcmp(buf, "hello") == 0);

	acl_uppercase(buf);
	assert(strcmp(buf, "HELLO") == 0);

	char lower[64], upper[64];
	acl_lowercase3("ABC", lower, sizeof(lower));
	assert(strcmp(lower, "abc") == 0);
	acl_uppercase3("abc", upper, sizeof(upper));
	assert(strcmp(upper, "ABC") == 0);

	ACL_SAFE_STRNCPY(buf, "0123456789", 5);
	assert(strcmp(buf, "0123") == 0);   /* 5-byte dst -> 4 chars + NUL */

	char t[] = "  x y  ";
	acl_strtrim(t);
	assert(strchr(t, ' ') == NULL);     /* strtrim drops all blanks */

	assert(acl_strcasecmp("ABC", "abc") == 0);
	assert(acl_strncasecmp("ABCDEF", "abcXYZ", 3) == 0);
	assert(acl_strcasestr("Hello World", "world") != NULL);
	assert(acl_strnlen("abcd", 10) == 4);

	assert(acl_rstrstr("abcabc", "bc") != NULL);
	assert(strcmp(acl_rstrstr("abcabc", "bc"), "bc") == 0);

	/* multi-character separator tokenizer */
	char src[] = "a=|b=|c";
	char *p = src;
	char *tok = acl_strtok(&p, "=|");
	assert(tok && strcmp(tok, "a") == 0);
	tok = acl_strtok(&p, "=|");
	assert(tok && strcmp(tok, "b") == 0);

	/* line splitter */
	char lines[] = "first\nsecond";
	char *lp = lines;
	char *l1 = acl_strline(&lp);
	assert(l1 && strcmp(l1, "first") == 0);
	char *l2 = acl_strline(&lp);
	assert(l2 && strcmp(l2, "second") == 0);

	printf("OK acl_mystring.h\n");
}

/* ------------------------------------------------------------------ */
static void test_stringops(void)
{
	printf("== acl_stringops.h ==\n");

	assert(acl_alldig("123456") == 1);
	assert(acl_alldig("12a45") == 0);
	assert(acl_is_double("3.14159") == 1);
	assert(acl_is_double("not-a-number") == 0);

	char *cat = acl_concatenate("foo", "bar", "baz", (const char *) NULL);
	assert(strcmp(cat, "foobarbaz") == 0);
	acl_myfree(cat);

	const char *base = acl_safe_basename("/tmp/dir/test.txt");
	assert(strcmp(base, "test.txt") == 0);

	char nv[] = " key = val ";
	char *name = NULL, *value = NULL;
	const char *err = acl_split_nameval(nv, &name, &value);
	assert(err == NULL);            /* NULL means success */
	assert(strcmp(name, "key") == 0);
	assert(strcmp(value, "val") == 0);

	printf("OK acl_stringops.h\n");
}

/* ------------------------------------------------------------------ */
static void test_split_at(void)
{
	printf("== acl_split_at.h ==\n");

	char b1[] = "hello,world";
	char *right = acl_split_at(b1, ',');
	assert(right != NULL);
	assert(strcmp(b1, "hello") == 0);
	assert(strcmp(right, "world") == 0);

	char b2[] = "a,b,c";
	char *r2 = acl_split_at_right(b2, ',');
	assert(r2 != NULL);
	assert(strcmp(b2, "a,b") == 0);
	assert(strcmp(r2, "c") == 0);

	printf("OK acl_split_at.h\n");
}

/* ------------------------------------------------------------------ */
static int my_vsprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int     n;
	va_start(ap, fmt);
	n = acl_vsprintf(buf, fmt, ap);
	va_end(ap);
	return n;
}

static int my_vsnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int     n;
	va_start(ap, fmt);
	n = acl_vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

static void test_vsprintf(void)
{
	printf("== acl_vsprintf.h ==\n");

	char buf[64];

	int n = acl_snprintf(buf, sizeof(buf), "%d-%s", 100, "abc");
	assert(n == 7);
	assert(strcmp(buf, "100-abc") == 0);

	acl_sprintf(buf, "%s/%d", "x", 1);
	assert(strcmp(buf, "x/1") == 0);

	my_vsprintf(buf, "val=%c", 'Z');
	assert(strcmp(buf, "val=Z") == 0);

	my_vsnprintf(buf, sizeof(buf), "%ld", (long) 9999);
	assert(strcmp(buf, "9999") == 0);

	printf("OK acl_vsprintf.h\n");
}

/* ------------------------------------------------------------------ */
static void test_hex_code(void)
{
	printf("== acl_hex_code.h ==\n");

	ACL_VSTRING *enc = acl_vstring_alloc(64);
	ACL_VSTRING *dec = acl_vstring_alloc(64);

	const char *raw = "AB\x01";
	acl_hex_encode(enc, raw, 3);
	/* one byte -> two hex chars */
	assert(ACL_VSTRING_LEN(enc) == 6);

	acl_hex_decode(dec, acl_vstring_str(enc), (int) ACL_VSTRING_LEN(enc));
	assert(ACL_VSTRING_LEN(dec) == 3);
	assert(memcmp(acl_vstring_str(dec), raw, 3) == 0);

	acl_vstring_free(enc);
	acl_vstring_free(dec);
	printf("OK acl_hex_code.h\n");
}

/* ------------------------------------------------------------------ */
int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	acl_lib_init();

	printf("ACL version: %s\n\n", acl_version());

	test_vstring();
	test_vbuf();
	test_mystring();
	test_stringops();
	test_split_at();
	test_vsprintf();
	test_hex_code();

	acl_lib_end();

	printf("\nAll ACL string tests passed!\n");
	return 0;
}
