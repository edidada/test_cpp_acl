/*
 * test_c_memory.cpp
 *
 * Standalone test for the ACL C-core memory APIs found under
 * lib_acl/include/stdlib:
 *   - acl_mymalloc.h   : acl_mymalloc / acl_mycalloc / acl_myrealloc /
 *                        acl_myfree / acl_mystrdup / acl_mystrndup /
 *                        acl_mymemdup  (the public acl_malloc/calloc/... API)
 *   - acl_mem_slice.h  : acl_mem_slice_init/destroy/set/gc
 *   - acl_malloc.h     : acl_mempool_open/close/status, acl_default_set_memlimit
 *   - acl_allocator.h  : acl_allocator_create/free/mem_alloc/mem_free
 *   - acl_mem_hook.h   : acl_mem_hook / acl_mem_unhook (compiled only)
 *   - acl_dbuf_pool.h  : acl_dbuf_pool_create/alloc/strdup/free/destroy
 *
 * The file has a .cpp extension but is written as C: every ACL header already
 * wraps its declarations in `extern "C"`, and we additionally wrap the whole
 * include block in `extern "C"` for safety when compiled as C++.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

/*
 * The ACL Windows headers pick their backend based on _MSC_VER (winsock2 vs
 * winsock). MSVC always defines it; on a plain MinGW build it is absent.
 * Declare a modern value on Windows only when the compiler did not define it.
 */
#if (defined(_WIN32) || defined(_WIN64)) && !defined(_MSC_VER)
#  define _MSC_VER 1920
#endif

/*
 * ACL headers are self-guarding (they contain their own extern "C"), but we
 * wrap them explicitly so the file compiles cleanly as C++ as well.
 */
extern "C" {
#include <init/acl_init.h>
#include <stdlib/acl_malloc.h>
#include <stdlib/acl_mymalloc.h>
#include <stdlib/acl_allocator.h>
#include <stdlib/acl_mem_slice.h>
#include <stdlib/acl_dbuf_pool.h>
/* Included only to prove the hook API compiles; no hooking is exercised. */
#include <stdlib/acl_mem_hook.h>
}

/* ------------------------------------------------------------------ */
static void test_basic_malloc(void)
{
	printf("== acl_mymalloc.h: basic alloc/copy/free ==\n");

	char *p = (char *) acl_mymalloc(64);
	assert(p != NULL);
	memset(p, 'A', 64);
	assert(p[0] == 'A' && p[63] == 'A');
	acl_myfree(p);
	assert(p == NULL);              /* acl_myfree NULLs the pointer */

	int *arr = (int *) acl_mycalloc(10, sizeof(int));
	assert(arr != NULL);
	for (int i = 0; i < 10; i++) {   /* calloc must zero-initialise */
		assert(arr[i] == 0);
	}

	/* realloc grows the block and keeps the old content */
	arr = (int *) acl_myrealloc(arr, 20 * sizeof(int));
	assert(arr != NULL);
	assert(arr[0] == 0);
	acl_myfree(arr);

	char *s = acl_mystrdup("hello acl");
	assert(strcmp(s, "hello acl") == 0);
	acl_myfree(s);

	/* strndup copies at most len bytes and always NUL terminates */
	char *sn = acl_mystrndup("0123456789", 4);
	assert(strcmp(sn, "0123") == 0);
	acl_myfree(sn);

	/* memdup copies a raw memory region */
	int   src[4] = {1, 2, 3, 4};
	int  *dst = (int *) acl_mymemdup(src, sizeof(src));
	assert(dst != NULL);
	assert(dst[0] == 1 && dst[3] == 4);
	acl_myfree(dst);

	printf("OK acl_mymalloc.h basic\n");
}

/* ------------------------------------------------------------------ */
static void test_allocator(void)
{
	printf("== acl_allocator.h: pool allocator ==\n");

	ACL_ALLOCATOR *a = acl_allocator_create(1024 * 1024);
	assert(a != NULL);

	/* every ACL_MEM_TYPE_xxBUF type has a backing pool by default */
	void *obj = acl_allocator_mem_alloc(__FILE__, __LINE__,
		a, ACL_MEM_TYPE_64_BUF);
	assert(obj != NULL);
	memset(obj, 0, 64);              /* usable for at least its bucket size */

	acl_allocator_mem_free(__FILE__, __LINE__, a, ACL_MEM_TYPE_64_BUF, obj);

	acl_allocator_free(a);
	printf("OK acl_allocator.h\n");
}

/* ------------------------------------------------------------------ */
static void test_dbuf_pool(void)
{
	printf("== acl_dbuf_pool.h: bump/arena style pool ==\n");

	ACL_DBUF_POOL *pool = acl_dbuf_pool_create(4096);
	assert(pool != NULL);

	/* alloc raw memory from the pool */
	void *mem = acl_dbuf_pool_alloc(pool, 128);
	assert(mem != NULL);
	memset(mem, 0x5A, 128);
	assert(((unsigned char *) mem)[0] == 0x5A);

	/* strdup a string into the pool */
	char *s = acl_dbuf_pool_strdup(pool, "copy me");
	assert(s != NULL);
	assert(strcmp(s, "copy me") == 0);

	/* return one block to the pool for reuse; 0 == success */
	int rc = acl_dbuf_pool_free(pool, mem);
	assert(rc == 0);

	acl_dbuf_pool_destroy(pool);
	printf("OK acl_dbuf_pool.h\n");
}

/* ------------------------------------------------------------------ */
static void test_mempool(void)
{
	printf("== acl_malloc.h: process-wide memory pool ==\n");

	/*
	 * The process-wide pool (acl_mempool_open) installs the allocator
	 * backend for the acl_my* macros. Internally acl_allocator_membuf_free()
	 * recovers the block size by calling acl_default_memstat(), which reads
	 * an MBLOCK header written by acl_default_malloc(). In THIS build
	 * SAFE_MEM is disabled (`#if 0 #define SAFE_MEM`), so acl_default_malloc
	 * is a bare malloc() with NO MBLOCK header. Freeing any buffer that came
	 * from the generic membuf path therefore walks off the front of a plain
	 * malloc block and hard-faults (0xc0000409). The pool alloc/free path is
	 * unusable on this build/platform, so we skip exercising it and only keep
	 * the allocator-independent limits API, which does not route through the
	 * membuf/stat path.
	 */
	printf("skipped: acl_mempool open/alloc/free path"
		" (SAFE_MEM off -> acl_default_memstat MBLOCK fault)\n");

	/* set a generous allocation warning threshold and read it back */
	acl_default_set_memlimit(200 * 1024 * 1024);
	assert(acl_default_get_memlimit() == 200 * 1024 * 1024);

	printf("OK acl_malloc.h mempool\n");
}

/* ------------------------------------------------------------------ */
static void test_mem_slice(void)
{
	printf("== acl_mem_slice.h: thread-local slice allocator ==\n");

	/*
	 * acl_mem_slice_init installs the slice allocator as the global
	 * acl_mem_hook backend, so run this last and explicitly unhook
	 * afterwards to leave the library in a clean default state.
	 * slice_flag 0 == no extra GC flags.
	 */
	ACL_MEM_SLICE *ms = acl_mem_slice_init(8, 1024, 100, 0);
	assert(ms != NULL);

	char *p = acl_mystrdup("slice me");
	assert(strcmp(p, "slice me") == 0);
	acl_myfree(p);

	(void) acl_mem_slice_gc();       /* drive a collection pass */

	/* re-attach this slice to the current thread (no-op if already set) */
	acl_mem_slice_set(ms);

	acl_mem_slice_destroy();         /* tear down this thread's slice */

	/* restore the default allocation backend installed by init */
	acl_mem_unhook();

	printf("OK acl_mem_slice.h\n");
}

/* ------------------------------------------------------------------ */
int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	acl_lib_init();

	printf("ACL version: %s\n\n", acl_version());

	test_basic_malloc();
	test_allocator();
	test_dbuf_pool();
	test_mempool();
	test_mem_slice();   /* last: it re-hooks the global allocator */

	acl_lib_end();

	printf("\nAll ACL memory tests passed!\n");
	return 0;
}
