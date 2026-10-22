#include <assert.h>
#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <slub.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * 简化版 SLUB：页 -> slab -> 对象
 *
 *  - struct slab 放在页首，所以"由对象地址就能找到它的 slab"（页对齐掩码）；
 *  - 空闲对象用自己前 8 字节串成单链表，不额外占内存；
 *  - partial 放"还有空闲对象的 slab"，full 放"已满的 slab"；
 *  - slab 全空时整页还给页层。
 * ------------------------------------------------------------------------- */

#define SLUB_MAX_CACHES 8
#define SLUB_ALIGN sizeof(void *)
#define SLUB_MAX_OBJS 256

struct kmem_cache {
    const char *name;
    size_t obj_size;                 /* 对齐后的对象大小 */
    unsigned int objs_per_slab;
    unsigned int inuse;              /* 已分配对象数 */
    unsigned int slabs;              /* 当前持有的 slab 数 */
    list_entry_t partial;
    list_entry_t full;
};

struct slab {
    struct kmem_cache *cache;
    void *freelist;
    unsigned int inuse;
    unsigned int total;
    list_entry_t slab_link;
};

#define SLUB_OBJ_OFFSET (ROUNDUP(sizeof(struct slab), SLUB_ALIGN))

static struct kmem_cache cache_pool[SLUB_MAX_CACHES];
static unsigned int cache_used = 0;

static inline size_t
slub_align_up(size_t size) {
    return (size + SLUB_ALIGN - 1) & ~(SLUB_ALIGN - 1);
}

/* 描述符 -> 内核虚拟地址 */
static inline void *
slub_page_to_kva(struct Page *page) {
    return (void *)(page2pa(page) + va_pa_offset);
}

/* 对象所属的 slab 就在它所在页的页首 */
static inline struct slab *
slub_slab_of(void *obj) {
    return (struct slab *)((uintptr_t)obj & ~(uintptr_t)(PGSIZE - 1));
}

/* slab 页首 -> 页描述符。这里直接做 va - va_pa_offset 的逆运算，
 * 不用 PADDR：PADDR 会拒绝低于 KERNBASE 的 kva，而低端物理页的
 * 直接映射地址恰好落在 KERNBASE 以下，用它做反查会误 panic。 */
static inline struct Page *
slub_slab_to_page(struct slab *slab) {
    return pa2page((uintptr_t)slab - va_pa_offset);
}

void
slub_init(void) {
    cache_used = 0;
    memset(cache_pool, 0, sizeof(cache_pool));
}

struct kmem_cache *
kmem_cache_create(const char *name, size_t size) {
    if (name == NULL || size == 0) {
        return NULL;
    }
    size_t obj_size = slub_align_up(size);
    if (obj_size < SLUB_ALIGN) {
        obj_size = SLUB_ALIGN;
    }
    /* 一页至少要能装下"头部 + 一个对象" */
    if (SLUB_OBJ_OFFSET + obj_size > PGSIZE) {
        return NULL;
    }
    /* 同样大小的对象共用一个 cache */
    for (unsigned int i = 0; i < cache_used; i++) {
        if (cache_pool[i].obj_size == obj_size) {
            return &cache_pool[i];
        }
    }
    if (cache_used >= SLUB_MAX_CACHES) {
        return NULL;
    }

    struct kmem_cache *cache = &cache_pool[cache_used++];
    cache->name = name;
    cache->obj_size = obj_size;
    cache->objs_per_slab = (unsigned int)((PGSIZE - SLUB_OBJ_OFFSET) / obj_size);
    cache->inuse = 0;
    cache->slabs = 0;
    list_init(&cache->partial);
    list_init(&cache->full);
    return cache;
}

/* 向页层要一页，切成对象，挂到 partial */
static struct slab *
slub_new_slab(struct kmem_cache *cache) {
    struct Page *page = alloc_pages(1);
    if (page == NULL) {
        return NULL;
    }
    struct slab *slab = (struct slab *)slub_page_to_kva(page);
    slab->cache = cache;
    slab->total = cache->objs_per_slab;
    slab->inuse = 0;
    slab->freelist = NULL;

    char *first = (char *)slab + SLUB_OBJ_OFFSET;
    for (unsigned int i = 0; i < slab->total; i++) {
        void *obj = first + (size_t)i * cache->obj_size;
        *(void **)obj = slab->freelist;   /* 空闲链表就地串起来 */
        slab->freelist = obj;
    }

    list_add(&cache->partial, &slab->slab_link);
    cache->slabs++;
    return slab;
}

void *
kmem_cache_alloc(struct kmem_cache *cache) {
    assert(cache != NULL);

    struct slab *slab;
    if (list_empty(&cache->partial)) {
        slab = slub_new_slab(cache);
        if (slab == NULL) {
            return NULL;
        }
    } else {
        slab = to_struct(list_next(&cache->partial), struct slab, slab_link);
    }

    assert(slab->freelist != NULL);
    void *obj = slab->freelist;
    slab->freelist = *(void **)obj;
    slab->inuse++;
    cache->inuse++;

    if (slab->freelist == NULL) {
        /* 满了：从 partial 挪到 full */
        list_del(&slab->slab_link);
        list_add(&cache->full, &slab->slab_link);
    }
    return obj;
}

void
kmem_cache_free(struct kmem_cache *cache, void *obj) {
    if (obj == NULL) {
        return;
    }
    assert(cache != NULL);

    struct slab *slab = slub_slab_of(obj);
    assert(slab->cache == cache);
    assert(slab->inuse > 0 && slab->inuse <= slab->total);

    bool was_full = (slab->inuse == slab->total);

    *(void **)obj = slab->freelist;
    slab->freelist = obj;
    slab->inuse--;
    cache->inuse--;

    if (slab->inuse == 0) {
        /* 全空：整页还给页层 */
        list_del(&slab->slab_link);
        cache->slabs--;
        free_pages(slub_slab_to_page(slab), 1);
    } else if (was_full) {
        /* 原来满、现在有空位：挪回 partial */
        list_del(&slab->slab_link);
        list_add(&cache->partial, &slab->slab_link);
    }
}

/* ------------------------------- 自检 ------------------------------- */

static void *slub_test_objs[SLUB_MAX_OBJS];
static struct kmem_cache *slub_test_caches[8];

void
slub_check(void) {
    static const size_t sizes[] = {16, 40, 96, 256, 1024};
    const unsigned int nr_sizes = (unsigned int)(sizeof(sizes) / sizeof(sizes[0]));
    size_t free_before = nr_free_pages();

    assert(nr_sizes <= 8);

    /* --- 1. 每个档位批量分配（会跨 slab），再全部释放 --- */
    for (unsigned int s = 0; s < nr_sizes; s++) {
        struct kmem_cache *cache = kmem_cache_create("slub-test", sizes[s]);
        assert(cache != NULL);
        assert(cache->objs_per_slab >= 1);
        slub_test_caches[s] = cache;

        size_t want = (size_t)cache->objs_per_slab * 3 + 1;
        if (want > SLUB_MAX_OBJS) {
            want = SLUB_MAX_OBJS;
        }

        for (size_t i = 0; i < want; i++) {
            void *obj = kmem_cache_alloc(cache);
            assert(obj != NULL);
            uintptr_t off = (uintptr_t)obj & (uintptr_t)(PGSIZE - 1);
            assert(off >= (uintptr_t)SLUB_OBJ_OFFSET);          /* 落在对象区 */
            assert(off + cache->obj_size <= (uintptr_t)PGSIZE); /* 不越页 */
            for (size_t j = 0; j < i; j++) {
                assert(slub_test_objs[j] != obj);               /* 不重复 */
            }
            slub_test_objs[i] = obj;
        }
        cprintf("[Test] SLUB: size %lu -> %lu objects, %u slab(s), all distinct\n",
                (unsigned long)sizes[s], (unsigned long)want, cache->slabs);

        for (size_t i = 0; i < want; i++) {
            kmem_cache_free(cache, slub_test_objs[i]);
            slub_test_objs[i] = NULL;
        }
        assert(cache->inuse == 0);
        assert(cache->slabs == 0);        /* 每个 slab 页都还回去了 */
    }

    /* --- 2. 多个 cache 交错分配，随机顺序释放 --- */
    unsigned int mix = (nr_sizes < 3) ? nr_sizes : 3;
    size_t total_objs = 0;
    for (unsigned int i = 0; i < 24; i++) {
        struct kmem_cache *cache = slub_test_caches[i % mix];
        void *obj = kmem_cache_alloc(cache);
        assert(obj != NULL);
        for (size_t j = 0; j < total_objs; j++) {
            assert(slub_test_objs[j] != obj);
        }
        slub_test_objs[total_objs++] = obj;
    }
    /* 确定性洗牌 */
    unsigned int seed = 12345;
    for (size_t i = 0; i < total_objs; i++) {
        seed = seed * 1103515245u + 12345u;
        size_t k = (size_t)(seed >> 8) % total_objs;
        void *tmp = slub_test_objs[i];
        slub_test_objs[i] = slub_test_objs[k];
        slub_test_objs[k] = tmp;
    }
    for (size_t i = 0; i < total_objs; i++) {
        struct slab *slab = slub_slab_of(slub_test_objs[i]);   /* 由对象反推属主 cache */
        kmem_cache_free(slab->cache, slub_test_objs[i]);
        slub_test_objs[i] = NULL;
    }
    for (unsigned int i = 0; i < mix; i++) {
        assert(slub_test_caches[i]->inuse == 0);
        assert(slub_test_caches[i]->slabs == 0);
        assert(list_empty(&slub_test_caches[i]->partial));
        assert(list_empty(&slub_test_caches[i]->full));
    }
    cprintf("[Test] SLUB: %lu interleaved objects from %u caches released\n",
            (unsigned long)total_objs, mix);

    /* --- 3. 边界 --- */
    kmem_cache_free(slub_test_caches[0], NULL);                            /* 空操作 */
    assert(kmem_cache_create("slub-test-again", 16) == slub_test_caches[0]); /* 复用 */
    assert(kmem_cache_create("slub-too-big", PGSIZE * 2) == NULL);         /* 切不动 */
    void *obj = kmem_cache_alloc(slub_test_caches[0]);
    assert(obj != NULL);
    kmem_cache_free(slub_test_caches[0], obj);
    void *again = kmem_cache_alloc(slub_test_caches[0]);
    assert(again != NULL);
    kmem_cache_free(slub_test_caches[0], again);
    assert(slub_test_caches[0]->inuse == 0);
    assert(slub_test_caches[0]->slabs == 0);

    /* --- 4. 页资源必须全部归还 --- */
    assert(nr_free_pages() == free_before);
    cprintf("[Test] SLUB: PASS (free pages restored to %lu)\n",
            (unsigned long)free_before);
}
