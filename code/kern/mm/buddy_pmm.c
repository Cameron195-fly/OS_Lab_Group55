#include <assert.h>
#include <buddy_pmm.h>
#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Buddy System 物理内存管理器（lab2 任务三）
 *
 * 不变量
 * ------
 *  - 每个空闲块覆盖 2^order 页，order ∈ [0, BUDDY_MAX_ORDER]；
 *  - 块首页是块头：带 PG_property，property = 本块**页数**；
 *  - buddy_nr_free 是空闲页总数，故所有块长度之和 == buddy_nr_free；
 *  - 起始下标为 idx 的块，其伙伴下标是 idx ^ 2^order。
 *    这里"下标对齐"等价于"物理地址对齐"，因为 nbase = 0x80000 是 2^19 的倍数。
 * ------------------------------------------------------------------------- */

#define BUDDY_MAX_ORDER 11                  /* 最大块 2^11 页 = 8 MiB */
#define BUDDY_NR_ORDERS (BUDDY_MAX_ORDER + 1)

static list_entry_t buddy_free_list[BUDDY_NR_ORDERS];
static size_t buddy_nr_free = 0;

/* 本管理器当前托管的区间。合并时只允许在区间内找伙伴：
 * 区间外的描述符属于活动管理器（best_fit）的空闲链表，也带 PG_property，
 * 如果按"同阶就合并"去碰它们，会把别人的块拆坏。 */
static struct Page *buddy_begin = NULL;
static size_t buddy_pages = 0;

static inline size_t
buddy_page_index(struct Page *page) {
    return (size_t)(page - pages);
}

static inline size_t
buddy_block_size(int order) {
    return (size_t)1 << order;
}

/* n 页需要的最小 order：ceil(log2(n)) */
static int
buddy_order_of(size_t n) {
    int order = 0;
    assert(n > 0);
    assert(n <= buddy_block_size(BUDDY_MAX_ORDER));
    while (buddy_block_size(order) < n) {
        order++;
    }
    return order;
}

static void
buddy_init(void) {
    for (int order = 0; order < BUDDY_NR_ORDERS; order++) {
        list_init(&buddy_free_list[order]);
    }
    buddy_nr_free = 0;
    buddy_begin = NULL;
    buddy_pages = 0;
}

/* 把 [base, base+n) 注册为空闲区。
 * n 不要求是 2 的幂、base 也不要求阶对齐：贪心切出"能放下的最大阶对齐块"，
 * 剩下的继续切。这样后续合并时伙伴关系才是严格的。 */
static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    buddy_begin = base;
    buddy_pages = n;

    struct Page *p = base;
    for (; p != base + n; p++) {
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }
    buddy_nr_free += n;

    size_t done = 0;
    while (done < n) {
        struct Page *blk = base + done;
        size_t idx = buddy_page_index(blk);
        int order = BUDDY_MAX_ORDER;
        while (order > 0) {
            size_t size = buddy_block_size(order);
            if (size <= n - done && (idx & (size - 1)) == 0) {
                break;
            }
            order--;
        }
        size_t size = buddy_block_size(order);
        SetPageProperty(blk);
        blk->property = (unsigned int)size;
        list_add(&buddy_free_list[order], &(blk->page_link));
        done += size;
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    assert(n <= buddy_block_size(BUDDY_MAX_ORDER));

    int order = buddy_order_of(n);
    size_t size = buddy_block_size(order);
    if (size > buddy_nr_free) {
        return NULL;
    }

    /* 1. 从 order 阶往上找第一个非空链表 */
    int cur = order;
    while (cur <= BUDDY_MAX_ORDER && list_empty(&buddy_free_list[cur])) {
        cur++;
    }
    if (cur > BUDDY_MAX_ORDER) {
        return NULL;
    }

    /* 2. 取出这块，逐级分裂到目标阶 */
    struct Page *page = le2page(list_next(&buddy_free_list[cur]), page_link);
    list_del(&(page->page_link));
    ClearPageProperty(page);
    page->property = 0;

    while (cur > order) {
        cur--;
        size = buddy_block_size(cur);
        struct Page *buddy = page + size;   /* 上半块挂回低一阶链表 */
        SetPageProperty(buddy);
        buddy->property = (unsigned int)size;
        list_add(&buddy_free_list[cur], &(buddy->page_link));
    }

    buddy_nr_free -= buddy_block_size(order);
    return page;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    assert(n <= buddy_block_size(BUDDY_MAX_ORDER));

    int order = buddy_order_of(n);
    const size_t released = buddy_block_size(order);

    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(!PageReserved(p));
        assert(!PageProperty(p));
    }
    assert((buddy_page_index(base) & (released - 1)) == 0);
    assert(buddy_begin != NULL && base >= buddy_begin &&
           base + n <= buddy_begin + buddy_pages);

    /* 只要伙伴是同阶空闲块就合并，反复升阶；伙伴必须落在本管理器托管的区间内 */
    while (order < BUDDY_MAX_ORDER) {
        size_t idx = buddy_page_index(base);
        size_t bsize = buddy_block_size(order);
        size_t buddy_idx = idx ^ bsize;
        if (buddy_idx >= npage - nbase) {
            break;   /* 伙伴落在可管理范围外 */
        }
        struct Page *buddy = pages + buddy_idx;
        if (buddy < buddy_begin || buddy >= buddy_begin + buddy_pages) {
            break;   /* 伙伴不归本管理器管，不能合并 */
        }
        if (!PageProperty(buddy) || buddy->property != (unsigned int)bsize) {
            break;   /* 伙伴不是同阶空闲块 */
        }
        list_del(&(buddy->page_link));
        ClearPageProperty(buddy);
        if (buddy < base) {
            base = buddy;
        }
        order++;
    }

    SetPageProperty(base);
    base->property = (unsigned int)buddy_block_size(order);
    list_add(&buddy_free_list[order], &(base->page_link));
    buddy_nr_free += released;
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_nr_free;
}

/* --------------------------- 自检辅助函数 --------------------------- */

/* 块内除块头外都不该像块头；这同时能抓出"空闲块互相重叠" */
static void
buddy_check_block_pages(struct Page *base, size_t size) {
    for (size_t i = 1; i < size; i++) {
        assert(!PageProperty(base + i));
    }
}

static size_t
buddy_count_blocks(void) {
    size_t count = 0;
    for (int order = 0; order < BUDDY_NR_ORDERS; order++) {
        list_entry_t *le = &buddy_free_list[order];
        while ((le = list_next(le)) != &buddy_free_list[order]) {
            count++;
        }
    }
    return count;
}

/* 全结构自检：每个块只出现在它该在的阶上、阶对齐、页面归属不重叠、总数守恒 */
static void
buddy_check_consistent(void) {
    size_t total = 0;
    for (int order = 0; order < BUDDY_NR_ORDERS; order++) {
        size_t size = buddy_block_size(order);
        list_entry_t *le = &buddy_free_list[order];
        while ((le = list_next(le)) != &buddy_free_list[order]) {
            struct Page *p = le2page(le, page_link);
            assert(PageProperty(p));
            assert(p->property == (unsigned int)size);
            assert((buddy_page_index(p) & (size - 1)) == 0);
            assert(buddy_page_index(p) < npage - nbase);
            buddy_check_block_pages(p, size);
            total += size;
        }
    }
    assert(total == buddy_nr_free);
}

/* 把整段测试区间还给活动管理器 */
static void
buddy_release_area(struct Page *base, size_t n) {
    buddy_init();
    struct Page *p = base;
    for (; p != base + n; p++) {
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }
    free_pages(base, n);
}

void
buddy_check(void) {
    const size_t area_pages = buddy_block_size(9);      /* 512 页 = 2 MiB */
    const size_t half = area_pages >> 1;                /* 256 页，order 8 */

    size_t free_before = nr_free_pages();
    static struct Page *held[512];
    size_t held_nr = 0;

    /* 从活动管理器借一段私有区间；分配只保证页对齐，所以裁剪成 half 对齐的子块 */
    struct Page *area = alloc_pages(area_pages);
    if (area == NULL) {
        cprintf("[Test] Buddy: FAIL (cannot reserve %lu pages for the test)\n",
                (unsigned long)area_pages);
        return;
    }
    size_t start = (buddy_page_index(area) + half - 1) & ~(half - 1);
    struct Page *base = pages + start;
    size_t n = half;
    cprintf("[Test] Buddy: test area = %lu pages at index %lu\n",
            (unsigned long)n, (unsigned long)start);

    /* --- 1. 初始化：对齐的 2^8 页区间应是一整块 --- */
    buddy_init();
    buddy_init_memmap(base, n);
    assert(buddy_nr_free_pages() == n);
    assert(buddy_count_blocks() == 1);
    buddy_check_consistent();

    /* --- 2. 分配：返回的块阶对齐、互不重叠 --- */
    struct Page *a = buddy_alloc_pages(1);
    struct Page *b = buddy_alloc_pages(2);
    struct Page *c = buddy_alloc_pages(4);
    struct Page *d = buddy_alloc_pages(3);   /* 向上取整到 order 2 */
    assert(a == base);
    assert(b != NULL && c != NULL && d != NULL);
    assert((buddy_page_index(b) & 1) == 0);
    assert((buddy_page_index(c) & 3) == 0);
    assert((buddy_page_index(d) & 3) == 0);
    assert(!PageProperty(a) && !PageProperty(b) && !PageProperty(c) && !PageProperty(d));
    struct Page *blk[4] = {a, b, c, d};
    size_t blk_size[4] = {1, 2, 4, 4};
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            assert(blk[i] + blk_size[i] <= blk[j] || blk[j] + blk_size[j] <= blk[i]);
        }
    }
    assert(buddy_nr_free_pages() == n - 11);
    buddy_check_consistent();
    cprintf("[Test] Buddy: alloc(1/2/4/3) ok, nr_free = %lu\n",
            (unsigned long)buddy_nr_free_pages());

    /* --- 3. 乱序释放后应合并回一整块 --- */
    buddy_free_pages(d, 3);
    buddy_free_pages(b, 2);
    buddy_free_pages(a, 1);
    buddy_free_pages(c, 4);
    assert(buddy_nr_free_pages() == n);
    assert(buddy_count_blocks() == 1);
    buddy_check_consistent();
    cprintf("[Test] Buddy: split/merge round trip ok, blocks = %lu\n",
            (unsigned long)buddy_count_blocks());

    /* --- 4. 整段分配 + 空时失败路径 --- */
    struct Page *whole = buddy_alloc_pages(n);
    assert(whole == base);
    assert(buddy_nr_free_pages() == 0);
    assert(buddy_alloc_pages(1) == NULL);
    assert(buddy_alloc_pages(buddy_block_size(BUDDY_MAX_ORDER)) == NULL);
    buddy_free_pages(whole, n);
    assert(buddy_nr_free_pages() == n);
    assert(buddy_count_blocks() == 1);
    buddy_check_consistent();

    /* --- 5. 耗尽：一次一页全分配，再全释放 --- */
    held_nr = 0;
    while (held_nr < n) {
        struct Page *p = buddy_alloc_pages(1);
        if (p == NULL) {
            break;
        }
        held[held_nr++] = p;
    }
    assert(held_nr == n);
    assert(buddy_nr_free_pages() == 0);
    assert(buddy_alloc_pages(1) == NULL);
    for (size_t i = 0; i < held_nr; i++) {
        buddy_free_pages(held[i], 1);
    }
    assert(buddy_nr_free_pages() == n);
    assert(buddy_count_blocks() == 1);
    buddy_check_consistent();
    cprintf("[Test] Buddy: %lu single page allocations and releases ok\n",
            (unsigned long)n);

    /* --- 6. 未对齐区间也要能正确处理 --- */
    struct Page *raw = alloc_pages(4);
    assert(raw != NULL);
    size_t odd = buddy_page_index(raw);
    if ((odd & 1) == 0) {
        odd += 1;                       /* 故意取奇数下标：2 页区间无法构成阶对齐块 */
    }
    buddy_init();
    buddy_init_memmap(pages + odd, 2);
    assert(buddy_nr_free_pages() == 2);
    assert(buddy_count_blocks() == 2);  /* 两个孤立的 order-0 块 */
    buddy_check_consistent();
    struct Page *one = buddy_alloc_pages(1);
    assert(one != NULL);
    assert(buddy_nr_free_pages() == 1);
    buddy_free_pages(one, 1);
    assert(buddy_nr_free_pages() == 2);
    buddy_release_area(raw, 4);

    /* --- 7. 全部归还，活动管理器状态不变 --- */
    buddy_release_area(area, area_pages);
    assert(nr_free_pages() == free_before);
    cprintf("[Test] Buddy: PASS (free pages restored to %lu)\n",
            (unsigned long)free_before);
}

/* lab2 任务三：注册 Buddy 管理器。
 * 放在文件末尾，此时 buddy_check 已经定义过，不需要前置声明。 */
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
