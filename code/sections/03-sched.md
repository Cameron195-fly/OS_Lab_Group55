# 模块三：进程调度与上下文切换

> 本模块由丙（2412440-王朝阳）统一撰写。代码实现上，`cpu_idle`、`proc_run` 由乙在 `kern/process/proc.c` 内完成；
> `schedule` 与 `switch_to` 由丙在 `kern/schedule/sched.c`、`kern/process/switch.S` 内完成。本节按“一次调度到底发生了什么”的顺序把四个函数串起来讲。

本模块涉及的代码改动：

| 文件 | 负责人 | 改动内容 |
|---|---|---|
| `kern/trap/trapentry.S` | 丙 | `RESTORE_ALL` 恢复 32 个通用寄存器与 `sstatus/sepc`；`__alltraps` 把 `sp` 作为参数传给 `trap()` |
| `kern/trap/trap.c` | 丙 | `interrupt_handler`（时钟续期、tick 统计、定时关机）与 `exception_handler`（非法指令/断点推进 `epc`） |
| `kern/process/switch.S` | 丙 | `switch_to(from, to)` 保存/恢复 `ra, sp, s0~s11` |
| `kern/schedule/sched.c` | 丙 | `schedule()` 轮转选择可运行进程并调用 `proc_run()` |

## 3.1 模块三在 lab4 中的位置

Lab4 的三个模块可以概括为：先把物理页和页表操作补齐（模块一），再把进程控制块和第一次运行链路搭起来（模块二），
最后让多个进程能够轮流占用 CPU（模块三）。模块三之前的启动链路是：

```text
kern_init()
  ├─ pmm_init()                 模块一：物理页、页表
  ├─ proc_init()                模块二：idleproc / initproc，initproc 进入 proc_list
  └─ cpu_idle()                 模块三：调度循环
        └─ schedule()           选下一个 PROC_RUNNABLE
              └─ proc_run()     改 current、切 satp、flush_tlb
                    └─ switch_to()  保存 from 上下文，恢复 to 上下文
```

模块三要解决的核心问题是：**CPU 只有一个，进程有多个，怎样让它们看起来在同时运行**。
在本实验中，“同时运行”通过**协作式轮转**近似：当前进程主动把 `need_resched` 置 1 并调用 `schedule()`，
调度器从 `proc_list` 里挑出下一个可运行进程，`proc_run()` 完成地址空间与寄存器的切换。

本实验只有两个调度触发点：

1. `cpu_idle()` 的循环：空闲进程发现 `current->need_resched` 为真时调用 `schedule()`；
2. 评分测试 `test_lab4_schedule()`：它创建两个内核线程，然后连续 8 次“置 `need_resched` + 调 `schedule()`”，
   验证两个线程都能被轮到。

> 注意：本实验的时钟中断只做“续期 + 计数 + 输出”，并没有在中断处理里把 `need_resched` 置 1，
> 也没有在 Trap 返回路径上检查 `need_resched`。所以严格说它**不是时间片抢占式调度**，而是“显式让出 + 空闲时调度”。
> 这一点在 `06-gaps.md` 里作为未覆盖知识点展开。

## 3.2 cpu_idle：空闲进程的主体

```c
// kern/process/proc.c（乙实现）
void cpu_idle(void)
{
    while (1)
    {
        if (current->need_resched)
        {
            schedule();
        }
    }
}
```

理解这个函数要抓住三点：

1. **idleproc 不在 `proc_list` 里。** `proc_init()` 只给 idleproc 分配 PCB、把 `kstack` 指向 `bootstack`、
   把 `state` 设为 `PROC_RUNNABLE`、把 `need_resched` 置 1，并让 `current = idleproc`；
   它不会被 `list_add` 进 `proc_list`。原因是 idleproc 是调度器的“兜底”，不是普通可运行进程：
   如果把它挂进链表，`schedule()` 可能先选中它，而 `proc_run()` 发现 `from == proc` 会直接返回，测试就会卡住。
2. **`need_resched` 是“请求调度”的软标志。** 它不是硬件寄存器，而是 `struct proc_struct` 的字段。
   谁想放弃 CPU，就把它置 1；调度器进入后第一件事就是把自己这一轮的标志清 0，避免重复调度。
3. **idle 是忙等。** `while (1)` 在没有可运行进程时会持续空转。真实操作系统通常会执行 `wfi`
   （wait for interrupt）或进入低功耗状态，把 CPU 让给硬件中断。

第一次调度就发生在 `cpu_idle()` 里：`proc_init()` 把 `idleproc->need_resched` 置 1，
`cpu_idle()` 看到后调用 `schedule()`，于是从 idleproc 切到 initproc，initproc 从 `forkret` 开始第一次运行。

## 3.3 proc_run：把 current 换掉并切换地址空间

```c
// kern/process/proc.c（乙实现）
void proc_run(struct proc_struct *proc)
{
    bool intr_flag;
    local_intr_save(intr_flag);

    struct proc_struct *from = current;
    if (from != proc)
    {
        current = proc;
        // Sv39: MODE 在 bit 63..60，PPN 在 bit 43..0
        write_csr(satp, 0x8000000000000000 | (proc->pgdir >> RISCV_PGSHIFT));
        flush_tlb();
        switch_to(&from->context, &proc->context);
    }

    local_intr_restore(intr_flag);
}
```

几个容易写错的点：

- **先保存 `from`，再改 `current`。** `switch_to` 需要旧进程的 `context` 地址；
  如果先写 `current = proc` 再取 `current->context`，保存的就会是新进程的上下文，旧进程的返回点丢失。
- **`satp` 的写法。** Sv39 的 MODE 是 8，位于 bit 63..60，因此是 `8UL << 60`，也就是
  `0x8000000000000000`；PPN 是物理页号，放在 bit 43..0。不能把 `8` 直接或到低位，
  也不能用 `lsatp()`（它表达不了 Sv39 模式）。
- **`flush_tlb()` 不能省。** 换了页表后，旧进程留在 TLB 里的虚拟地址映射对新进程无效，
  必须清掉，否则可能访问到错误的物理页。
- **整个切换过程关中断。** `current`、`satp`、`context` 三者必须一起更新；
  如果中途来了时钟中断，中断处理看到的是“新 current + 旧 satp”这种不一致状态。
- **`from == proc` 时直接返回。** 例如 `schedule()` 没找到别的可运行进程，回退到 idleproc，
  而当前本来就是 idleproc，此时不需要做任何切换。

### 第一次切换为什么不会跳到 0

新进程的 `context.ra` 在 `copy_thread()` 里被设为 `forkret`（C 函数），`context.sp` 被设为
`proc->tf`（内核栈顶部的 trapframe）。所以 `switch_to` 恢复新进程上下文后，`ret` 跳到的是：

```c
static void forkret(void)
{
    forkrets(current->tf);   // 汇编：move sp, a0; j __trapret
}
```

`forkrets` 把 `sp` 指向该进程的 trapframe，随后 `RESTORE_ALL` 按 trapframe 恢复 32 个通用寄存器和
`sstatus/sepc`，最后 `sret` 进入 `kernel_thread_entry`，由它执行 `move a0, s1; jalr s0`，
也就是调用 `kernel_thread()` 里准备好的函数和参数。整条“第一次运行”链路因此是：

```text
switch_to → ret → forkret → forkrets(current->tf) → RESTORE_ALL → sret
          → kernel_thread_entry → fn(arg) → do_exit
```

## 3.4 schedule：轮转选择下一个进程

```c
// kern/schedule/sched.c（丙实现）
void schedule(void)
{
    bool intr_flag;
    list_entry_t *head = &proc_list;
    list_entry_t *le, *start;
    struct proc_struct *next = NULL;

    local_intr_save(intr_flag);
    current->need_resched = 0;

    start = (current == idleproc) ? list_next(head) : list_next(&(current->list_link));
    le = start;

    if (!list_empty(head)) {
        do {
            if (le != head) {
                struct proc_struct *proc = le2proc(le, list_link);
                if (proc->state == PROC_RUNNABLE && proc != current) {
                    next = proc;
                    break;
                }
            }
            le = list_next(le);
        } while (le != start);
    }

    if (next == NULL) {
        next = idleproc;
    }

    proc_run(next);
    local_intr_restore(intr_flag);
}
```

算法可以拆成四步：

1. **关中断并清标志。** `local_intr_save` 保存当前中断状态并关中断；
   `current->need_resched = 0` 表示这一次调度请求已经被处理。
2. **确定起点。** 当前是 idleproc 时它不在链表里，从 `proc_list` 的第一个节点开始；
   否则从当前进程的 `list_link` 的下一个节点开始，实现轮转（round-robin）。
3. **找第一个 `PROC_RUNNABLE`。** `proc_list` 是带哨兵头的循环链表，
   `list_next` 走到最后一个节点后会回到 `&proc_list`。哨兵不是进程，必须跳过；
   同时排除 `current` 自己（否则只有自己可运行时会在原地空转）。
4. **找不到就回退 idleproc，然后 `proc_run(next)`。**

### 为什么用“整圈遍历”而不是“遇到 head 就停”

假设链表顺序是 `head → t2 → t1 → init → head`，当前进程是 `init`（链表尾）。
如果写成 `while (le != head)`，`init` 的下一个就是 `head`，循环一次都不执行，
`next` 为 `NULL`，于是回退 idleproc。此时 t1、t2 明明可运行，却没有被选中，
测试里的两个线程可能一直轮不到，`assert(test_thread1_ran && test_thread2_ran)` 就会失败。

正确做法是从 `start` 开始走满一圈再停：`do { ... } while (le != start)`，
中途遇到 `head` 只跳过、不终止。这样即使当前是链表尾，也能继续走到链表头的第一个进程。

### 用评分测试走一遍

`test_lab4_schedule()` 先创建线程 1、线程 2（`list_add` 插在表头），再循环 8 次调度。
链表顺序为 `head → t2 → t1 → init → head`，追踪前几次切换：

| 轮次 | current | 起点 | 选中 | 结果 |
|---|---|---|---|---|
| 1 | init | head → t2 | t2 | t2 第一次运行，置 `thread2_ran` |
| 2 | t2 | t1 | t1 | t1 第一次运行，置 `thread1_ran` |
| 3 | t1 | init | init | 回到 init 的第 1 次循环之后 |
| 4 | init | head → t2 | t2 | t2 继续第 2 次循环 |
| … | … | … | … | 两个线程交错推进，8 轮内两个标志都被置 1 |

这说明：**轮转的正确性依赖“跳过哨兵 + 走满一圈”**，而不是简单的 `while (le != head)`。

## 3.5 switch_to：只保存 callee-saved 寄存器

`struct context` 的字段顺序与偏移如下（RV64，每个字段 8 字节）：

| 字段 | 偏移 | 说明 |
|---|---:|---|
| `ra` | 0 | 返回地址，决定切回来后从哪继续执行 |
| `sp` | 8 | 栈指针，指向该进程的内核栈 |
| `s0` | 16 | callee-saved |
| `s1` | 24 | callee-saved |
| `s2` | 32 | callee-saved |
| `s3` | 40 | callee-saved |
| `s4` | 48 | callee-saved |
| `s5` | 56 | callee-saved |
| `s6` | 64 | callee-saved |
| `s7` | 72 | callee-saved |
| `s8` | 80 | callee-saved |
| `s9` | 88 | callee-saved |
| `s10` | 96 | callee-saved |
| `s11` | 104 | callee-saved |

实现（`kern/process/switch.S`）：

```asm
switch_to:
    # from = a0, to = a1
    sd ra,  0*8(a0)
    sd sp,  1*8(a0)
    sd s0,  2*8(a0)
    ...
    sd s11, 13*8(a0)

    ld ra,  0*8(a1)
    ld sp,  1*8(a1)
    ld s0,  2*8(a1)
    ...
    ld s11, 13*8(a1)
    ret
```

为什么只保存 14 个寄存器？

- RISC-V 调用约定把寄存器分成 caller-saved（`t0~t6`、`a0~a7`）和 callee-saved（`s0~s11`、`sp`、`ra`）。
- `switch_to` 是一个被 C 代码调用的函数。对调用者来说，`t*`、`a*` 本来就可能被任何函数调用破坏，
  编译器在调用 `switch_to` 前后会自己保存需要保留的值；`switch_to` 只要保证 `s*`、`sp`、`ra`
  在返回时与进入时一致即可。
- 因此上下文只需要 14 个字段。把 32 个寄存器全存一遍不是错误，但会多占内存、多花时间，而且没有必要。

另外两点：

- **`sp` 的位置不能和 `s0` 写反。** 偏移是按 `struct context` 的字段顺序硬编码的，
  写反后恢复出来的栈指针是 `s0` 的值，轻则崩溃，重则“看起来能跑但数据错乱”。
- **`ret` 用的是刚恢复的 `ra`。** 对普通进程，`ra` 指向它上次调用 `switch_to` 之后的返回地址
  （在 `proc_run()` 里）；对新进程，`ra` 是 `forkret`。这就是“切回来”和“第一次运行”共用同一段代码的原因。

## 3.6 与 lab3 Trap 基线的衔接

模块三能跑起来，前提是 lab3 的 Trap 基线完整：

- `trapentry.S` 的 `SAVE_ALL` 把被打断时的 32 个通用寄存器和 4 个 CSR 保存成 `struct trapframe`；
- `RESTORE_ALL` 按“先 `sstatus`/`sepc`，再 `x1`、`x3~x31`，最后 `sp += 36*REGBYTES`”的顺序恢复；
- `__alltraps` 把 `sp` 作为第一个参数传给 `trap()`，C 代码才能按 trapframe 处理中断/异常；
- `trap.c` 的 `interrupt_handler` 负责时钟续期与 tick 统计，`exception_handler` 负责非法指令/断点推进 `epc`。

在 lab4 的调度场景里，Trap 基线的意义是：**进程被时钟中断打断后，能原样回到被打断的指令**。
如果 `RESTORE_ALL` 顺序写错（例如先恢复 `sp` 再按原偏移取寄存器），
被中断的调度代码会从错误的栈位置恢复，表现为随机崩溃或“调度一次后再也不动”。

本实验没有做“中断返回时检查 `need_resched` 并抢占”，所以时钟中断不会主动引发调度；
调度只由 `cpu_idle()` 和测试代码显式触发。这个边界在 `06-gaps.md` 中说明。

## 3.7 模块三的 spec 与 prompt

### 3.7.1 spec（给实现者的约束）

| 项目 | 内容 |
|---|---|
| 输入 | `proc_list`（带哨兵头的循环链表）、`current`、`idleproc`、每个进程的 `state` 与 `need_resched` |
| 输出 | 无返回值；副作用是 `current` 指向被选中的进程，并完成寄存器/页表切换 |
| 选择规则 | 从当前进程的下一个节点开始轮转；当前是 idleproc 时从链表头开始；只选 `PROC_RUNNABLE`；跳过哨兵；找不到回退 idleproc |
| 原子性 | 选择 + `proc_run` 全过程关中断；`schedule` 与 `proc_run` 的关中断必须可嵌套 |
| 上下文 | 只保存 `ra, sp, s0~s11`，偏移 `0, 8, ..., 104`；不保存 caller-saved |
| 验收 | `[Test Schedule] PASS`，并输出 `this initproc, pid = 1, name = "init"`、`To U: "Hello world!!".`、`To U: "en.., Bye, Bye. :)"`；`make grade` 为 30/30 |

### 3.7.2 prompt（实际使用）

```text
你在 ucore lab4（RISC-V 64，Sv39）中实现进程调度，只改两个文件：
1) kern/schedule/sched.c 的 schedule()：
   - local_intr_save 关中断，current->need_resched = 0；
   - 当前不是 idleproc 时从 current->list_link 的下一个节点开始轮转，
     当前是 idleproc 时从 proc_list 的第一个节点开始；
   - proc_list 是带哨兵头的循环链表，必须跳过 &proc_list；
     当前是链表尾时要继续走到链表头，不能遇到哨兵就停；
   - 只选 state == PROC_RUNNABLE 且不等于 current 的进程；
   - 找不到就回退 idleproc；最后 proc_run(next)，local_intr_restore。
2) kern/process/switch.S 的 switch_to(from=a0, to=a1)：
   - 保存 from 的 ra, sp, s0~s11，共 14 个 8 字节字段，偏移 0,8,...,104；
   - 从 to 恢复同样的字段，最后 ret；
   - 只保存 callee-saved，不要保存 t0~t6、a0~a7。
约束：不要改 proc.c、不要整文件覆盖 lab3 代码；保持 //LAB4 function3 注释；
不要用 lsatp()，satp 由 proc_run 负责。
```

### 3.7.3 初稿的 bug 与修正

**Bug 1：遍历在哨兵处提前结束。**
初稿把选择循环写成 `while (le != head)`。静态推演测试场景时发现：当 `current` 是链表尾 `init` 时，
`init->list_link` 的下一个就是 `head`，循环一次都不执行，直接回退 idleproc，
t1/t2 永远轮不到，`assert(test_thread1_ran && test_thread2_ran)` 会失败。
修正为 `do { ... } while (le != start)`，中途遇到 `head` 只跳过、不终止，保证走满一圈。

**Bug 2：忘记排除 `current`。**
如果当前进程是唯一 `PROC_RUNNABLE` 的进程，不排除自己就会选中自己，
`proc_run` 里 `from == proc` 直接返回，`need_resched` 已经被清 0，
看起来“调度成功”，实际上没有任何进展。加上 `proc != current` 后，
没有别的可运行进程时会回退 idleproc，语义更清晰。

**Bug 3：`switch_to` 用 32 位 `sw/lw` 的诱惑。**
RV64 的寄存器是 64 位，`struct context` 的字段也是 8 字节。初稿如果照搬 RV32 的 `sw/lw`，
只能存/取低 32 位，`ra`、`sp` 的高位丢失，切换后必定跳飞。
最终统一使用 `sd/ld`，偏移按 `0*8 ... 13*8` 书写，避免手算错位。

## 3.8 测试结果

### 3.8.1 本地环境说明

本机使用 WSL2 Ubuntu 22.04 + `riscv64-unknown-elf-gcc 10.2.0` + QEMU 6.2。
有两个环境差异需要显式指定：

- 工具链里没有 `riscv64-unknown-elf-gdb`，评分脚本的 `GDB` 变量改用 `gdb-multiarch`；
- shell 环境里已经定义了 `QEMU=qemu`，会覆盖 Makefile 里的默认值，
  因此运行时显式写 `QEMU=qemu-system-riscv64`。

对应命令：

```bash
make grade QEMU=qemu-system-riscv64 GDB=gdb-multiarch
```

### 3.8.2 评分结果

`make grade` 的三个注入测试全部通过：

```text
  -check PMM:                                OK
  -check Process Create:                     OK
  -check Schedule:                           OK
Total Score: 30/30
```

![make grade 通过](./images/lab4_make_grade.png)

`make qemu` 的实际运行输出（initproc 打印两条消息后主动退出，`do_exit()` 触发 panic，
这是本实验简化 init 流程的预期行为）：

```text
++ setup timer interrupts
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
kernel panic at kern/process/proc.c:208:
    process exit!!.
```

![make qemu 输出](./images/lab4_make_qemu.png)

## 3.9 小结

模块三的四个函数职责清晰、边界明确：

- `cpu_idle` 决定“什么时候调度”，是调度请求的消费者；
- `schedule` 决定“调度谁”，负责链表遍历与轮转策略；
- `proc_run` 决定“怎么换”，负责 `current`、`satp`、TLB 与上下文切换；
- `switch_to` 是唯一碰寄存器的地方，只保存 callee-saved，保证“切出去还能切回来”。

把四者串起来，Lab4 才从“能创建一个进程”走到“多个进程能轮流执行”。
