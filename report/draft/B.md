# Lab3 乙部分报告内容（草稿）

> 本文件汇总乙（成员 B，2412386）负责的报告小节：4.2 功能模块二、4.3.2 Best-Fit 回填、
> 5.1–5.4 测试与验证。待与甲、丙的草稿合并进 `report/report.md`。

---

## 四、功能实现

### 4.2 功能模块二：中断与异常处理（`kern/trap/trap.c`）

#### 4.2.1 模块位置与执行流

`__alltraps` 保存 Trap frame 后调用 `trap(tf)`，`trap()` 内部由 `trap_dispatch()` 按
`scause` 最高位分流：

- bit63 = 1 → `interrupt_handler(tf)`：异步中断（时钟、软件、外部）
- bit63 = 0 → `exception_handler(tf)`：同步异常（非法指令、断点等）

两个 handler 返回后统一回到 `__trapret`，由 `RESTORE_ALL` + `sret` 恢复现场。因此本模块
只负责"分类 + 处理 + 按需推进 `epc`"，不参与上下文保存/恢复。

#### 4.2.2 三个核心函数

| 函数 | 职责 | 关键点 |
|---|---|---|
| `interrupt_handler(tf)` | 处理中断 | 先去掉 `scause` 的 bit63；时钟路径先续期再计数；未知中断打印原因码 + Trap frame |
| `exception_handler(tf)` | 处理异常 | 非法指令/断点按规定格式输出，并按真实指令长度推进 `tf->epc`；其余异常只打印 Trap frame |
| `insn_length(epc)`（static 辅助） | 判断指令字节长度 | 读 `epc` 处 2 字节，低 2 位 `0b11` → 4 字节，否则 2 字节（压缩指令） |

`interrupt_handler` 的时钟分支：

```c
case IRQ_S_TIMER:
    clock_set_next_event();          // 先续期：OpenSBI 的定时器是一次性的
    ticks++;                         // 无条件自增，不能写进 if 里
    if (ticks % TICK_NUM == 0) {
        print_ticks();               // 已有的 "100 ticks" 输出
        if (ticks >= (size_t)SHUTDOWN_AFTER * TICK_NUM) {
            sbi_shutdown();          // 累计输出 SHUTDOWN_AFTER 次后关机
        }
    }
    break;
```

`exception_handler` 的两条异常路径：

```c
case CAUSE_ILLEGAL_INSTRUCTION:
    cprintf("Illegal instruction at 0x%016lx\n", tf->epc);
    tf->epc += insn_length(tf->epc);
    break;
case CAUSE_BREAKPOINT:
    cprintf("Breakpoint at 0x%016lx\n", tf->epc);
    tf->epc += insn_length(tf->epc);
    break;
default:
    print_trapframe(tf);             // 不改 epc
    break;
```

#### 4.2.3 六个关键设计点

1. **`scause` 必须先去掉最高位**：`scause` 的 bit63 只表示"中断/异常"标志，原因码在低
   63 位。`trap_dispatch()` 用有符号 `(intptr_t)tf->cause < 0` 分流之后，handler 里若
   直接用原值比较 `IRQ_S_TIMER`，时钟分支永远不命中。实现用
   `cause = (intptr_t)((tf->cause << 1) >> 1)` 清掉最高位再 `switch`。
   注意异常侧 bit63 本来就是 0，直接 `switch (tf->cause)` 即可。
2. **时钟必须先续期、后计数**：`clock_set_next_event()` 每次只设置"下一次"时钟事件，不
   重新调用就只能收到一次中断。先续期再 `ticks++`，保证本次中断处理完时下一次事件已经
   排好，中断流不会中断。
3. **`ticks++` 必须无条件执行**：统计输出只在 `ticks % TICK_NUM == 0` 时发生，但计数
   每一次时钟中断都要加 1；若把 `ticks++` 写进 `if`，`ticks` 永远不会到达整数倍，
   系统表现为"没有任何输出、也永不关机"。
4. **`epc` 按真实指令长度推进**：RISC-V 有 16 位压缩指令，`ebreak` 常被汇编成 2 字节的
   `c.ebreak`；编译器的非法指令（本实验测试用 4 字节 `.word 0x7b`）是 4 字节。固定写
   `epc += 4` 会让 2 字节异常指令跳过下一条合法指令，测试 1 立刻失败。实现读取 `epc`
   处 2 字节编码，低 2 位 `0b11` 为 32 位指令（+4），否则 +2。
5. **兜底分支是硬要求**：未知中断输出 `Unknown interrupt: cause %d`（原因码已去掉最高位，
   例如 `0x800000000000000f` 输出 15）并调用 `print_trapframe(tf)`；未知异常只调用
   `print_trapframe(tf)`，且**不修改** `epc`——本实验不要求恢复其它异常。
6. **两个 handler 不能声明为 `static`**：`tools/test_lab3.c` 用 `extern` 直接调用它们做
   评分（软件中断/未知中断/未知异常的兜底路径），`static` 会导致链接期找不到符号。
   另外它们必须定义在 `trap_dispatch()` 之前，否则 `-Werror` 下隐式声明直接编译失败。

补充：`sbi_shutdown()` 声明在 `libs/sbi.h`，必须 `#include <sbi.h>`；
`SHUTDOWN_AFTER` 骨架未定义，本实验取 10（`TICK_NUM = 100`，即 1000 次 tick 后关机，
大于测试 3 需要的 200 次，不会提前关机）。

#### 4.2.4 功能模块二 spec 摘要

- **Pre**：`tf` 是合法 Trap frame；`interrupt_handler` 收到 bit63=1 的 `cause`，
  `exception_handler` 收到 bit63=0 的 `cause`；两个 handler 在 `trap_dispatch()` 之前可见。
- **Post**：返回后由 `__trapret` 恢复现场；除非法指令/断点按需推进 `tf->epc` 外，
  不修改其它字段。
- **时钟路径**：已重新设置下一次事件，`ticks` 恰好 +1；`ticks` 为 `TICK_NUM` 整数倍时
  输出 `100 ticks`；累计输出 `SHUTDOWN_AFTER` 次后调用 `sbi_shutdown()`。
- **异常路径**：非法指令输出 `Illegal instruction at 0x%016lx`、断点输出
  `Breakpoint at 0x%016lx`，地址为 16 位补零小写十六进制，`epc` 推进 2 或 4 字节；
  其余异常 `print_trapframe(tf)` 兜底。
- **禁止**：关中断、`panic()`、阻塞/递归逻辑、修改 `print_ticks()`/`trap_dispatch()`/
  `trap()`/`print_trapframe()` 等既有函数。

#### 4.2.5 prompt 摘要

本模块按"四段式"（[PROMPT] / [RELY] / [GUARANTEE] / [SPECIFICATION]）设计提示词，
把上述六个关键点全部写进 RELY 与 SPECIFICATION 作为硬约束，再让 AI 生成。完整 prompt
与迭代记录见 `report/prompt.md` 的 P2。提示词要点：

- RELY 给出真实接口（`ticks`、`clock_set_next_event`、`sbi_shutdown`、`IRQ_*`、
  `CAUSE_*`）和判分子串，避免 AI 虚构 API/输出格式；
- GUARANTEE 明确"两个 handler 不得 static""可加 static 辅助函数""不改既有函数"；
- SPECIFICATION 逐分支写清顺序（续期→计数→输出→关机）、`epc` 长度判定和兜底行为。

---

### 4.3.2 Best-Fit 连续物理内存分配器（`kern/mm/best_fit_pmm.c`）

#### 功能描述

Best-Fit 复用 First-Fit（`default_pmm.c`）的全部内部状态约定：

- `free_list`：按物理地址升序排列的空闲块双向链表；
- `nr_free`：空闲页总数；
- "空闲块只看头页"：只有块首页带 `PG_property`，其 `property` 记录本块页数；
- 回收时必须与物理相邻块合并。

与 First-Fit 唯一的差别是**选块策略**：从所有 `property >= n` 的块中挑 `property`
**最小**的块，尽量少产生切分后的剩余碎片。

#### 核心函数

| 函数 | 职责 | 关键点 |
|---|---|---|
| `best_fit_init()` | 初始化管理器状态 | `list_init(&free_list)`、`nr_free = 0` |
| `best_fit_init_memmap(base, n)` | 登记一个空闲块 | 断言 `PageReserved` → 清 `flags/property/ref` → 块头设 `property=n` + `SetPageProperty` → `nr_free += n` → 按地址升序插入 |
| `best_fit_alloc_pages(n)` | 分配 n 个连续页 | 扫全链表取**最小**可装块；剩余部分切块插回；`nr_free -= n`、清块头标志 |
| `best_fit_free_pages(base, n)` | 释放并合并 | 断言非保留非空闲 → 重设块头 → 有序插入 → 向前/向后合并 |
| `best_fit_nr_free_pages()` | 返回 `nr_free` | — |

#### 关键点

1. **块头约定**：`init_memmap` 只在 `base` 上写 `property = n` 并 `SetPageProperty(base)`，
   块内其它页的 `property` 无意义；分配后必须 `ClearPageProperty(page)`，表示该块已不再是
   空闲块头。
2. **有序链表**：插入时用 `base < page` 比较 `struct Page *` 指针。`pages` 是按物理页号
   连续排布的描述符数组，指针序等价于物理地址序，因此可以用指针比较完成"按物理地址
   升序插入"。
3. **最小块选择**：遍历完整条链表，用 `page == NULL || p->property < page->property`
   替换候选；`property == n` 时提前 `break`（正好装下，不可能更优）。等长块保留链表里
   第一个（物理地址最低）的候选。
4. **切分与插回**：先记前驱 `prev` 再 `list_del`；仅当 `page->property > n`（严格大于）
   时把 `page + n` 作为新块头插回 `prev` 之后，链表仍保持有序。
5. **双向合并**：释放后先看前驱是否满足 `p + p->property == base`，再看后继是否满足
   `base + base->property == p`；合并时更新 `property`、`ClearPageProperty` 被并入的块头、
   `list_del` 多余节点。
6. **管理器注册**：文件末尾定义
   `const struct pmm_manager best_fit_pmm_manager`，`check` 指向骨架已提供的
   `best_fit_check`；`pmm.c` 的 `init_pmm_manager()` 只改一行，把活动管理器从
   `default_pmm_manager` 切换为 `best_fit_pmm_manager`。
7. **与 Lab2 骨架的差异**：Lab3 骨架里已经带有 `basic_check()` / `best_fit_check()`，
   回填时**不要整文件拷贝 Lab2**，否则会重复定义；只补实现函数、宏和注册结构体。

---

## 五、测试与验证

### 5.1 编译

```bash
cd code
make clean && make
```

输出（节选）：

```text
+ cc kern/trap/trap.c
+ cc kern/trap/trapentry.S
+ cc kern/mm/best_fit_pmm.c
+ cc kern/mm/default_pmm.c
+ cc kern/mm/pmm.c
...
+ ld bin/kernel
riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img
```

编译选项含 `-Wall -Werror`，全程无警告、无错误。

### 5.2 `make qemu`

```bash
make qemu
```

关键输出：

```text
memory management: best_fit_pmm_manager
physical memory map:
  memory: 0x0000000008000000, [0x0000000080000000, 0x0000000087ffffff].
check_alloc_page() succeeded!
++ setup timer interrupts
100 ticks
100 ticks
...（共 10 行）
```

结论：Best-Fit 通过启动自检 `check_alloc_page()`，活动管理器确认为 `best_fit_pmm_manager`；
时钟中断持续产生，`SHUTDOWN_AFTER = 10` 时累计 10 行 `100 ticks` 后正常关机，说明
"续期→计数→输出→关机"链路正确。

### 5.3 注入测试（Test 1 / 2 / 3）

```bash
make grade
```

#### Test 1：Trap 入口、寄存器恢复与断点 EPC（35 分）

```text
Breakpoint at 0xffffffffc0201a28
[Test 1] Trap entry and breakpoint return: PASS (35/35)
```

结论：`__alltraps`/`RESTORE_ALL`（成员 A）保存并恢复了 35 项上下文；断点异常被
`exception_handler` 识别，按 `c.ebreak` 的 2 字节长度推进 `epc` 后 `sret` 正常返回，
没有重复触发断点。

#### Test 2：非法指令 EPC 推进与兜底（25 分）

```text
Illegal instruction at 0xffffffffc02019d8
Supervisor software interrupt
Unknown interrupt: cause 15
  cause    0x0000000f
  cause    0x00000014
[Test 2] Illegal instruction return: PASS (25/25)
```

结论：

- 4 字节自定义非法指令 `.word 0x7b` 触发异常，打印地址后 `epc += 4`，返回后越过该指令；
- 软件中断路径输出 `Supervisor software interrupt`；
- `cause = 0x800000000000000f` 去掉最高位后为 15，走未知中断分支，输出
  `Unknown interrupt: cause 15` 并打印 Trap frame（其中 `cause 0x0000000f`）；
- 未知异常（cause 20）走 `print_trapframe` 兜底，输出 `cause 0x00000014`，且不修改 `epc`。

#### Test 3：时钟中断续期与重复返回（40 分）

```text
100 ticks
100 ticks
[Test 3] Sustained supervisor timer: PASS (40/40, ticks=200)
```

结论：时钟中断在每次处理后都重新设置下一次事件，`ticks` 持续增长到 200，说明没有
"只中断一次"的问题，且 Trap 返回路径可重复执行。

### 5.4 评分汇总

```text
  - trap entry, register restore, and breakpoint EPC: PASS (35/35)
  - illegal instruction EPC advance and return: PASS (25/25)
  - re-armed supervisor timer and repeated trap return: PASS (40/40)
Total Score: 100/100
```

| 测试 | 分值 | 结果 |
|---|---|---|
| Test 1 Trap 入口/寄存器/断点 | 35 | PASS |
| Test 2 非法指令/兜底 | 25 | PASS |
| Test 3 时钟续期 | 40 | PASS |
| **合计** | **100** | **100/100** |

本次两项实现均为一次通过，未出现运行期 bug；提示词在设计阶段已把这几个高频坑写死
（详见 P2 的迭代记录）。
