#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

/* 简化版 SLUB 对象分配器（lab2 Challenge）。
 * 两层架构：
 *   1. 页层   —— 复用活动 pmm_manager 的 alloc_pages()/free_pages()；
 *   2. 对象层 —— 每个 kmem_cache 把它持有的页（slab）切成固定大小的对象，
 *                空闲对象用对象自身的前 8 字节串成 freelist。
 */
struct kmem_cache;

struct kmem_cache *kmem_cache_create(const char *name, size_t size);
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *obj);

void slub_init(void);      /* 复位 cache 池，第一次 create 之前调用 */
void slub_check(void);     /* 自检 */

#endif /* !__KERN_MM_SLUB_H__ */
