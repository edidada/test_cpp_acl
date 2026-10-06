# ACL 库 API 参考

> ACL (Advanced Communication Library) 是一个跨平台（Linux / Windows / macOS / FreeBSD / Android / HarmonyOS）的 C/C++ 网络通信框架与服务端开发库。

## 官方资源

| 资源 | 地址 |
|------|------|
| GitHub 仓库 | <https://github.com/acl-dev/acl> |
| 官方网站 | <https://acl-dev.cn> |
| GitHub Pages | <http://acl-dev.cn/acl/> |
| GitHub Wiki | <https://github.com/acl-dev/acl/wiki> |
| 中文 README | <https://github.com/acl-dev/acl/blob/master/README_CN.md> |
| 英文 README | <https://github.com/acl-dev/acl/blob/master/README.md> |
| 示例代码 | <https://github.com/acl-dev/acl/tree/master/lib_acl_cpp/samples> |
| 协程文档 | <https://github.com/acl-dev/acl/blob/master/lib_fiber/README_en.md> |
| Redis 客户端 | <https://github.com/acl-dev/acl/blob/master/lib_acl_cpp/samples/redis/README.md> |
| MQTT 文档 | <https://github.com/acl-dev/acl/blob/master/lib_acl_cpp/samples/mqtt/README.md> |

---

## 库结构总览

ACL 由三个子库组成：

| 子库 | 说明 | 头文件目录 |
|------|------|-----------|
| **lib_acl** | 核心 C 库 — 内存、容器、字符串、网络、事件、线程、JSON、XML、DB、编解码等 | `lib_acl/include/` |
| **lib_protocol** | 协议库 — HTTP/SMTP/ICMP 协议解析 | `lib_protocol/include/` |
| **lib_acl_cpp** | C++ 封装 — 面向对象的高级 API，含 HTTP/Redis/MySQL/MQTT/MIME/阿里云 OSS 等 | `lib_acl_cpp/include/acl_cpp/` |

---

# 第一部分：C 核心库 (lib_acl)

## 1. 初始化 (`init/`)

总头文件：`#include <init/acl_init.h>` 或 `#include <lib_acl.h>`

```c
void acl_lib_init(void);           // 初始化 ACL 库（必须首先调用）
void acl_lib_end(void);            // 清理 ACL 库
const char *acl_version(void);     // 获取版本信息
const char *acl_verbose(void);     // 获取编译配置信息
unsigned long acl_main_thread_self(void);  // 获取主线程 ID
void acl_poll_prefered(int yesno); // 是否优先使用 poll
```

---

## 2. 标准库 (`stdlib/`)

总头文件：`#include <stdlib/acl_stdlib.h>`，包含以下所有子模块。

### 2.1 平台定义 (`acl_define.h`)

跨平台基础类型和宏定义：

- `ACL_SOCKET` — 套接字句柄类型
- `ACL_FILE_HANDLE` — 文件句柄类型
- `acl_int64` / `acl_uint64` — 64 位整数
- `ACL_FMT_I64D` / `ACL_FMT_I64U` — 64 位格式化字符串
- 平台检测宏：`ACL_UNIX`, `ACL_LINUX`, `ACL_MACOSX`, `ACL_WINDOWS`, `ACL_FREEBSD`

### 2.2 内存管理

#### 基础分配 (`acl_mymalloc.h`)

```c
void *acl_malloc(size_t size);
void *acl_calloc(size_t nmemb, size_t size);
void *acl_realloc(void *ptr, size_t size);
void  acl_free(void *ptr);
char *acl_strdup(const char *s);
char *acl_strndup(const char *s, size_t n);
void *acl_memdup(const void *s, size_t size);
```

#### 内存池 (`acl_mem_slice.h`)

```c
ACL_MEM_SLICE *acl_mem_slice_init(int base, int nslice, int nalloc_gc, unsigned int slice_flag);
void acl_mem_slice_destroy(void);
void acl_mem_slice_set(ACL_MEM_SLICE *slice);
int  acl_mem_slice_gc(void);
```

#### 内存池管理 (`acl_malloc.h`)

```c
void acl_mempool_open(size_t max_size, int use_mutex);
void acl_mempool_close(void);
int  acl_mempool_total_allocated(void);
void acl_mempool_status(void);
void acl_default_set_memlimit(size_t len);
```

#### 自定义分配器 (`acl_allocator.h`)

```c
typedef enum {
    ACL_MEM_TYPE_NONE,
    ACL_MEM_TYPE_8_BUF, ..., ACL_MEM_TYPE_1M_BUF,  // 19 种大小级别
    ACL_MEM_TYPE_VSTRING, ACL_MEM_TYPE_MAX
} acl_mem_type;

ACL_ALLOCATOR *acl_allocator_create(size_t mem_limit);
void acl_allocator_free(ACL_ALLOCATOR *allocator);
void *acl_allocator_mem_alloc(const char *file, int line, ACL_ALLOCATOR *a, acl_mem_type type);
void  acl_allocator_mem_free(const char *file, int line, ACL_ALLOCATOR *a, acl_mem_type type, void *obj);
```

#### 内存钩子 (`acl_mem_hook.h`)

```c
void acl_mem_hook(malloc_fn, calloc_fn, realloc_fn, strdup_fn, strndup_fn, memdup_fn, free_fn);
void acl_mem_unhook(void);
```

#### 会话内存池 (`acl_dbuf_pool.h`)

```c
ACL_DBUF_POOL *acl_dbuf_pool_create(size_t init_size);
void acl_dbuf_pool_free(ACL_DBUF_POOL *pool);
void *acl_dbuf_pool_alloc(const char *file, int line, ACL_DBUF_POOL *pool, size_t size);
char *acl_dbuf_pool_strdup(const char *file, int line, ACL_DBUF_POOL *pool, const char *s);
void  acl_dbuf_pool_free_obj(ACL_DBUF_POOL *pool, void *obj);
```

### 2.3 动态字符串 (`acl_vstring.h`)

```c
// 结构体
typedef struct ACL_VSTRING {
    ACL_VBUF vbuf;
    ssize_t  maxlen;
} ACL_VSTRING;

// 创建与销毁
ACL_VSTRING *acl_vstring_alloc(size_t len);
void acl_vstring_free(ACL_VSTRING *vp);
void acl_vstring_init(ACL_VSTRING *vp, size_t len);     // 栈上初始化
void acl_vstring_free_buf(ACL_VSTRING *vp);

// 字符串操作
ACL_VSTRING *acl_vstring_strcpy(ACL_VSTRING *vp, const char *src);
ACL_VSTRING *acl_vstring_strncpy(ACL_VSTRING *vp, const char *src, size_t len);
ACL_VSTRING *acl_vstring_strcat(ACL_VSTRING *vp, const char *src);
ACL_VSTRING *acl_vstring_strncat(ACL_VSTRING *vp, const char *src, size_t len);
char *acl_vstring_strstr(ACL_VSTRING *vp, const char *needle);
void acl_vstring_truncate(ACL_VSTRING *vp, ssize_t len);
void acl_vstring_reset(ACL_VSTRING *vp);

// 格式化
ACL_VSTRING *acl_vstring_sprintf(ACL_VSTRING *vp, const char *fmt, ...);
ACL_VSTRING *acl_vstring_sprintf_append(ACL_VSTRING *vp, const char *fmt, ...);

// 宏
#define acl_vstring_str(vp)   ((char*)(vp)->vbuf.data)     // 获取 C 字符串
#define ACL_VSTRING_LEN(vp)   ((size_t)((vp)->vbuf.ptr - (vp)->vbuf.data))  // 获取长度

// 从流读取
int acl_vstring_gets(ACL_VSTRING *vs, ACL_VSTREAM *fp);           // 读到换行
int acl_vstring_gets_nonl(ACL_VSTRING *vs, ACL_VSTREAM *fp);      // 不含换行
int acl_vstring_gets_null(ACL_VSTRING *vs, ACL_VSTREAM *fp);      // 读到 NUL
int acl_vstring_gets_bound(ACL_VSTRING *vs, ACL_VSTREAM *fp, ssize_t bound);
```

### 2.4 虚拟缓冲区 (`acl_vbuf.h`)

```c
typedef struct ACL_VBUF {
    unsigned char *data;    // 数据起始
    unsigned char *ptr;     // 当前指针
    unsigned char *end;     // 数据末尾
    int    cnt;             // 剩余计数
    int    flag;
    ...
} ACL_VBUF;

ACL_VBUF *acl_vbuf_alloc(size_t len);
void acl_vbuf_free(ACL_VBUF *vb);
ACL_VBUF *acl_vbuf_append(ACL_VBUF *vb, const void *data, size_t len);
void acl_vbuf_reset(ACL_VBUF *vb);
```

### 2.5 虚拟流 (`acl_vstream.h`)

```c
// 文件/套接字 I/O 流
ACL_VSTREAM *acl_vstream_fopen(const char *path, int flags, int mode, int buf_size);
ACL_VSTREAM *acl_vstream_fdopen(int fd, int buf_size, int flag_read, int flag_write, int type);
int acl_vstream_fclose(ACL_VSTREAM *fp);
int acl_vstream_fflush(ACL_VSTREAM *fp);

ssize_t acl_vstream_read(ACL_VSTREAM *fp, void *buf, size_t len);
ssize_t acl_vstream_write(ACL_VSTREAM *fp, const void *buf, size_t len);
ssize_t acl_vstream_writen(ACL_VSTREAM *fp, const void *buf, size_t len);

int acl_vstream_printf(ACL_VSTREAM *fp, const char *fmt, ...);
int acl_vstream_fputs(ACL_VSTREAM *fp, const char *s);

off_t acl_vstream_fseek(ACL_VSTREAM *fp, off_t offset, int whence);
off_t acl_vstream_ftell(ACL_VSTREAM *fp);

int acl_vstream_set_nonblocking(ACL_VSTREAM *fp, int yesno);
int acl_vstream_set_tcp_nodelay(ACL_VSTREAM *fp, int yesno);
int acl_vstream_set_read_timeout(ACL_VSTREAM *fp, int timeout);
int acl_vstream_set_write_timeout(ACL_VSTREAM *fp, int timeout);

ACL_SOCKET acl_vstream_sockhandle(ACL_VSTREAM *fp);
```

### 2.6 字符串工具 (`acl_mystring.h`)

```c
char *acl_lowercase(char *s);                        // 转小写（原地）
char *acl_uppercase(char *s);                        // 转大写（原地）
char *acl_lowercase3(const char *s, char *buf, size_t size);
char *acl_uppercase3(const char *s, char *buf, size_t size);
char *acl_strtrim(char *str);                        // 移除所有空白
char *acl_strtok(char **src, const char *sep);       // 字符串分割
char *acl_strline(char **src);                       // 提取一行
int acl_strcasecmp(const char *s1, const char *s2);  // 忽略大小写比较
int acl_strncasecmp(const char *s1, const char *s2, size_t n);
char *acl_strcasestr(const char *haystack, const char *needle);
size_t acl_strnlen(const char *s, size_t count);
char *acl_rstrstr(const char *haystack, const char *needle);
```

#### 安全字符串宏 (`acl_mystring.h`)

```c
ACL_SAFE_STRNCPY(dst, src, size);   // 安全拷贝，保证 NUL 结尾
ACL_SAFE_STRCPY(dst, src);          // 安全拷贝（自动计算大小）
```

#### 字符串操作 (`acl_stringops.h`)

```c
int acl_alldig(const char *str);                       // 是否全数字
int acl_is_double(const char *s);                      // 是否浮点数
char *acl_concatenate(const char *arg0, ...);          // 拼接（NULL 结尾，需 acl_myfree）
const char *acl_safe_basename(const char *path);       // 提取文件名
const char *acl_split_nameval(char *buf, char **name, char **value);  // 解析 name=value
```

#### 分割辅助 (`acl_split_at.h`)

```c
char *acl_split_at(char *string, int delimiter);        // 从左分割
char *acl_split_at_right(char *string, int delimiter);  // 从右分割
```

### 2.7 安全格式化 (`acl_vsprintf.h`)

```c
int acl_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int acl_snprintf(char *buf, size_t size, const char *fmt, ...);
int acl_vsprintf(char *buf, const char *fmt, va_list args);
int acl_sprintf(char *buf, const char *fmt, ...);
```

### 2.8 容器

#### 动态数组 (`acl_array.h`)

```c
ACL_ARRAY *acl_array_create(int init_size);
void acl_array_free(ACL_ARRAY *a, void (*free_fn)(void*));
int acl_array_append(ACL_ARRAY *a, void *data);
int acl_array_prepend(ACL_ARRAY *a, void *data);
void *acl_array_index(const ACL_ARRAY *a, int idx);
void *acl_array_pop(ACL_ARRAY *a);
int acl_array_size(const ACL_ARRAY *a);
void *acl_array_remove(ACL_ARRAY *a, int idx);
void acl_array_sort(ACL_ARRAY *a, int (*compar)(const void*, const void*));
void acl_array_walk(ACL_ARRAY *a, void (*walk_fn)(void*, void*), void *arg);
```

#### 哈希表 (`acl_hash.h`)

```c
ACL_HASH *acl_hash_create(int size);
void acl_hash_free(ACL_HASH *h, void (*free_fn)(void*));
void *acl_hash_enter(ACL_HASH *h, const char *key);
int acl_hash_delete(ACL_HASH *h, const char *key, void (*free_fn)(void*));
void *acl_hash_find(ACL_HASH *h, const char *key);
void acl_hash_walk(ACL_HASH *h, int (*walk_fn)(const char*, void*, void*), void *arg);
int acl_hash_size(const ACL_HASH *h);
```

#### 双链哈希表 (`acl_htable.h`)

```c
ACL_HTABLE *acl_htable_create(int size, int flag);
void acl_htable_free(ACL_HTABLE *ht, void (*free_fn)(void*));
void *acl_htable_enter(ACL_HTABLE *ht, const char *key);
int acl_htable_delete(ACL_HTABLE *ht, const char *key, void (*free_fn)(void*));
void *acl_htable_find(const ACL_HTABLE *ht, const char *key);
void acl_htable_walk(ACL_HTABLE *ht, int (*walk_fn)(const char*, void*, void*), void *arg);
int acl_htable_used(const ACL_HTABLE *ht);
```

#### 二进制键哈希表 (`acl_binhash.h`)

```c
ACL_BINHASH *acl_binhash_create(int size, unsigned int flag);
ACL_BINHASH_INFO *acl_binhash_enter(ACL_BINHASH *h, const void *key, int key_len, void *value);
void *acl_binhash_find(ACL_BINHASH *h, const void *key, int key_len);
int acl_binhash_delete(ACL_BINHASH *h, const void *key, int key_len, void (*free_fn)(void*));
void acl_binhash_free(ACL_BINHASH *h, void (*free_fn)(void*));

// 迭代器宏
ACL_BINHASH_FOREACH(iter, table_ptr) { ... }
```

#### 双向链表 (`acl_ring.h`)

```c
typedef struct ACL_RING {
    struct ACL_RING *prev;
    struct ACL_RING *next;
} ACL_RING;

void acl_ring_init(ACL_RING *head);
void acl_ring_insert(ACL_RING *head, ACL_RING *node);
void acl_ring_append(ACL_RING *head, ACL_RING *node);
void acl_ring_remove(ACL_RING *node);
int acl_ring_size(ACL_RING *head);
ACL_RING *acl_ring_head(ACL_RING *head);
ACL_RING *acl_ring_tail(ACL_RING *head);
int acl_ring_empty(ACL_RING *head);

// 遍历宏
ACL_RING_FOREACH(node, head) { ... }
ACL_RING_FOREACH_SAFE(node, head, tmp) { ... }
```

#### 先进先出队列 (`acl_fifo.h`)

```c
ACL_FIFO *acl_fifo_create(void);
void acl_fifo_free(ACL_FIFO *f, void (*free_fn)(void*));
int acl_fifo_push(ACL_FIFO *f, void *data);
void *acl_fifo_pop(ACL_FIFO *f);
void *acl_fifo_peek(ACL_FIFO *f);
int acl_fifo_size(const ACL_FIFO *f);
int acl_fifo_empty(const ACL_FIFO *f);
void acl_fifo_walk(ACL_FIFO *f, void (*walk_fn)(void*, void*), void *arg);
```

#### 栈 (`acl_stack.h`)

```c
ACL_STACK *acl_stack_create(void);
void acl_stack_free(ACL_STACK *s, void (*free_fn)(void*));
int acl_stack_push(ACL_STACK *s, void *data);
void *acl_stack_pop(ACL_STACK *s);
void *acl_stack_peek(ACL_STACK *s);
int acl_stack_size(const ACL_STACK *s);
```

#### AVL 树 (`acl_avl.h`)

源自 Solaris 内核 AVL 树实现：

```c
void acl_avl_create(acl_avl_tree_t *tree, int (*compar)(const void*, const void*), size_t size, size_t offset);
void *acl_avl_find(acl_avl_tree_t *tree, void *node, acl_avl_index_t *where);
void acl_avl_insert(acl_avl_tree_t *tree, void *node, acl_avl_index_t where);
void acl_avl_add(acl_avl_tree_t *tree, void *node);
void acl_avl_remove(acl_avl_tree_t *tree, void *node);
void *acl_avl_first(acl_avl_tree_t *tree);
void *acl_avl_last(acl_avl_tree_t *tree);
ulong_t acl_avl_numnodes(acl_avl_tree_t *tree);
void acl_avl_destroy(acl_avl_tree_t *tree);

#define AVL_NEXT(tree, node)    // 后继
#define AVL_PREV(tree, node)    // 前驱
```

#### 二叉搜索树 (`acl_btree.h`)

```c
ACL_BTREE *acl_btree_create(void);
int acl_btree_destroy(ACL_BTREE *tree);
void *acl_btree_find(ACL_BTREE *tree, unsigned int key);
int acl_btree_add(ACL_BTREE *tree, unsigned int key, void *data);
void *acl_btree_remove(ACL_BTREE *tree, unsigned int key);
int acl_btree_get_min_key(ACL_BTREE *tree, unsigned int *key);
int acl_btree_get_max_key(ACL_BTREE *tree, unsigned int *key);
int acl_btree_depth(ACL_BTREE *tree);
```

#### 缓存 (`acl_cache.h` / `acl_cache2.h`)

```c
// acl_cache — 基础缓存
ACL_CACHE *acl_cache_create(int max_size, void (*free_fn)(const ACL_CACHE_INFO*, void*));
ACL_CACHE_INFO *acl_cache_enter(ACL_CACHE *cache, const char *key, void *value, int timeout);
void *acl_cache_find(ACL_CACHE *cache, const char *key);
int acl_cache_delete(ACL_CACHE *cache, const char *key);
void acl_cache_free(ACL_CACHE *cache);

// acl_cache2 — 线程安全缓存（带引用计数）
ACL_CACHE2 *acl_cache2_create(int max_size, void (*free_fn)(const ACL_CACHE2_INFO*, void*));
ACL_CACHE2_INFO *acl_cache2_enter(ACL_CACHE2 *cache, const char *key, void *value, int timeout);
void *acl_cache2_find(ACL_CACHE2 *cache, const char *key);
void acl_cache2_refer(ACL_CACHE2_INFO *info);
void acl_cache2_unrefer(ACL_CACHE2_INFO *info);
void acl_cache2_lock(ACL_CACHE2 *cache);
void acl_cache2_unlock(ACL_CACHE2 *cache);
ACL_CACHE2_INFO *acl_cache2_upsert(ACL_CACHE2 *cache, const char *key, void *value, int timeout, int *exist);
```

#### 参数列表 (`acl_argv.h`)

```c
ACL_ARGV *acl_argv_create(void);
void acl_argv_free(ACL_ARGV *argv);
void acl_argv_add(ACL_ARGV *argv, const char *arg);
void acl_argv_addn(ACL_ARGV *argv, const char *arg, size_t len);
int acl_argv_count(const ACL_ARGV *argv);
const char *acl_argv_index(const ACL_ARGV *argv, int idx);
ACL_ARGV *acl_argv_split(const char *string, const char *sep);
char *acl_argv_join(const ACL_ARGV *argv, const char *sep);
void acl_argv_sort(ACL_ARGV *argv, int (*compar)(const void*, const void*));
```

### 2.9 原子操作 (`acl_atomic.h`)

```c
ACL_ATOMIC *acl_atomic_new(void);
void acl_atomic_free(ACL_ATOMIC *a);
void *acl_atomic_cas(ACL_ATOMIC *a, void *cmp, void *value);    // CAS
void *acl_atomic_xchg(ACL_ATOMIC *a, void *value);              // 交换
long long acl_atomic_int64_fetch_add(ACL_ATOMIC *a, long long n); // 先加后返回
long long acl_atomic_int64_add_fetch(ACL_ATOMIC *a, long long n); // 先返回后加

// 原子时钟
ACL_ATOMIC_CLOCK *acl_atomic_clock_alloc(void);
long long acl_atomic_clock_count(ACL_ATOMIC_CLOCK *c);
long long acl_atomic_clock_users(ACL_ATOMIC_CLOCK *c);
long long acl_atomic_clock_atime(ACL_ATOMIC_CLOCK *c);
```

### 2.10 位图 (`acl_bits_map.h`)

纯宏实现：

```c
ACL_BITS_MASK_ALLOC(mask, nmax);
ACL_BITS_MASK_FREE(mask);
ACL_BITS_MASK_ZERO(mask);
ACL_BITS_MASK_SET(number, mask);
ACL_BITS_MASK_ISSET(number, mask);
ACL_BITS_MASK_CLR(number, mask);
```

### 2.11 块链 (`acl_chunk_chain.h`)

```c
ACL_CHAIN *acl_chain_new(size_t init_size, acl_int64 off_begin);
void acl_chain_free(ACL_CHAIN *chain);
void acl_chain_add(ACL_CHAIN *chain, const void *data, acl_int64 from, int dlen);
const char *acl_chain_data(ACL_CHAIN *chain);
int acl_chain_data_len(ACL_CHAIN *chain);
int acl_chain_size(ACL_CHAIN *chain);
```

### 2.12 区间容器 (`acl_dlink.h`)

```c
ACL_DLINK *acl_dlink_create(int nsize);
void acl_dlink_free(ACL_DLINK *dl);
ACL_DITEM *acl_dlink_insert(ACL_DLINK *dl, acl_int64 begin, acl_int64 end);
int acl_dlink_delete(ACL_DLINK *dl, acl_int64 n);
int acl_dlink_delete_range(ACL_DLINK *dl, acl_int64 begin, acl_int64 end);
ACL_DITEM *acl_dlink_lookup(const ACL_DLINK *dl, acl_int64 n);
int acl_dlink_size(const ACL_DLINK *dl);
```

### 2.13 IP 区间 (`acl_iplink.h`)

基于 `acl_dlink` 的 IP 范围管理：

```c
ACL_IPLINK *acl_iplink_create(int nsize);
void acl_iplink_free(ACL_IPLINK *il);
int acl_iplink_insert(ACL_IPLINK *il, const char *ip_begin, const char *ip_end);
int acl_iplink_lookup_str(ACL_IPLINK *il, const char *ip);
int acl_iplink_delete_by_ip(ACL_IPLINK *il, const char *ip);
```

### 2.14 无锁队列 / 邮箱

#### 无锁队列 (`acl_yqueue.h`)

```c
ACL_YQUEUE *acl_yqueue_new(void);
void acl_yqueue_free(ACL_YQUEUE *q, void (*free_fn)(void*));
void acl_yqueue_push(ACL_YQUEUE *q);
void acl_yqueue_pop(ACL_YQUEUE *q);
void **acl_yqueue_front(ACL_YQUEUE *q);
void **acl_yqueue_back(ACL_YQUEUE *q);
```

#### 无锁管道 (`acl_ypipe.h`)

```c
ACL_YPIPE *acl_ypipe_new(void);
void acl_ypipe_write(ACL_YPIPE *p, void *data);
void *acl_ypipe_read(ACL_YPIPE *p);
int acl_ypipe_check_read(ACL_YPIPE *p);
int acl_ypipe_flush(ACL_YPIPE *p);
void acl_ypipe_free(ACL_YPIPE *p, void (*free_fn)(void*));
```

#### 无锁邮箱 (`acl_mbox.h`)

```c
#define ACL_MBOX_T_SPSC  0   // 单生产者单消费者
#define ACL_MBOX_T_MPSC  1   // 多生产者单消费者

ACL_MBOX *acl_mbox_create(void);
ACL_MBOX *acl_mbox_create2(unsigned type);
int acl_mbox_send(ACL_MBOX *mbox, void *msg);
void *acl_mbox_read(ACL_MBOX *mbox, int timeout_ms, int *success);
void acl_mbox_free(ACL_MBOX *mbox, void (*free_fn)(void*));
```

### 2.15 动态库加载 (`acl_dll.h`)

```c
ACL_DLL_HANDLE acl_dlopen(const char *dlname);
void acl_dlclose(ACL_DLL_HANDLE handle);
ACL_DLL_FARPROC acl_dlsym(void *handle, const char *name);
const char *acl_dlerror(void);
```

### 2.16 Hex 编解码 (`acl_hex_code.h`)

```c
ACL_VSTRING *acl_hex_encode(ACL_VSTRING *buf, const char *ptr, int len);
ACL_VSTRING *acl_hex_decode(ACL_VSTRING *buf, const char *ptr, int len);
```

### 2.17 文件与目录

#### 文件操作 (`acl_make_dirs.h`, `acl_scan_dir.h`)

```c
int acl_make_dirs(const char *path, int perms);   // 递归创建目录

// 目录扫描
ACL_SCAN_DIR *acl_scan_dir_open(const char *path, int recursive);
void acl_scan_dir_close(ACL_SCAN_DIR *sd);
const char *acl_scan_dir_next(ACL_SCAN_DIR *sd);
const char *acl_scan_dir_path(ACL_SCAN_DIR *sd);
const char *acl_scan_dir_file(ACL_SCAN_DIR *sd);
unsigned acl_scan_dir_ndirs(ACL_SCAN_DIR *sd);
unsigned acl_scan_dir_nfiles(ACL_SCAN_DIR *sd);
acl_int64 acl_scan_dir_nsize(ACL_SCAN_DIR *sd);
acl_int64 acl_scan_dir_size(const char *path, int recursive, int *nfile, int *ndir);
acl_int64 acl_scan_dir_rm(const char *path, int recursive, int *ndir, int *nfile);
```

#### 路径工具 (`acl_sane_basename.h`)

```c
char *acl_sane_basename(ACL_VSTRING *bp, const char *path);
char *acl_sane_dirname(ACL_VSTRING *bp, const char *path);
```

#### 文件锁 (`acl_myflock.h`)

```c
#define ACL_FLOCK_STYLE_FLOCK  1
#define ACL_FLOCK_STYLE_FCNTL  2

int acl_myflock(ACL_FILE_HANDLE fd, int lock_style, int operation);
// operation: ACL_FLOCK_OP_SHARED / ACL_FLOCK_OP_EXCLUSIVE / ACL_FLOCK_OP_NOWAIT
```

### 2.18 日志系统 (`acl_msg.h`, `acl_mylog.h`)

```c
// 基础日志宏
acl_msg_info(fmt, ...);
acl_msg_warn(fmt, ...);
acl_msg_error(fmt, ...);
acl_msg_fatal(fmt, ...);

// 多目标日志
int acl_open_log(const char *recipients, const char *plog_pre);
// recipients 格式："/tmp/test.log|UDP:127.0.0.1:12345|TCP:127.0.0.1:12345"
int acl_write_to_log(const char *fmt, ...);
void acl_close_log(void);

// 调试日志
void acl_debug_init(const char *ptr);   // 格式："1:1, 2:10, 3:8"
int acl_do_debug(int section, int level);
acl_debug(section, level) << "message";
```

### 2.19 安全函数 (`acl_safe.h`)

```c
int acl_unsafe(void);
char *acl_safe_getenv(const char *name);
```

### 2.20 getopt (`acl_getopt.h`)

```c
int acl_getopt(int argc, char *argv[], const char *opts);
void acl_getopt_init(void);
extern int acl_optind;
extern char *acl_optarg;
```

### 2.21 命令行执行 (`acl_exec_command.h`)

```c
void acl_exec_command(const char *command);
```

---

## 3. 网络模块 (`net/`)

头文件：`#include <net/acl_net.h>`

### 3.1 套接字初始化 (`acl_sys_patch.h`)

```c
int acl_socket_init(void);                              // Windows WSAStartup
int acl_socket_end(void);
int acl_socket_close(ACL_SOCKET fd);
int acl_socket_shutdown(ACL_SOCKET fd, int how);
int acl_socket_alive(ACL_SOCKET fd);
int acl_socket_read(ACL_SOCKET fd, void *buf, size_t size, int timeout, ...);
int acl_socket_write(ACL_SOCKET fd, const void *buf, size_t size, int timeout, ...);
```

### 3.2 DNS 解析 (`acl_dns.h`, `acl_hosts.h`)

```c
const char *acl_gethostbyname(const char *domain, char *ip, size_t size);
int acl_inet_aton(const char *cp, struct in_addr *addr);
const char *acl_inet_ntoa(struct in_addr addr, char *buf, size_t size);
int acl_get_local_ips(ACL_ARRAY *ips);
int acl_dns_lookup(const char *name, char *ip_buf, size_t ip_size);
```

### 3.3 连接与监听

```c
// 客户端连接
ACL_VSTREAM *acl_vstream_connect(const char *addr, int block_flag, int conn_timeout, int rw_timeout, int cache_size);
ACL_VSTREAM *acl_vstream_connect_timeout(const char *addr, int conn_timeout, int rw_timeout, int cache_size);

// 服务端监听
ACL_VSTREAM *acl_listen(const char *addr, int backlog, int reuse_addr);
ACL_VSTREAM *acl_vstream_listen(const char *addr, int backlog);

// 接受连接
ACL_VSTREAM *acl_accept(ACL_VSTREAM *fp);
```

### 3.4 TCP 选项

```c
int acl_set_tcp_nodelay(ACL_SOCKET fd, int yesno);
int acl_set_tcp_defer_accept(ACL_SOCKET fd, int yesno);
int acl_set_tcp_quickack(ACL_SOCKET fd, int yesno);
int acl_set_tcp_solinger(ACL_SOCKET fd, int linger);
int acl_set_tcp_sendbuf(ACL_SOCKET fd, int size);
int acl_set_tcp_recvbuf(ACL_SOCKET fd, int size);
int acl_set_nonblocking(ACL_SOCKET fd, int yesno);
```

### 3.5 网络接口 (`acl_ifconf.h`)

```c
ACL_ARRAY *acl_get_ifconf(void);   // 获取网络接口列表
```

---

## 4. 事件驱动 (`event/`)

头文件：`#include <event/acl_events.h>`

支持 select / poll / epoll (Linux) / kqueue (macOS/BSD) / IOCP (Windows)。

```c
// 事件引擎
ACL_EVENT *acl_event_new(const char *name, int use_threads);
void acl_event_free(ACL_EVENT *event);
const char *acl_event_name(ACL_EVENT *event);

// 事件循环
void acl_event_loop(ACL_EVENT *event);
void acl_event_exit_loop(ACL_EVENT *event);

// 定时器
void acl_event_request_timer(ACL_EVENT *event, ACL_EVENT_TIMER_FN callback, void *arg, int delay);
void acl_event_cancel_timer(ACL_EVENT *event, ACL_EVENT_TIMER_FN callback, void *arg);

// 文件描述符事件
void acl_event_enable_read(ACL_EVENT *event, ACL_VSTREAM *fp, int timeout, ACL_EVENT_NOTIFY callback, void *arg);
void acl_event_enable_write(ACL_EVENT *event, ACL_VSTREAM *fp, int timeout, ACL_EVENT_NOTIFY callback, void *arg);
void acl_event_disable_read(ACL_EVENT *event, ACL_VSTREAM *fp);
void acl_event_disable_write(ACL_EVENT *event, ACL_VSTREAM *fp);
void acl_event_enable_keepread(ACL_EVENT *event, ACL_VSTREAM *fp);
void acl_event_disable_keepread(ACL_EVENT *event, ACL_VSTREAM *fp);

// 异步流
ACL_ASTREAM *acl_astream_open(ACL_EVENT *event);
void acl_astream_close(ACL_ASTREAM *as);
```

---

## 5. 线程模块 (`thread/`)

头文件：`#include <thread/acl_thread.h>`

```c
// 线程
ACL_THREAD *acl_thread_create(void (*start_fn)(void*), void *arg, int detachable);
void acl_thread_join(ACL_THREAD *thread);
unsigned long acl_thread_self(void);
unsigned long acl_thread_id(const ACL_THREAD *thread);

// 线程池
ACL_THREAD_POOL *acl_thread_pool_create(int threads_limit, int idle_seconds);
void acl_thread_pool_free(ACL_THREAD_POOL *pool);
void acl_thread_pool_run(ACL_THREAD_POOL *pool, void (*start_fn)(void*), void *arg);
int acl_thread_pool_threads_count(const ACL_THREAD_POOL *pool);
int acl_thread_pool_task_qlen(const ACL_THREAD_POOL *pool);

// 互斥锁
ACL_THREAD_MUTEX *acl_thread_mutex_create(void);
void acl_thread_mutex_free(ACL_THREAD_MUTEX *mutex);
void acl_thread_mutex_lock(ACL_THREAD_MUTEX *mutex);
int acl_thread_mutex_trylock(ACL_THREAD_MUTEX *mutex);
void acl_thread_mutex_unlock(ACL_THREAD_MUTEX *mutex);

// 读写锁
ACL_THREAD_RWLOCK *acl_thread_rwlock_create(void);
void acl_thread_rwlock_free(ACL_THREAD_RWLOCK *lock);
void acl_thread_rwlock_rdlock(ACL_THREAD_RWLOCK *lock);
void acl_thread_rwlock_wrlock(ACL_THREAD_RWLOCK *lock);
void acl_thread_rwlock_unlock(ACL_THREAD_RWLOCK *lock);

// 信号量
ACL_THREAD_SEM *acl_thread_sem_create(unsigned int init_val);
void acl_thread_sem_free(ACL_THREAD_SEM *sem);
void acl_thread_sem_wait(ACL_THREAD_SEM *sem);
int acl_thread_sem_trywait(ACL_THREAD_SEM *sem);
void acl_thread_sem_post(ACL_THREAD_SEM *sem);

// 条件变量
ACL_THREAD_COND *acl_thread_cond_create(void);
void acl_thread_cond_free(ACL_THREAD_COND *cond);
void acl_thread_cond_signal(ACL_THREAD_COND *cond);
void acl_thread_cond_broadcast(ACL_THREAD_COND *cond);
int acl_thread_cond_wait(ACL_THREAD_COND *cond, ACL_THREAD_MUTEX *mutex);
int acl_thread_cond_timedwait(ACL_THREAD_COND *cond, ACL_THREAD_MUTEX *mutex, unsigned int ms);
```

### Unix 专属 (`stdlib/unix/`)

```c
int acl_chroot_uid(const char *chroot_dir, const char *user);   // chroot + 降权
void acl_set_core_limit(long long int max);                      // core dump 限制
int acl_mychown(const char *path, const char *owner, const char *group);
int acl_set_ugid(uid_t uid, gid_t gid);                         // 设置用户/组
const char *acl_username(void);
int acl_timed_waitpid(pid_t pid, ACL_WAIT_STATUS_T *status, int options, int timeout);
int acl_read_fd(int fd, void *ptr, int nbytes, int *recv_fd);   // Unix FD 传递
int acl_write_fd(int fd, void *ptr, int nbytes, int send_fd);

// 看门狗
ACL_WATCHDOG *acl_watchdog_create(unsigned timeout, ACL_WATCHDOG_FN fn, char *ctx);
void acl_watchdog_start(ACL_WATCHDOG *wd);
void acl_watchdog_stop(ACL_WATCHDOG *wd);
void acl_watchdog_pat(void);   // 喂狗

// 栈追踪
void acl_trace_save(const char *filepath);
void acl_trace_info(void);
```

---

## 6. JSON 解析 (`json/`)

头文件：`#include <json/acl_json.h>`

```c
ACL_JSON *acl_json_alloc(void);
void acl_json_free(ACL_JSON *json);
int acl_json_update(ACL_JSON *json, const char *data, int dlen);
int acl_json_complete(ACL_JSON *json);
void acl_json_reset(ACL_JSON *json);

ACL_JSON_NODE *acl_json_create(ACL_JSON *json, const char *tag);
ACL_JSON_NODE *acl_json_root(const ACL_JSON *json);
ACL_JSON_NODE *acl_json_first_node(const ACL_JSON *json);
ACL_JSON_NODE *acl_json_node_next(const ACL_JSON_NODE *node);

const char *acl_json_tag(const ACL_JSON_NODE *node);
const char *acl_json_text(const ACL_JSON_NODE *node);
const char *acl_json_node_to_string(const ACL_JSON_NODE *node);

// 查找
ACL_ARRAY *acl_json_get_elements(const ACL_JSON *json, const char *tag);
ACL_JSON_NODE *acl_json_get_first_element_by_tag(const ACL_JSON *json, const char *tag);
ACL_JSON_NODE *acl_json_get_element_by_id(const ACL_JSON *json, const char *id);

// 构建
void acl_json_node_append_text(ACL_JSON_NODE *node, const char *text);
void acl_json_node_append_child(ACL_JSON_NODE *parent, ACL_JSON_NODE *child);
```

---

## 7. XML 解析 (`xml/`)

头文件：`#include <xml/acl_xml.h>` / `acl_xml2.h` / `acl_xml3.h`

```c
ACL_XML *acl_xml_alloc(void);
void acl_xml_free(ACL_XML *xml);
int acl_xml_update(ACL_XML *xml, const char *data, int dlen);
int acl_xml_complete(ACL_XML *xml);

ACL_XML_NODE *acl_xml_root(const ACL_XML *xml);
ACL_XML_NODE *acl_xml_node_first_child(const ACL_XML_NODE *node);
ACL_XML_NODE *acl_xml_node_next(const ACL_XML_NODE *node);

const char *acl_xml_node_tag(const ACL_XML_NODE *node);
const char *acl_xml_node_text(const ACL_XML_NODE *node);
const char *acl_xml_node_attr(const ACL_XML_NODE *node, const char *attr_name);
int acl_xml_node_children_count(const ACL_XML_NODE *node);

ACL_ARRAY *acl_xml_get_elements_by_tagname(const ACL_XML *xml, const char *tag);
ACL_XML_NODE *acl_xml_get_first_element_by_tag(const ACL_XML *xml, const char *tag);
ACL_XML_NODE *acl_xml_get_element_by_id(const ACL_XML *xml, const char *id);
```

---

## 8. 数据库 (`db/`)

头文件：`#include <db/acl_db.h>`

```c
// 通用数据库接口
ACL_DB_HANDLE acl_db_open(const char *dbpath, int dbflag);
void acl_db_close(ACL_DB_HANDLE handle);
int acl_db_put(ACL_DB_HANDLE handle, const char *key, int key_len, const void *val, int val_len);
int acl_db_get(ACL_DB_HANDLE handle, const char *key, int key_len, void *val_buf, int val_size);
int acl_db_del(ACL_DB_HANDLE handle, const char *key, int key_len);
int acl_db_exists(ACL_DB_HANDLE handle, const char *key, int key_len);

// 内存数据库
ACL_MEMDB *acl_memdb_create(int (*key_cmp)(const void*, const void*, size_t));
void acl_memdb_destroy(ACL_MEMDB *db);

// ZDB (磁盘键值存储)
ACL_ZDB *acl_zdb_create(const char *dbpath);
void acl_zdb_close(ACL_ZDB *zdb);
```

---

## 9. 编解码 (`code/`)

头文件：`#include <code/acl_code.h>`

```c
// Base64
int acl_base64_encode(const char *in, int in_len, char *out, int out_size);
int acl_base64_decode(const char *in, int in_len, char *out, int out_size);

// URL 编解码
char *acl_url_encode(const char *src, char *dst, size_t dst_size);
char *acl_url_decode(const char *src, char *dst, size_t dst_size);

// HTML 实体
char *acl_html_encode(const char *src, char *dst, size_t dst_size);
char *acl_html_decode(const char *src, char *dst, size_t dst_size);

// GBK 编码
char *acl_gb_encode(const char *src, char *dst, size_t dst_size);
```

---

## 10. 异步 I/O (`aio/`)

头文件：`#include <aio/acl_aio.h>`

```c
ACL_AIO *acl_aio_create(ACL_EVENT *event);
void acl_aio_free(ACL_AIO *aio);

// 异步连接
void acl_aio_connect(ACL_AIO *aio, const char *addr, int timeout, ACL_AIO_CALLBACK callback, void *arg);

// 异步读写
void acl_aio_read(ACL_AIO *aio, ACL_VSTREAM *fp, int timeout, ACL_AIO_CALLBACK callback, void *arg);
void acl_aio_write(ACL_AIO *aio, ACL_VSTREAM *fp, const void *data, int dlen, int timeout, ACL_AIO_CALLBACK callback, void *arg);

// 异步定时器
void acl_aio_timer(ACL_AIO *aio, int delay, ACL_AIO_CALLBACK callback, void *arg);
```

---

## 11. 消息队列 (`msg/`)

头文件：`#include <msg/acl_aqueue.h>`, `msg/acl_msgio.h`

```c
// 异步队列
ACL_AQUEUE *acl_aqueue_create(void);
void acl_aqueue_free(ACL_AQUEUE *aq);
int acl_aqueue_push(ACL_AQUEUE *aq, void *data);
void *acl_aqueue_pop(ACL_AQUEUE *aq);
int acl_aqueue_size(const ACL_AQUEUE *aq);

// 消息 I/O
ACL_MSGIO *acl_msgio_create(int timeout);
void acl_msgio_free(ACL_MSGIO *msgio);
void acl_msgio_add(ACL_MSGIO *msgio, ACL_VSTREAM *fp);
void acl_msgio_delete(ACL_MSGIO *msgio, ACL_VSTREAM *fp);
int acl_msgio_poll(ACL_MSGIO *msgio, ACL_MSGIO_NOTIFY callback, void *arg);
```

---

## 12. Ioctl / Spool (`ioctl/`)

头文件：`#include <ioctl/acl_ioctl.h>`, `ioctl/acl_spool.h`

```c
// Spool — 流数据缓冲
ACL_SPOOL *acl_spool_create(void);
void acl_spool_free(ACL_SPOOL *spool);
void acl_spool_append(ACL_SPOOL *spool, const void *data, int dlen);
int acl_spool_size(const ACL_SPOOL *spool);
int acl_spool_write(ACL_SPOOL *spool, ACL_VSTREAM *fp);
```

---

## 13. 主服务框架 (`master/`)

头文件：`#include <master/acl_master.h>`

提供多进程/多线程服务端框架，支持配置文件驱动。

```c
// 主进程 API
void acl_master_main(const ACL_MASTER_PROC_ENT *proc_table, const char *proc_name, const char *config_file);

// 配置读取
int acl_master_get_int(const char *name, int default_val);
const char *acl_master_get_str(const char *name, const char *default_val);
int64_t acl_master_get_int64(const char *name, int64_t default_val);
```

---

## 14. 进程控制 (`proctl/`)

头文件：`#include <proctl/acl_proctl.h>`

```c
int acl_proctl_start(const char *path, const char *args[]);
int acl_proctl_stop(const char *proc_name);
int acl_proctl_restart(const char *path, const char *args[]);
int acl_proctl_status(const char *proc_name);
```

---

## 15. 单元测试 (`unit_test/`)

头文件：`#include <unit_test/acl_unit_test.h>`

```c
ACL_TEST_T *acl_test_create(const char *name);
void acl_test_free(ACL_TEST_T *test);
void acl_test_add(ACL_TEST_T *test, void (*test_fn)(void));
int acl_test_run(ACL_TEST_T *test);
```

---

## 16. 实验性功能 (`experiment/`)

头文件：`#include <experiment/experiment.h>`

实验性 API，接口可能变化。

---

# 第二部分：协议库 (lib_protocol)

## 1. HTTP 协议 (`http/`)

头文件：`#include <http/lib_http.h>`, `<http/lib_http_util.h>`

### 1.1 HTTP 头部结构

```c
typedef struct HTTP_HDR HTTP_HDR;           // 通用头部
typedef struct HTTP_HDR_REQ HTTP_HDR_REQ;   // 请求头
typedef struct HTTP_HDR_RES HTTP_HDR_RES;   // 响应头
typedef struct HTTP_REQ HTTP_REQ;           // 请求对象（含 body）
typedef struct HTTP_RES HTTP_RES;           // 响应对象（含 body）
typedef struct HTTP_HDR_ENTRY HTTP_HDR_ENTRY; // 单个头部条目
```

### 1.2 请求头操作

```c
HTTP_HDR_REQ *http_hdr_req_new(void);
HTTP_HDR_REQ *http_hdr_req_create(const char *url, ...);
void http_hdr_req_free(HTTP_HDR_REQ *hh);
int http_hdr_req_parse(HTTP_HDR_REQ *hh);

const char *http_hdr_req_method(const HTTP_HDR_REQ *hh);
const char *http_hdr_req_url(const HTTP_HDR_REQ *hh);
const char *http_hdr_req_url_path(const HTTP_HDR_REQ *hh);
const char *http_hdr_req_host(const HTTP_HDR_REQ *hh);
const char *http_hdr_req_param(const HTTP_HDR_REQ *hh, const char *name);
const char *http_hdr_req_cookie_get(HTTP_HDR_REQ *hh, const char *name);
int http_hdr_req_range(const HTTP_HDR_REQ *hh, ...);
```

### 1.3 响应头操作

```c
HTTP_HDR_RES *http_hdr_res_new(void);
void http_hdr_res_free(HTTP_HDR_RES *hh);
int http_hdr_res_parse(HTTP_HDR_RES *hh);
int http_hdr_res_range(const HTTP_HDR_RES *hh, ...);
const char *http_status_line(int status);
```

### 1.4 通用头部操作

```c
void http_hdr_put_str(HTTP_HDR *hdr, const char *name, const char *value);
void http_hdr_put_int(HTTP_HDR *hdr, const char *name, int value);
void http_hdr_put_fmt(HTTP_HDR *hdr, const char *name, const char *fmt, ...);
HTTP_HDR_ENTRY *http_hdr_entry(const HTTP_HDR *hh, const char *name);
char *http_hdr_entry_value(const HTTP_HDR *hh, const char *name);
int http_hdr_entry_replace(HTTP_HDR *hh, const char *name, const char *value, int force);
void http_hdr_build(const HTTP_HDR *hdr, ACL_VSTRING *strbuf);
```

### 1.5 同步 / 异步 I/O

```c
// 同步
int http_hdr_req_get_sync(HTTP_HDR_REQ *hdr, ...);
int http_hdr_res_get_sync(HTTP_HDR_RES *hdr, ...);
http_off_t http_req_body_get_sync(HTTP_REQ *request, ACL_VSTREAM *stream, ...);
http_off_t http_res_body_get_sync(HTTP_RES *respond, ACL_VSTREAM *stream, ...);

// 异步
void http_hdr_req_get_async(HTTP_HDR_REQ *hdr, ACL_ASTREAM *as, ...);
void http_hdr_res_get_async(HTTP_HDR_RES *hdr, ACL_ASTREAM *as, ...);
```

### 1.6 HTTP 工具函数

```c
typedef struct HTTP_UTIL { ... } HTTP_UTIL;

HTTP_UTIL *http_util_req_new(const char *url, const char *method);
void http_util_free(HTTP_UTIL *http_util);
void http_util_set_req_entry(HTTP_UTIL *u, const char *name, const char *value);
void http_util_set_req_cookie(HTTP_UTIL *u, const char *name, const char *value);
void http_util_set_req_keep_alive(HTTP_UTIL *u, int timeout);
int http_util_req_open(HTTP_UTIL *u);
int http_util_get_res_hdr(HTTP_UTIL *u);
int http_util_get_res_body(HTTP_UTIL *u, char *buf, size_t size);
int http_util_dump_url(const char *url, const char *dump);
```

---

## 2. SMTP 协议 (`smtp/`)

头文件：`#include <smtp/smtp_client.h>`

```c
typedef struct SMTP_CLIENT {
    ACL_VSTREAM *conn;
    int smtp_code;
    char *buf;
    int size;
    unsigned int flag;
    int message_size_limit;
} SMTP_CLIENT;

SMTP_CLIENT *smtp_open(const char *addr, int conn_timeout, int rw_timeout, int line_limit);
void smtp_close(SMTP_CLIENT *client);

int smtp_get_banner(SMTP_CLIENT *client);
int smtp_helo(SMTP_CLIENT *client, const char *helo);
int smtp_ehlo(SMTP_CLIENT *client, const char *ehlo);
int smtp_auth(SMTP_CLIENT *client, const char *user, const char *pass);
int smtp_mail(SMTP_CLIENT *client, const char *from);
int smtp_rcpt(SMTP_CLIENT *client, const char *to);
int smtp_data(SMTP_CLIENT *client);
int smtp_send(SMTP_CLIENT *client, const char *src, size_t len);
int smtp_send_file(SMTP_CLIENT *client, const char *filepath);
int smtp_data_end(SMTP_CLIENT *client);
int smtp_quit(SMTP_CLIENT *client);
```

---

## 3. ICMP/Ping (`icmp/`)

头文件：`#include <icmp/lib_icmp.h>`

### 3.1 高层会话 API

```c
ICMP_CHAT *icmp_chat_create(ACL_AIO *aio, int check_tid);
void icmp_chat_free(ICMP_CHAT *chat);
void icmp_chat(ICMP_HOST *host);
void icmp_stat(ICMP_CHAT *chat);

ICMP_HOST *icmp_host_new(ICMP_CHAT *chat, const char *domain, const char *ip,
                          size_t npkt, size_t dlen, int delay, int timeout);
void icmp_host_free(ICMP_HOST *host);
void icmp_host_set(ICMP_HOST *host, void *arg,
                    void (*stat_respond)(ICMP_PKT_STATUS*, void*),
                    void (*stat_timeout)(ICMP_PKT_STATUS*, void*),
                    void (*stat_unreach)(ICMP_PKT_STATUS*, void*),
                    void (*stat_finish)(ICMP_HOST*, void*));

void icmp_ping_one(ICMP_CHAT *chat, const char *domain, const char *ip,
                   size_t npkt, int delay, int timeout);
```

### 3.2 统计结构

```c
struct ICMP_STAT {
    double tmin, tmax, tsum, tave;
    size_t nsent, nreceived;
    double loss;
};

struct ICMP_PKT_STATUS {
    size_t reply_len;
    char from_ip[64];
    double rtt;
    unsigned short seq;
    unsigned char ttl;
    char status;    // ICMP_STATUS_OK / ICMP_STATUS_UNREACH / ICMP_STATUS_TIMEOUT
};
```

---

# 第三部分：C++ 封装库 (lib_acl_cpp)

所有类位于 `acl` 命名空间。总头文件：`#include <lib_acl.hpp>`

初始化：`acl_cpp_init()` — Windows 上必须首先调用。

---

## 1. 基础工具 (`stdlib/`)

### 1.1 字符串 (`string.hpp`)

```cpp
class string {
    const char *c_str() const;
    size_t length() const;
    bool empty() const;
    void clear();
    string &append(const char *s, size_t n = 0);
    string &format(const char *fmt, ...);
    string &format_append(const char *fmt, ...);
    string substr(size_t pos, size_t len = npos) const;
    size_t find(const char *s, size_t pos = 0) const;
    string &replace(const char *from, const char *to);
    std::vector<string> split(const char *sep) const;
    string &trim();
    int to_int() const;
    long long to_int64() const;
    string &to_lower();
    string &to_upper();
    // URL 编解码、Base64、字符集转换
    string &url_encode();
    string &url_decode();
    string &base64_encode();
    string &base64_decode();
};
```

### 1.2 JSON (`json.hpp`)

```cpp
class json_node : public dbuf_obj {
    const char *tag_name() const;
    const char *get_text() const;
    json_node *get_obj() const;
    const char *get_string(const char *tag) const;
    int get_int(const char *tag) const;
    long long get_int64(const char *tag) const;
    double get_double(const char *tag) const;
    bool get_bool(const char *tag) const;
    json_node *add_child(const char *tag);
    json_node *add_object(const char *tag);
};

class json {
    bool update(const char *data, size_t len);
    bool complete() const;
    void reset();
    std::string to_string() const;
    json_node *get_root() const;
    json_node *create_node(const char *tag);
    std::vector<json_node*> getElementsByTagName(const char *tag) const;
    json_node *getFirstElementByTag(const char *tag) const;
    json_node *getElementById(const char *id) const;
};
```

### 1.3 XML (`xml.hpp`)

```cpp
class xml_node : public dbuf_obj {
    const char *tag_name() const;
    const char *id() const;
    const char *text() const;
    const char *attr_value(const char *name) const;
    xml_node *first_child() const;
    xml_node *next_child() const;
    xml_node *add_child(const char *tag);
    xml_attr *add_attr(const char *name, const char *value);
    int children_count() const;
};

class xml : public pipe_stream, public dbuf_obj {
    bool update(const char *data, size_t len);
    bool complete() const;
    void reset();
    std::string to_string() const;
    xml_node *get_root() const;
    std::vector<xml_node*> getElementsByTagName(const char *tag) const;
    xml_node *getFirstElementByTag(const char *tag) const;
};
```

### 1.4 日志 (`log.hpp`)

```cpp
class log {
    static void open(const char *logfile);
    static void close();
    static void msg1(const char *fmt, ...);
    static void warn1(const char *fmt, ...);
    static void error1(const char *fmt, ...);
    static void fatal1(const char *fmt, ...);
};
// 宏：logger(), logger_warn(), logger_error(), logger_fatal(), logger_debug()
```

### 1.5 线程 (`thread.hpp`, `thread_pool.hpp`)

```cpp
class thread : public thread_job {
    void start(bool sync = false);
    void wait();
    void set_detachable(bool yes);
    void set_stacksize(size_t size);
    unsigned long thread_id() const;
protected:
    virtual void run() = 0;
};

class thread_pool : public noncopyable {
    void start();
    void stop();
    void run(thread_job *job);
    void execute(thread_job *job);
    void set_limit(int limit);
    void set_idle(int seconds);
    int threads_count() const;
    int task_qlen() const;
};
```

### 1.6 互斥锁 (`locker.hpp`)

```cpp
class locker : public noncopyable {
    void open(const char *file_path = NULL);
    void lock();
    bool try_lock();
    void unlock();
};

class lock_guard : public noncopyable {
    lock_guard(locker &l);   // RAII
};
```

### 1.7 会话内存池 (`dbuf_pool.hpp`)

```cpp
class dbuf_pool {
    void *dbuf_alloc(size_t size);
    void *dbuf_calloc(size_t nmemb, size_t size);
    char *dbuf_strdup(const char *s);
    void dbuf_free(void *obj);
    void dbuf_reset();
};

class dbuf_guard : public dbuf_pool {
    template<class T, ...> static T *create(...);  // 工厂方法
};
```

### 1.8 管道流 (`pipe_stream.hpp`)

```cpp
class pipe_stream : public noncopyable {
    virtual void push_pop(const char *data, size_t len) = 0;
    virtual void pop_end() = 0;
    virtual void clear() = 0;
};

class pipe_string : public pipe_stream { ... };
class pipe_manager : public noncopyable { ... };  // 链式管道
```

### 1.9 字符集转换 (`charset_conv.hpp`)

```cpp
class charset_conv : public pipe_stream {
    bool convert(const char *from, const char *to, const char *in, size_t in_len, string &out);
};
```

### 1.10 单例 (`singleton.hpp`)

```cpp
template<class T> class singleton {
    static T &get_instance();
};
```

---

## 2. 流体系 (`stream/`)

### 2.1 基础流

```cpp
class stream : public noncopyable {
    virtual void close() = 0;
    bool eof() const;
    bool opened() const;
    void set_rw_timeout(int read_ms, int write_ms);
};

class istream : virtual public stream {
    int read(void *buf, size_t size);
    int read(string &buf);
    int gets(string &buf);
    int getch();
    bool readable(int timeout);
};

class ostream : virtual public stream, public pipe_stream {
    int write(const void *data, size_t size);
    int write(const string &buf);
    int format(const char *fmt, ...);
    int puts(const char *s);
    int fflush();
};
```

### 2.2 Socket 流

```cpp
class socket_stream : public istream, public ostream {
    bool open(ACL_SOCKET fd);
    bool open(const char *addr, int conn_timeout, int rw_timeout);
    void shutdown_read();
    void shutdown_write();
    ACL_SOCKET sock_handle() const;
    bool get_peer(string &ip, unsigned short *port) const;
    bool get_local(string &ip, unsigned short *port) const;
    bool alive() const;
    void set_tcp_nodelay(bool on);
    void set_tcp_non_blocking(bool on);
    void set_tcp_sendbuf(int size);
    void set_tcp_recvbuf(int size);
};

class server_socket : public noncopyable {
    bool open(const char *addr);
    socket_stream *accept();
    void close();
    void set_tcp_defer_accept(bool on);
};
```

### 2.3 文件流

```cpp
class fstream : public istream, public ostream {
    bool open(const char *path, const char *mode);
    bool open_trunc(const char *path);
    static bool create(const char *path);
    static bool remove(const char *path);
    static bool rename(const char *from, const char *to);
    bool fseek(off_t offset, int whence);
    off_t ftell();
    off_t fsize();
    void lock();
    void unlock();
};

class ifstream : public fstream { ... };
class ofstream : public fstream { ... };
```

### 2.4 异步流

```cpp
class aio_handle : private noncopyable {
    enum aio_handle_type { ENGINE_SELECT, ENGINE_POLL, ENGINE_KERNEL, ENGINE_WINMSG };
    aio_handle(aio_handle_type type);
    void keep_read(aio_istream &stm);
    void set_timer(unsigned int id, unsigned int delay);
    void del_timer(unsigned int id);
};

class aio_istream : virtual public aio_stream { ... };
class aio_ostream : virtual public aio_stream, public pipe_stream { ... };
class aio_socket_stream : public aio_istream, public aio_ostream {
    static void open(aio_handle *handle, const char *addr, int timeout, aio_callback *callback);
    static void bind(aio_handle *handle, const char *addr, aio_accept_callback *callback);
};
class aio_listen_stream { ... };
class aio_fstream { ... };
```

### 2.5 SSL/TLS

```cpp
class sslbase_conf : public noncopyable {
    enum ssl_version_t { ssl_ver_3_0, tls_ver_1_0, tls_ver_1_1, tls_ver_1_2, tls_ver_1_3 };
    void set_version(ssl_version_t min_ver, ssl_version_t max_ver);
    bool load_ca(const char *ca_file);
    bool add_cert(const char *cert_file);
    bool set_key(const char *key_file, const char *pass);
    virtual sslbase_io *create(bool nonblock) = 0;
};

class openssl_conf : public sslbase_conf { ... };   // OpenSSL 实现
class mbedtls_conf : public sslbase_conf { ... };   // mbedTLS 实现
```

---

## 3. HTTP 模块 (`http/`)

### 3.1 HTTP 头部

```cpp
class http_header : public dbuf_obj {
    // 请求
    void set_url(const char *url);
    void set_host(const char *host);
    void set_method(http_method_t method);
    void add_param(const char *name, const char *value);
    void add_entry(const char *name, const char *value);
    void add_cookie(const char *name, const char *value);
    void set_content_length(off_t len);
    void set_content_type(const char *type);
    void set_keep_alive(int timeout);
    void set_chunked(bool on);
    std::string build_request() const;
    std::string build_response(int status) const;
    // WebSocket
    void set_ws_origin(const char *origin);
    void set_ws_key(const char *key);
};
```

### 3.2 HTTP 客户端

```cpp
class http_request : public connect_client {
    bool get(const char *url);
    bool post(const char *url, const char *data, size_t len);
    bool request(http_method_t method, const char *url);
    bool upload(const char *url, const char *filepath);
    int http_status() const;
    const char *header_value(const char *name) const;
    bool get_body(string &body);
    bool get_body(json &j);
    bool get_body(xml &x);
    void set_ssl(const sslbase_conf *conf);
    void keep_alive(bool on);
    http_header &request_header();
};
```

### 3.3 HTTP 服务端 (Servlet 模式)

```cpp
class HttpServlet : public noncopyable {
    void start(const char *addrs, const char *conf_path = NULL);
    void doRun();
protected:
    virtual void doGet(HttpServletRequest &req, HttpServletResponse &res);
    virtual void doPost(HttpServletRequest &req, HttpServletResponse &res);
    virtual void doPut(HttpServletRequest &req, HttpServletResponse &res);
    virtual void doDelete(HttpServletRequest &req, HttpServletResponse &res);
    virtual void doHead(HttpServletRequest &req, HttpServletResponse &res);
    virtual void doOptions(HttpServletRequest &req, HttpServletResponse &res);
    virtual void doWebSocket(HttpServletRequest &req, websocket &ws);
};

class HttpServletRequest {
    const char *getMethod() const;
    const char *getHeader(const char *name) const;
    const char *getQueryString() const;
    const char *getPathInfo() const;
    const char *getRequestUri() const;
    const char *getCookieValue(const char *name) const;
    HttpSession &getSession(bool create, const char *sid = NULL);
};

class HttpServletResponse {
    void setStatus(int status);
    void setContentLength(off_t len);
    void setContentType(const char *type);
    void setHeader(const char *name, const char *value);
    void setChunkedTransferEncoding(bool on);
    void setKeepAlive(bool on);
    ostream &get_body();
};
```

### 3.4 WebSocket

```cpp
class websocket : public noncopyable {
    void send_frame_data(const void *data, size_t len, unsigned char opcode);
    int read_frame(string &buf, unsigned char *opcode);
    void sendText(const char *data, size_t len);
    void sendBinary(const void *data, size_t len);
    void sendPing(const void *data, size_t len);
    void sendPong(const void *data, size_t len);
};

class WebSocketServlet : public HttpServlet {
protected:
    virtual void onMessage(websocket &ws, const char *data, size_t len, unsigned char opcode) = 0;
    virtual void onPing(websocket &ws, const char *data, size_t len) = 0;
    virtual void onPong(websocket &ws, const char *data, size_t len) = 0;
    virtual void onClose(websocket &ws);
};
```

### 3.5 Session / Cookie

```cpp
class HttpSession : public dbuf_obj {
    const char *getAttribute(const char *name) const;
    void setAttribute(const char *name, const char *value, size_t len);
    void removeAttribute(const char *name);
};

class HttpCookie : public dbuf_obj {
    void setCookie(const char *name, const char *value);
    void setDomain(const char *domain);
    void setPath(const char *path);
    void setMaxAge(int seconds);
};
```

---

## 4. Redis 客户端 (`redis/`)

### 4.1 连接管理

```cpp
class redis_client : public connect_client {
    void set_ssl_conf(const sslbase_conf *conf);
    void set_password(const char *pass);
    void set_db(int db);
};

class redis_client_pool : public connect_pool {
    void set_password(const char *pass);
    void set_db(int db);
};

class redis_client_cluster : public connect_manager {
    void set_slot(int slot, const char *addr);
    void set_all_slot(const char *addr, int max_conns, int conn_timeout, int rw_timeout);
};
```

### 4.2 Redis 命令类

所有命令类继承自 `redis_command`：

```cpp
class redis_command : public noncopyable {
    void set_client(redis_client *client);
    void set_cluster(redis_client_cluster *cluster);
    void set_pipeline(redis_client_pipeline *pipeline);
    int result_type() const;
    const char *result_error() const;
    int result_size() const;
    long long result_number() const;
    const char *get_result() const;
};
```

| 类名 | 覆盖命令 |
|------|---------|
| `redis_key` | DEL, EXISTS, EXPIRE, KEYS, PERSIST, RENAME, TTL, TYPE, SCAN, DUMP, RESTORE ... |
| `redis_string` | SET, GET, MGET, MSET, INCR, DECR, APPEND, GETRANGE, SETRANGE, SETEX, SETNX ... |
| `redis_hash` | HSET, HGET, HMSET, HMGET, HDEL, HKEYS, HVALS, HGETALL, HINCRBY ... |
| `redis_list` | LPUSH, RPUSH, LPOP, RPOP, LRANGE, LLEN, LINDEX, LSET, BLPOP, BRPOP ... |
| `redis_set` | SADD, SREM, SMEMBERS, SISMEMBER, SCARD, SPOP, SINTER, SUNION, SDIFF ... |
| `redis_zset` | ZADD, ZREM, ZSCORE, ZRANK, ZRANGE, ZRANGEBYSCORE, ZCARD, ZCOUNT, ZINCRBY ... |
| `redis_pubsub` | PUBLISH, SUBSCRIBE, UNSUBSCRIBE, PSUBSCRIBE, PUNSUBSCRIBE, PUBSUB |
| `redis_transaction` | MULTI, EXEC, DISCARD, WATCH, UNWATCH |
| `redis_script` | EVAL, EVALSHA, SCRIPT LOAD/EXISTS/FLUSH |
| `redis_server` | PING, ECHO, SELECT, AUTH, INFO, DBSIZE, FLUSHDB, SAVE, BGSAVE, CONFIG, SLOWLOG, TIME ... |
| `redis_hyperloglog` | PFADD, PFCOUNT, PFMERGE |
| `redis_geo` | GEOADD, GEODIST, GEOHASH, GEOPOS, GEORADIUS, GEORADIUSBYMEMBER |
| `redis_stream` | XADD, XLEN, XRANGE, XREAD, XREADGROUP, XACK, XCLAIM, XINFO, XDEL, XTRIM ... |
| `redis_cluster` | CLUSTER 系列命令 |
| `redis_sentinel` | SENTINEL 系列命令 |

---

## 5. 数据库 (`db/`)

```cpp
class db_handle : public connect_client {
    virtual const char *dbtype() const = 0;
    bool tbl_exists(const char *table);
    int sql_select(const char *sql, db_rows &rows);
    int sql_update(const char *sql);
    bool begin_transaction();
    bool commit();
    bool rollback();
    int affect_count() const;
    const char *escape_string(const char *src, size_t src_len, string &dst);
};

class db_row : public noncopyable {
    const char *operator[](const char *field) const;
    const char *operator[](int idx) const;
    int field_count() const;
};

class db_rows : public noncopyable {
    int size() const;
    db_row *operator[](int idx);
};

class db_mysql : public db_handle {
    db_mysql(const char *addr, const char *db, const char *user, const char *pass, ...);
    static db_mysql *load(const char *addr, ...);
};

class db_pgsql : public db_handle { ... };
class db_sqlite : public db_handle { ... };
```

---

## 6. 连接池 (`connpool/`)

```cpp
class connect_client : public noncopyable {
    virtual bool open() = 0;
    virtual bool alive() const;
    virtual void set_timeout(int conn_timeout, int rw_timeout);
};

class connect_pool : public noncopyable {
    void set_timeout(int conn_timeout, int rw_timeout);
    void set_conns_min(int min_conns);
    void set_idle_ttl(int seconds);
    connect_client *get();
    void put(connect_client *conn);
};

class connect_manager : public noncopyable {
    void init(const std::vector<conn_config> &confs);
    connect_client *get(const char *addr);
    connect_client *peek();   // 轮询
    connect_client *peek(const char *key);  // 哈希
};

class tcp_client : public connect_client {
    bool send(const void *data, size_t len, string *out = NULL);
};

class tcp_pool : public connect_pool { ... };
```

---

## 7. 服务器框架 (`master/`)

```cpp
class master_base : public noncopyable {
    void set_cfg_int(const char *name, int value);
    void set_cfg_str(const char *name, const char *value);
    void daemon_mode(bool on);
protected:
    virtual void proc_on_init() {}
    virtual void proc_on_exit() {}
};

// 多线程服务器
class master_threads : public master_base {
    void run_daemon(int argc, char *argv[]);
    void run_alone(const char *addrs, const char *path, int count, int threads_count);
protected:
    virtual void thread_on_read(socket_stream *conn) = 0;
    virtual void thread_on_accept(socket_stream *conn);
    virtual void thread_on_close(socket_stream *conn);
    virtual void thread_on_init();
    virtual void thread_on_exit();
};

// 异步 I/O 服务器
class master_aio : public master_base, public aio_accept_callback {
    void run_daemon(int argc, char *argv[]);
    void run_alone(const char *addrs, const char *path, aio_handle::aio_handle_type type);
protected:
    virtual void on_accept(aio_socket_stream *conn) = 0;
};

// 多进程服务器
class master_proc : public master_base {
    void run_daemon(int argc, char *argv[]);
    void run_alone(const char *addrs, const char *path, int count);
protected:
    virtual void on_accept(socket_stream *conn) = 0;
};

// UDP 服务器
class master_udp : public master_base {
    void run_daemon(int argc, char *argv[]);
    void run_alone(const char *addrs, const char *path, int count);
protected:
    virtual void on_read(socket_stream *conn) = 0;
};

// 定时器服务
class master_trigger : public master_base {
    void run_daemon(int argc, char *argv[]);
    void run_alone(const char *path, int count, int interval);
protected:
    virtual void on_trigger() = 0;
};
```

---

## 8. MQTT 客户端 (`mqtt/`)

```cpp
class mqtt_client : public connect_client {
    bool send(const mqtt_message &msg);
    bool get_message(mqtt_message &msg);
    socket_stream &sock_stream();
};
```

---

## 9. Beanstalk 队列 (`beanstalk/`)

```cpp
class beanstalk : public noncopyable {
    bool use(const char *tube);
    int put(const char *data, size_t len, int priority, int delay, int ttr);
    bool watch(const char *tube);
    int reserve(string &data, int timeout = 0);
    bool delete_job(int id);
    bool release(int id, int priority, int delay);
    bool bury(int id, int priority);
    int kick(int bound);
    bool peek(int id, string &data);
    bool stats(string &stats);
};
```

---

## 10. SMTP 客户端 (`smtp/`)

```cpp
class smtp_client : public noncopyable {
    bool send(const char *addr, const char *from, const char **to, int to_count,
              const char *subject, const char *body, size_t body_len);
    void set_ssl(const sslbase_conf *conf);
};
```

---

## 11. MIME 邮件解析 (`mime/`)

```cpp
class mime : public noncopyable {
    bool update(const char *data, size_t len);
    void parse();
    bool save_as(const char *path);
    bool save_as(ostream &out);
    mime_node *get_body_node() const;
    bool get_plain_body(string &body) const;
    bool get_html_body(string &body) const;
    std::vector<mime_node*> get_attachments() const;
    std::vector<mime_node*> get_images() const;
};

class mime_node : public noncopyable {
    const char *get_name() const;
    const char *get_ctype() const;
    const char *get_encoding() const;
    const char *get_charset() const;
    bool save(const char *path);
    bool save(ostream &out);
};
```

---

## 12. 序列化 (`serialize/`)

```cpp
// JSON 序列化/反序列化
template<typename T> bool deserialize(json &j, T &o, string *err = NULL);
template<typename T> void serialize(T &o, string &buf);
```

---

## 13. IPC / RPC (`ipc/`)

```cpp
class rpc_request : public ipc_request {
    virtual void rpc_run() = 0;
    virtual void rpc_onover() = 0;
    void rpc_signal();
    void cond_wait();
    void cond_signal();
};

class rpc_service : public ipc_service {
    void rpc_fork(rpc_request *req);
};
```

---

## 14. Session 管理 (`session/`)

```cpp
class session : public dbuf_obj {
    void reset();
    const char *get_sid() const;
    void set_sid(const char *sid);
    // 基于 Memcached / Redis 后端
};
```

---

## 15. 队列管理 (`queue/`)

```cpp
class queue_manager : public noncopyable {
    queue_file *create_file(const char *filename);
    queue_file *open_file(const char *filename);
    void close_file(queue_file *file);
    void delete_file(const char *filename);
    bool move_file(const char *from, const char *to);
    static bool parse_filePath(const char *path, string &dir, string &name);
};
```

---

## 16. 阿里云 OSS (`aliyun/`)

```cpp
class OSSClient {
    OSSClient(const char *keyId, const char *keySecret);
    OSSClient(const char *endPoint, const char *keyId, const char *keySecret);

    // Bucket 操作
    bool createBucket(const char *bucket);
    bool deleteBucket(const char *bucket);
    bool doesBucketExist(const char *bucket);
    std::vector<Bucket> listBuckets();

    // Object 操作
    bool putObject(const char *bucket, const char *key, const void *data, size_t len);
    bool getObject(const char *bucket, const char *key, string &data);
    bool deleteObject(const char *bucket, const char *key);
    bool copyObject(const char *src_bucket, const char *src_key,
                    const char *dst_bucket, const char *dst_key);
    ObjectMetadata getObjectMetadata(const char *bucket, const char *key);

    // 分片上传
    std::string initiateMultipartUpload(const char *bucket, const char *key);
    UploadPartResult uploadPart(const char *bucket, const char *key,
                                 const std::string &uploadId, int partNumber,
                                 const void *data, size_t len);
    bool completeMultipartUpload(const char *bucket, const char *key,
                                  const std::string &uploadId,
                                  const std::vector<Part> &parts);
};
```

---

## 17. HandlerSocket (`hsocket/`)

```cpp
class hsclient : public noncopyable {
    bool open_index(int id, const char *db, const char *table, const char *columns, const char *keys);
    bool get(int index_id, const char **values, int nvalues, ...);
    bool insert(int index_id, const char **values, int nvalues);
    bool update(int index_id, const char **values, int nvalues, ...);
};
```

---

## 18. 事件定时器 (`event/`)

```cpp
class event_timer : public noncopyable {
    void set_task(unsigned int id, unsigned int delay);
    void del_task(unsigned int id);
    void trigger();
    unsigned int min_delay() const;
    bool empty() const;
    int length() const;
protected:
    virtual void timer_callback(unsigned int id) = 0;
};
```

---

## 类继承关系总览

```
noncopyable
├── stream
│   ├── istream ──────────────┐
│   │   ├── fstream (+ ostream)
│   │   ├── socket_stream (+ ostream)
│   │   └── aio_istream ──────┤
│   └── ostream (+ pipe_stream) ┘
│       ├── aio_ostream ──────┘
│       │   └── aio_socket_stream (+ aio_istream)
├── server_socket
├── connect_client
│   ├── db_handle → db_mysql / db_pgsql / db_sqlite
│   ├── http_request
│   ├── redis_client
│   ├── mqtt_client
│   └── tcp_client
├── connect_pool → redis_client_pool / tcp_pool
├── connect_manager → redis_client_cluster
├── thread_job → thread
├── pipe_stream → pipe_string / charset_conv / xml
├── master_base
│   ├── master_threads
│   ├── master_aio (+ aio_accept_callback)
│   ├── master_proc
│   ├── master_udp
│   └── master_trigger
├── redis_command
│   ├── redis_key / redis_string / redis_hash / redis_list / redis_set / redis_zset
│   ├── redis_pubsub / redis_transaction / redis_script / redis_server
│   ├── redis_hyperloglog / redis_geo / redis_stream / redis_cluster
│   └── disque
├── HttpServlet → WebSocketServlet
├── sslbase_conf → openssl_conf / mbedtls_conf
└── websocket

dbuf_obj → json_node / xml_node / http_header / HttpCookie / HttpSession / session
dbuf_pool → dbuf_guard
```
