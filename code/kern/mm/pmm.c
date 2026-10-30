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
#include <../sync/sync.h>
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
    pmm_manager = &best_fit_pmm_manager;   // lab2 function2-2: 切换为 Best-Fit
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
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

//lab2 请补充你在lab2中的代码
//lab2 function1 2413541
/* page_init - 物理内存探测 + Page 描述符布局 + 把空闲页交给 pmm_manager
 *
 * 初始化后的物理内存布局：
 *   [0x80000000, 0x80200000)   OpenSBI，保留
 *   [0x80200000, end)          内核镜像（代码/数据/bss），保留
 *   [end, pages)               补齐到页边界的空隙，保留
 *   [pages, pages_end)         Page 描述符数组，保留
 *   [free_start, free_end)     交给 pmm_manager 的完整空闲页
 */
static void page_init(void) {
    extern char end[];

    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    if (mem_base == 0 || mem_size == 0) {
        panic("page_init: invalid physical memory info from DTB (base=0x%lx, size=0x%lx)",
              (unsigned long)mem_base, (unsigned long)mem_size);
    }

    uint64_t mem_end = mem_base + mem_size;
    uint64_t manageable_end = (uint64_t)KERNTOP - PHYSICAL_MEMORY_OFFSET;
    if (mem_end > manageable_end) {
        mem_end = manageable_end;
    }
    if (mem_end <= mem_base) {
        panic("page_init: empty manageable physical memory range");
    }

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: no manageable physical page (nbase=0x%lx, npage=0x%lx)",
              (unsigned long)nbase, (unsigned long)npage);
    }

    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    uintptr_t pages_end = (uintptr_t)pages + (npage - nbase) * sizeof(struct Page);

    uintptr_t free_start_pa = ROUNDUP(PADDR(pages_end), PGSIZE);
    uintptr_t free_end_pa = ROUNDDOWN((uintptr_t)mem_end, PGSIZE);

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
