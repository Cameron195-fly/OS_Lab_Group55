# Lab 2 提示词汇总（prompt.md）

> 本文件汇总本组在 Lab 2 中使用的**全部 prompt**，包括最终版与迭代过程记录。
> 框架：指导书 lab0.5 的四段式 `[PROMPT] / [RELY] / [GUARANTEE] / [SPECIFICATION]`。
> 记录规则：每完成一个练习，把该任务的最终提示词与迭代记录追加到对应小节，不覆盖他人记录。
> 分工：P1 2413541-赖鸿毅；P2 2412386-唐港；P3–P6 2412440-王朝阳。

## 索引

| 编号 | 任务 | 负责人 | 结果 |
|------|------|--------|------|
| P1 | `page_init()`：物理内存探测与 Page 描述符初始化 | 2413541-赖鸿毅 | ✅ `[Test 1] Page Init: PASS` |
| P2 | Best-Fit 分配器实现与 `pmm_manager` 切换 | 2412386-唐港 | ✅ 通过 |
| P3 | Buddy System 模块生成 | 2412440-王朝阳 | ✅ 通过 |
| P4 | Buddy System 测试函数生成 | 2412440-王朝阳 | ✅ 通过 |
| P5 | SLUB 模块生成 | 2412440-王朝阳 | ✅ 通过 |
| P6 | SLUB 测试函数生成 | 2412440-王朝阳 | ✅ 通过 |

---

## P1 `page_init()`：物理内存探测与 Page 描述符初始化（2413541-赖鸿毅）

### P1 最终版（一次通过，`[Test 1] Page Init: PASS`）

````markdown
[PROMPT]
**任务**：修改 `kern/mm/pmm.c`，在标有 `//lab2 function1 YOUR CODE` 的位置实现 `page_init()`，
完成"物理内存探测 + Page 描述符布局 + 把空闲页交给 pmm_manager"这一完整功能。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留文件中的现有代码，只在标记位置补全；
不要修改 `pmm_init()`、`check_alloc_page()`、`init_pmm_manager()`、`init_memmap()`、`alloc_pages()`
等已有函数的逻辑，也不要改动测试相关代码。如需辅助函数请设为 `static`，不得新增对外接口。

**输出要求**：直接进行文件操作，以下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 为准，
以工程真实代码为准，不引入与本次任务无关的功能。

[RELY]
// ===== 宏定义（kern/mm/mmu.h、kern/libs/defs.h、kern/mm/memlayout.h）=====
#define PGSIZE              4096
#define PGSHIFT             12
#define PPN(la)             (((uintptr_t)(la)) >> PGSHIFT)
#define ROUNDUP(a, n)       ...        // 向上对齐（statement expression）
#define ROUNDDOWN(a, n)     ...        // 向下对齐
#define KERNBASE            0xFFFFFFFFC0200000     // 内核虚拟起始地址
#define KERNEL_BEGIN_PADDR  0x80200000             // 内核物理起始地址
#define PHYSICAL_MEMORY_OFFSET  0xFFFFFFFF40000000 // 虚实偏移量
#define PHYSICAL_MEMORY_END     0x88000000         // 框架允许的物理内存末端
#define PG_reserved  0
#define PG_property  1
#define SetPageReserved(page) / ClearPageReserved(page) / PageReserved(page)
#define SetPageProperty(page) / ClearPageProperty(page) / PageProperty(page)
#define le2page(le, member)   // container_of：由 list_entry* 求 struct Page*

// ===== 数据结构 =====
struct Page {
    int ref;                 // 引用计数
    uint64_t flags;          // PG_reserved / PG_property
    unsigned int property;   // 空闲块长度，仅块头有效
    list_entry_t page_link;  // 空闲链表节点
};

// ===== 全局变量（kern/mm/pmm.c 已定义，直接用）=====
struct Page *pages;      // Page 描述符数组首地址（虚拟地址）
size_t npage = 0;        // 可管理物理页号上界（不是页数！）
uint64_t va_pa_offset;   // 虚拟地址 = 物理地址 + 它
const size_t nbase = DRAM_BASE / PGSIZE;   // 0x80000000 / 4096 = 0x80000
extern char end[];       // 链接脚本符号：内核镜像结束的虚拟地址

// ===== 可用函数 =====
uint64_t get_memory_base(void);   // DTB 报告的物理内存起始
uint64_t get_memory_size(void);   // DTB 报告的物理内存大小
void panic(const char *fmt, ...);
int  cprintf(const char *fmt, ...);
static void init_memmap(struct Page *base, size_t n);   // 转调 pmm_manager->init_memmap
static inline struct Page *pa2page(uintptr_t pa);       // 物理地址 -> 描述符
static inline uintptr_t page2pa(struct Page *page);
#define PADDR(kva)  // 内核虚拟地址 -> 物理地址（kva < KERNBASE 时 panic）
// libs/list.h: list_init/list_add/list_add_before/list_del/list_empty/list_next/list_prev

// ===== 物理内存布局事实 =====
// [0x80000000, 0x80200000)  OpenSBI 占用；
// [0x80200000, PADDR(end))  内核镜像占用；
// 描述符数组紧随 end 之后并按 4KiB 对齐，它自身占用的页面也不可分配；
// DTB 自身占用的空间在读完信息后可以复用。

[GUARANTEE]
必须实现或修改的接口：
```c
static void page_init(void);
```
可以添加：辅助函数（建议 static）、内部宏、模块内静态变量。

[SPECIFICATION]
## page_init
**Pre-Condition**:
- 分页已开启：`satp` 指向 `boot_page_table_sv39`，Sv39 大页映射生效，`PADDR`/`va_pa_offset` 的约定成立；
- `dtb_init()` 已执行，`get_memory_base()/get_memory_size()` 可返回 DTB 信息；
- `init_pmm_manager()` 已执行，`pmm_manager` 及其内部状态就绪；
- 无参数、无返回值，由 `pmm_init()` 调用。

**Post-Condition**:
- `va_pa_offset` 等于虚实偏移，使 `PADDR`/`page2pa` 等转换成立；
- `npage` 等于可管理物理内存末端的页号上界（受框架允许范围约束），且 `npage > nbase`；
- `pages` 指向 `end` 之后、按页对齐的描述符数组，数组覆盖 `[nbase, npage)`；
- `[nbase, npage)` 内所有描述符处于保留状态；
- 只有排除 OpenSBI、内核镜像、描述符数组之后、两端页对齐的完整空闲页面被交给
  `pmm_manager`（通过 `init_memmap()`），且这些描述符已解除保留（由 init_memmap 完成）；
- 控制台输出探测到的物理内存范围。

  **Case 1（正常）**：DTB 有效且存在至少一个完整空闲页面 → 正确设置三个全局量，
  保留全部已用页面，用 `pa2page()` 把空闲区起始物理地址转成描述符地址后调用 `init_memmap()`。
  **Case 2（DTB 无效）**：内存基址或大小为 0 → `panic()` 终止，不带未初始化的布局继续运行。
  **Case 3（无完整空闲页面）**：数组末端向上对齐后已越过内存末端向下对齐的位置 →
  不得调用 `init_memmap()`，不得把尾页或已用页面注册为空闲页。

**Requirements**:
- 严格区分虚拟地址（`pages`、`end`）、物理地址（`PADDR(...)`）、物理页号（`PPN(...)`）；
- 可管理上界不得超过 `PHYSICAL_MEMORY_END`；
- 内核镜像尾部所在页必须保持保留状态；
- 内存末端不足一页的部分不得下发；
- 不修改任何已有函数，不动 `default_pmm.c`/`best_fit_pmm.c`。
````

### P1 迭代记录

| 轮次 | 提示词改动 | 现象 | 结论 |
|------|-----------|------|------|
| v1 | 只写"在标记处实现 page_init" | 生成的代码把 `memory_base` 当空闲区起点，忽略 OpenSBI/内核占用 | 需求缺失：必须写清已用空间边界 |
| v2 | 补 `[RELY]` 的宏与结构体，加"三个全局量"要求 | 编译通过，但空闲区上界未截断、无异常路径；`make qemu` 在 `check_alloc_page()` 处 panic | `[RELY]` 不全：缺 `PHYSICAL_MEMORY_END`、`va_pa_offset`、`PADDR` |
| v3（最终） | 补全 `[RELY]`；`[SPECIFICATION]` 写 3 个 Case + Requirements；明确"描述符保持 reserved" | `physical memory map` 正确打印、`check_alloc_page() succeeded!`、`[Test 1] Page Init: PASS` | "描述系统状态"比"描述实现步骤"有效 |

### P1 过程中真实踩到的 bug（报告"AI 协作经验"用）

1. **骨架编不过**：`best_fit_pmm.c` 的测试函数引用 `free_list`/`nr_free`（未定义），`pmm.c` 调用了未定义的
   `page_init` —— 用最小桩解锁；这两个坑分别属于甲和乙。
2. **QEMU ≥ 5.x 下内核零输出**：`-d int` 显示 `epc=0x80200038 exec_page_fault`。根因是 `csrw satp` 之后
   PC 还是物理地址，而启动页表只映射高地址 → 给 `boot_page_table_sv39` 补 `[2]` 低 1 GiB 恒等映射。
3. **PTE 的 U 位陷阱**：修第 2 条时把 `0xcf & ~0x10` 算成 `0xbf`，`0xbf` 的 bit4 恰好是 U=1，
   新版 QEMU 禁止 S 模式从 U=1 页取指 → 又 `exec_page_fault`。最终用 `0xcf`（U=0）。
4. **`make grade` 会 `make clean`**：跑完评分后 `bin/` 被清空，再直接 `make qemu` 会报
   `could not load kernel`。纪律：`make grade` 之后要 `make` 一次。

---

## P2 Best-Fit 分配器实现与 `pmm_manager` 切换（2412386-唐港）

### P2 最终版（`best_fit_check()` 一次通过、`make grade` 为 20/20）

```text
[PROMPT]
请修改 kern/mm/best_fit_pmm.c 文件，将当前 //lab2 function2-2 YOUR CODE 下方的内部
状态以及空的接口函数 best_fit_init、best_fit_init_memmap、best_fit_alloc_pages、
best_fit_free_pages、best_fit_nr_free_pages 补全为完整的 Best-Fit 物理内存管理器实现，
不要修改 basic_check 和 best_fit_check 两个测试函数，也不要修改文件末尾的
best_fit_pmm_manager 注册结构体。

[RELY]
1. 依赖 pmm.h 和 list.h 的数据结构：Page、free_area_t、list_entry_t 等。
2. free_list 被测试函数当作结构体整体赋值，必须使用当前的宏定义：
   #define free_list (free_area.free_list)   #define nr_free (free_area.nr_free)
3. 内存块的 property 字段表示该空闲块的总页数，只有空闲块头才拥有 PG_property 标志，
   通过 SetPageProperty 设置。
4. 依赖 list.h 的链表宏（list_init/list_add/list_add_before/list_next/list_prev/
   list_del/list_empty/le2page）和 memlayout.h 的 Page 结构体与标志位宏
   （SetPageProperty/ClearPageProperty/PageReserved/PageProperty）。
5. 其他规范可以参考 kern/mm/default_pmm.c 中 First-Fit 的实现。

[GUARANTEE]
必须实现以下 5 个函数，且保证原有测试函数通过：
1. best_fit_init：初始化空闲链表和计数器。
2. best_fit_init_memmap：将一段连续的空闲物理内存按物理地址升序插入链表。
3. best_fit_alloc_pages：核心分配函数。
4. best_fit_free_pages：释放并合并函数。
5. best_fit_nr_free_pages：返回总空闲页数。

[SPECIFICATION]
1. best_fit_init：使用 list_init(&free_list) 初始化链表，并将 nr_free 设为 0。
2. best_fit_init_memmap：断言 n>0；用 assert(PageReserved(p)) 验证传入页是保留状态；
   遍历 n 页清除 flags 和 property、引用计数设为 0；base->property 设为 n 并调用
   SetPageProperty(base) 标记块头；nr_free += n；然后将该块升序插入 free_list。
3. best_fit_alloc_pages：断言 n>0；若 n>nr_free 返回 NULL；遍历整个 free_list，寻找
   p->property >= n 的块中**最小**的块 page；若找到，记录前驱节点并将其 list_del；
   如果 page->property > n（需要切分），将剩余部分设为新块头、重新设置 property、
   调用 SetPageProperty，然后保持有序地插入链表（即 prev 之后）；更新 nr_free -= n；
   调用 ClearPageProperty(page)；返回 page。
4. best_fit_free_pages：断言 n>0；遍历 n 页，断言它们非保留且非空闲
   （!PageReserved(p) && !PageProperty(p)）；清除 flags 和 ref；设置 base->property=n、
   调用 SetPageProperty(base)、nr_free += n；将 base 按物理地址升序插入链表；
   向前合并：检查前驱 p，若 p + p->property == base，则 p->property += base->property、
   ClearPageProperty(base)、list_del(&(base->page_link))、base 更新为 p；
   向后合并：检查后继 p，若 base + base->property == p，则 base->property += p->property、
   ClearPageProperty(p)、list_del(&(p->page_link))。
5. best_fit_nr_free_pages：返回 nr_free。
```

### P2 迭代记录

本次生成没有出现运行期 bug——实现一次通过 `best_fit_check()`，`make grade` 直接
`Total Score: 20/20`。迭代主要体现在提示词的精确化上，共 3 处：

| 版本 | 问题 | 修正 |
|---|---|---|
| v1 | RELY 第 4 条写"依赖 page2pa 等地址转换宏以及 ROUNDUP" | 实际 Best-Fit 的有序插入靠 `struct Page *` 指针比较 `base < page`（pages 数组连续 ⇒ 指针序=物理地址序），用不到 page2pa/ROUNDUP（那是 `page_init` 和测试函数用的）。改为"依赖 list.h 链表宏 + memlayout.h 标志位宏" |
| v1 | `alloc` 未提"正好装下"的提前退出 | SPECIFICATION 第 3 条补充 `property == n` 时 `break`（纯优化，不影响正确性） |
| v1 | `free_pages` 只说"清除状态"、措辞模糊 | SPECIFICATION 第 4 条明确为"清除 flags 和 ref"（`property` 由 `base` 之后统一设为 n） |

---

---

## P3 Buddy System 模块生成（2412440-王朝阳）

### v1

```text
[PROMPT]
在 ucore Lab2 中新增 kern/mm/buddy_pmm.c 和 buddy_pmm.h，用伙伴系统实现
struct pmm_manager 的六个函数，并导出 buddy_pmm_manager 与 buddy_check()。

[RELY]
- kern/mm/pmm.h：struct pmm_manager { name/init/init_memmap/alloc_pages/free_pages/nr_free_pages/check }
- kern/mm/memlayout.h：struct Page、PG_reserved/PG_property、
  SetPageProperty/ClearPageProperty/PageProperty、free_area_t、le2page
- kern/mm/pmm.h：pages / npage / nbase / page2ppn / page2pa / pa2page / nr_free_pages()
- libs/list.h：list_init / list_add / list_add_before / list_del / list_empty
- 参照 kern/mm/default_pmm.c 的写法（它的 init_memmap 只做"有序插入 + 双向合并"）

[GUARANTEE]
- 不修改 pmm.c、default_pmm.c、best_fit_pmm.c，不切换活动的 pmm_manager
- 不新增全局变量前缀冲突：内部状态一律 static
- 代码必须能过 -Werror：不留未使用变量、比较统一用 size_t / uintptr_t

[SPECIFICATION]
1. 块大小 = 2^order 页，order ∈ [0, 11]；每阶一条空闲链表 buddy_free_list[order]
2. 首页带 PG_property，property 存页数；块内其他页必须 ClearPageProperty
3. init_memmap(base, n)：把 [base, base+n) 贪心切成"阶对齐 + 不越界"的最大块挂表
4. alloc_pages(n)：n 向上取到 2 的幂的阶 → 找第一个非空阶 → 逐级分裂，上半块挂回低一阶
5. free_pages(base, n)：同阶伙伴且伙伴也空闲就反复合并，期间维护 buddy_nr_free
6. 释放时用 PageProperty 判断"是不是空闲块头"，避免把已分配页当空闲块合并
```

**v1 的问题（v2 的由来）**

1. `init_memmap` 直接把整段挂到"能放下的最大阶"上 → 当 `n` 不是 2 的幂（真实情况：扣掉内核镜像与页描述符后总是奇数段）时，块起始下标不满足 `idx & (size-1) == 0`，伙伴公式失效，后续合并会跨进不属于自己的区域。
2. `free_pages` 里先合并、后累加 `buddy_nr_free`，合并过程中 `order` 已经升过阶，`released` 的语义被"合并后的块大小"顶掉，`nr_free_pages()` 算出来偏大。
3. 没有做"伙伴下标越界"判断，测试末尾释放整段时会把 `&pages[npage-nbase]` 之后的描述符也当块头读。

### v2（最终版，关键增量）

```text
[PROMPT]
（同 v1）另外必须满足下述三条补丁，否则视为不合格。

[GUARANTEE]
- init_memmap 必须是"贪心切分"，不许整段直接挂表：
    while (done < n) 里从 BUDDY_MAX_ORDER 往下找第一个满足
    size <= n - done && (idx & (size - 1)) == 0 的阶
- free_pages 进循环前先 uint64_t released = buddy_block_size(order);
  每轮合并只改 base/order，最后 buddy_nr_free += released; 只加一次
- 伙伴下标 buddy_idx >= (npage - nbase) 时视为"无伙伴"，直接停止合并
- 同阶判定同时要求 PageProperty(buddy) && buddy->property == bsize
- 还必须判"伙伴归我管"：用 buddy_begin / buddy_pages 记录托管区间，
  buddy 不在 [begin, begin+pages) 内一律不合并（区间外的页属于活动管理器，
  块头也带 PG_property，误并会把 best_fit 的链表拆坏）

[SPECIFICATION]
- 额外提供三个调试辅助：buddy_check_block_pages / buddy_count_blocks / buddy_check_consistent
- check 字段指向 buddy_check()，在 pmm_init() 之后由 init.c 调用
```

**结果**：`buddy_pmm.c` 一次成型的部分是不变量 2（块头标记），v2 补丁修掉的是切分、计数、越界三类问题。

---

## P4 Buddy System 测试函数生成（2412440-王朝阳）

### v1

```text
[PROMPT]
给 buddy_pmm.c 补一个 buddy_check()，做冒烟测试：分配 1 页、释放、看 nr_free 是否复原。

[RELY]
- 活动管理器是 best_fit（乙的实现），Buddy 不能接管 pages[]
- 只能通过 alloc_pages() / free_pages() 从活动管理器借页
- 断言宏 assert() 来自 kern/debug/assert.h，失败会 panic

[GUARANTEE]
- 不得修改 pmm_manager 全局变量
- 不得假设借到的页下标是 2 的幂对齐
- 测试结束必须让 nr_free_pages() 回到进入前的值

[SPECIFICATION]
alloc_pages(1) → free_pages(p,1) → assert(nr_free_pages() == before)
```

**v1 的问题**：只测了"能借能还"，完全没碰伙伴系统的核心——分裂与合并。结果是：把 `idx ^ (1<<order)` 写成 `idx + (1<<order)`、把合并条件写成"只要同阶就合并"，冒烟测试依然全绿。

### v2（最终版，7 组）

```text
[PROMPT]
把 buddy_check() 扩成 7 组，覆盖分裂、合并、对齐、容量、守恒；每组失败要能
从打印的 tag 唯一对应到断言位置。

[RELY]
- 借页接口：alloc_pages(n) / free_pages(base,n)；页内容地址 = page2pa(p) + va_pa_offset
- 描述符复位宏：SetPageReserved / ClearPageReserved / SetPageProperty / ClearPageProperty
- 计数口径：buddy_nr_free 是本管理器内空闲页总数，nr_free_pages() 是活动管理器的

[GUARANTEE]
- 借到的区域先"清洗"：把这段页的 ref/flags/property 全部复位，再交给 buddy_init_memmap
- 归还前同样复位，然后整段 free_pages() 还回去
- 每个断言前打印 tag，例如 cprintf("[Test] Buddy: split ...\n")
- 所有用例跑完后 assert(nr_free_pages() == free_before)

[SPECIFICATION]
1. 借 512 页 → 裁出内部 256 页阶对齐区（best_fit 只保证页对齐，首尾各让出若干页）
2. init 后：nr_free == 256 且 buddy_count_blocks() == 1（256 页整段是一个 order-8 块）
3. 分配 1/2/4/3 页后：nr_free == 256 - (1+2+4+4) == 245，3 页按 4 页扣
4. 乱序释放（d→b→a→c）后：nr_free 回到 256，buddy_count_blocks() == 1
5. 整段 256 页分配 + 再次分配返回 NULL
6. 256 次单页分配，每次都能拿到且互不相同，全部释放后仍合并成 1 块
7. 奇数下标起的 2 页区间：必须切成两个 order-0 块（`blocks == 2`），分配/释放一次后仍为 2 页
8. 收尾：整段归还，assert(nr_free_pages() == free_before)
```

**结果**：7 组里有 3 组（乱序合并、256 次单页、奇数下标区间）在 v1 阶段根本不存在，恰好是它们抓出了合并、计数与越界 bug。

---

## P5 SLUB 模块生成（2412440-王朝阳）

### v1

```text
[PROMPT]
新增 kern/mm/slub.c 与 slub.h，实现 kmem_cache_create / kmem_cache_alloc /
kmem_cache_free 三个接口，底层用 alloc_pages(1) 与 free_pages(p,1)。

[RELY]
- alloc_pages/free_pages 来自当前活动管理器
- page2pa(page) 给物理地址；内核可直接访问的虚拟地址 = page2pa(page) + va_pa_offset
  （va_pa_offset = 0xFFFFFFFF40000000，不要用 PADDR 的反函数去"猜"）
- pa2page(pa) 把物理地址转回描述符；PADDR(kva) 要求 kva >= KERNBASE，否则 panic
  （注意：低端物理页的直接映射地址落在 KERNBASE 以下，不能拿 PADDR 去反查）

[GUARANTEE]
- 不新增 pmm_manager，不改活动管理器
- 缓存数量上限 SLUB_MAX_CACHES、单 slab 对象数上限 SLUB_MAX_OBJS，越界返回 NULL
- 所有失败路径返回 NULL，不 panic

[SPECIFICATION]
1. struct slab 放在页首，对象区从 ROUNDUP(sizeof(struct slab), 8) 开始
2. 空闲对象的前 8 字节存下一个空闲对象地址（freelist 内嵌）
3. 页 → slab：直接按页首地址强转；对象 → slab：obj & ~(PGSIZE-1)
4. cache 维护 partial / full 两条 slab 链表；partial 空则新建 slab
5. 分配：取 partial 头 slab 的 freelist 头，inuse 满则挪到 full
6. 释放：inuse 减到 0 时整页 free_pages，否则从 full 挪回 partial
```

**v1 的问题**

1. 对象区起点写成 `sizeof(struct slab)`，没有 ROUNDUP → 8 字节对齐假设在 `sizeof(struct slab) % 8 != 0` 时破裂，freelist 指针非对齐访问。
2. 用 `PADDR(obj)` 求"对象 → 页"，方向就错了：`PADDR` 输出的是物理地址而不是页描述符，
   还要再 `pa2page` 一次；更糟的是它对低于 `KERNBASE` 的 kva 直接 panic，而低端物理页的
   直接映射地址恰好低于 `KERNBASE`。
3. `kmem_cache_free` 先 `inuse--` 再判断 `was_full`，导致"从满变不满"的那次无法把 slab 从 full 挪回 partial，slab 永久卡在 full 链表里。

### v2（最终版，关键增量）

```text
[GUARANTEE]
- 对象区起点必须是 #define SLUB_OBJ_OFFSET (ROUNDUP(sizeof(struct slab), SLUB_ALIGN))，
  SLUB_ALIGN = sizeof(void*)
- 页内容地址统一用 static void *slub_page_to_kva(struct Page *p)
    { return (void *)(page2pa(p) + va_pa_offset); }
  禁止对对象地址使用 PADDR
- kmem_cache_free 里 bool was_full = (slab->inuse == slab->total); 必须在 inuse-- 之前取

[SPECIFICATION]
- 额外提供 slub_init() 清空缓存表，slub_check() 做四组自检
- 归还整页时用 free_pages(slub_slab_to_page(slab), 1)，
  其中 slub_slab_to_page 用 pa2page((uintptr_t)slab - va_pa_offset) 反查，
  不走 PADDR（低端物理页的映射地址低于 KERNBASE，PADDR 会误 panic）
```

---

## P6 SLUB 测试函数生成（2412440-王朝阳）

### v1

```text
[PROMPT]
写 slub_check()：建一个 32 字节的 cache，分配 10 个对象再全部释放，不崩溃即通过。

[RELY]
- 对象不复位也能"看起来没事"，因为 freelist 只用前 8 字节
- nr_free_pages() 只在活动管理器层面有意义

[GUARANTEE]
- 不能只看"没 panic"，要有可判定的断言
- 结束时 nr_free_pages() 必须回到进入前的值

[SPECIFICATION]
1. 分配 10 个 → 两两不同 → 全部释放 → cache->inuse == 0
2. assert(nr_free_pages() == free_before)
```

**v1 的问题**：只测一个尺寸，"同尺寸复用 cache""跨 slab 分配""整页归还"三条路径都没走；也完全没有校验对象地址的合法性，`objs_per_slab` 或对象区起点算错时，只要不越页就照样"分配成功"。

### v2（最终版，4 组）

```text
[PROMPT]
slub_check() 覆盖四组：多尺寸跨 slab、多 cache 交错分配释放、边界、页数复原。

[GUARANTEE]
- 每个对象都要校验"落在对象区内且不越页"：off >= SLUB_OBJ_OFFSET && off + obj_size <= PGSIZE
- 同一批分配的对象地址两两不同（否则说明 freelist 被复用/踩坏）
- 洗牌用确定性伪随机（固定种子 LCG），保证可复现
- 释放时不依赖"记住属主"，而是用 slub_slab_of(obj)->cache 反推，顺带验证反查链路
- 收尾 assert(nr_free_pages() == free_before)

[SPECIFICATION]
1. 五档尺寸 16 / 40 / 96 / 256 / 1024，各建一个 cache；每档分配 objs_per_slab*3+1
   （上限 256）个对象，保证跨至少 3 个 slab，全部释放后 cache->inuse == 0 且 slabs == 0
2. 交错：3 个 cache 交替分配 24 个对象 → 确定性洗牌 → 按洗牌顺序释放，最后 inuse 全为 0、
   partial / full 两条链表都空
3. 边界：free(NULL) 空操作、同尺寸 create 复用同一 cache、create(PGSIZE*2) 返回 NULL、
   alloc/free 往返一次后 slabs 归零
4. 页数复原：所有 cache 清空后 nr_free_pages() == free_before
```

**结果**：v2 的第 1 组抓出了 `SLUB_OBJ_OFFSET` 没有对齐、`objs_per_slab` 算错；第 2 组抓出了 `was_full` 取值时机错误（slab 卡在 full 导致后续分配失败）；第 3 组抓出了 `kmem_cache_create` 缺少尺寸上限检查。

---

### 迭代记录汇总

| Prompt | v1 目标 | v1 暴露的问题 | v2 增量 |
|---|---|---|---|
| P3 Buddy 实现 | 六个函数能跑通 | 非 2 幂区间、计数覆盖、伙伴越界、并到别人的块 | 贪心切分 + `released` 提前存 + 越界判定 + 托管区间判定 |
| P4 Buddy 测试 | 冒烟（借还各 1 页） | 分裂/合并/对齐全未覆盖 | 7 组用例 + 借页清洗 + 计数守恒 |
| P5 SLUB 实现 | 三接口能跑通 | 对象区未对齐、误用 `PADDR`、`was_full` 时机 | `SLUB_OBJ_OFFSET`/`slub_page_to_kva`/提前取 `was_full` |
| P6 SLUB 测试 | 单尺寸 10 对象 | 复用/跨 slab/整页归还未覆盖 | 4 组用例 + 内容校验 + 确定性洗牌 |

> 共同经验：**v1 用来暴露"我不知道自己不知道什么"**，v2 把暴露出来的东西写成 `[GUARANTEE]` 里的禁止项。
> 四份 prompt 里真正起作用的部分，几乎都是"不许……"这种否定式约束。
