#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 2413541
/* page_init - 物理内存探测 + Page 描述符布局 + 把空闲页交给 pmm_manager
 *
 * 初始化完成后的物理内存布局：
 *   [0x80000000, 0x80200000)   OpenSBI，保留
 *   [0x80200000, end)          内核镜像（代码/数据/bss），保留
 *   [end, pages)               补齐到页边界的空隙，保留
 *   [pages, pages + (npage-nbase)*sizeof(struct Page))  Page 描述符数组，保留
 *   [free_start, mem_end)      交给 pmm_manager 的完整空闲页
 *
 * 三类地址不能混用：
 *   虚拟地址（pages / end） -> PADDR() -> 物理地址 -> PPN() -> 页号
 */
static void page_init(void) {
    extern char end[];

    // 1. 设备树告诉我们能管哪段物理内存
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();

    if (mem_base == 0 || mem_size == 0) {
        panic("page_init: invalid physical memory info from DTB (base=0x%lx, size=0x%lx)",
              (unsigned long)mem_base, (unsigned long)mem_size);
    }

    // 可管理上界不能超过框架允许的范围
    uint64_t mem_end = mem_base + mem_size;
    if (mem_end > (uint64_t)PHYSICAL_MEMORY_END) {
        mem_end = (uint64_t)PHYSICAL_MEMORY_END;
    }
    if (mem_end <= mem_base) {
        panic("page_init: empty manageable physical memory range");
    }

    // 2. 先定虚实偏移，后面 PADDR()/page2pa() 都依赖它
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // npage 是"可管理物理页号上界"，不是页数：描述符覆盖 [nbase, npage)
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: no manageable physical page (nbase=0x%lx, npage=0x%lx)",
              (unsigned long)nbase, (unsigned long)npage);
    }

    // 3. 描述符数组紧接在内核镜像之后，并按页对齐
    //    pages 是虚拟地址；要算内存布局，必须先用 PADDR() 转成物理地址
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);

    uintptr_t pages_end = (uintptr_t)pages + (npage - nbase) * sizeof(struct Page);
    // 空闲区起点：描述符数组之后的完整页
    uintptr_t free_start_pa = ROUNDUP(PADDR(pages_end), PGSIZE);
    // 空闲区末端：内存末端向下对齐，不足一页的尾巴不下发
    uintptr_t free_end_pa = ROUNDDOWN((uintptr_t)mem_end, PGSIZE);

    // 4. 先把 [nbase, npage) 的描述符全部标记为保留
    //    布局没做完之前不会有页面被误分配，内核尾页也就天然保持 reserved。
    //    注意：这里不解除保留，init_memmap() 内部自己会清（它断言 PageReserved）
    struct Page *p = pages;
    for (size_t i = 0; i < npage - nbase; i++, p++) {
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        SetPageReserved(p);
    }

    cprintf("physical memory map:\n");
    cprintf("  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n",
            (unsigned long)(mem_end - mem_base), (unsigned long)mem_base,
            (unsigned long)(mem_end - 1));

    // 5. 把完整空闲页交给当前 pmm_manager
    if (free_start_pa < free_end_pa) {
        init_memmap(pa2page(free_start_pa), (free_end_pa - free_start_pa) / PGSIZE);
    } else {
        cprintf("page_init: no complete free page after the page descriptor array\n");
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
