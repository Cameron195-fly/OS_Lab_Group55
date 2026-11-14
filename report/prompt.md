# Lab4 Prompt 记录（A：2413541-赖鸿毅）

> 本文档记录 A 负责的 lab2 PMM 基线与 lab4 功能模块一使用的提示词。
> 每个提示词都按“上下文 / 规格 / 约束 / 输出 / 自检”组织，便于复现和写报告。
> B、C 的提示词可以按同样结构追加到本文档后面。

---

## A-1：best-fit 物理页管理器

### 最终提示词

```text
[CONTEXT]
项目：riscv64-ucore Lab4，分支 lab4，框架来自 lab4 初始代码。
文件：code/kern/mm/default_pmm.c。
现状：
- 文件里已经有 basic_check() 和 best_fit_check()，这两个测试函数不能改；
- 文件开头仍然 #include <best_fit_pmm.h>，但项目中没有 best_fit_pmm.c/h；
- code/kern/mm/pmm.c 的 init_pmm_manager() 使用 default_pmm_manager；
- pmm.h 已提供 free_area_t、struct Page、list_entry_t、PageReserved、
  SetPageProperty、ClearPageProperty、page_ref、set_page_ref 等接口。

[SPEC]
在 default_pmm.c 中实现 best-fit 物理页管理器：
1. 定义 static free_area_t free_area，并提供 free_list / nr_free 宏；
2. init：初始化 free_list，nr_free = 0；
3. init_memmap(base, n)：
   - 每个页先 assert(PageReserved(p))；
   - 清 flags/property/ref；
   - 首页 property = n，SetPageProperty；
   - nr_free += n；
   - 按物理地址升序插入 free_list；
4. alloc_pages(n)：
   - n > nr_free 返回 NULL；
   - 遍历 free_list，选择 property >= n 且 property 最小的块；
   - 如果块比 n 大，把剩余部分作为新块插回 free_list；
   - 被分配的 n 页 SetPageReserved、ClearPageProperty、property=0、ref=0；
   - nr_free -= n，返回首页；
5. free_pages(base, n)：
   - 每页 assert(PageReserved)，清 flags/property/ref；
   - 首页 property = n，SetPageProperty，nr_free += n；
   - 按物理地址插入 free_list；
   - 与后一个空闲块合并，再与前一个空闲块合并；
6. 在文件末尾注册：
   const struct pmm_manager default_pmm_manager = {
       .name = "default_pmm_manager",
       .init = ...,
       .init_memmap = ...,
       .alloc_pages = ...,
       .free_pages = ...,
       .nr_free_pages = ...,
       .check = best_fit_check,
   };

[CONSTRAINTS]
- 只改 code/kern/mm/default_pmm.c；
- 不改 basic_check()/best_fit_check()；
- 不新增 best_fit_pmm.c/h，避免和缺失头文件/manager 命名冲突；
- 不使用 lab3 分支整文件覆盖；
- 保留 //lab2 注释，并加上 2413541-赖鸿毅 标识；
- 不修改 pmm.c 的 init_pmm_manager()，让它继续使用 default_pmm_manager。

[OUTPUT]
只输出需要插入 default_pmm.c 的实现代码和 manager 定义，不重写测试函数。

[SELF-CHECK]
- basic_check 中 alloc/free 后 nr_free 是否正确；
- best_fit_check 中 5 页分配、拆分、合并、再次分配是否一致；
- free_pages(p0,5) 后能否与相邻块合并；
- 是否有 static free_area 导致 best_fit_check 链接不到。
```

### 迭代记录

**v1 的问题：**

最初让 AI“把 lab2 的 best-fit 代码搬过来”，生成结果仍然引用 `best_fit_pmm.h`，并注册 `best_fit_pmm_manager`，和 `pmm.c` 使用的 `default_pmm_manager` 不一致；如果另建文件，`static free_area` 还会让 `best_fit_check()` 看不到 `free_list/nr_free`。

**v2 的修正：**

在 prompt 中明确：

- 不新增文件；
- 实现和 manager 都放回 `default_pmm.c`；
- 名字统一为 `default_pmm_manager`；
- 保留 `best_fit_check` 作为 `check`。

---

## A-2：`page_init`

### 最终提示词

```text
[CONTEXT]
文件：code/kern/mm/pmm.c。
pmm.c 已包含 dtb.h、memlayout.h、pmm.h；dtb.h 提供
get_memory_base()、get_memory_size()。
pmm_init() 已经调用 page_init()，但 page_init() 还没有实现。
全局变量：pages、npage、va_pa_offset、nbase = DRAM_BASE / PGSIZE。
链接脚本提供 end 符号；pmm.h 提供 PADDR、pa2page、init_memmap。

[SPEC]
在 pmm.c 的 //lab2 位置实现 static void page_init(void)：
1. 读取 mem_base、mem_size；任一为 0 则 panic；
2. 计算 mem_end = mem_base + mem_size；
3. manageable_end = KERNTOP - PHYSICAL_MEMORY_OFFSET；
   mem_end 超过时截断；
4. mem_end <= mem_base 则 panic；
5. va_pa_offset = PHYSICAL_MEMORY_OFFSET；
6. npage = PPN(mem_end)；npage <= nbase 则 panic；
7. pages = ROUNDUP((uintptr_t)end, PGSIZE)；
8. pages_end = pages + (npage - nbase) * sizeof(struct Page)；
9. free_start_pa = ROUNDUP(PADDR(pages_end), PGSIZE)；
10. free_end_pa = ROUNDDOWN(mem_end, PGSIZE)；
11. 初始化 pages[0..npage-nbase)：
    flags=0、property=0、ref=0、SetPageReserved；
12. 如果 free_start_pa < free_end_pa：
    init_memmap(pa2page(free_start_pa),
                (free_end_pa - free_start_pa) / PGSIZE)；
    否则打印“没有可用页”；
13. 可以打印物理内存范围，便于调试。

[CONSTRAINTS]
- 只改 pmm.c 的 //lab2 位置；
- 不改 check_pgdir/check_boot_pgdir；
- 不把 pages 数组放到未对齐地址；
- 不把描述符数组尾部当成空闲页；
- 不越界处理超过 KERNTOP 的物理内存；
- 保留 //lab2 注释，并加 2413541-赖鸿毅 标识。

[OUTPUT]
只给 page_init() 函数实现。

[SELF-CHECK]
- QEMU 128MB 内存时 npage/nbase 是否合理；
- pages_end 是否按页对齐；
- free_start_pa 是否在描述符数组之后；
- 所有描述符是否先 reserved，再只把可用区交给 init_memmap；
- check_alloc_page() 是否能拿到空闲页。
```

### 迭代记录

**v1 的问题：**

最初只写了“从 DTB 拿内存，初始化 pages”，没有处理 `mem_end` 超过 `KERNTOP` 的情况，也没有把 `pages` 数组尾部和空闲区起点按页对齐。

**v2 的修正：**

在 spec 中补充：

- `manageable_end` 截断；
- `pages` 用 `ROUNDUP(end, PGSIZE)`；
- `free_start_pa` 用 `ROUNDUP(PADDR(pages_end), PGSIZE)`；
- `free_end_pa` 用 `ROUNDDOWN`；
- 所有页先 `SetPageReserved`，再 `init_memmap`。

---

## A-3：`page_insert`

### 最终提示词

```text
[CONTEXT]
文件：code/kern/mm/pmm.c，位置 //LAB4 function1。
已有：
- get_pte(pgdir, la, create)
- pte_create(ppn, perm)
- pte2page(pte)
- page_ref_inc/page_ref_dec/free_page
- tlb_invalidate(pgdir, la)
- PTE_V/PTE_U/PTE_W/PTE_R
- page2ppn(page)

[SPEC]
实现 int page_insert(pde_t *pgdir, struct Page *page,
                     uintptr_t la, uint32_t perm)：
1. ptep = get_pte(pgdir, la, 1)；NULL 返回 -E_NO_MEM；
2. page_ref_inc(page)；
3. 如果 *ptep & PTE_V：
   - old_page = pte2page(*ptep)；
   - old_page == page：只更新权限，page_ref_dec(page) 抵消 inc；
   - old_page != page：先撤销旧映射（旧页减引用，必要时 free_page，
     清 PTE，刷新 TLB）；
4. *ptep = pte_create(page2ppn(page), perm | PTE_V)；
5. tlb_invalidate(pgdir, la)；
6. 返回 0。

[CONSTRAINTS]
- 不能简单覆盖旧 PTE；
- 同一页重复映射不能把引用计数加两次；
- 不同页替换必须先处理旧页；
- get_pte 失败不能留下叶子映射；
- 只改 pmm.c 的 //LAB4 function1；
- 保留 //LAB4 function1 注释，并把 YOUR CODE 换成 2413541-赖鸿毅；
- 不影响 A-4 的 page_remove。

[OUTPUT]
只给 page_insert 和必要的内部 helper。

[SELF-CHECK]
- 空地址第一次插入；
- 同一页重复插入并改权限；
- 同一地址替换成另一页；
- 旧页引用计数归零时是否释放；
- TLB 是否刷新。
```

### 迭代记录

**v1 的问题：**

AI 生成的版本直接：

```c
page_ref_inc(page);
*ptep = ...;
```

没有判断原 PTE 是否已经指向同一页，导致同一页重复映射时引用计数偏大；如果先 `page_remove` 再插入，又可能把正在使用的页释放。

**v2 的修正：**

明确三种情况，尤其是：

```c
if (old_page == page) {
    page_ref_dec(page);   // 抵消 inc
} else {
    page_remove_pte(...);
}
```

---

## A-4：`page_remove`

### 最终提示词

```text
[CONTEXT]
文件：code/kern/mm/pmm.c，位置 //LAB4 function1。
A-3 已经实现 page_remove_pte(pgdir, la, ptep)：
- 检查 PTE_V；
- pte2page、page_ref_dec、引用为 0 时 free_page；
- 清 PTE；
- tlb_invalidate。
现在需要实现公开接口 page_remove。

[SPEC]
实现 void page_remove(pde_t *pgdir, uintptr_t la)：
1. ptep = get_pte(pgdir, la, 0)；
2. ptep == NULL 直接返回；
3. 调用 page_remove_pte(pgdir, la, ptep)；
4. 不回收中间页表页；
5. 保证对未映射地址、重复调用都安静返回。

[CONSTRAINTS]
- 只改 pmm.c 的 //LAB4 function1；
- 不能把空页表页 free 掉，check_pgdir 会断言
  page_ref(pde2page(boot_pgdir_va[0])) == 1；
- 叶子页引用归零才 free_page；
- 保留注释和 2413541-赖鸿毅 标识。

[OUTPUT]
只给 page_remove() 函数实现。

[SELF-CHECK]
- 未映射地址调用；
- 已映射地址调用；
- 同一地址连续调用两次；
- 页表页引用计数是否仍为 1；
- TLB 是否刷新。
```

### 迭代记录

**v1 的问题：**

AI 生成版本在清空 PTE 后顺手回收空页表页，导致 `check_pgdir()` 的页表引用计数断言失败。

**v2 的修正：**

明确写入约束：

- `page_remove` 只处理叶子 PTE；
- 页表页生命周期由 `get_pte` 管理；
- 不回收中间页表页；
- 重复调用幂等。

---

## 通用验证提示词

```text
请按以下顺序检查，不要修改测试文件：

1. 只编译 A 负责的目标：
   make obj/kern/mm/default_pmm.o
   make obj/kern/mm/pmm.o

2. 检查注释：
   - //lab2 和 //LAB4 function1 是否保留；
   - YOUR CODE 是否替换为 2413541-赖鸿毅。

3. 检查代码边界：
   - 是否改了 basic_check/best_fit_check；
   - 是否改了 proc.c/trap.c/sched.c/switch.S；
   - 是否把 lab3 的整文件代码复制进来。

4. 集成后检查：
   - check_alloc_page() succeeded!
   - check_pgdir() succeeded!
   - check_boot_pgdir() succeeded!
   - [Test PMM] PASS

如果失败，先判断是 PMM 后端、page_init、page_insert 还是 page_remove，
只在 A 自己的文件里修，不改 B/C 的文件。
```

---

## 备注

- A 的 4 个代码 commit 分别是：`63e7743`、`323a587`、`6e32210`、`b293bf7`。
- A 的最终 `make grade` 截图在 B/C 合并、`[Test PMM] PASS` 后补到 `report/images/`。
- 本文件只记录 A 的提示词；B/C 的提示词可以继续往后追加。

---
