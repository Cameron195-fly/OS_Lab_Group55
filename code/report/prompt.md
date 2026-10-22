# Lab2 · 丙的 AI 协作 Prompt 记录（P3–P6）

> 说明：每份 prompt 用四段式书写。
>
> | 段 | 作用 |
> |---|---|
> | `[PROMPT]` | 直接交给 AI 的任务描述，一句话说清"做什么" |
> | `[RELY]` | 依赖的上下文：文件、结构体、宏、已有函数，避免 AI 凭空造 API |
> | `[GUARANTEE]` | 硬约束/禁止项，写"不许做什么"比写"要做什么"更能防跑偏 |
> | `[SPECIFICATION]` | 可验收的细节规格：数据结构、算法、边界、输出 |
>
> 每份 prompt 都经历了 v1（能跑）→ v2（把踩到的坑写回约束里）两轮。P3–P6 与报告 1.3 / 2.3 节对应。

---

## P3 · Buddy System 实现

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

## P4 · Buddy System 测试

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

## P5 · SLUB 实现

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

## P6 · SLUB 测试

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

## 迭代记录汇总

| Prompt | v1 目标 | v1 暴露的问题 | v2 增量 |
|---|---|---|---|
| P3 Buddy 实现 | 六个函数能跑通 | 非 2 幂区间、计数覆盖、伙伴越界、并到别人的块 | 贪心切分 + `released` 提前存 + 越界判定 + 托管区间判定 |
| P4 Buddy 测试 | 冒烟（借还各 1 页） | 分裂/合并/对齐全未覆盖 | 7 组用例 + 借页清洗 + 计数守恒 |
| P5 SLUB 实现 | 三接口能跑通 | 对象区未对齐、误用 `PADDR`、`was_full` 时机 | `SLUB_OBJ_OFFSET`/`slub_page_to_kva`/提前取 `was_full` |
| P6 SLUB 测试 | 单尺寸 10 对象 | 复用/跨 slab/整页归还未覆盖 | 4 组用例 + 内容校验 + 确定性洗牌 |

> 共同经验：**v1 用来暴露"我不知道自己不知道什么"**，v2 把暴露出来的东西写成 `[GUARANTEE]` 里的禁止项。
> 四份 prompt 里真正起作用的部分，几乎都是"不许……"这种否定式约束。
