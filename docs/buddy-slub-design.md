# Buddy / SLUB 设计笔记

## Buddy

- 块大小 = 2^order 页，order ∈ [0, 11]（最大 8 MiB）
- 每阶一条空闲链表：`buddy_free_list[order]`
- 块头：`PG_property` 置位，`property` = 块**页数**（沿用 First-Fit 语义）
- 伙伴下标：`buddy_idx = idx ^ (1 << order)`
- 下标对齐 == 物理地址对齐：`nbase = 0x80000` 是 2^19 的倍数
- 分配：`order = ceil(log2(n))`，从该阶向上找第一个非空，取块后逐级分裂
- 释放：与同阶空闲伙伴反复合并升阶；伙伴必须落在本管理器托管区间内
  （`buddy_begin / buddy_pages`），区间外的块头属于其他管理器，不能合并

三个必须守住的不变量：

1. 链表中每个块都是从"阶对齐"的页开始，长度正好 `2^order`；
2. 除块头外，块内其余页都不带 `PG_property`（否则会被当成另一个块的块头，出现重叠）；
3. 所有块长度之和 == `buddy_nr_free`。

## SLUB

- 两层：页层（`alloc_pages`/`free_pages`）+ 对象层（`kmem_cache`）
- slab 元数据放页首：`struct slab { cache, freelist, inuse, total, slab_link }`
- 对象区从 `ROUNDUP(sizeof(struct slab), 8)` 开始
- freelist 内嵌在对象前 8 字节
- 由对象地址反推 slab：`obj & ~(PGSIZE - 1)`
- `inuse` 归零 → 整页还给页层

## 为什么两个模块都不切换活动 pmm_manager

`make grade` 的 Test 2 直接比较 `pmm_manager != &best_fit_pmm_manager`，一旦把 Buddy 设成活动
管理器，Test 2 立刻从 10 分变 0 分。所以 Buddy/SLUB 都采用"自检"形态：

1. 从当前活动管理器（Best-Fit）借一段私有页；
2. 用 Buddy/SLUB 自己管这段页，跑完整测试；
3. 复位描述符（`flags/property/ref`），把整段页原样还回去；
4. 断言 `nr_free_pages()` 回到借之前的值。

第 4 步是隔离性的证据：加分项既不会污染主线的 20/20，也不会掩盖自身的页面泄漏。
