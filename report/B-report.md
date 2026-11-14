# Lab4 实验报告（B：2412386-唐港）

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 4：进程管理 |
| **小组成员** | 2413541-赖鸿毅（A）、2412386-唐港（B）、[待填：C 学号-姓名] |
| **完成日期** | 2026-11-14 |
| **代码分支** | `lab4-b` |
| **代码提交** | `514cca4`、`1d00468`、`5e74d45`、`1e2b541`、`b458cb5`、`f5f7204` |
| **同步提交** | `a5e0a2c`（merge: sync origin/lab4） |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2413541-赖鸿毅（A） | lab2 PMM 基线（best-fit、`page_init`）、功能模块一（`page_insert`/`page_remove`）、整体逻辑线、`get_pte` Challenge |
| 2412386-唐港（B） | 功能模块二（`alloc_proc`、`copy_mm`、`copy_thread`、`do_fork`、`kernel_thread`、`proc_init`）、功能模块三中的 `cpu_idle`/`proc_run` 代码、分页模式 Challenge、知识点对照 |
| [待填：C 学号-姓名] | lab3 trap 基线、功能模块三（`schedule`/`switch_to`）、模块三报告正文、`local_intr_save` Challenge、未覆盖知识点、测试截图 |

> 说明：按小组约定，模块三的报告正文由 C 统一撰写，包含 B 实现的 `cpu_idle`/`proc_run`。
> 本文只记录这两个函数的接口实现事实，详细分析见 C 的模块三章节，避免重复。

---

## 一、实验目的

1. 理解内核线程与用户进程在运行模式、地址空间上的区别；
2. 掌握进程控制块（PCB）的字段布局与生命周期状态机；
3. 掌握“复制当前执行现场 → 登记新线程 → 首次运行”的完整链路；
4. 掌握上下文切换只保存 callee-saved 寄存器的原理；
5. 理解上下文切换、页表切换与中断屏蔽之间的原子性要求；
6. 通过 Challenge 深入理解 Sv32/Sv39/Sv48 三种分页模式的异同。

---

## 二、实验环境

| 项目 | 内容 |
|------|------|
| 运行环境 | WSL2 Ubuntu（Windows 11） |
| 交叉工具链 | `/opt/riscv/bin/riscv64-unknown-elf-*` |
| 模拟器 | `qemu-system-riscv64` |
| 编译检查 | `make obj/kern/process/proc.o` |
| 全量构建 | `make -j4` |

B 只修改一个文件：`code/kern/process/proc.c`。单文件编译命令：

```bash
cd code
export PATH=/opt/riscv/bin:$PATH
make obj/kern/process/proc.o
```

---

## 三、实验整体逻辑分析（B 视角）

### 3.1 本章节的逻辑主线

本实验的执行链可以概括为：

```text
kern_init
  ├─ pmm_init          （A：物理页 + 页表）
  ├─ pic_init/idt_init
  ├─ vmm_init          （框架已提供）
  ├─ proc_init         （B-5：idleproc + initproc）
  ├─ clock_init
  ├─ intr_enable
  └─ cpu_idle          （B-6：调度点）
        └─ schedule    （C）
             └─ proc_run    （B-6）
                  └─ switch_to   （C）
                       └─ forkret → forkrets → __trapret → sret
                            └─ kernel_thread_entry → init_main
```

A 负责把“地址空间”准备好，C 负责 trap 返回与底层切换，B 负责“线程从哪里来、如何登记、如何第一次运行”。

### 3.2 功能模块二的边界

模块二围绕 `struct proc_struct` 展开，六个函数各管一段：

| 函数 | 职责 | 不负责 |
|------|------|--------|
| `alloc_proc` | 从内核堆取 PCB，置安全初值 | 内核栈、PID、链表 |
| `copy_mm` | 决定 `proc->mm`（本实验恒 NULL） | 真正的地址空间复制 |
| `copy_thread` | 把 trapframe 放到新栈顶，构造 `context` | 分配栈、分配 PID |
| `do_fork` | 组合资源、登记链表、置就绪态 | 切换 current、写 satp |
| `kernel_thread` | 构造“首次运行”的 trapframe 模板 | 创建 PCB |
| `proc_init` | 初始化进程表，包装 idleproc，创建 initproc | 调度 |

这种“造壳 / 装修 / 登记 / 首次运行”的分层，是本模块最重要的设计思想。

### 3.3 B 的实现范围

- 代码：`code/kern/process/proc.c` 第 221~413 行，两个 `//LAB4 function` 标记区域；
- 报告：模块二全部、分页模式 Challenge、知识点对照；
- 不涉及：`pmm.c`、`default_pmm.c`（A），`sched.c`、`switch.S`、`trap*.c/S`（C）。

---

## 四、实验内容与实现

### 功能模块二：进程创建与初始化

#### 模块功能描述

模块二要把内核从“只能以单一执行流运行”推进到“已有多个内核线程对象、并能登记和初次运行”。完成后系统中至少存在：

- `idleproc`：0 号线程，复用 `kern_init` 的当前执行现场；
- `initproc`：1 号线程，由 `kernel_thread` + `do_fork` 正常创建。

#### 核心函数理解

##### 1. `alloc_proc`

```c
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL)
    {
        memset(proc, 0, sizeof(struct proc_struct));
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->pgdir = boot_pgdir_pa;
    }
    return proc;
}
```

要点：

- PCB 用 `kmalloc`（SLOB 堆）而不是 `alloc_pages`：PCB 约 232 字节，用整页存放太浪费，而 `MAX_PROCESS` 为 4096；
- `kmalloc` 复用块**不保证清零**，所以必须 `memset`。测试断言了 `runs == 0`、`flags == 0`、`context.ra == 0`、`tf == NULL`，逐字段初始化容易漏；
- 三个“正确初值不是 0”的字段必须显式设置：
  - `state = PROC_UNINIT`：状态机起点，且 `wakeup_proc` 的断言要求它不是 `PROC_RUNNABLE`；
  - `pid = -1`：0 保留给 idleproc，正数由 `get_pid()` 分配；
  - `pgdir = boot_pgdir_pa`：**物理地址**，因为 `proc_run` 要把它右移 12 位写进 `satp`。写成 `boot_pgdir_va` 会在首次切换时把虚拟地址当物理地址，导致页表基址错误。

##### 2. `copy_mm`

```c
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    if (clone_flags & CLONE_VM)
        proc->mm = current->mm;
    else
        proc->mm = NULL;
    return 0;
}
```

本实验只创建内核线程，`current->mm` 恒为 NULL，因此两条路径结果都是 NULL。保留 `CLONE_VM` 分支是为了保持函数语义完整，供 lab5 用户进程扩展。测试断言 `initproc->mm == NULL`。

##### 3. `copy_thread`

```c
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf;

    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)proc->tf;
}
```

关键点：

- `kstack` 是内核栈**低地址端**，`kstack + KSTACKSIZE` 是栈顶。栈向下增长，所以 trapframe 放在最高的 288 字节处：`(trapframe *)... - 1`；
- `*(proc->tf) = *tf` 必须是**深拷贝**。模板 `tf` 在 `kernel_thread` 的栈帧上，`do_fork` 返回后即失效；
- `gpr.a0 = 0`：子线程返回值；
- `esp == 0` 时栈指针指向自己的 trapframe 顶部；
- `context.ra = forkret`、`context.sp = proc->tf` 是“第一次调度入口契约”。

编译期已核对：`sizeof(struct trapframe) == 288`（36 × 8），`sizeof(struct context) == 112`（14 × 8）。

##### 4. `do_fork`

```c
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;

    if (nr_process >= MAX_PROCESS)
        goto fork_out;
    ret = -E_NO_MEM;

    if ((proc = alloc_proc()) == NULL)
        goto fork_out;
    proc->parent = current;

    if (setup_kstack(proc) != 0)
        goto bad_fork_cleanup_proc;
    if (copy_mm(clone_flags, proc) != 0)
        goto bad_fork_cleanup_kstack;
    copy_thread(proc, stack, tf);

    bool intr_flag;
    local_intr_save(intr_flag);
    {
        proc->pid = get_pid();
        hash_proc(proc);
        list_add(&proc_list, &(proc->list_link));
        nr_process++;
    }
    local_intr_restore(intr_flag);

    wakeup_proc(proc);
    ret = proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}
```

要点：

- `proc->parent = current` 不能漏：测试断言 `initproc->parent == idleproc`；
- 顺序约束：`setup_kstack` 必须早于 `copy_thread`，否则 `proc->tf` 会落在 `kstack = 0` 的地址上；
- 临界区必须包含 `get_pid`、`hash_proc`、`list_add`、`nr_process++` 四步：`get_pid` 会遍历 `proc_list`，并发 fork 可能拿到重复 PID 或撕裂链表；
- `get_pid` 必须在 `list_add` 之前：此时新 PCB 还不在链表里；
- `wakeup_proc` 放在临界区外：它把 `PROC_UNINIT` 改成 `PROC_RUNNABLE`，若提前改状态会触发它内部的断言；
- 失败标签顺序贯穿：从 `bad_fork_cleanup_kstack` 进入会先 `put_kstack` 再落入 `kfree`，顺序反了就是 use-after-free。

##### 5. `kernel_thread`

```c
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;

    memset(&tf, 0, sizeof(struct trapframe));

    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.gpr.a0 = 0;

    tf.epc = (uintptr_t)kernel_thread_entry;

    tf.status = read_csr(sstatus);
    tf.status &= ~SSTATUS_SIE;
    tf.status |= SSTATUS_SPIE;
    tf.status |= SSTATUS_SPP;

    return do_fork(clone_flags, 0, &tf);
}
```

要点：

- `kernel_thread_entry` 是 `move a0, s1; jalr s0`，所以函数指针必须在 `s0`、参数在 `s1`；
- `epc` 决定 `sret` 的目标；
- `status` 三个位：
  - 清 `SIE`：从恢复 `sstatus` 到执行 `sret` 之间不能被中断打断；
  - 置 `SPIE`：`sret` 时 `SIE ← SPIE`，保证线程跑起来后开中断；
  - 置 `SPP`：`sret` 后回到 S 态；若为 0 会“返回”到 U 态执行内核代码；
- `gpr.sp` 保持 0，由 `copy_thread` 回退到 `proc->tf`。

##### 6. `proc_init`

```c
void
proc_init(void)
{
    int i, pid;

    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i++)
        list_init(&hash_list[i]);

    if ((idleproc = alloc_proc()) == NULL)
        panic("cannot alloc idleproc.\n");

    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    current = idleproc;
    set_proc_name(idleproc, "idle");
    nr_process++;

    pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0)
        panic("create init_main failed.\n");
    initproc = find_proc(pid);
    set_proc_name(initproc, "init");
}
```

要点：

- `idleproc` **不能挂进 `proc_list` / `hash_list`**。它是调度器的兜底指针，不是可调度节点。挂进去后 `schedule()` 可能选中它，`proc_run` 里 `from == proc` 直接返回，测试会卡死；
- `idleproc` 的 `kstack = bootstack`：它没有经过 `setup_kstack`，一直使用启动栈；
- `need_resched = 1`：让 `cpu_idle` 第一次检查就调度到 initproc；
- `kernel_thread` 返回的是 **int pid**，必须用 `find_proc(pid)` 取回 PCB；
- 测试注入在 `proc_init` 末尾，所有初始化必须在返回前完成。

#### 完整首次运行链路

```text
proc_init
  └─ kernel_thread(init_main, "Hello world!!", 0)
       └─ 造模板 tf: s0=init_main, s1=arg, a0=0, epc=kernel_thread_entry
            status: 清 SIE / 置 SPIE / 置 SPP
       └─ do_fork(..., 0, &tf)
            ├─ alloc_proc      (parent=idleproc)
            ├─ setup_kstack    (2 页)
            ├─ copy_mm         (NULL)
            ├─ copy_thread     (tf 放到新栈顶, context.ra=forkret)
            └─ pid/hash/list/wakeup
...
cpu_idle → schedule → proc_run → switch_to
  └─ ret 到 forkret
       └─ forkrets(current->tf)
            └─ move sp, a0; j __trapret
                 └─ RESTORE_ALL; sret → epc = kernel_thread_entry
                      └─ move a0, s1; jalr s0
                           └─ init_main("Hello world!!")
```

`a0` 在这条链上有三种含义：`forkrets` 里是 tf 指针；`__trapret` 恢复后是 0（子线程返回值）；`kernel_thread_entry` 里被 `move a0, s1` 覆盖成参数。

#### 最终提示词

每个任务使用课程的四段式框架：

```text
[PROMPT]        做什么、改哪个文件、输出要求
[RELY]          真实的结构体/宏/可用函数
[GUARANTEE]     必须实现的函数签名
[SPECIFICATION] Pre-Condition / Post-Condition / Case / Requirements
```

模块二按“一个可独立验证的功能”拆成五轮提示词（`alloc_proc`；`copy_mm`+`copy_thread`；`do_fork`；`kernel_thread`；`proc_init`），模块三接口一轮（`cpu_idle`+`proc_run`）。完整提示词见 `report/B-prompt.md`。

#### 实现迭代过程

六轮中 AI 首次生成的代码均通过编译。但在生成前的提示词审查和生成后的逐行核对中，发现并修正了以下会直接导致失败的规格缺口。这里如实记录，作为提示词迭代的依据。

##### 第一次：`alloc_proc` 的页目录地址

提示词初版只说了“指向内核页表”，没有区分 `boot_pgdir_va` 与 `boot_pgdir_pa`。若按字面实现成虚拟地址，`proc_run` 写 `satp` 时会把虚拟地址当物理页号，首次切换 page fault。

修正：在 `[RELY]` 中并列两个符号并注明用途，在 `[SPECIFICATION]` 明确“`pgdir` 是物理地址，`satp` 直接使用”。

##### 第二次：`copy_thread` 的深拷贝与栈顶定位

初版规格只说“复制中断帧”，没有说明模板的生命周期。AI 很容易写成 `proc->tf = tf`（只存指针）。该指针在 `do_fork` 返回后失效，首次 `sret` 会跳到一个垃圾 `epc`。

修正：补充“模板在调用者栈帧上，必须深拷贝”，并明确 `(trapframe *)(kstack + KSTACKSIZE) - 1` 的栈顶定位。

##### 第三次：`do_fork` 的 parent 与回滚顺序

初版漏了 `proc->parent = current`。测试断言 `initproc->parent == idleproc`，漏掉会直接挂。另外没有写失败标签的贯穿顺序。

修正：`[SPECIFICATION]` 增加 `parent` 赋值，并画出“`bad_fork_cleanup_kstack` → `put_kstack` → 落入 `bad_fork_cleanup_proc` → `kfree`”的路径。

##### 第四次：`kernel_thread` 的寄存器约定

初版只说要“设置函数和参数”，没有绑定 `kernel_thread_entry` 的 `s0/s1` 约定。写反后 `jalr` 会跳到参数地址。

修正：`[RELY]` 直接给出汇编实现，`[SPECIFICATION]` 明确 `s0 = fn`、`s1 = arg`、`epc = kernel_thread_entry`。

##### 第五次：`proc_init` 的 idleproc 入链陷阱

初版没有说明 idleproc 不能挂链表，只说“初始化为可调度线程”。AI 若按常规 `list_add`，会触发调度器死锁。

修正：`[SPECIFICATION]` 明确“idleproc 留在所有链表之外，是兜底指针”。

##### 第六次：`proc_run` 的 satp 写法

`SATP_MODE_SV39` 的定义是 `8`（int），写成 `SATP_MODE_SV39 << 60` 在 32 位整数上是未定义行为。初版提示词没有提醒。

修正：直接采用 `enable_paging()` 的写法 `0x8000000000000000 | (proc->pgdir >> RISCV_PGSHIFT)`，并把“不得使用 `lsatp()`”写进约束。

#### 关键改进点总结

1. **提示词要写“状态”，不要写“步骤”**：`[SPECIFICATION]` 用 Pre/Post/Case 描述结果，AI 才能在所有异常路径上保持一致；
2. **真实语义必须显式保留**：物理地址 vs 虚拟地址、模板生命周期、首次运行的寄存器约定，这些都不能靠“常识”；
3. **边界和失败路径要单独成 Case**：`alloc_proc` 失败、`setup_kstack` 失败、`copy_mm` 失败的回滚顺序，写清楚才能避免 use-after-free；
4. **测试断言是最好的需求文档**：`initproc->parent == idleproc`、`context.ra != 0` 等断言直接反推出规格。

### 功能模块三接口：`cpu_idle` 与 `proc_run`

> 模块三的完整分析由 C 撰写。这里只记录 B 交付的两个函数的事实。

```c
void cpu_idle(void)
{
    while (1)
        if (current->need_resched)
            schedule();
}

void proc_run(struct proc_struct *proc)
{
    bool intr_flag;
    local_intr_save(intr_flag);

    struct proc_struct *from = current;
    if (from != proc)
    {
        current = proc;
        write_csr(satp, 0x8000000000000000 | (proc->pgdir >> RISCV_PGSHIFT));
        flush_tlb();
        switch_to(&from->context, &proc->context);
    }

    local_intr_restore(intr_flag);
}
```

实现要点（供 C 引用）：

1. `cpu_idle` 是唯一的调度点，且不清 `need_resched`（那是 `schedule` 的职责）；
2. `local_intr_save` 关闭中断，保证 `current`、`satp`、寄存器现场三者原子更新；
3. **先保存 `from = current` 再比较**：`current` 改写后旧指针就丢了；
4. `current = proc` 必须在 `switch_to` 之前：`switch_to` 一旦执行，返回时已经在新线程里；
5. `proc->pgdir` 是物理地址，右移 12 位后与 MODE=8 组合写 `satp`；
6. `switch_to` 只保存/恢复 `ra, sp, s0~s11`，其余由编译器按 ABI 处理；
7. 本实验所有内核线程共享 `boot_pgdir_pa`，satp 写入值相同，但机制必须保留。

### Challenge：Sv32 / Sv39 / Sv48 分页模式

> 与 A 的报告存在部分重叠：A 从 `get_pte()` 的代码形态出发，本文补充寄存器、PTE 位域和地址翻译的细节，供最终拼接时合并或裁剪。

#### 1. 三种模式的总体参数

| 项目 | Sv32 | Sv39 | Sv48 |
|------|------|------|------|
| 虚拟地址宽度 | 32 | 39 | 48 |
| 页表级数 | 2 | 3 | 4 |
| 每级索引位数 | 10 | 9 | 9 |
| 每级页表项数 | 1024 | 512 | 512 |
| 页内偏移 | 12 | 12 | 12 |
| 基页大小 | 4 KiB | 4 KiB | 4 KiB |
| 大页 | 4 MiB（level 1） | 2 MiB / 1 GiB | 2 MiB / 1 GiB / 512 GiB |
| PTE 大小 | 4 字节 | 8 字节 | 8 字节 |
| 物理地址宽度 | 34 | 56 | 56 |
| `satp` MODE | 1 | 8 | 9 |
| 规范地址要求 | 无（RV32 全 32 位可用） | bit 63:39 = bit 38 符号扩展 | bit 63:48 = bit 47 符号扩展 |

#### 2. `satp` 寄存器布局

RV64（Sv39/Sv48 共用）：

```text
 63      60 59        44 43                              0
+----------+------------+---------------------------------+
|   MODE   |    ASID    |              PPN                |
|  4 bits  |  16 bits   |            44 bits              |
+----------+------------+---------------------------------+
```

RV32（Sv32）：

```text
 31 | 30        22 | 21                              0
+---+-------------+----------------------------------+
|MODE|    ASID    |               PPN                |
| 1  |   9 bits   |             22 bits              |
+----+------------+----------------------------------+
```

- `MODE = 0` 表示 Bare（不翻译），`1 = Sv32`，`8 = Sv39`，`9 = Sv48`；
- 切换页表时，必须写完整 64 位值。uvm 里 `write_csr(satp, 0x8000000000000000 | (pgdir >> 12))` 中的 `0x8...` 就是 MODE=8；
- `SATP_MODE_SV39` 宏的值是 8，若写成 `SATP_MODE_SV39 << 60`，在 `int` 上是未定义行为，必须用 `8UL` 或直接使用完整常量。

#### 3. PTE 位域

Sv32（4 字节）：

```text
 31        20 19      10 9  8 7 6 5 4 3 2 1 0
+------------+----------+----+-+-+-+-+-+-+-+-+
|  PPN[1]    |  PPN[0]  |RSW |D|A|G|U|X|W|R|V|
|  12 bits   |  10 bits |2bit|               |
+------------+----------+----+-+-+-+-+-+-+-+-+
```

Sv39 / Sv48（8 字节）：

```text
 63        54 53                              10 9 8 7 6 5 4 3 2 1 0
+------------+----------------------------------+---+-+-+-+-+-+-+-+-+
|  reserved  |              PPN[2:0]            |RSV|D|A|G|U|X|W|R|V|
|            |              44 bits              |   |               |
+------------+----------------------------------+---+-+-+-+-+-+-+-+-+
```

- `V`：有效；`R/W/X`：可读/可写/可执行；
- **非叶子 PTE 必须 `R = W = X = 0` 且 `V = 1`**，此时 PPN 指向下一级页表；
- `U`：用户可访问；内核页表项通常不置 `U`，但 `get_pte` 分配中间页表时置 `PTE_U | PTE_V`，便于后续用户态扩展；
- `A/D`：访问位/脏位，本实验不实现硬件更新，保留为 0。

`PTE_ADDR(pte)` 的移位 `<< (PTXSHIFT - PTE_PPN_SHIFT)` 即 `<< 2`：PTE 中 PPN 从 bit 10 开始，物理页号要从 bit 12 开始，相差 2 位。Sv32 的 PPN[0] 同样从 bit 10 开始，所以这个移位在两个模式下形式一致，只是掩码宽度不同。

#### 4. 地址翻译过程与一个大页示例

以 Sv39 下的 `la = 0x0000000080200000`（内核加载地址 0x80200000）为例：

```text
la = 0x0000_0000_8020_0000
VPN[2] = (la >> 30) & 0x1FF = 2
VPN[1] = (la >> 21) & 0x1FF = 1
VPN[0] = (la >> 12) & 0x1FF = 0
offset = 0x000
```

翻译步骤：

1. 用 `satp.PPN` 找到根页表，取第 `VPN[2] = 2` 项；
2. 该 PTE 非叶子，取 `PPN` 作为二级页表基址，取第 `VPN[1] = 1` 项；
3. 仍非叶子，取 `PPN` 作为三级页表基址，取第 `VPN[0] = 0` 项；
4. 该项是叶子，物理页基址 = `PTE.PPN << 12`，加 `offset` 得到物理地址。

若第 2 步的 PTE 直接是叶子（`R/W/X` 非零），则它映射的是 2 MiB 大页，后续 VPN 位直接作为大页内偏移的一部分；若第 1 步就是叶子，则是 1 GiB 大页。Sv48 多一级，最大叶子可达 512 GiB；Sv32 只有两级，最大叶子 4 MiB。

#### 5. 为什么 `get_pte()` 的两段代码如此相像

`get_pte()` 在每一级执行的动作完全相同：

```text
取当前级索引 -> 读 PTE
  ├─ V=1：取 PTE.PPN 作为下一级页表基址
  └─ V=0 且 create=1：分配一页 -> 清零 -> 写 V -> 继续
```

- Sv39 需要“根 → 中间 → 叶子”三次索引，因此代码里展开为两段“检查/分配”加最后返回叶子；
- Sv32 只需“根 → 叶子”两次索引，所以只需要一段这样的代码；
- Sv48 需要“根 → 中间1 → 中间2 → 叶子”四次索引，理论上需要三段这样的代码。

**两段代码相像不是巧合，而是多级页表“每级同构”的直接结果**：PTE 格式统一、索引位宽统一、非叶子项都是“PPN 指向下一级”。`get_pte` 目前是 Sv39 专用实现，如果要移植到 Sv32/Sv48，需要同时修改索引位数、PTE 宽度和 `PTE_ADDR` 的掩码。

#### 6. 小结

- Sv32/Sv39/Sv48 的共同点是 4 KiB 基页、逐级索引、PTE 权限位一致；
- 差异在虚拟地址宽度、页表级数、每级索引位数、PTE 大小和 `satp` 的 MODE；
- 本实验的 `get_pte` 把 Sv39 的两级“检查/分配”展开，所以看起来重复；
- 理解这些差异，才能解释为什么 `satp` 必须写 `0x8...`、为什么内核线程共享页表时切换 `satp` 是安全的。

---

## 五、重要知识点与 OS 原理对照

| 本实验中的概念 | 对应 OS 原理 | 含义与关系 | 差异 |
|---|---|---|---|
| `struct proc_struct` | Linux `task_struct` | 进程/线程的控制块，记录身份、状态、上下文、地址空间 | ucore 字段大幅简化，没有信号、文件、凭证等 |
| `PROC_UNINIT / RUNNABLE / SLEEPING / ZOMBIE` | `TASK_RUNNING / TASK_INTERRUPTIBLE / EXIT_ZOMBIE` | 进程生命周期状态机 | ucore 只有四态，没有 stopped、parked 等 |
| `kstack` + `setup_kstack` | 每任务内核栈 | 进入内核后的执行栈 | ucore 用 2 个连续页，位置不固定；Linux 有 guard page、thread_info 复用 |
| `struct trapframe` | `struct pt_regs` | 中断/异常时保存的用户/内核寄存器现场 | ucore 固定 36 槽；Linux 与架构相关，可能包含浮点/向量状态 |
| `struct context` | `cpu_context` | 切换时保存的 callee-saved 寄存器 | ucore 只存 14 个；Linux 还包含 TLS、FPU 等 |
| `forkret → forkrets → __trapret → sret` | `ret_from_fork` | 新线程“第一次从 trap 返回”的构造技巧 | ucore 用 trapframe 模板；Linux 用 `ret_from_fork` 汇编 |
| `get_pid()` + `hash_list` | PID 分配 + pid hash | 快速查找进程 | ucore 用静态上限 `MAX_PROCESS`，无 PID namespace |
| `proc_list` + FIFO/RR | 运行队列 + CFS/实时调度 | 选择下一个可运行实体 | ucore 是最简单的循环链表轮转，无优先级、时间片 |
| `local_intr_save/restore` | `local_irq_save/restore`、`spin_lock_irqsave` | 保护临界区 | ucore 单核，关中断即互斥；Linux 多核需要原子锁 |
| `mm_struct` / `pgdir` | `mm_struct` / `pgd` | 地址空间描述 | lab4 全为内核线程，`mm == NULL`，共享内核页表 |
| `satp` + `flush_tlb` | `CR3` + TLB shootdown | 切换地址空间 | ucore 每次全量刷 TLB；Linux 用 ASID/PCID 减少刷新 |
| `kernel_thread` | `kthread_create` | 创建内核线程 | ucore 走 `do_fork`；Linux 有 kthreadd 管理 |
| `CLONE_VM` | `clone(2)` 的 `CLONE_VM` | 共享地址空间 | 本实验未真正使用 |

### 差异总结

1. **单核 vs 多核**：本实验关中断即可保证原子性；多核需要自旋锁和内存屏障。
2. **非抢占 vs 抢占**：本实验只有 `cpu_idle` 一个调度点，线程必须主动让出；Linux 有抢占和实时调度。
3. **无用户态**：lab4 只有内核线程，没有系统调用、缺页异常和用户栈；这些是 lab5 的内容。
4. **无睡眠队列**：`wakeup_proc` 只是把状态改为 `PROC_RUNNABLE`，没有等待队列、优先级继承等机制。
5. **地址空间预映射**：本实验不做按需分页和页面置换，`vmm.c` 只提供 VMA 管理框架。
6. **资源上限固定**：`MAX_PROCESS = 4096`，没有动态配额和 cgroup。

---

## 六、测试与验证

### 6.1 阶段性验证

| 检查项 | 结果 |
|---|---|
| `make obj/kern/process/proc.o` | 通过 |
| `sizeof(struct trapframe) == 288` | 通过（编译期断言） |
| `sizeof(struct context) == 112` | 通过（编译期断言） |
| 两个 `//LAB4 function` 标记保留并替换学号 | 通过 |
| 全量 `make -j4` | 所有 `.o` 编译成功；链接停在 `undefined reference to 'schedule'`（C 未完成） |
| 合并 A 的 `pmm.c` 后编译 | 通过 |

### 6.2 集成后预期

```text
[Test Process Create] PASS
[Test Schedule] PASS
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
```

`test_lab4_process_create()` 由 `inject.py` 注入在 `proc_init` 末尾，`test_lab4_schedule()` 注入在 `init_main` 开头。模块二的断言覆盖：

- `idleproc->pid == 0`、`state == PROC_RUNNABLE`、`kstack == bootstack`、`parent == NULL`、`mm == NULL`、`tf == NULL`、`runs == 0`、`flags == 0`、`context.ra == 0`；
- `initproc->pid == 1`、`parent == idleproc`、`mm == NULL`、`context.ra != 0`、`context.sp != 0`。

> 最终 `make grade` 的完整输出与截图由 A/C 在集成完成后补充。

---

## 七、B 的个人总结

模块二是本实验里“链路最长、最容易看起来对但跑不起来”的部分。写完后我最大的体会是：

1. **PCB 的每个初值都对应一个后续假设**。`pgdir` 是物理地址、`state` 必须是 `UNINIT`、`context.ra` 必须是 0，这些不是随手填的，而是被 `proc_run`、`wakeup_proc`、`switch_to` 分别依赖。
2. **首次运行不是“跳转到函数”**，而是“伪造一次 trap 返回”。理解了 `forkret → forkrets → __trapret → sret` 这条链，`s0/s1/epc/status` 的赋值才有意义。
3. **提示词的质量直接决定代码质量**。六轮里所有能想到的坑，都是先在 `[SPECIFICATION]` 里写清楚，才没有变成代码里的 bug。反过来，凡是提示词没写的状态（`parent`、返回值、失败回滚），都是最危险的地方。
4. **测试断言就是需求文档**。`initproc->parent == idleproc` 这一条，比任何文字描述都更能说明 `do_fork` 必须做什么。

---

## 附：提交记录

```text
514cca4 lab4-2: implement alloc_proc
1d00468 lab4-2: implement copy_mm and copy_thread
5e74d45 lab4-2: implement do_fork
1e2b541 lab4-2: implement kernel_thread
b458cb5 lab4-2: implement proc_init
f5f7204 lab4-3: implement cpu_idle and proc_run
a5e0a2c merge: sync origin/lab4 (lab2 + pmm)
```
