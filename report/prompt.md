# 操作系统 Lab3 提示词记录（P0–P3）

> 本文件汇总第 55 组在 Lab3 中实际使用的提示词：P0 是甲/乙/丙共用的四段式模板，
> P1/P2 分别对应两个功能模块，P3 是环境排障与验证流程，P-lab2-* 为 Lab2 代码回填。
> 每个模块都包含 Spec 摘要、最终提示词与迭代记录，可据此复现整个实现过程。

## 提示词索引

| 编号 | 主题 | 负责人 | 对应实现 |
|---|---|---|---|
| P0 | 四段式模板与使用原则 | 丙 2412440-王朝阳 | 全部模块 |
| P1 | 功能模块一：`RESTORE_ALL`/`__alltraps` | 甲 2413541-赖鸿毅 | `kern/trap/trapentry.S` |
| P2 | 功能模块二：中断与异常处理 | 乙 2412386-唐港 | `kern/trap/trap.c` |
| P3 | 环境排障与验证（P3-1/2/3） | 丙 2412440-王朝阳 | 构建、注入、截图 |
| P-lab2-page_init | Lab2 `page_init()` 回填 | 甲 2413541-赖鸿毅 | `kern/mm/pmm.c` |
| P-lab2-bestfit | Lab2 Best-Fit 回填 | 乙 2412386-唐港 | `kern/mm/best_fit_pmm.c` |

---

## P0：四段式提示词模板（甲/乙/丙共用）

### 模板

```text
[PROMPT]
**任务**：说明要改哪个文件、哪一处标记、实现什么功能、完成后系统应达到什么可观测状态。
**操作要求**：必须修改实际文件；只允许在指定标记处插入代码；列出允许新增的 include/宏；
保留原有注释并把 YOUR CODE 换成学号；列出"不得修改"的文件与函数；不新增文件、不实现 Challenge。
**输出要求**：直接进行文件操作；严格使用下方 RELY/GUARANTEE/SPECIFICATION 的信息；
不引入无关功能；`-Wall -Werror` 下无警告。

[RELY]
// 只写骨架中真实存在的符号，逐条给出：
// 1) 现有宏与常量（如 TICK_NUM、IRQ_S_*、CAUSE_*）
// 2) 现有结构体字段与偏移（如 struct trapframe）
// 3) 可用接口与全局变量（如 clock_set_next_event、ticks、sbi_shutdown）
// 4) 已有函数的行为与调用顺序（如 trap_dispatch 用有符号 cause 分流）
// 5) 判分子串 / 期望输出格式

[GUARANTEE]
// 必须实现的函数原型与可见性（如两个 handler 不得 static）
// 允许新增的内容（static 辅助函数、宏、模块内静态变量）
// 禁止修改的内容（既有函数、tools/、Makefile、结构体、宏体）

[SPECIFICATION]
## 公共约定
**Pre-Condition**：进入该代码前的状态。
**Post-Condition**：返回后的状态与不变量。
**Requirements**：禁止事项（关中断、panic、阻塞、递归等）。

## 分支 1 / 2 / 3 ...
逐条写清判断条件、执行顺序、输出格式、边界处理。
```

### 使用原则

1. **RELY 只写"能在仓库里指出来"的符号**。写"某个函数大概叫 xxx"会让 AI 虚构 API；
   写 `void clock_set_next_event(void);` 才能得到可编译的调用。
2. **把历史坑写进 GUARANTEE/SPECIFICATION**，而不是等出错再修。例如
   "`sp` 最后恢复""`ticks++` 必须无条件""`epc` 不得固定 +4""两个 handler 不得 `static`"。
3. **顺序类要求写成编号步骤**。时钟路径的"先续期、再计数、再输出、最后关机"如果只写
   "处理时钟中断"，AI 有较大概率把 `ticks++` 放进 `if`。
4. **判分子串原样给出**。`grade.sh` 按子串计分，`Breakpoint at 0x%016lx` 与
   `Breakpoint at` 是两种不同的严格程度。
5. **一次只改一个模块**。模块一（汇编）与模块二（C）分开提，避免跨文件约束互相稀释。

---

---

## P1：功能模块一（Trap 上下文恢复与入口）

### Spec 摘要

| 项目 | 内容 |
|---|---|
| 输入 | 当前 `sp` 指向的 36 项 Trap frame（`SAVE_ALL` 已建好） |
| 输出 | `sstatus`/`sepc` + `x1`、`x3`–`x31` 已恢复；`sp` 回到 Trap 前；`trap()` 已按调用约定收到 `tf` |
| 关键不变量 | 偏移与 `struct trapframe` 一一对应；`x0` 不恢复；`x2(sp)` 最后恢复；`a0 = sp`；`jal trap` 后 `sp` 不变 |
| 约束 | 不改 `SAVE_ALL`、`struct trapframe`、`trap()` 签名、`__trapret`；只填两处 `#lab3 function1` |
| 验收 | Test 1：`Breakpoint at 0x...` + PASS（35/35），被检查寄存器全部原值 |

### 最终提示词

```text
[PROMPT]
在 riscv64 ucore 的 code/kern/trap/trapentry.S 中，补全 RESTORE_ALL 宏，以及 __alltraps
里 SAVE_ALL 之后、__trapret 之前的代码，使 Trap 能形成
"保存上下文 -> 调 C 处理函数 -> 恢复上下文 -> sret" 的闭环。

[RELY]
1. SAVE_ALL 已给出，不得修改。它先把原 sp 写入 sscratch，再 addi sp, sp, -36*REGBYTES，
   然后按 0*REGBYTES~31*REGBYTES 保存 x0~x31（跳过 x2 的常规保存）；
   最后用 csrrw s0, sscratch, x0 取回原 sp，存到 2*REGBYTES(sp)，
   并把 sstatus/sepc/sbadaddr/scause 存到 32/33/34/35*REGBYTES(sp)。
2. struct trapframe 的字段顺序是：gpr.zero..gpr.t6、status、epc、badvaddr、cause，
   在 RV64 下每个字段 8 字节，共 36 项。
3. 已经存在 __trapret 标签，内容是 RESTORE_ALL; sret。
4. trap(struct trapframe *tf) 的签名不能改；RISC-V 调用约定第一个参数放 a0。
5. 本实验 Trap 来自 S 模式（sstatus.SPP=1），sret 返回 S 模式；不要手动清 SPP。

[GUARANTEE]
- 不修改 SAVE_ALL、struct trapframe、trap() 签名和 __trapret 的位置；
- 只允许使用 riscv.h 里的 LOAD/REGBYTES 宏；
- 保留 #lab3 注释，把 YOUR CODE 换成 2413541；
- 不实现 Challenge（`lab3_switch_test` 是旧版遗留），不改 init.c；
- 如果工具链报 unknown CSR 'sbadaddr'，只在文件顶部加
  #ifndef sbadaddr / #define sbadaddr stval / #endif，不改 SAVE_ALL 宏体。

[SPECIFICATION]
1. RESTORE_ALL：
   a) 先 LOAD sstatus/sepc（32/33 项）并用 csrw 写回；
   b) 恢复 x1、x3~x31，偏移与 SAVE_ALL 严格一一对应；
   c) x0 不恢复；
   d) x2(sp) 必须最后处理：用 addi sp, sp, 36*REGBYTES 恢复，
      禁止在任何其它 LOAD 之前改动 sp。
2. __alltraps：
   a) SAVE_ALL 之后执行 addi a0, sp, 0，把 Trap frame 首地址作为第一个参数；
   b) jal trap；返回后 sp 必须仍然等于 Trap frame 首地址，顺序落入 __trapret。
3. 验收：Test 1 的 35/35 通过；被检查的通用寄存器全部原值；不会重复执行断点指令。
```

### 提示词迭代记录

**v1：按“恢复所有寄存器”直译。**

提示词只写了“从 Trap frame 恢复所有通用寄存器和必要 CSR”，AI 生成的第一版以：

```asm
LOAD sp, 2*REGBYTES(sp)
```

开头。问题：`sp` 是恢复过程中的地址基址，一旦提前被原栈顶覆盖，后面的 `LOAD xN, N*REGBYTES(sp)`
全部指向错误位置，Test 1 的寄存器检查失败，严重时 `trap()` 返回后跑飞。

修正：在 SPECIFICATION 中单列“x2 最后恢复，禁止提前动 sp”，并把 `x2` 从常规恢复列表移除。

**v2：`__alltraps` 只写了 `jal trap`。**

第二版 `RESTORE_ALL` 正确，但入口没有设置 `a0`，`trap()` 收到 Trap 发生时的旧 `a0`（野指针），
`print_trapframe()` 打印乱码。

修正：在 SPECIFICATION 明确“SAVE_ALL 之后 `addi a0, sp, 0`，再 `jal trap`；返回后 sp 必须仍等于 tf”。

**v3：工具链兼容（不是逻辑 bug）。**

用较新的 binutils 汇编时，`SAVE_ALL` 里的 `csrr s3, sbadaddr` 报：

```text
Error: unknown CSR `sbadaddr'
```

`sbadaddr` 是旧别名，CSR 编号与 `stval` 相同（0x143）。修正：在文件顶部加

```asm
#ifndef sbadaddr
#define sbadaddr stval
#endif
```

`SAVE_ALL` 宏体一字未改，旧工具链和新工具链都能编。

### 验证

```text
Breakpoint at 0xffffffffc0201a28
[Test 1] Trap entry and breakpoint return: PASS (35/35)
```

`ebreak` 是 2 字节 `c.ebreak`，模块二按指令长度推进 `epc` 后 `sret` 正常返回；该地址随构建/工具链变化，A 早期草稿的 `0xffffffffc02019a8` 与最终构建的 `0xffffffffc0201a28` 的差异见 P3-3。

---

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
（如原因码 15 输出 15；若 bit63 置位先清掉），再调用 `print_trapframe(tf)`。

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

**模块二没有出现运行期 bug**：按上面的提示词一次生成，`make` 无警告，`make grade`
直接 `Total Score: 100/100`。原因是提示词把历史上最容易踩的 6 个坑写成了硬约束，
生成结果没有机会犯：

1. `scause` 最高位未清除 → 时钟分支永远不命中；
2. 忘记重新调用 `clock_set_next_event()` → 只收到一次时钟中断；
3. `ticks++` 写进 `if (ticks % TICK_NUM == 0)` → `ticks` 永远到不了整数倍；
4. `epc += 4` 固定偏移 → 2 字节的 `c.ebreak` 会被跳过一条合法指令；
5. handler 写成 `static` → `tools/test_lab3.c` 的 `extern` 引用链接失败；
6. 漏 `#include <sbi.h>` 或漏定义 `SHUTDOWN_AFTER` → `-Werror` 下编译失败。

> 说明：以上是模块二"提示词在设计阶段主动规避"的清单，不是模块二实际发生的运行期 bug；
> 模块一实际修掉的 `sp`/`a0`/`sbadaddr` 见 P1，报告按实际情况记录。

---

---

## P3：环境排障与验证提示词

### P3-1：`make grade` 全部 FAIL / 串口无输出

#### Spec 摘要

| 项目 | 内容 |
|---|---|
| 现象 | `make grade` 三个用例全 `FAIL`，`-serial file:.qemu.out` 为 0 字节；手动 QEMU 只见 OpenSBI 横幅 |
| 目标 | 判断是"内核代码错"还是"启动方式错"，给出最小验证步骤与不修改仓库的绕行方案 |
| 约束 | 不改 `tools/`、`Makefile`；所有验证命令必须可复制执行；结论要有输出证据 |
| 验收 | 能解释 `a1`（DTB 地址）从哪来，并给出让内核继续启动的启动参数 |

#### 最终提示词

```text
[PROMPT]
**任务**：排查 riscv64 ucore Lab3 在 QEMU 6.2 下 `make grade` 三个用例全部 FAIL 的问题。
现象是 grade.sh 用 `-device loader,file=bin/ucore.img,addr=0x80200000` 装载内核，
运行后串口只打印 OpenSBI 横幅，`-serial file:.qemu.out` 为 0 字节，内核无任何输出。
请判断问题出在内核代码还是启动方式，并给出不修改仓库文件的最小验证与解决方案。

**操作要求**：先给出验证命令与预期输出，再给结论；不修改 code/tools/ 与 Makefile；
如需绕行，只允许在仓库外增加一层启动包装。

**输出要求**：按"现象 → 最小验证 → 根因 → 处理 → 验证结果"组织，命令可直接复制。

[RELY]
// 启动链：OpenSBI(fw_jump) -> 0x80200000 -> kern_init()
// 内核通过 a1 获取 DTB 地址，dtb_init() 依赖它；没有 DTB 时会在
// "(THU.CST) os is loading ..." 之前挂死
// 参考现象（正常启动）：
//   Domain0 Next Arg1 : 0x0000000087000000
//   DTB Address: 0x87000000
//   128 MB @ 0x80000000
// grade.sh 使用：qemu-system-riscv64 -machine virt -nographic -bios default
//                -device loader,file=bin/ucore.img,addr=0x80200000 -serial file:.qemu.out

[GUARANTEE]
- 不修改 tools/grade.sh、tools/inject.py、Makefile；
- 不要求重装 QEMU 或升级工具链；
- 允许在 /usr/local/bin 放置包装脚本，参数原样透传。

[SPECIFICATION]
1. 对比两种装载方式的差异：`-device loader` 只写内存，不设置 `a1`；
   `-kernel` 会把镜像与 DTB 一起交给固件，`a1` 指向 DTB。
2. 最小验证：分别用 `-device loader` 与 `-kernel` 启动同一镜像，比较 `DTB Address` 与
   首个内核输出是否出现。
3. 处理：包装 qemu-system-riscv64，把 `-device loader,file=X,addr=0x80200000`
   改写为 `-kernel X`，其余参数透传；不改仓库任何文件。
4. 验收：`DTB Address: 0x87000000` 正常打印，`make grade` 输出 `Total Score: 100/100`。
```

#### 迭代记录

**v1：先怀疑内核代码。** 重新 `make clean && make`、逐个用例检查 `trap.c` 与 `trapentry.S`，
结果编译无警告，用例仍全 FAIL，`.qemu.out` 依旧 0 字节。**结论：不是代码问题**——
如果内核已启动，至少应出现 `(THU.CST) os is loading ...`。

**v2：对比启动方式。** 用同一份 `bin/ucore.img`：

```bash
qemu-system-riscv64 -machine virt -nographic -bios default \
  -device loader,file=bin/ucore.img,addr=0x80200000     # 只有 OpenSBI 横幅
qemu-system-riscv64 -machine virt -nographic -bios default \
  -kernel bin/ucore.img                                  # DTB Init -> os is loading
```

第二条命令打印 `DTB Address: 0x87000000`、`DTB init completed`，说明镜像本身正常，
差异只在 DTB 是否被注入。根因：QEMU 6.2 的 `-device loader` 不设置 `a1`。

**v3：加包装脚本后复测。** 包装脚本把 loader 参数改写为 `-kernel`，`make grade` 得到
`Total Score: 100/100`；仓库 `git status` 无代码改动。

### P3-2：单独加 `-DGRADE_LAB3_*` 编译，测试没有触发

#### Spec 摘要

| 项目 | 内容 |
|---|---|
| 现象 | `make clean && make DEFS+=-DGRADE_LAB3_CONTEXT` 后启动，输出与普通 `make qemu` 相同 |
| 目标 | 说明测试是如何注入的，给出正确的手工执行步骤 |
| 约束 | 不在仓库内留下注入产生的文件；不修改 `init.c`、`Makefile` |
| 验收 | 能复现 `[Test 1] ... PASS (35/35)`，且仓库工作区保持干净 |

#### 最终提示词

```text
[PROMPT]
**任务**：解释 ucore Lab3 中 `-DGRADE_LAB3_CONTEXT` 为什么单独编译不触发 Test 1，
并给出不污染仓库的手工执行步骤。

[RELY]
// Makefile 的 grade 目标是：复制 code/ 到 /tmp/lab3_grade -> python3 tools/inject.py /tmp/lab3_grade
//   -> sh tools/grade.sh
// inject.py 做三件事：
//   1) 把 tools/test_lab3.c/.h/test_lab3_context.S 复制到 kern/test/
//   2) 给 Makefile 的 KINCLUDE/KSRCDIR 追加 kern/test
//   3) 给 init.c 加 #include <test_lab3.h>，并在 idt_init()/clock_init()/intr_enable()
//      之后插入 test_lab3_context() 等调用
// 因此只加 -D 宏，没有任何测试代码被编译进去。

[GUARANTEE]
- 不在 code/ 内直接运行 inject.py（会改动 init.c 与 Makefile）；
- 只在 /tmp 副本中注入。

[SPECIFICATION]
1. 在仓库根目录复制工作副本：`cp -r code /tmp/lab3_shot && cd /tmp/lab3_shot`；
2. 执行 `python3 tools/inject.py .`；
3. `make clean && make DEFS+=-DGRADE_LAB3_CONTEXT`，再限时运行 QEMU；
4. 验收：输出包含 `Breakpoint at 0x...` 与 `[Test 1] ... PASS (35/35)`；
   原仓库 `git status` 不受影响。
```

#### 迭代记录

**v1：直接加宏编译。** 输出与普通运行完全一致，没有 `[Test 1]` 行。检查 `Makefile`
发现 `grade:` 目标里才有 `inject.py`，`DEFS` 只负责宏定义。

**v2：在原目录跑注入。** 出现了 `kern/test/` 与 `init.c` 的改动，违反"不改 init.c"
的约束。修正为在 `/tmp` 副本中注入，仓库只保留原始文件。

**v3：按副本流程执行。** 得到真实输出：

```text
Breakpoint at 0xffffffffc0201a28
[Test 1] Trap entry and breakpoint return: PASS (35/35)
```

### P3-3：截图与报告数据一致性

#### Spec 摘要

| 项目 | 内容 |
|---|---|
| 目标 | 保证报告中所有地址/计数/分值来自同一台机器、同一套工具链的最终一轮输出；注入测试会改变内核大小，地址以对应场景为准 |
| 约束 | 不凭记忆填写；截图与日志必须可追溯到具体命令 |
| 验收 | 报告中的 `0xffffffffc0201a28`、`0xffffffffc02019d8`、`ticks=200`、`100/100` 与截图逐字一致 |

#### 最终提示词

```text
[PROMPT]
**任务**：把 Lab3 的构建/运行/测试输出整理成报告用截图与数据表，要求同一台机器/同一套工具链、按场景分别构建、可追溯。

[RELY]
// 构建：make clean && make
// 运行：make qemu（SHUTDOWN_AFTER=10，即 10 行 "100 ticks" 后关机）
// 测试：make grade（35 + 25 + 40 = 100）
// 可用证据：.qemu.out、grade 日志、注入副本的串口输出

[GUARANTEE]
- 不修改 tools/ 与 Makefile；
- 报告只引用实际存在的图片文件（./images/01_build.png … 07_make_grade.png）。

[SPECIFICATION]
1. 每个场景单独保存原始文本输出，记录命令与时间；
2. 报告中的每个数字都能在对应输出中检索到；
3. 图片命名固定为 01–07，正文引用与文件名一一对应；
4. 交叉核对：Test 1 地址、Test 2 的 cause 0x0f/0x14（截图未覆盖的部分以日志补全）、Test 3 的 ticks 与总分。
```

#### 迭代记录

**v1：先写报告后补截图。** 发现 A 草稿里的 Test 1 地址是 `0xffffffffc02019a8`，
与最终构建的 `0xffffffffc0201a28` 不一致。原因是草稿来自更早的一次编译。

**v2：统一以最终环境为准。** 以同一台机器、同一套工具链的最终一轮输出为准，按场景分别构建并生成 7 张截图；把合并报告中的地址统一为 `0xffffffffc0201a28`，正文注明“地址随构建/工具链变化”。Test 2 的 `cause 0x14` 若截图未覆盖，以测试日志补全并在图注说明。

---

---

## P-lab2-page_init：Lab2 `page_init()` 回填

### Spec 摘要

| 项目 | 内容 |
|---|---|
| 输入 | DTB 提供的 `mem_base`/`mem_size`；链接符号 `end`；`KERNTOP`、`PHYSICAL_MEMORY_OFFSET` |
| 输出 | `pages` 指向 Page 描述符数组；`npage` 为可管理页号上界；`[nbase, npage)` 全部 reserved；完整空闲页交给 `init_memmap()` |
| 关键不变量 | 描述符数组紧接 `end` 且页对齐；OpenSBI/内核/描述符数组不下发；空闲区两端页对齐；`init_memmap` 收到 reserved 页 |
| 约束 | 只改 `pmm.c` 的 `//lab2` 注释处；不动 `alloc_pages`/`free_pages`/`nr_free_pages` 的中断保护封装 |
| 验收 | `check_alloc_page() succeeded!`；`make grade` 100/100 |

### 最终提示词

```text
[PROMPT]
把 Lab2 的 page_init() 回填到 Lab3 骨架 code/kern/mm/pmm.c 的
"//lab2 请补充你在lab2中的代码" 处，保证内核能正确建立 Page 描述符并启动。

[RELY]
1. dtb_init() 已解析设备树，get_memory_base()/get_memory_size() 返回物理内存范围。
2. memlayout.h 只有 KERNTOP 和 PHYSICAL_MEMORY_OFFSET，没有 PHYSICAL_MEMORY_END；
   KERNTOP - PHYSICAL_MEMORY_OFFSET = 0x88000000 是物理内存上界。
3. pmm.h 提供 pages、npage、nbase、va_pa_offset、PADDR、pa2page、page2pa。
4. struct Page 的描述符数组要放在 end 之后并按页对齐；
   只有描述符数组之后的完整页才能交给 init_memmap()。

[GUARANTEE]
- 只改 pmm.c 的 lab2 注释处，不修改 alloc_pages/free_pages/nr_free_pages 的中断保护，
  不修改 pmm_init/check_alloc_page；
- 保留 lab2 注释，补上 2413541。

[SPECIFICATION]
1. 取 DTB 的 mem_base/mem_size；mem_end 用 KERNTOP - PHYSICAL_MEMORY_OFFSET 截断；
2. va_pa_offset = PHYSICAL_MEMORY_OFFSET；npage = PPN(mem_end)；
3. pages = ROUNDUP(end, PGSIZE)，描述符数组覆盖 [nbase, npage)；
4. 先把 [nbase, npage) 全部标 reserved；
5. free_start = ROUNDUP(PADDR(pages_end), PGSIZE)，
   free_end = ROUNDDOWN(mem_end, PGSIZE)；
6. 若 free_start < free_end，调用 init_memmap(pa2page(free_start), 页数)；
7. 非法 DTB/空范围/无完整空闲页要有 panic 或提示。
```

### 提示词迭代记录

**v1：直接复用 Lab2 实现。**

Lab2 的 `page_init()` 用了 `PHYSICAL_MEMORY_END` 作为上界。Lab3 的 `memlayout.h` 没有这个宏，
编译报：

```text
error: 'PHYSICAL_MEMORY_END' undeclared (first use in this function)
```

修正：改为 `KERNTOP - PHYSICAL_MEMORY_OFFSET`，并在 SPECIFICATION 中写明“骨架没有 PHYSICAL_MEMORY_END”。

**v2：补上边界检查。**

第一版没有处理 `mem_end <= mem_base`、`npage <= nbase`、`free_start >= free_end` 的情况。
修正：加 `panic`/提示分支，避免描述符数组越界或空空闲区调用 `init_memmap()`。

### 验证

```text
memory management: best_fit_pmm_manager
physical memory map:
  memory: 0x0000000008000000, [0x0000000080000000, 0x0000000087ffffff].
check_alloc_page() succeeded!
```

`make grade` 100/100；Test 1 为 35/35，Test 2 为 25/25，Test 3 为 40/40。

---

## P-lab2-bestfit：Lab2 Best-Fit 回填

### 背景与回填提示词

Lab3 依赖 Lab2 的物理内存管理，需要把 Best-Fit 回填到 Lab3 骨架。本次按分工要求
**直接复用 `lab2` 分支上已经验证过的实现**（`git show lab2:code/kern/mm/best_fit_pmm.c`），
而不是重新生成。为保证可复现，下面保留 Lab2 阶段的最终提示词：

> 说明：以下为 Lab2 原文；Lab3 骨架的标记是 `//lab2 请补充你在lab2中的代码`，且
> `best_fit_pmm_manager` 在 Lab3 中需要新增（不是修改），具体见 4.3.2 与下文"适配点"。

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

---

## 汇总说明

### 提示词索引

| 编号 | 主题 | 负责人 | 对应实现 |
|---|---|---|---|
| P0 | 四段式模板与使用原则 | 丙 | 全部模块 |
| P1 | 功能模块一：`RESTORE_ALL`/`__alltraps` | 甲 | `kern/trap/trapentry.S` |
| P2 | 功能模块二：中断与异常处理 | 乙 | `kern/trap/trap.c` |
| P3 | 环境排障与验证（P3-1/2/3） | 丙 | 构建、注入、截图 |
| P-lab2-page_init | Lab2 `page_init()` 回填 | 甲 | `kern/mm/pmm.c` |
| P-lab2-bestfit | Lab2 Best-Fit 回填 | 乙 | `kern/mm/best_fit_pmm.c` |

### 覆盖情况

| 模块 | Spec 摘要 | 最终提示词 | 迭代记录 |
|---|---|---|---|
| 模块一 | 有（输入/输出/不变量/约束/验收） | 有（四段式，含 36 项偏移） | 有（`sp` 提前恢复、`a0` 未赋值、`sbadaddr`） |
| 模块二 | 有（分支级 Pre/Post） | 有（四段式，含 6 个坑） | 有（设计期规避清单 + 模块二一次通过说明） |
| `page_init` | 有 | 有 | 有（`PHYSICAL_MEMORY_END`、边界检查） |
| Best-Fit | 有 | 有 | 有（回填适配点） |
| 环境/验证 | 有（P3-1/2/3） | 有 | 有（loader/FDT、注入、数据一致性） |

### 结论

1. **提示词的有效性来自"RELY 真实 + GUARANTEE 硬 + SPECIFICATION 有序"**：
   三份实现提示词都没有出现"AI 虚构接口"或"输出格式不符"的问题。
2. **运行期问题主要来自环境**：`-device loader` 不注入 FDT 是本轮最严重的问题，靠"串口是否有输出"
   这一最小证据快速定位；代码侧的迭代问题（`sp`、`a0`、`sbadaddr`）见 P1。
3. **报告数据必须与最终环境绑定**：截图、地址、分值来自同一台机器/同一套工具链的最终一轮输出；
   各测试场景分别构建，地址以对应场景为准，并在合并阶段统一核对，避免甲/乙草稿与丙截图之间出现版本差异。
