# Lab4 实验报告（A：2413541-赖鸿毅）

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 4：进程管理 |
| **小组成员** | 2413541-赖鸿毅（A）、[待填：B 学号-姓名]、[待填：C 学号-姓名] |
| **完成日期** | 2026-11-10 |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2413541-赖鸿毅（A） | lab2 PMM 基线（best-fit、`page_init`）、功能模块一（`page_insert`/`page_remove`）、整体逻辑线、`get_pte` Challenge |
| [待填：B 学号-姓名] | 功能模块二（`alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm`）、`cpu_idle`/`proc_run`、分页模式 Challenge、知识点对照 |
| [待填：C 学号-姓名] | lab3 trap 基线、功能模块三（`schedule`/`switch_to`）、模块三报告、`local_intr_save` Challenge、未覆盖知识点、测试截图 |

实验报告的协作方式：每人写自己的 `A-report.md`、`B-report.md`、`C-report.md`，最后由 B 机械拼接为 `report/report.md`；不安排交叉复核，只保留“编译通过、测试通过”两个客观门禁。

---

## 一、实验目的

本实验的主要目的是：

1. 在 lab2/lab3 的物理内存管理和中断基础上，补齐多级页表的映射建立、撤销和 TLB 维护，使内核能够可靠地管理虚拟地址空间。
2. 理解进程控制块、内核线程、内核栈、上下文和中断帧之间的关系，能够创建 `idleproc` 与 `initproc`，并让新线程第一次正确运行。
3. 理解 `cpu_idle -> schedule -> proc_run -> switch_to` 的调度和上下文切换链路，掌握临界区、就绪队列和首次运行恢复路径。
4. 通过 spec + prompt 的方式使用 AI 辅助生成代码，记录 AI 生成代码中的问题、定位过程和修正方法。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2413541-赖鸿毅（A） | DeepSeek Harness | deepseek-flash | 使用终端/编辑器协作，人工检查并提交 |
| [待填：B 学号-姓名] | [待填] | [待填] | [可选] |
| [待填：C 学号-姓名] | [待填] | [待填] | [可选] |

**构建与运行环境：**

- WSL Ubuntu 22.04；
- `riscv64-unknown-elf-gcc`、`riscv64-unknown-elf-gdb`；
- `qemu-system-riscv64`；
- `make`、`python3`。

> A 的模块以 `make obj/kern/mm/default_pmm.o` 和 `make obj/kern/mm/pmm.o` 做单文件验证；最终的 `make qemu` 和 `make grade` 在 B/C 代码合并后执行。

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

Lab4 的核心问题不再是“怎样管理物理页”，而是“怎样让一个执行实体拥有自己的地址空间，并且能够被内核创建、登记、调度和切换”。

因此，本章可以概括为三条递进的主线：

1. **虚拟内存页操作**：在 lab2 已有多级页表的基础上，实现 `page_insert` 和 `page_remove`，让“线性地址 ↔ 物理页”的映射可以建立、更新和撤销，并保证页表项、引用计数和 TLB 三者一致。
2. **进程创建与初始化**：引入 `struct proc_struct` 和内核栈，把当前执行现场包装成 `idleproc`，再通过 `kernel_thread`/`do_fork` 创建 `initproc`。
3. **进程调度与切换**：通过 `cpu_idle -> schedule -> proc_run -> switch_to`，把已经准备好的 `initproc` 真正交给 CPU 执行。

对应的执行链可以写成：

```text
kern_init
  -> pmm_init
       -> page_init
       -> check_alloc_page
       -> check_pgdir          // 依赖 page_insert/page_remove
       -> check_boot_pgdir
       -> kmalloc_init
  -> pic_init / idt_init
  -> vmm_init
  -> proc_init
       -> idleproc             // 复用当前执行现场
       -> kernel_thread(init_main)
            -> do_fork
                 -> alloc_proc
                 -> setup_kstack
                 -> copy_mm
                 -> copy_thread
       -> initproc
  -> clock_init / intr_enable
  -> cpu_idle
       -> schedule
            -> proc_run
                 -> switch_to
                      -> forkret
                           -> forkrets
                                -> __trapret
                                     -> kernel_thread_entry
                                          -> init_main
```

这条链的前半段负责“把地址空间和线程对象准备好”，后半段负责“让第一个可运行线程真正跑起来”。两者不能颠倒：没有页表映射，`check_pgdir` 就过不了；没有 PCB 和内核栈，调度器就没有可调度的对象；没有 `context.ra` 和 `trapframe` 的预先组织，`switch_to` 即使完成寄存器恢复，也回不到正确入口。

### 3.2 功能的逐步实现

本章按“先地址空间、再线程对象、最后调度切换”的顺序实现：

1. **先补 lab2/lab3 基线。**
   - lab2 的 best-fit PMM 和 `page_init` 决定物理页描述符、空闲链表和 `alloc_page/free_page` 是否可用；
   - lab3 的 `trapentry.S`、`trap.c` 决定内核能否保存/恢复中断现场、能否响应时钟中断。
   - 这两部分是 lab4 的运行前提，不是可选内容。

2. **再实现功能模块一：页表映射维护。**
   - `get_pte` 负责定位叶子页表项，必要时创建中间级页表；
   - `page_insert` 负责建立或更新映射；
   - `page_remove` 负责撤销映射。
   - 这一层完成后，`check_pgdir()`、`check_boot_pgdir()` 以及后续线程地址空间相关逻辑才有可靠基础。

3. **然后实现功能模块二：进程创建与初始化。**
   - `alloc_proc` 得到干净的 PCB；
   - `proc_init` 把当前执行现场包装成 `idleproc`，并创建 `initproc`；
   - `kernel_thread` 把函数入口和参数整理成临时 `trapframe`；
   - `do_fork` 组合 PCB、内核栈、PID、链表和状态；
   - `copy_thread` 把 `trapframe` 和 `context` 放到新线程自己的内核栈上；
   - `copy_mm` 在当前阶段只承担地址空间接口的占位作用。
   - 完成后系统中至少存在 `idleproc` 和 `initproc`，且 `initproc` 已经处于 `PROC_RUNNABLE`。

4. **最后实现功能模块三：调度与上下文切换。**
   - `cpu_idle` 作为 idle 线程的执行主体，发现 `need_resched` 后调用 `schedule`；
   - `schedule` 从 `proc_list` 中选择下一个 `PROC_RUNNABLE`，找不到时回退 `idleproc`；
   - `proc_run` 更新 `current`、切换 `satp`、刷新 TLB，并调用 `switch_to`；
   - `switch_to` 只保存/恢复 `ra、sp、s0~s11`，不参与调度策略。
   - 由于 `copy_thread` 已经把 `context.ra` 设为 `forkret`，新线程第一次被切换时会沿 `forkret -> forkrets -> __trapret -> kernel_thread_entry` 走到 `init_main`。

### 3.3 A 的实现范围

A 负责第 1 步中的 lab2 基线和第 2 步的功能模块一。对应提交如下：

| commit | 内容 |
|---|---|
| `63e7743` | best-fit PMM 核心 |
| `323a587` | `page_init` |
| `6e32210` | `page_insert` |
| `b293bf7` | `page_remove` |

A 不修改 `vmm.c`、`proc.c`、`trap.c`、`sched.c`、`switch.S` 等 B/C 负责的文件；A 的页表接口通过 `pmm.h` 暴露给后续模块使用。

---

## 四、实验内容与实现

### 功能模块一：虚拟内存页操作

**负责人：** 2413541-赖鸿毅（A）

#### 模块功能描述

**需要实现/修改的函数：**

```c
/* lab2 基线 */
static void page_init(void);
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);

/* lab4 功能模块一 */
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);

/* page_insert 使用的内部辅助函数 */
static void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep);
```

**功能说明：**

本模块负责在现有多级页表上维护“线性地址到物理页”的映射：

- `page_insert` 把一个物理页挂到指定线性地址上，并设置权限；
- `page_remove` 撤销指定线性地址上的映射；
- `page_remove_pte` 是二者共用的叶子页表项清理逻辑。

模块完成后，`pmm_init` 中的 `check_pgdir()`、`check_boot_pgdir()` 才能正常运行，后续线程共享内核地址空间、`proc_run` 切换 `satp` 也才有可靠基础。

本模块还包含 lab2 的两个前提：

1. best-fit 物理页管理器：维护按物理地址排序的空闲块链表，分配时选择“能装下 n 页的最小空闲块”，释放时与前后相邻空闲块合并；
2. `page_init`：从 DTB 读取物理内存范围，建立 `pages` 描述符数组，把内核镜像和描述符数组占用的页标记为 reserved，把剩余连续物理页交给 PMM。

#### 关键实现

**1. `page_insert` 的三种情况**

`page_insert` 不能简单覆盖 PTE，必须先检查原 PTE：

```c
pte_t *ptep = get_pte(pgdir, la, 1);
if (ptep == NULL) {
    return -E_NO_MEM;
}

page_ref_inc(page);
if (*ptep & PTE_V) {
    struct Page *old_page = pte2page(*ptep);
    if (old_page == page) {
        page_ref_dec(page);       // 同一页：抵消刚才的 inc
    } else {
        page_remove_pte(...);     // 不同页：先撤销旧映射
    }
}

*ptep = pte_create(page2ppn(page), perm | PTE_V);
tlb_invalidate(pgdir, la);
return 0;
```

三种情况分别是：

| 原 PTE | 处理 |
|---|---|
| 无效 | 直接写入新 PTE，新页引用 +1 |
| 有效且指向同一页 | 只更新权限，引用计数净变化为 0 |
| 有效但指向其他页 | 先清旧 PTE、旧页引用 −1，再写入新 PTE、新页引用 +1 |

其中“同一页”情况如果也走 `page_remove_pte`，会把旧页引用减到 0，甚至释放正在使用的页；如果只 `page_ref_inc` 不 `dec`，又会让同一页的引用虚高。因此必须单独处理。

`get_pte(..., 1)` 可能分配中间级页表页；如果返回 `NULL`，说明内存不足，`page_insert` 直接返回 `-E_NO_MEM`，不写入叶子 PTE，避免留下半完成映射。

**2. `page_remove` 的幂等性**

```c
void page_remove(pde_t *pgdir, uintptr_t la) {
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep == NULL) {
        return;
    }
    page_remove_pte(pgdir, la, ptep);
}
```

`get_pte(..., 0)` 只查找、不分配。即使返回了页表项指针，PTE 也可能是无效的；`page_remove_pte` 内部会再判断 `PTE_V`。因此：

- 地址没有页表：安静返回；
- 地址有页表但 PTE 无效：安静返回；
- 地址有有效映射：叶子页减引用、必要时 `free_page`、清 PTE、刷新 TLB；
- 重复调用：第二次看到无效 PTE，不会再释放。

**3. 引用计数与页表页**

- `page_insert` 只对**叶子物理页**加引用；
- `page_remove_pte` 只对**叶子物理页**减引用；
- 中间级页表页的引用计数由 `get_pte` 在创建时设置，不在 `page_remove` 中回收；
- `check_pgdir()` 最后会断言 `page_ref(pde2page(boot_pgdir_va[0])) == 1`，如果 `page_remove` 顺手把空页表页释放了，这个断言会失败。

**4. TLB 同步**

修改 PTE 后必须调用：

```c
tlb_invalidate(pgdir, la);
```

否则 CPU 可能继续使用旧翻译。`page_insert` 在更新权限/替换页之后刷新，`page_remove_pte` 在清除 PTE 之后刷新。

#### 最终提示词

完整提示词见 `report/prompt.md` 的 A-1～A-4。最终版的核心约束是：

```text
[CONTEXT]
kern/mm/pmm.c 已有 get_pte、pte_create、pte2page、page_ref_inc/dec、
free_page、tlb_invalidate；default_pmm.c 需要补 lab2 的 best-fit PMM。

[SPEC]
page_insert(pgdir, page, la, perm)：
  - get_pte(pgdir, la, 1) 失败返回 -E_NO_MEM；
  - 原 PTE 无效、同一页、不同页三种情况分别处理；
  - 成功返回 0，权限由 perm 决定；
  - 更新后刷新 TLB。
page_remove(pgdir, la)：
  - get_pte(pgdir, la, 0)，无页表或 PTE 无效时安静返回；
  - 叶子页减引用、归零时 free_page，再清 PTE；
  - 幂等，不回收中间页表页。

[CONSTRAINTS]
只改 default_pmm.c 和 pmm.c 的 lab2/function1 标记；
保留 basic_check/best_fit_check；
不改 vmm.c、proc.c、test_lab4.c；
不使用 lab3 分支整文件覆盖。

[OUTPUT]
只给函数实现和必要注释，不重写整个文件。

[SELF-CHECK]
单文件编译；检查同一页重复映射、不同页替换、重复 remove、
check_pgdir 的页表引用计数、TLB 失效。
```

#### 实现迭代过程

本模块经历过 4 次主要迭代。

##### 第一次迭代：best-fit PMM 和缺失头文件

**遇到的问题：**

- 框架里的 `default_pmm.c` 仍然 `#include <best_fit_pmm.h>`，但目录中没有这个文件；
- `pmm.c` 的 `init_pmm_manager()` 使用 `default_pmm_manager`，而 lab2 的 best-fit 代码可能注册为 `best_fit_pmm_manager`；
- 如果另建 `best_fit_pmm.c`，`free_area` 若设为 `static`，`default_pmm.c` 里的 `best_fit_check()` 就看不到 `free_list/nr_free`。

**问题解决策略：**

- 不新增文件，直接把 best-fit 实现放回 `default_pmm.c`；
- 在同一个文件中定义 `free_area`、`free_list`、`nr_free`；
- 注册 `default_pmm_manager`，与 `pmm.c` 对齐；
- 删除缺失的 `best_fit_pmm.h` 引用，保留 `basic_check()`/`best_fit_check()` 原样。

**结果：**

`default_pmm.c` 可以单独生成 `.o`，best-fit 的空闲块分配、拆分和合并逻辑完整。

##### 第二次迭代：`page_insert` 的引用计数错误

**遇到的问题：**

最初的思路是“先 `page_ref_inc(page)`，再判断原 PTE”。如果原 PTE 已经映射到同一个 `page`，就会变成“inc 一次，然后 remove 旧映射又 dec 一次”；如果 `page_remove_pte` 发现引用归零，还可能把正在使用的页释放掉。

**问题解决策略：**

- 先 `page_ref_inc(page)`；
- 如果原 PTE 有效且 `old_page == page`，只 `page_ref_dec(page)` 抵消，不调用 `page_remove_pte`；
- 只有 `old_page != page` 时才撤销旧映射。

这样同一页重复映射的引用计数净变化为 0，页不会被误释放。

##### 第三次迭代：`page_remove` 误回收页表页

**遇到的问题：**

如果只考虑“删除映射后清空页表”，很容易在 PTE 清空后把空的中间页表页也 `free_page`。但 `check_pgdir()` 期望：

```c
assert(page_ref(pde2page(boot_pgdir_va[0])) == 1);
```

页表页被提前回收会让这个断言失败。

**问题解决策略：**

- `page_remove` 只处理叶子 PTE；
- 中间页表页的生命周期由 `get_pte` 管理，本实验不回收；
- `page_remove_pte` 只对 `pte2page(*ptep)` 的叶子页做 `page_ref_dec`/`free_page`。

##### 第四次迭代：TLB 和 `page_init` 边界

**遇到的问题：**

- 修改 PTE 后没有刷新 TLB，测试可能出现“页表已改、CPU 仍用旧翻译”；
- `page_init` 如果没有把 `mem_end` 截断到 `KERNTOP` 对应的物理上限，`pages` 描述符数组可能越界；
- `pages` 数组和空闲区起点没有按页对齐，会把描述符数组尾部当成可用页。

**问题解决策略：**

- `page_insert` 和 `page_remove_pte` 更新 PTE 后都调用 `tlb_invalidate`；
- `page_init` 先计算 `manageable_end = KERNTOP - PHYSICAL_MEMORY_OFFSET`，必要时截断 `mem_end`；
- `pages` 用 `ROUNDUP((uintptr_t)end, PGSIZE)`，空闲区起点用 `ROUNDUP(PADDR(pages_end), PGSIZE)`，终点用 `ROUNDDOWN(mem_end, PGSIZE)`；
- 所有 `struct Page` 先 `SetPageReserved`，再把描述符数组之后的连续区间交给 `init_memmap`。

#### 关键改进点总结

1. 把 best-fit 实现收回 `default_pmm.c`，避免缺失头文件和 manager 命名不一致；
2. `page_insert` 单独处理“同一页重复映射”，保证引用计数净变化为 0；
3. `page_remove` 只回收叶子页，不回收中间页表页，保证 `check_pgdir()` 的页表引用计数；
4. PTE 修改后统一 `tlb_invalidate`，并在 `page_init` 中处理内存上限和页对齐。

### Challenge：`get_pte()` 与不同分页模式

**负责人：** 2413541-赖鸿毅（A）

#### 1. `get_pte()` 在做什么

`get_pte(pgdir, la, create)` 的目标是：给定页表根地址 `pgdir` 和线性地址 `la`，返回 `la` 对应的**叶子页表项**的内核虚拟地址；如果中间级页表不存在且 `create == 1`，则先分配并初始化中间级页表页。

在 Sv39 下，一个线性地址被分成四段：

```text
+--------9-------+-------9--------+-------9--------+---------12----------+
| VPN[2] / PDX1  | VPN[1] / PDX0  | VPN[0] / PTX   |    page offset      |
+----------------+----------------+----------------+---------------------+
```

`get_pte()` 的代码结构正好对应这三层：

1. `pdep1 = &pgdir[PDX1(la)]`：根页表项；
2. 如果根项无效，分配一个页表页，清零，写 `PTE_U | PTE_V`；
3. `pdep0 = &((pte_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)]`：中间页表项；
4. 如果中间项无效，再分配一个页表页，清零，写 `PTE_U | PTE_V`；
5. 返回 `&((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)]`：叶子页表项。

注意：**叶子页表项本身不需要再分配页表页**，它直接由调用者（例如 `page_insert`）写入物理页号和权限。

#### 2. 为什么两段代码看起来几乎一样

两段代码都在做同一件事：

```text
检查当前级 PTE 的 V 位
  -> 无效且 create=1：alloc_page
  -> set_page_ref(page, 1)
  -> memset(KADDR(page2pa(page)), 0, PGSIZE)
  -> *pdep = pte_create(page2ppn(page), PTE_U | PTE_V)
  -> 继续下一级
```

它们相似的根本原因是：**RISC-V 的每一级页表格式是统一的**。Sv39 的根页表、中间页表、叶子页表都使用相同的 512 项数组、相同的 8 字节 PTE 布局和相同的 V/R/W/X/U 权限位；不同之处只有：

- 取哪 9 位作为索引（`PDX1`、`PDX0`、`PTX`）；
- 当前是第几级；
- 最后一级返回的是叶子 PTE，不再继续分配。

因此代码可以把“检查 V、分配页表页、清零、写非叶子 PTE”这套动作重复两次。Sv32 只有两级，所以只需要一段这样的代码；Sv48 有四级，所以理论上需要三段这样的代码。形式相似不是巧合，而是分页模式“每级结构同构”的直接体现。

#### 3. Sv32、Sv39、Sv48 的异同

| 项目 | Sv32 | Sv39 | Sv48 |
|---|---|---|---|
| 虚拟地址位数 | 32 | 39 | 48 |
| 页表级数 | 2 | 3 | 4 |
| 每级索引位数 | 10 | 9 | 9 |
| 每级页表项数 | 1024 | 512 | 512 |
| PTE 大小 | 4 字节 | 8 字节 | 8 字节 |
| 物理地址位数 | 34 | 56 | 56 |
| `satp` MODE | 1 | 8 | 9 |
| 页大小 | 4 KiB | 4 KiB | 4 KiB |

共同点：

- 都采用多级页表；
- 页内偏移都是 12 位，所以页大小都是 4 KiB；
- PTE 都有 V/R/W/X/U/G/A/D 等权限/状态位；
- 非叶子 PTE 只需要 `V`（以及需要时的 `U`），叶子 PTE 才设置 `R/W/X/U`；
- 地址翻译都是从根页表开始，逐级用 VPN 索引，最后加页内偏移。

差异点：

- Sv32 的 PTE 是 4 字节，PPN 字段较窄；Sv39/Sv48 的 PTE 是 8 字节；
- Sv32 两级，Sv39 三级，Sv48 四级；
- Sv39/Sv48 的虚拟地址需要符号扩展/规范地址检查，Sv32 的地址空间更小；
- `satp` 的 MODE 字段不同，切换页表时必须写对应的模式。

`get_pte()` 当前实现是 **Sv39 专用**的：它只展开 `PDX1` 和 `PDX0` 两级，然后返回 `PTX` 叶子项；它使用的 `PTE_ADDR`、`PTE_PPN_SHIFT` 也是按 8 字节 PTE 设计的。如果要支持 Sv32，PTE 宽度、索引位数和 `PTE_ADDR` 都要改；如果要支持 Sv48，则还要再加一层同样的页表遍历代码。

#### 4. 把“查找”和“分配”合在一个函数里好不好

当前 `get_pte(pgdir, la, create)` 同时承担两件事：

1. 查找页表项；
2. 在 `create == 1` 时分配缺失的中间级页表页。

**优点：**

- 调用者接口简单：`page_insert` 只需要一次 `get_pte(..., 1)`；
- 查找和建立路径集中在一个函数里，页表遍历逻辑不需要重复；
- `create == 0` 时可以直接复用于只读查找（例如 `get_page`）。

**缺点：**

- “查找”是只读语义，“分配”有副作用，两者混在一起后，调用者不容易判断失败时到底改了哪些状态；
- 如果分配了第一级页表页、第二级分配失败，函数返回 `NULL`，中间页表页会留在页表中，失败回滚不完整；
- 不利于单独测试“只查找”路径；
- 如果未来要加锁、按需分页或并发页表修改，查找和分配的生命周期不同，混在一起会更难处理；
- `create` 参数让函数内部出现分支，调用者必须理解这个布尔值的语义。

**是否有必要拆开：**

从操作系统设计角度，拆分更清晰，例如：

```c
pte_t *walk_pte(pde_t *pgdir, uintptr_t la);              // 只查找，不分配
pte_t *get_pte_create(pde_t *pgdir, uintptr_t la);        // 查找，缺失时分配
```

`page_insert` 调 `get_pte_create`，`get_page`/只读查询调 `walk_pte`。这样失败语义、回滚策略和测试边界都更明确。

但在本实验的规模下，页表层级固定、没有并发修改、没有按需分页，保留 `get_pte(..., create)` 这种合并写法是可以接受的：代码短、调用方便，实验测试也能覆盖主要路径。更合理的结论是：

> **教学实验里可以保留合并写法；如果要把这段代码扩展成真正的虚拟内存子系统，应该把“遍历查找”和“页表页分配”拆开，至少把失败回滚独立出来。**

#### 5. 小结

- 两段代码相似，是因为 Sv39 的根级和中间级页表结构完全同构，执行的都是“检查 V → 分配页表页 → 清零 → 写非叶子 PTE”；
- Sv32 少一级，Sv48 多一级，所以相似代码的段数会变化；
- 当前 `get_pte` 是 Sv39 专用实现，不能直接用于 Sv32/Sv48；
- 查找与分配合并写法在实验中可接受，但工程上拆分更利于错误处理、测试和后续扩展。

---

## 五、测试与验证

A 的模块先做单文件验证：

```bash
cd /mnt/d/桌面/os_lab/code
make obj/kern/mm/default_pmm.o
make obj/kern/mm/pmm.o
```

预期两个 `.o` 都能生成。

集成后需要确认：

```text
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
[Test PMM] PASS
```

> A 完成本模块时 B/C 的 `proc.c`、`trap.c`、`sched.c`、`switch.S` 还没有合并，因此完整的 `make qemu`/`make grade` 要等集成阶段执行；A 的最终 `[Test PMM] PASS` 截图在集成通过后补到 `report/images/`。

---

## 六、A 的个人总结

这次实现的核心体会是：页表操作不能只看“PTE 有没有写进去”，还要同时检查引用计数和 TLB。`page_insert` 最容易错的地方是同一页重复映射；`page_remove` 最容易错的地方是顺手回收页表页；`page_init` 最容易错的地方是内存边界和页对齐。把这三类问题分开验证，比直接跑完整内核更容易定位。

AI 生成代码时，最有效的做法不是让它“照着 ucore 写一遍”，而是把接口、约束、错误路径和自检项写清楚。例如明确“同一页重复映射不能增加引用”“不能回收中间页表页”“PTE 修改后必须刷新 TLB”，生成结果的质量会明显提高。

> 说明：知识点与 OS 原理的对照、OS 原理中未覆盖的知识点由 B/C 在各自报告中补充；本文件只覆盖 A 负责的章节。

---
