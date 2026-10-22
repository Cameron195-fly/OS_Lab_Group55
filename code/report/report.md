# Lab2 实验报告 · 丙（Buddy System + SLUB）

> 本文只包含丙负责的章节：**四、扩展任务：Buddy System**、**四、扩展任务 Challenge：SLUB**、
> **六.2 AI 协作开发的经验**。其余章节（实验基本信息、一/二/三、功能模块一/二/三、五、六.1）
> 由甲、乙分别撰写，合并时按"基本信息 → 三 → 功能模块一 → 功能模块二 → 功能模块三 → 五 →
> 扩展任务 Buddy → 扩展任务 SLUB → 六.1 → 六.2"的顺序拼接即可。

## 0. 我负责的部分与验收口径

| 项 | 内容 |
|---|---|
| 练习 5 | 功能模块二 · 任务三：Buddy System（两份 prompt + 测试），新增 `kern/mm/buddy_pmm.c/.h` |
| 练习 6 | 扩展 Challenge：SLUB（两份 prompt + 测试），新增 `kern/mm/slub.c/.h` |
| 报告 | 两个扩展任务章节 + 六.2 |
| prompt | P3（Buddy 实现）、P4（Buddy 测试）、P5（SLUB 实现）、P6（SLUB 测试） |

验收口径与全组一致（`make grade` 必须仍是 20/20）：

```text
-check page init:                          OK
-check best fit:                           OK
Total Score: 20/20
```

**关键约束**：`tools/test_lab2.c` 里 Test 2 是 `pmm_manager != &best_fit_pmm_manager` 就直接判失败，
所以 Buddy/SLUB **不能**替换活动管理器。两者都以"自检函数"的形态接入：从当前管理器借一段私有页 →
自己管这段页跑测试 → 复位描述符 → 整段归还 → 断言 `nr_free_pages()` 复原。
这样加分项不会影响主线评分，也不会因为自身泄漏而悄悄改坏别人的 Test 1。

---

## 一、扩展任务：Buddy System

### 1.1 设计

Buddy 的全部状态只有三样：**每阶一条空闲链表**、**块头标记**、**空闲页计数**。

```c
#define BUDDY_MAX_ORDER 11                  /* 最大块 2^11 页 = 8 MiB */
#define BUDDY_NR_ORDERS (BUDDY_MAX_ORDER + 1)

static list_entry_t buddy_free_list[BUDDY_NR_ORDERS];
static size_t buddy_nr_free = 0;
```

三条不变量（也是后面所有断言的依据）：

1. 链表 `buddy_free_list[order]` 上的每个块，起始页下标都满足 `idx & (2^order - 1) == 0`，
   长度正好 `2^order` 页；
2. 块首页带 `PG_property`，`property` 存**页数**；块内其余页都不带这个位
   （否则会被误认成另一个块的块头，出现"两个空闲块重叠"）；
3. 所有块长度之和恒等于 `buddy_nr_free`。

"下标对齐"之所以等于"物理地址对齐"：`page2ppn = page - pages + nbase`，而
`nbase = DRAM_BASE / PGSIZE = 0x80000 = 2^19`，是 2 的幂，所以低位的对齐关系在"下标"和
"物理页号"之间是一一对应的。这也是伙伴公式 `idx ^ (1 << order)` 能直接用的前提。

### 1.2 三个核心函数

**`buddy_init_memmap(base, n)`：贪心切成阶对齐块。**

`page_init()` 交给分配器的空闲区只保证"页对齐"，长度也不保证是 2 的幂（要扣掉内核镜像和
描述符数组占的页）。如果整段直接挂到某个阶上，后续合并就会把不属于同一伙伴的两块强行合并。
所以这里做贪心切分：

```c
while (done < n) {
    struct Page *blk = base + done;
    size_t idx = buddy_page_index(blk);
    int order = BUDDY_MAX_ORDER;
    while (order > 0) {                      /* 找"放得下且阶对齐"的最大阶 */
        size_t size = buddy_block_size(order);
        if (size <= n - done && (idx & (size - 1)) == 0) break;
        order--;
    }
    ...                                      /* 挂到 buddy_free_list[order] */
    done += buddy_block_size(order);
}
```

这样无论输入区间怎么歪，落到链表上的块一定满足不变量 1。

**`buddy_alloc_pages(n)`：向上取阶 + 逐级分裂。**

- `order = ceil(log2(n))`：请求 3 页实际按 4 页扣，`buddy_nr_free` 也按 4 页减；
- 从 `order` 阶往上找第一个非空链表 `cur`，摘下这块；
- `cur → order` 逐级分裂：下半块留着自己用，上半块（`page + 2^cur`）挂回低一阶链表；
- 返回的块头必须 `ClearPageProperty`，它已经不是空闲块了。

**`buddy_free_pages(base, n)`：伙伴合并。**

```c
while (order < BUDDY_MAX_ORDER) {
    size_t bsize = buddy_block_size(order);
    size_t buddy_idx = buddy_page_index(base) ^ bsize;
    if (buddy_idx >= npage - nbase) break;                 /* 伙伴在物理内存范围外 */
    struct Page *buddy = pages + buddy_idx;
    if (buddy < buddy_begin || buddy >= buddy_begin + buddy_pages) break;  /* 不归我管 */
    if (!PageProperty(buddy) || buddy->property != bsize) break;  /* 不是同阶空闲块 */
    list_del(&buddy->page_link);
    ClearPageProperty(buddy);
    if (buddy < base) base = buddy;                        /* 合并后取较小者 */
    order++;
}
```

四个易错点：

1. `released = 2^(最初的 order)` 必须在合并**之前**存好，计数只能 += 它；
   写成 `+= 2^合并后的 order` 会多算；
2. 合并条件是"伙伴是**同阶**空闲块"，只判 `PageProperty` 会把不同阶的块并进来；
3. 合并后 `base` 要取两者中地址较小的那个，否则块头会指到后半块；
4. 伙伴还必须落在**本管理器托管的区间**内。区间外的描述符归活动管理器（best_fit）管，
   它们的块头同样带 `PG_property`，只判"同阶 + property 相等"就可能并到别人的块里，
   把 best_fit 的空闲链表拆坏（这一步在自检第 6 组暴露过）。

### 1.3 spec 与 prompt 是怎么写的

P3 负责"模块实现"、P4 负责"自检"，都按四段式写（完整内容见 `report/prompt.md`）：

- `[PROMPT]`：一句话说清要做什么（Buddy 物理页分配器，接口对齐 `struct pmm_manager`）；
- `[RELY]`：把 AI 必须依赖的既有事实列全——`struct Page` 的字段、`PG_property` 语义、
  `page2ppn` 与 `nbase`、`le2page/list_add/list_del` 的用法、`npage - nbase` 是可管理页数；
- `[GUARANTEE]`：把不变量写成"必须始终成立"的约束（阶对齐、块内只有块头带 property、
  长度和等于计数、不切换活动管理器）；
- `[SPECIFICATION]`：逐条列出函数签名、边界条件（`n` 不是 2 的幂、区间不对齐、空链表返回
  `NULL`）和自检的 7 组场景。

三次迭代（真实发生过的偏差）：

| 版本 | 偏差 | 修正 |
|---|---|---|
| P3 v1 | 没写"区间可能不对齐、长度可能不是 2 的幂"，AI 生成的 `init_memmap` 是整段 `list_add` 到某一阶 | 第 6 组自检（未对齐起点 + 2 页）必然挂；在 `[SPECIFICATION]` 补上"必须贪心切出阶对齐块"后重生成 |
| P4 v1 | 自检只做了"分配—释放—计数复原"的冒烟测试，抓不到分裂/合并的错误 | `[SPECIFICATION]` 里改成 7 组场景，并要求每组都调用 `buddy_check_consistent()` 做全结构校验 |
| P3 v2 | 释放时用合并后的 `order` 累加计数 | 增加 `released` 常量，并在 `[GUARANTEE]` 里写明"计数增量只与请求长度有关" |

### 1.4 测试覆盖了哪些核心行为与边界

`buddy_check()` 从活动管理器借 512 页，裁出 256 页阶对齐区域（`order 8`）做测试，共 7 组：

| 测试组 | 覆盖的行为 | 抓什么 bug |
|---|---|---|
| 1 | 对齐区间初始化成一整块（`blocks == 1`，`nr_free == 256`） | 切块逻辑把连续区间切碎 |
| 2 | 1/2/4/3 页分配 + 阶对齐 + 互不重叠（`nr_free == 245`） | 分裂时上下半块搞反、对齐算错、3 页没按 4 页扣 |
| 3 | 乱序释放（d→b→a→c）后合并回单块 | 合并条件写成"只要邻居空闲就并" |
| 4 | 整段 256 页分配 + 空时返回 `NULL` | 失败路径漏判、`nr_free` 算错 |
| 5 | 256 次单页分配再全释放 | 反复分裂合并下的计数漂移 |
| 6 | 奇数下标起点、长度 2 页的区间（起点不对齐，2 页凑不成 order-1 块），再分配/释放一次 | `init_memmap` 只会处理"整段对齐"的输入 |
| 7 | 借出/归还后 `nr_free_pages()` 复原 | 测试自身泄漏页面（会污染 Test 1/2） |

每组除了"结果对不对"，还都调用 `buddy_check_consistent()` 做**全结构校验**：遍历所有阶的链表，
检查块头带 `PG_property`、`property == 2^order`、起始下标阶对齐、块内其余页不带 property、
所有块长度求和等于 `buddy_nr_free`。

### 1.5 为什么这 7 组能有效检验

Buddy 的语义只有三件事：**分裂、合并、计数守恒**。

- 第 2 组管分裂：请求 1/2/4/3 页，正好覆盖"不分裂 / 一层分裂 / 两层分裂 / 向上取整"四种路径，
  并检查每个返回块的阶对齐与互不重叠；
- 第 3、5 组管合并，且用两种极端顺序：第 3 组是**乱序、部分释放**（只有等伙伴也空了才允许合并），
  第 5 组是**穷举式**的全分配再全释放，能暴露"顺序敏感"的合并错误；
- 第 1、4、7 组管守恒与隔离：初始一块、整段分配后归零、最后整段归还；
- 第 6 组专门打"现实中不会给你对齐区间"这个边界，这正是 `page_init()` 真实输入的样子。

这些断言全部是**内部不变量**（阶对齐、块头标志、页面归属互斥、长度求和等于计数），
不依赖具体物理地址，也不依赖 `31927` 这类环境相关的数字，所以换 QEMU 版本、换内核大小都不会误报；
反过来，只要实现里有一处把"伙伴"和"邻居"混为一谈，第 3 组就一定挂。

### 1.6 生成代码过程中出现的 bug 与解决

1. **`init_memmap` 整段挂链表**：第一版没有处理"起点不对齐"，第 6 组断言 `blocks == 2` 失败。
   改成贪心切"能放下的最大阶对齐块"后通过。
2. **释放计数偏大**：`buddy_nr_free += buddy_block_size(order)` 写在合并循环之后，`order` 已经被
   提升，257 页的区间释放后计数会超过区间长度。改为先存 `released` 再合并。
3. **块头没有 `ClearPageProperty`**：分配出去的块头仍带 `PG_property`，`buddy_check_consistent()`
   遍历时把它当成空闲块，出现"同一页属于两个块"。在 `alloc` 摘链后立刻清除。
4. **自检位置放错**：一开始把 `buddy_check()` 写在 `pmm_init()` 里面，结果 `make grade` 注入的
   `test_page_init()` 还没执行就先跑了自检；改到 `kern_init()` 中 `pmm_init();` **之后**，
   两条 PASS 行先打印，加分项再打印。
5. **合并越界到别人的块**：只判"同阶 + property 相等"时，测试区边界外的页也满足条件（那些页正在
   best_fit 的空闲链表里），释放最后一个块会把它并进不属于自己的区域，表现为后续 `make grade`
   的 `check best fit` 偶发失败。加 `buddy_begin / buddy_pages` 记录托管区间，合并前先判
   `buddy ∈ [begin, begin+pages)`。

### 1.7 运行输出（本机实测）

实测环境：WSL2 + Ubuntu 22.04.5，`riscv64-unknown-elf-gcc 10.2.0`，`qemu-system-riscv64 6.2.0`。

```text
memory management: best_fit_pmm_manager
...
check_alloc_page() succeeded!
satp virtual address: 0xffffffffc0206000
satp physical address: 0x0000000080206000
[Test] Buddy: test area = 256 pages at index 1024
[Test] Buddy: alloc(1/2/4/3) ok, nr_free = 245
[Test] Buddy: split/merge round trip ok, blocks = 1
[Test] Buddy: 256 single page allocations and releases ok
[Test] Buddy: PASS (free pages restored to 31927)
```

三个关键数字：`index 1024` 说明借到的私有区间位置（不同环境会变）；`nr_free = 245`
= 256 − (1+2+4+4)，证明 3 页请求按 4 页扣；`blocks = 1` 说明乱序释放后合并回一整块。

---

## 二、扩展任务 Challenge：SLUB

### 2.1 两层架构

SLUB 把"页"和"对象"分开：

```text
kmem_cache_alloc/free          ← 对象层：固定大小、O(1) 取还
        ↓
slub_new_slab / free_pages     ← 页层：直接复用活动 pmm_manager
```

- **页层**：完全复用 `alloc_pages(1)` / `free_pages(page, 1)`，不重复实现物理页管理；
- **对象层**：一个 `kmem_cache` 把自己的页切成 `objs_per_slab` 个等长对象，
  空闲对象用**自身前 8 字节**串成单链表（freelist），不额外占内存。

`struct slab` 放在页首，因此"由对象地址反推 slab"只需一次掩码：

```c
static inline struct slab *slub_slab_of(void *obj) {
    return (struct slab *)((uintptr_t)obj & ~(uintptr_t)(PGSIZE - 1));
}
```

### 2.2 三个接口的语义

| 接口 | 行为 |
|---|---|
| `kmem_cache_create(name, size)` | 对象大小按 8 字节向上对齐；**同尺寸复用同一个 cache**；`sizeof(slab)+obj_size > PGSIZE` 返回 `NULL`；池满返回 `NULL` |
| `kmem_cache_alloc(cache)` | 优先从 `partial` 取 slab，没有就 `slub_new_slab()` 向页层要一页；取 freelist 头，取空后把该 slab 从 `partial` 挪到 `full`；页层无页时返回 `NULL` |
| `kmem_cache_free(cache, obj)` | `obj == NULL` 直接返回；把对象头插回 freelist；`inuse` 归零则整页还给页层；原本是满的则从 `full` 挪回 `partial` |

`kmem_cache_create` 里的 `objs_per_slab = (PGSIZE - SLUB_OBJ_OFFSET) / obj_size`，
其中 `SLUB_OBJ_OFFSET = ROUNDUP(sizeof(struct slab), 8)`，保证对象区不会压到 slab 头。

### 2.3 我的简化点

1. **不做 per-CPU 缓存**：真实 SLUB 每个 CPU 有一份 current slab 以减少锁竞争，本实验是单核
   无并发，直接用 `partial/full` 两条链表；
2. **不做对象构造/析构回调**：接口只有 create/alloc/free，没有 `ctor/dtor`；
3. **slab 全空立即整页归还**：不做"保留空 slab 以复用"的策略，这样 `slabs == 0` 可以当作
   "页层资源已归还"的强断言；
4. **固定 cache 池**（8 个）而不是动态 `kmalloc` 元数据：内核里还没有堆，用静态池最省事。

### 2.4 测试覆盖了哪些行为、为什么有效

| 测试组 | 覆盖的行为 | 抓什么 bug |
|---|---|---|
| 1 | 5 个尺寸档（16/40/96/256/1024）各自批量分配到**跨 slab**，再全部释放 | 对象区起点算错、`objs_per_slab` 算错、slab 满了不挪链表 |
| 1 的偏移断言 | 每个对象 `off >= SLUB_OBJ_OFFSET` 且 `off + obj_size <= PGSIZE` | 对象压到 slab 头 / 越过页尾 |
| 2 | 3 个 cache 交错分配 24 个对象，**确定性洗牌**后按随机顺序释放，并且用 `slub_slab_of(obj)->cache` 反推属主 | 由对象反推 slab 算错、free 到错误的 cache、partial/full 归位错 |
| 3 | `free(NULL)`、同尺寸 create 复用、超页 create 返回 `NULL`、alloc/free 往返 | 边界与失败路径 |
| 4 | 结束时 `nr_free_pages() == free_before` | slab 页没还干净（被 `full` 挂着没取下、`inuse` 判 0 的分支没进） |

为什么有效：

- 第 1 组的对象数刻意取 `objs_per_slab * 3 + 1`（上限 256），保证**至少跨 3 个 slab**，
  这样"分配顺序 = 填满一个 slab 再填下一个"这条逻辑才被真正走到；
- 第 2 组是这套测试的核心：交错分配让 3 个 cache 的 slab 交替增长，洗牌释放则打乱
  "后进先出"，任何"按分配顺序回收"的错误假设都会暴露；用 `slab->cache` 反推属主还顺带验证了
  "对象地址 → 页首 → slab"这条链路；
- 第 3、4 组把"边界"和"资源守恒"分开断言，泄漏时会精确停在 `nr_free_pages() != free_before`
  这一行，而不是在主线的 Test 1 里莫名失败。

### 2.5 生成代码过程中出现的 bug 与解决

1. **`slub_page_to_kva` 忘加 `va_pa_offset`**：第一版写成 `(void *)page`，直接把**描述符数组**
   的地址当成了页的内容地址，一写 freelist 就把描述符改坏。正确写法是
   `page2pa(page) + va_pa_offset`。
2. **`was_full` 判断顺序**：写在 `slab->inuse--` 之后，条件永远为假，导致满 slab 释放一个对象后
   停留在 `full` 链表里，下次分配再也拿不到它（表现为"能分配但 page 越用越多"）。
   改为自减**之前**判断。
3. **对象区起点**：用 `sizeof(struct Page)` 算偏移会越过页尾（`struct Page` 比 `struct slab` 大），
   改成 `ROUNDUP(sizeof(struct slab), 8)`。
4. **`inuse == 0` 分支顺序**：先判断 `was_full` 再做归零判断，空 slab 会被挪回 `partial`
   而不归还页层，`cache->slabs` 永远不归零，第 4 组断言失败。

### 2.6 运行输出（本机实测）

```text
[Test] SLUB: size 16 -> 256 objects, 2 slab(s), all distinct
[Test] SLUB: size 40 -> 256 objects, 3 slab(s), all distinct
[Test] SLUB: size 96 -> 127 objects, 4 slab(s), all distinct
[Test] SLUB: size 256 -> 46 objects, 4 slab(s), all distinct
[Test] SLUB: size 1024 -> 10 objects, 4 slab(s), all distinct
[Test] SLUB: 24 interleaved objects from 3 caches released
[Test] SLUB: PASS (free pages restored to 31927)
```

每个档位的 `slab(s)` 至少是 2，说明真的跨了 slab；最后一行的数字与 Buddy 自检前后一致，
说明 SLUB 没有留下任何未归还的页。

---

## 三、六.2 AI 协作开发的经验

### 3.1 有效的做法

1. **四段式 prompt 比"帮我写个 buddy 分配器"有效一个数量级**。
   `[RELY]` 把 `struct Page`、`page2ppn`、`nbase`、`le2page` 这些"AI 看不到的本地事实"写清楚，
   能把大部分"编出来的 API"挡在第一步。
2. **先要"不变量"，再要"代码"**。让 AI 先把不变量用 3 条话写出来（阶对齐 / 块头唯一 /
   长度和等于计数），再让它在这些约束下写函数，比直接要代码更容易 review。
3. **让 AI 写自检，且要求"内部不变量"而不是"打印看看"**。7 组断言里有 5 组是结构性检查
   （`buddy_check_consistent`），这类断言与具体地址无关，换环境不会误报，也不会被"正好蒙对"。
4. **一次只要一个函数**。Buddy 分 init/alloc/free 三次要，SLUB 分 create+new_slab / alloc+free /
   check 三次要，每次都能编译、能跑，问题定位范围小。
5. **把"禁止事项"写进 `[GUARANTEE]`**：例如"不得修改 `pmm_manager` 指向"、"不得改
   `default_pmm.c`"。AI 很自然会想"把它设成活动管理器更方便测试"，这条约束省了一次返工。

### 3.2 踩过的坑

1. **AI 会假设"输入是理想的"**：第一版 `init_memmap` 默认区间阶对齐、长度是 2 的幂；
   而 `page_init()` 给的区间两头都不满足。这类问题必须在 prompt 里显式写明，不能靠 AI 猜。
2. **AI 生成的注释比代码更"正确"**：注释写着"与伙伴合并"，代码却是"与链表前一个块合并"。
   review 时要以代码为准，用自检断言去证伪。
3. **"看起来对"的计数错误最难发现**：`nr_free += 2^order` 在大多数路径下是对的，
   只有"释放 3 页（按 4 页算）并发生合并"时才错。最后是第 2 组的 `nr_free == 245`
   和第 5 组的穷举才把它逼出来。
4. **跨层假设最危险**：SLUB 的 `page2pa(page) + va_pa_offset`，AI 一开始写的是
   `(void *)page`。这类"描述符地址 vs 页内容地址"的混淆不会编译报错，只会在运行时把
   `struct Page` 数组写坏，症状离现场很远。所以我在 `[RELY]` 里专门写了一条
   "`alloc_pages()` 返回的是 `struct Page *`，不是可写内存"。

### 3.3 结论

AI 在我们这个任务里更像"打字很快、但完全没有上下文的新同事"：算法骨架（分裂/合并、
freelist、partial/full）一次就能给对，**但对本地约定（页描述符布局、活动管理器不能换、
`PG_property` 的语义）一无所知**。人能提供的价值集中在三件事上：把本地事实写进 prompt、
把不变量写成断言、把"看起来对"的实现用边界用例打穿。这三件事恰好也是本实验真正想训练的东西。
