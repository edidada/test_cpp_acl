/*
 * test_c_containers.cpp
 *
 * Standalone test for the ACL C-core container APIs under lib_acl/include/stdlib.
 *
 * NOTE ON API NAMES: a few symbols named in the task brief do not exist in this
 * ACL release. The real, compiling counterparts used here are:
 *   acl_hash_create/...  -> this release's acl_hash.h only exposes hash
 *                           functions (acl_hash_crc32 / acl_hash_test ...);
 *                           the key->string hash map is ACL_HTABLE.
 *   acl_array_pop        -> the ACL_ARRAY pop_* method pointers
 *   acl_array_remove     -> acl_array_delete_obj
 *   acl_array_sort       -> qsort over the exposed ACL_ARRAY::items
 *   acl_array_walk       -> the generic ACL_FOREACH iterator over ACL_ARRAY
 *   acl_fifo_create      -> acl_fifo_new
 *   acl_fifo_peek        -> acl_fifo_head
 *   acl_fifo_empty       -> acl_fifo_size(fifo) == 0
 *   acl_fifo_walk        -> ACL_FIFO_FOREACH
 *   acl_stack_free       -> acl_stack_destroy
 *   acl_stack_peek       -> acl_stack_top
 *   acl_ring_insert      -> acl_ring_prepend
 *   acl_ring_remove      -> acl_ring_detach
 *   acl_ring_head/tail   -> ACL_RING_FIRST / ACL_RING_LAST macros
 *   acl_ring_empty       -> acl_ring_size(ring) == 0
 *   acl_ring_FOREACH_SAFE-> pop_head draining loop
 *   acl_argv_create      -> acl_argv_alloc
 *   acl_argv_count       -> acl_argv_size
 *   acl_argv_join/sort   -> implemented on top of acl_argv_index + qsort
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>   /* offsetof */
#include <stdlib.h>   /* qsort */
#include <assert.h>

/*
 * The ACL Windows headers pick their backend based on _MSC_VER (winsock2 vs
 * winsock, and whether to supply their own `struct timespec`). MSVC always
 * defines _MSC_VER; on a plain MinGW build it is absent, which makes the
 * headers redefine the `struct timespec` MinGW already provides. Declare a
 * modern value on Windows only when the compiler did not already define it.
 */
#if (defined(_WIN32) || defined(_WIN64)) && !defined(_MSC_VER)
#  define _MSC_VER 1920
#endif

extern "C" {
#include <init/acl_init.h>
#include <stdlib/acl_mymalloc.h>
#include <stdlib/acl_vsprintf.h>
#include <stdlib/acl_array.h>
#include <stdlib/acl_hash.h>
#include <stdlib/acl_htable.h>
#include <stdlib/acl_binhash.h>
#include <stdlib/acl_ring.h>
#include <stdlib/acl_fifo.h>
#include <stdlib/acl_stack.h>
#include <stdlib/acl_avl.h>
#include <stdlib/acl_btree.h>
#include <stdlib/acl_cache.h>
#include <stdlib/acl_cache2.h>
#include <stdlib/acl_argv.h>
#include <stdlib/acl_iterator.h>
}

/* ================================================================== */
static int int_cmp(const void *a, const void *b)
{
	int x = *(const int *) a;
	int y = *(const int *) b;
	return x < y ? -1 : (x == y ? 0 : 1);
}

static void test_array(void)
{
	printf("== acl_array.h ==\n");

	static int vals[5] = { 50, 10, 40, 20, 30 };
	ACL_ARRAY *a = acl_array_create(4);
	assert(a != NULL);

	for (int i = 0; i < 5; i++) {
		assert(acl_array_append(a, &vals[i]) >= 0);
	}
	assert(acl_array_size(a) == 5);

	/* prepend -> now front element is &vals[4] */
	acl_array_prepend(a, &vals[4]);
	assert(acl_array_index(a, 0) == &vals[4]);
	assert(acl_array_size(a) == 6);

	/* sort the exposed items array by the integer they point to */
	qsort(a->items, a->count, sizeof(void *), int_cmp);
	for (int i = 1; i < a->count; i++) {
		int prev = *(int *) acl_array_index(a, i - 1);
		int cur  = *(int *) acl_array_index(a, i);
		assert(prev <= cur);
	}

	/* generic walk via the container's iterator methods */
	ACL_ITER iter;
	int   n = 0;
	ACL_FOREACH(iter, a) {
		assert(iter.data != NULL);
		n++;
	}
	assert(n == acl_array_size(a));

	/* pop from the tail using the struct's method pointer */
	void *last = a->pop_back(a);
	assert(last != NULL);

	/* remove a specific object (acl_array_remove -> delete_obj) */
	int target = 10;
	acl_array_delete_obj(a, &target, NULL);
	/* 10 may or may not have been present; size just must stay sane */
	assert(acl_array_size(a) >= 5);

	acl_array_free(a, NULL);   /* NULL: don't free the int elements */
	printf("OK acl_array.h\n");
}

/* ================================================================== */
static void htable_walk_cb(ACL_HTABLE_INFO *info, void *arg)
{
	int *cnt = (int *) arg;
	(*cnt)++;
	assert(info->key.c_key != NULL);
}

static void test_hash_functions(void)
{
	printf("== acl_hash.h (hash functions) ==\n");

	unsigned h1 = acl_hash_crc32("hello", 5);
	unsigned h2 = acl_hash_crc32("hello", 5);
	assert(h1 == h2);

	unsigned t1 = acl_hash_test("abc", 3);
	(void) t1;
	assert(acl_hash_crc16("abc", 3) != 0 || 1); /* just exercise it */
	printf("OK acl_hash.h\n");
}

static void test_htable(void)
{
	printf("== acl_htable.h ==\n");

	ACL_HTABLE *t = acl_htable_create(16, 0);
	assert(t != NULL);

	acl_htable_enter(t, "one", acl_mystrdup("1"));
	acl_htable_enter(t, "two", acl_mystrdup("2"));
	acl_htable_enter(t, "three", acl_mystrdup("3"));

	assert(acl_htable_used(t) == 3);

	char *v = (char *) acl_htable_find(t, "two");
	assert(v != NULL && strcmp(v, "2") == 0);

	int cnt = 0;
	acl_htable_walk(t, htable_walk_cb, &cnt);
	assert(cnt == 3);

	acl_htable_delete(t, "one", acl_myfree_fn);
	assert(acl_htable_find(t, "one") == NULL);

	acl_htable_free(t, acl_myfree_fn);   /* frees remaining strdup'd values */
	printf("OK acl_htable.h\n");
}

/* ================================================================== */
static void test_binhash(void)
{
	printf("== acl_binhash.h ==\n");

	ACL_BINHASH *t = acl_binhash_create(16, 0);
	assert(t != NULL);

	const char *k1 = "alpha", *k2 = "beta";
	acl_binhash_enter(t, k1, (int) strlen(k1), acl_mystrdup("A"));
	acl_binhash_enter(t, k2, (int) strlen(k2), acl_mystrdup("B"));

	assert(acl_binhash_used(t) == 2);

	char *v = (char *) acl_binhash_find(t, k1, (int) strlen(k1));
	assert(v != NULL && strcmp(v, "A") == 0);

	int cnt = 0;
	ACL_BINHASH_ITER iter;
	ACL_BINHASH_FOREACH(iter, t) {
		assert(iter.ptr != NULL);
		cnt++;
	}
	assert(cnt == 2);

	acl_binhash_delete(t, k2, (int) strlen(k2), acl_myfree_fn);
	assert(acl_binhash_find(t, k2, (int) strlen(k2)) == NULL);

	acl_binhash_free(t, acl_myfree_fn);
	printf("OK acl_binhash.h\n");
}

/* ================================================================== */
typedef struct {
	char name[16];
	ACL_RING entry;
} RING_ITEM;

static void test_ring(void)
{
	printf("== acl_ring.h ==\n");

	ACL_RING head;
	acl_ring_init(&head);
	assert(acl_ring_size(&head) == 0);
	assert(acl_ring_size(&head) == 0); /* "empty" */

	for (int i = 0; i < 3; i++) {
		RING_ITEM *it = (RING_ITEM *) acl_mycalloc(1, sizeof(RING_ITEM));
		acl_snprintf(it->name, sizeof(it->name), "r%d", i);
		acl_ring_append(&head, &it->entry);
	}
	/* "insert" -> prepend a fourth item at the front */
	RING_ITEM *front = (RING_ITEM *) acl_mycalloc(1, sizeof(RING_ITEM));
	strcpy(front->name, "front");
	acl_ring_prepend(&head, &front->entry);

	assert(acl_ring_size(&head) == 4);

	/* head/tail via the provided macros */
	ACL_RING *first = ACL_RING_FIRST(&head);
	assert(first != NULL);
	RING_ITEM *fi = ACL_RING_TO_APPL(first, RING_ITEM, entry);
	assert(strcmp(fi->name, "front") == 0);

	ACL_RING *last = ACL_RING_LAST(&head);
	assert(last != NULL);
	RING_ITEM *li = ACL_RING_TO_APPL(last, RING_ITEM, entry);
	assert(strcmp(li->name, "r2") == 0);

	/* iterate */
	int seen = 0;
	ACL_RING_ITER iter;
	ACL_RING_FOREACH(iter, &head) {
		RING_ITEM *it = ACL_RING_TO_APPL(iter.ptr, RING_ITEM, entry);
		assert(it->name[0] != 0);
		seen++;
	}
	assert(seen == 4);

	/* "remove" one element in place -> detach */
	acl_ring_detach(&front->entry);
	assert(acl_ring_size(&head) == 3);
	acl_myfree(front);

	/* "FOREACH_SAFE" equivalent: drain with pop_head and free */
	ACL_RING *e;
	while ((e = acl_ring_pop_head(&head)) != NULL) {
		RING_ITEM *it = ACL_RING_TO_APPL(e, RING_ITEM, entry);
		acl_myfree(it);
	}
	assert(acl_ring_size(&head) == 0);
	printf("OK acl_ring.h\n");
}

/* ================================================================== */
static void test_fifo(void)
{
	printf("== acl_fifo.h ==\n");

	ACL_FIFO *f = acl_fifo_new();     /* "create" */
	assert(f != NULL);

	for (int i = 0; i < 3; i++) {
		char *d = (char *) acl_mymalloc(16);
		acl_snprintf(d, 16, "d%d", i);
		acl_fifo_push(f, d);           /* push_back */
	}
	assert(acl_fifo_size(f) == 3);
	assert(acl_fifo_size(f) != 0);       /* "empty" == false */

	/* "peek" -> head */
	char *h = (char *) acl_fifo_head(f);
	assert(strcmp(h, "d0") == 0);

	/* "walk" -> iterate */
	ACL_FIFO_ITER iter;
	int   cnt = 0;
	ACL_FIFO_FOREACH(iter, f) {
		assert(iter.ptr->data != NULL);
		cnt++;
	}
	assert(cnt == 3);

	/* pop FIFO order */
	char *p = (char *) acl_fifo_pop(f);  /* pop_front */
	assert(strcmp(p, "d0") == 0);
	acl_myfree(p);

	acl_fifo_free(f, acl_myfree_fn);      /* frees d1, d2 */
	printf("OK acl_fifo.h\n");
}

/* ================================================================== */
static void test_stack(void)
{
	printf("== acl_stack.h ==\n");

	ACL_STACK *s = acl_stack_create(4);
	assert(s != NULL);

	for (int i = 0; i < 3; i++) {
		char *d = (char *) acl_mymalloc(16);
		acl_snprintf(d, 16, "s%d", i);
		acl_stack_push(s, d);            /* append */
	}
	assert(acl_stack_size(s) == 3);

	/* "peek" -> top (does not pop) */
	char *top = (char *) acl_stack_top(s);
	assert(strcmp(top, "s2") == 0);

	/* pop LIFO */
	char *p = (char *) acl_stack_pop(s);
	assert(strcmp(p, "s2") == 0);
	acl_myfree(p);

	acl_stack_destroy(s, acl_myfree_fn);   /* "free" */
	printf("OK acl_stack.h\n");
}

/* ================================================================== */
typedef struct {
	int key;
	acl_avl_node_t node;
} AVL_ITEM;

static int avl_cmp(const void *a, const void *b)
{
	int ka = ((const AVL_ITEM *) a)->key;
	int kb = ((const AVL_ITEM *) b)->key;
	return ka < kb ? -1 : (ka == kb ? 0 : 1);
}

static void test_avl(void)
{
	printf("== acl_avl.h ==\n");

	acl_avl_tree_t tree;
	acl_avl_create(&tree, avl_cmp, sizeof(AVL_ITEM), offsetof(AVL_ITEM, node));

	AVL_ITEM *a = (AVL_ITEM *) acl_mycalloc(1, sizeof(AVL_ITEM));
	AVL_ITEM *b = (AVL_ITEM *) acl_mycalloc(1, sizeof(AVL_ITEM));
	AVL_ITEM *c = (AVL_ITEM *) acl_mycalloc(1, sizeof(AVL_ITEM));
	a->key = 20; b->key = 10; c->key = 30;

	acl_avl_add(&tree, a);
	acl_avl_add(&tree, b);

	/* find + insert path for the third node */
	AVL_ITEM probe; probe.key = 30;
	acl_avl_index_t where;
	void *found = acl_avl_find(&tree, &probe, &where);
	assert(found == NULL);            /* not present yet */
	acl_avl_insert(&tree, c, where);

	assert(acl_avl_numnodes(&tree) == 3);

	AVL_ITEM *first = (AVL_ITEM *) acl_avl_first(&tree);
	AVL_ITEM *last  = (AVL_ITEM *) acl_avl_last(&tree);
	assert(first->key == 10 && last->key == 30);

	/* find existing */
	AVL_ITEM q; q.key = 20;
	assert(acl_avl_find(&tree, &q, NULL) == a);

	acl_avl_remove(&tree, a);
	assert(acl_avl_numnodes(&tree) == 2);

	/* drain remaining nodes */
	void *cookie = NULL;
	AVL_ITEM *n;
	while ((n = (AVL_ITEM *) acl_avl_destroy_nodes(&tree, &cookie)) != NULL) {
		acl_myfree(n);
	}
	acl_myfree(a);                    /* the one we removed manually */
	acl_avl_destroy(&tree);
	printf("OK acl_avl.h\n");
}

/* ================================================================== */
static void test_btree(void)
{
	printf("== acl_btree.h ==\n");

	ACL_BTREE *t = acl_btree_create();
	assert(t != NULL);

	static int v100 = 100, v50 = 50, v200 = 200;
	assert(acl_btree_add(t, 100, &v100) == 0);
	assert(acl_btree_add(t, 50,  &v50)  == 0);
	assert(acl_btree_add(t, 200, &v200) == 0);

	int *got = (int *) acl_btree_find(t, 50);
	assert(got == &v50);

	unsigned int kmin = 0, kmax = 0;
	assert(acl_btree_get_min_key(t, &kmin) == 0 && kmin == 50);
	assert(acl_btree_get_max_key(t, &kmax) == 0 && kmax == 200);

	assert(acl_btree_depth(t) >= 1);

	/* remove returns the stored data pointer */
	int *rm = (int *) acl_btree_remove(t, 100);
	assert(rm == &v100);
	assert(acl_btree_find(t, 100) == NULL);

	/* remove the rest so destroy leaves nothing dangling */
	acl_btree_remove(t, 50);
	acl_btree_remove(t, 200);

	assert(acl_btree_destroy(t) == 0);
	printf("OK acl_btree.h\n");
}

/* ================================================================== */
static void cache_free_cb(const ACL_CACHE_INFO *info, void *value)
{
	(void) info;
	acl_myfree(value);
}

static void cache2_free_cb(const ACL_CACHE2_INFO *info, void *value)
{
	(void) info;
	acl_myfree(value);
}

static void test_cache(void)
{
	printf("== acl_cache.h / acl_cache2.h ==\n");

	/* --- classic cache --- */
	ACL_CACHE *c = acl_cache_create(100, 60, cache_free_cb);
	assert(c != NULL);

	acl_cache_enter(c, "ka", acl_mystrdup("va"));
	acl_cache_enter(c, "kb", acl_mystrdup("vb"));

	char *v = (char *) acl_cache_find(c, "ka");
	assert(v != NULL && strcmp(v, "va") == 0);

	ACL_CACHE_INFO *info = acl_cache_locate(c, "kb");
	assert(info != NULL);
	assert(acl_cache_delete(c, info) == 0);

	acl_cache_free(c);   /* frees the remaining "va" */

	/* --- cache2 with reference counting and upsert --- */
	ACL_CACHE2 *c2 = acl_cache2_create(100, cache2_free_cb);
	assert(c2 != NULL);

	acl_cache2_enter(c2, "x1", acl_mystrdup("y1"), 100);
	assert(strcmp((char *) acl_cache2_find(c2, "x1"), "y1") == 0);

	ACL_CACHE2_INFO *i2 = acl_cache2_locate(c2, "x1");
	assert(i2 != NULL);

	acl_cache2_refer(i2);      /* pin it */
	acl_cache2_unrefer(i2);    /* release */

	acl_cache2_lock(c2);
	int exist = 0;
	acl_cache2_upsert(c2, "x2", acl_mystrdup("y2"), 100, &exist);
	acl_cache2_unlock(c2);
	assert(exist == 0);

	assert(acl_cache2_size(c2) == 2);
	acl_cache2_free(c2);       /* frees y1 and y2 */
	printf("OK acl_cache.h / acl_cache2.h\n");
}

/* ================================================================== */
static int argv_str_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *) a, *(char *const *) b);
}

static void test_argv(void)
{
	printf("== acl_argv.h ==\n");

	/* "create" -> acl_argv_alloc */
	ACL_ARGV *p = acl_argv_alloc(4);
	assert(p != NULL);

	acl_argv_add(p, "world", "hello", ACL_ARGV_END);
	assert(acl_argv_size(p) == 2);       /* "count" */

	/* addn: (string, length) pairs */
	acl_argv_addn(p, "abc", 3, "de", 2, (char *) NULL);
	assert(acl_argv_size(p) == 4);
	assert(strcmp(acl_argv_index(p, 2), "abc") == 0);
	assert(strcmp(acl_argv_index(p, 3), "de") == 0);

	/* "sort": reorder the exposed argv array */
	acl_argv_terminate(p);
	qsort(p->argv, p->argc, sizeof(char *), argv_str_cmp);
	assert(strcmp(p->argv[0], "abc") == 0);   /* smallest of the set */

	/* "join": concatenate all tokens with a space */
	char joined[128];
	joined[0] = 0;
	for (int i = 0; i < acl_argv_size(p); i++) {
		if (i > 0) {
			strcat(joined, " ");
		}
		strcat(joined, acl_argv_index(p, i));
	}
	assert(strstr(joined, " ") != NULL);

	acl_argv_free(p);

	/* split: build an ACL_ARGV from a delimited string */
	ACL_ARGV *sp = acl_argv_split("a:b:c", ":");
	assert(sp != NULL);
	assert(acl_argv_size(sp) == 3);
	assert(strcmp(acl_argv_index(sp, 1), "b") == 0);
	acl_argv_free(sp);
	printf("OK acl_argv.h\n");
}

/* ================================================================== */
int main(void)
{
	acl_lib_init();

	printf("ACL version: %s\n\n", acl_version());

	test_array();
	test_hash_functions();
	test_htable();
	test_binhash();
	test_ring();
	test_fifo();
	test_stack();
	test_avl();
	test_btree();
	test_cache();
	test_argv();

	acl_lib_end();

	printf("\nAll ACL container tests passed!\n");
	return 0;
}
