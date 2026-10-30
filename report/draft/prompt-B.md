# Lab3 乙部分提示词记录（草稿）

> 乙（成员 B，2412386）负责的提示词：P2 功能模块二（`trap.c` 中断与异常处理）、
> P-lab2-bestfit（Lab2 Best-Fit 回填）。最终合并进 `report/prompt.md`。

---

## P2：功能模块二（中断与异常处理）

### Spec 摘要

| 项目 | 内容 |
|---|---|
| 输入 | `struct trapframe *tf`（`gpr[32]` + `status/epc/badvaddr/cause`） |
| 输出 | 中断/异常已处理；非法指令与断点按真实指令长度推进 `tf->epc`；返回后由 `__trapret` 恢复现场 |
| 时钟路径 | 先 `clock_set_next_event()`，再无条件 `ticks++`；每 `TICK_NUM` 输出；累计 `SHUTDOWN_AFTER` 次后 `sbi_shutdown()` |
| 异常路径 | 非法指令/断点按规定格式输出并推进 `epc`（2 或 4 字节）；其余 `print_trapframe(tf)` 兜底 |
| 约束 | 两个 handler 不得 `static`；不改既有函数；不关中断、不 `panic()`、不递归 |

### 最终提示词

```text
[PROMPT]
**任务**：修改文件 `code/kern/trap/trap.c`，在 `//lab3 function2 YOUR CODE` 处实现
`interrupt_handler(struct trapframe *tf)` 与 `exception_handler(struct trapframe *tf)`，完成 Lab3 功能
模块二：中断侧完成时钟中断的续期、计数、统计输出与到点关机，区分软件中断/外部中断，未知中断兜底；
异常侧按规定格式处理非法指令与断点异常并推进 `tf->epc`，其余异常用 `print_trapframe(tf)` 兜底。
完成后系统应能正确处理上述 Trap，并经 `__trapret`/`sret` 正常返回。

**操作要求**：必须在项目中修改实际文件，只在上述标记处插入代码，并可在顶部补充
`#include <sbi.h>` 与 `#define SHUTDOWN_AFTER 10`；保留注释并把 `YOUR CODE` 改为学号 2412386。
不得修改 `print_ticks()`、`trap_dispatch()`、`trap()`、`print_trapframe()` 等既有函数，不得修改
`tools/`、`Makefile`、`init.c`、`trapentry.S`、`trap.h`；不实现 Challenge，不新增文件。

**输出要求**：直接进行文件操作，严格使用下方 [RELY]、[GUARANTEE]、[SPECIFICATION] 的信息，不引入
无关功能，`-Wall -Werror` 下无警告。

[RELY]
// 骨架现状
#define TICK_NUM 100              // print_ticks() 已实现，打印 "%d ticks\n"，即 "100 ticks"
// trap_dispatch() 用 (intptr_t)tf->cause < 0 分流，之后才调用两个 handler
// 因此两个 handler 必须定义在 trap_dispatch() 之前

// 宏（libs/riscv.h）
#define IRQ_S_SOFT   1
#define IRQ_S_TIMER  5
#define IRQ_S_EXT    9
#define CAUSE_ILLEGAL_INSTRUCTION 0x2
#define CAUSE_BREAKPOINT          0x3
// scause bit63 = 1 为中断，= 0 为同步异常；本实验 Trap 均来自 S 模式

// 结构（kern/trap/trap.h）
struct trapframe {
    struct pushregs gpr;   // 32 项通用寄存器
    uintptr_t status; uintptr_t epc; uintptr_t badvaddr; uintptr_t cause;
};
// tf->cause 为 scause 原值；tf->epc 为异常指令地址（2 字节对齐）

// 可用接口与变量
void print_trapframe(struct trapframe *tf);  // 输出完整 Trap frame
void clock_set_next_event(void);             // 一次性接口，每次时钟中断都必须重新调用
extern volatile size_t ticks;                // 时钟中断累计次数
void sbi_shutdown(void);                     // libs/sbi.h，关机且不返回
int cprintf(const char *fmt, ...);           // 支持 %d、%016lx

// 判分子串：Breakpoint at / Illegal instruction at / Supervisor software interrupt
//           Unknown interrupt: cause 15 / cause    0x00000014 / 100 ticks

[GUARANTEE]
```c
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
```
可添加：`static` 辅助函数（如指令长度判定）、宏定义、模块内静态变量。
禁止：把两个 handler 声明为 `static`（`tools/test_lab3.c` 以 `extern` 引用），或修改既有函数。

[SPECIFICATION]
## 公共约定
**Pre-Condition**: 两个 handler 在 `trap_dispatch()` 之前可见；`tf` 为合法 Trap frame。
**Post-Condition**: 返回后由 `__trapret` 恢复现场；除异常路径按需推进 `tf->epc` 外，不得修改 `tf`。
**Requirements**: 不关闭中断、不调用 `panic()`、不引入阻塞或递归逻辑。

## interrupt_handler
**Pre-Condition**: `tf->cause` 的 bit63 = 1。
**Post-Condition**: 本次中断已处理；时钟路径已重设下一次事件且 `ticks` 恰增 1；未识别中断已打印 Trap frame。

**Case 1** —— 时钟中断（去掉最高位后等于 `IRQ_S_TIMER`）：先调用 `clock_set_next_event()`，再执行
`ticks++`（顺序不可颠倒）；`ticks` 为 `TICK_NUM` 整数倍时调用已有的 `print_ticks()`；统计输出累计
达到 `SHUTDOWN_AFTER` 次（即 `ticks` 达到 `SHUTDOWN_AFTER × TICK_NUM`）时调用 `sbi_shutdown()`，
在此之前必须正常返回；`ticks++` 必须无条件执行。

**Case 2** —— 软件中断：输出 `Supervisor software interrupt`，正常返回。

**Case 3** —— 外部中断：输出对应提示，正常返回。

**Case 4** —— 其他中断：输出 `Unknown interrupt: cause %d`，`%d` 为去掉最高位后的原因码
（如 `0x800000000000000f` 应输出 15），再调用 `print_trapframe(tf)`。

**Requirements**: 判断前必须先清除最高位，不能用原始 `tf->cause` 直接分支；不得修改 `tf->epc`。

## exception_handler
**Pre-Condition**: `tf->cause` 的 bit63 = 0。
**Post-Condition**: 已按规定输出；两类已实现异常的 `tf->epc` 已推进到下一条指令；其余字段不变。

**Case 1** —— 非法指令：`cprintf` 输出 `Illegal instruction at 0x%016lx` 加换行；读取 `tf->epc` 处
2 字节编码，低 2 位为 `0b11` 则加 4，否则加 2，不得固定加 4。

**Case 2** —— 断点：输出 `Breakpoint at 0x%016lx` 加换行，同样按真实指令长度推进（`ebreak` 常被
汇编为 2 字节 `c.ebreak`）。

**Case 3** —— 其他异常：不改变 `tf->epc`，调用 `print_trapframe(tf)` 兜底。

**Requirements**: 输出前缀与判分子串完全一致，地址为 16 位补零小写十六进制；必须有兜底分支。
```

### 提示词迭代记录

**v1：首次提交。** 在使用前先做了一次规范性核对，结论是可直接实现：

- 接口全部对照骨架核对过：`ticks`（`clock.h`）、`clock_set_next_event()`、`sbi_shutdown()`
  （`sbi.h`）、`IRQ_S_*`/`CAUSE_*`（`riscv.h`）、`trapframe` 字段（`trap.h`）均真实存在；
- 判分的 5 个关键子串全部覆盖，且 `%016lx` 已被 `libs/printfmt.c` 支持；
- 明确了"handler 不得 static""必须定义在 `trap_dispatch` 之前"这两个会直接导致
  链接/编译失败的点。

**v2：实现前的两处补齐（提示词未写死的地方）。**

| 位置 | 提示词原文 | 实现时的处理 |
|---|---|---|
| Case 3 外部中断 | "输出对应提示" | 输出 `Supervisor external interrupt`；`grade.sh` 不检查该行，不影响评分 |
| 关机判定 | "统计输出累计达到 `SHUTDOWN_AFTER` 次（即 `ticks` 达到 `SHUTDOWN_AFTER × TICK_NUM`）" | 直接判 `ticks >= SHUTDOWN_AFTER * TICK_NUM`，不额外引入静态计数器，少一个状态 |

**本次没有出现运行期 bug**：按上面的提示词一次生成，`make` 无警告，`make grade`
直接 `Total Score: 100/100`。原因是提示词把历史上最容易踩的 6 个坑写成了硬约束，
生成结果没有机会犯：

1. `scause` 最高位未清除 → 时钟分支永远不命中；
2. 忘记重新调用 `clock_set_next_event()` → 只收到一次时钟中断；
3. `ticks++` 写进 `if (ticks % TICK_NUM == 0)` → `ticks` 永远到不了整数倍；
4. `epc += 4` 固定偏移 → 2 字节的 `c.ebreak` 会被跳过一条合法指令；
5. handler 写成 `static` → `tools/test_lab3.c` 的 `extern` 引用链接失败；
6. 漏 `#include <sbi.h>` 或漏定义 `SHUTDOWN_AFTER` → `-Werror` 下编译失败。

> 说明：以上是"提示词在设计阶段主动规避"的清单，不是本次实际发生的 bug；
> 报告"bug 与修正"栏按实际情况填写"一次通过，无运行期 bug"。

---

## P-lab2-bestfit：Lab2 Best-Fit 回填

### 背景与回填提示词

Lab3 依赖 Lab2 的物理内存管理，需要把 Best-Fit 回填到 Lab3 骨架。本次按分工要求
**直接复用 `lab2` 分支上已经验证过的实现**（`git show lab2:code/kern/mm/best_fit_pmm.c`），
而不是重新生成。为保证可复现，下面保留 Lab2 阶段的最终提示词：

```text
[PROMPT]
请修改 kern/mm/best_fit_pmm.c 文件，将当前 //lab2 function2-2 YOUR CODE 下方的内部状态
以及空的接口函数 best_fit_init、best_fit_init_memmap、best_fit_alloc_pages、
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
3. best_fit_alloc_pages：核心分配函数（取能装下的最小块）。
4. best_fit_free_pages：释放并合并函数（前向/后向合并）。
5. best_fit_nr_free_pages：返回总空闲页数。

[SPECIFICATION]
1. best_fit_init：使用 list_init(&free_list) 初始化链表，并将 nr_free 设为 0。
2. best_fit_init_memmap：断言 n>0；用 assert(PageReserved(p)) 验证传入页是保留状态；
   遍历 n 页清除 flags 和 property、引用计数设为 0；base->property 设为 n 并调用
   SetPageProperty(base) 标记块头；nr_free += n；然后将该块升序插入 free_list。
3. best_fit_alloc_pages：断言 n>0；若 n>nr_free 返回 NULL；遍历整个 free_list，寻找
   p->property >= n 的块中最小的块 page；若找到，记录前驱节点并将其 list_del；
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

### Lab3 回填时的适配点

1. **不要整文件拷贝**：Lab3 骨架的 `best_fit_pmm.c` 已内置 `basic_check()` /
   `best_fit_check()`，只补实现函数、宏和末尾的 `best_fit_pmm_manager` 注册结构体，
   否则重复定义直接编译失败。
2. **管理器切换**：`pmm.c` 的 `init_pmm_manager()` 只改一行
   `pmm_manager = &best_fit_pmm_manager;`，不改 `alloc_pages`/`free_pages` 等封装。
3. **验证**：`make -k` 中 `best_fit_pmm.c` 无编译错误；QEMU 启动输出
   `memory management: best_fit_pmm_manager` 与 `check_alloc_page() succeeded!`。

### 迭代记录

本次直接复用 Lab2 分支已验证的实现，没有重新生成，因此没有新增 bug；回填后编译通过，
启动自检 `check_alloc_page()` 通过，`make grade` 100/100。
