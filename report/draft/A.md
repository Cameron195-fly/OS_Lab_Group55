# Lab3 甲部分报告内容（草稿）

> 甲（成员 A，2413541-赖鸿毅）负责的报告小节：3.1、3.2、3.3、4.1、4.3.1。
> 待与乙（`B.md`）、丙（`C.md`）合并为 `report/report.md`。

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本章围绕 **“让内核从能产生 Trap，变成能保存上下文、进入 C 处理函数、完成中断/异常处理、再恢复上下文返回原执行流”** 展开，可以拆成三条主线：

```text
Trap 上下文保存与恢复  →  Trap 入口与返回  →  中断与异常处理
（SAVE_ALL / RESTORE_ALL）  （__alltraps / __trapret）  （interrupt_handler / exception_handler）
```

实验 2 结束时，内核已经运行在 Sv39 虚拟地址空间，也有物理页管理，但还没有“进入内核处理异步/同步事件并返回”的能力。实验 3 补的就是这条链：

1. **硬件侧**：Trap 发生时，CPU 把 `sepc`、`scause`、`stval` 写好，`sstatus.SPP/SPIE` 记录现场，`pc <- stvec`；
2. **汇编侧**：`__alltraps` 用 `SAVE_ALL` 把 32 个通用寄存器和 4 个 CSR 组织成 `struct trapframe`，再按 RISC-V 调用约定把 `tf` 传给 `trap()`；
3. **C 侧**：`trap_dispatch()` 按 `scause` 最高位分流，`interrupt_handler()` / `exception_handler()` 完成具体处理；
4. **返回侧**：`trap()` 返回后进入 `__trapret`，`RESTORE_ALL` 从 Trap frame 恢复寄存器和 CSR，`sret` 用 `sepc`/`sstatus` 回到原执行流。

这条链的“粘合剂”是一致性：**Trap frame 布局、`SAVE_ALL` 的保存顺序、`RESTORE_ALL` 的恢复顺序、`trap()` 的参数、`sret` 的返回状态**，任何一处假设不同，系统就会在“看起来能跑”和“偶发跑飞”之间摇摆。因此本实验真正的难点不是写几条指令，而是把这些约束固定下来。

本实验的 Trap 全部来自 **S 模式**（内核态）：时钟中断、`ebreak`、非法指令都发生在 `kern_init()` 及其调用链里，没有用户态进程，因此 `SAVE_ALL` 保存的 `sstatus.SPP = 1`，`sret` 返回 S 模式。用户态切换要到后续实验才涉及。

### 3.2 功能的逐步实现

1. **先回填 Lab 2 代码（`page_init()`、Best-Fit）**：Lab 3 骨架里 `pmm.c` 和 `best_fit_pmm.c` 留了 `lab2` 注释；没有 `page_init()`、没有 Best-Fit，内核连编译/启动都过不了，所以这是所有工作的前置。
2. **再做功能模块一（`RESTORE_ALL`/`__alltraps`）**：它是所有 Trap 的公共出口。没有恢复逻辑，任何处理函数返回后都无法回到原执行流，后面的测试也无从观察；`__alltraps` 同时负责把 Trap frame 按调用约定送进 C。
3. **再做功能模块二（两个 handler）**：有了入口/出口，才能安全地在 C 里分类处理。先处理最简单的软件中断/未知中断，再处理时钟中断（re-arm + 计数 + 输出 + 关机），最后处理异常（非法指令/断点 + `epc` 推进，其余兜底）。
4. **最后用测试闭环验证**：Test 1 验证入口、恢复、断点 `epc`；Test 2 验证非法指令、兜底和未知中断；Test 3 验证时钟持续触发和反复 Trap 返回。三个测试正好覆盖三条主线。

### 3.3 关键数据结构与不变量

`struct trapframe` 在 RV64 下是 36 个 8 字节字段，`SAVE_ALL` 的偏移必须与它逐项对应：

| 偏移 | 字段 | 偏移 | 字段 | 偏移 | 字段 |
|---|---|---|---|---|---|
| 0 | zero | 12 | a2 | 24 | s8 |
| 1 | ra | 13 | a3 | 25 | s9 |
| 2 | **sp（原 sp）** | 14 | a4 | 26 | s10 |
| 3 | gp | 15 | a5 | 27 | s11 |
| 4 | tp | 16 | a6 | 28 | t3 |
| 5 | t0 | 17 | a7 | 29 | t4 |
| 6 | t1 | 18 | s2 | 30 | t5 |
| 7 | t2 | 19 | s3 | 31 | t6 |
| 8 | s0 | 20 | s4 | 32 | status |
| 9 | s1 | 21 | s5 | 33 | epc |
| 10 | a0 | 22 | s6 | 34 | badvaddr |
| 11 | a1 | 23 | s7 | 35 | cause |

四条不变量（写 prompt 时逐条列出）：

1. **布局不变**：`SAVE_ALL`、`RESTORE_ALL`、`struct trapframe` 使用同一张偏移表，不能另起炉灶。
2. **`sp` 特殊**：`SAVE_ALL` 在 `2*REGBYTES(sp)` 存的是“减 `36*REGBYTES` 之前”的原 `sp`；`RESTORE_ALL` 必须最后用 `addi sp, sp, 36*REGBYTES` 恢复，不能在其它 `LOAD` 之前覆盖基址。
3. **入口参数**：`trap(struct trapframe *tf)` 的第一个参数在 `a0`，且 `a0 = sp`（`SAVE_ALL` 之后 `sp` 就是 Trap frame 首地址）。
4. **返回状态**：`sret` 的返回地址来自 `sepc`，特权级/中断使能来自 `sstatus.SPP/SPIE`；本实验 `SPP=1`，返回 S 模式。

> **指导书文字的坑**：`lab3_3_2` 里“将 `sstatus.SPP` 设为 0、返回 U 模式”是通用/用户态描述，不适用于本实验。`RESTORE_ALL` 只要把保存的 `sstatus` 原样写回；手动清 `SPP` 会让 `sret` 跳到 U 模式执行内核地址，直接崩溃。本实验也没有系统调用，不需要 `sepc + 4`。

---

## 四、实验内容与实现

### 4.1 功能模块一：Trap 上下文恢复与 Trap 入口

**负责人：** 2413541-赖鸿毅

#### 4.1.1 模块功能描述

**需要实现的汇编位置（`kern/trap/trapentry.S`）：**

```asm
    .macro RESTORE_ALL
        # 从当前 Trap frame 恢复处理器状态
    .endm

__alltraps:
    SAVE_ALL
    # 把 Trap frame 按调用约定传给 trap()
```

- `RESTORE_ALL`：输入是当前 `sp` 指向的 Trap frame，输出是恢复后的处理器执行状态；
- `__alltraps`：`SAVE_ALL` 之后把 `sp`（= tf）放进 `a0`，`jal trap`；`trap()` 返回后顺序落入 `__trapret`；
- 不修改 `SAVE_ALL`、`struct trapframe`、`trap()` 签名、`__trapret` 的位置。

#### 4.1.2 实现

```asm
    .macro RESTORE_ALL

    #lab3 function1 2413541
    LOAD s1, 32*REGBYTES(sp)      # sstatus
    LOAD s2, 33*REGBYTES(sp)      # sepc
    csrw sstatus, s1
    csrw sepc, s2

    LOAD x1, 1*REGBYTES(sp)
    LOAD x3, 3*REGBYTES(sp)
    ...
    LOAD x31, 31*REGBYTES(sp)     # x0 不恢复，x2 最后处理

    addi sp, sp, 36*REGBYTES      # 把 SAVE_ALL 减掉的空间加回来
    .endm

    .globl __alltraps
    .align(2)
__alltraps:
    SAVE_ALL

    #lab3 function1 2413541
    addi a0, sp, 0                # 第一个参数：Trap frame 首地址
    jal trap
    # sp should be the same as before "jal trap"
    # 返回后 sp 仍是 tf 首地址，顺序落入 __trapret
```

> 工具链兼容：新 binutils 移除了旧 CSR 别名 `sbadaddr`。`SAVE_ALL` 宏体没有改，只在文件顶部加了
> `#ifndef sbadaddr / #define sbadaddr stval / #endif`（`stval` 的 CSR 编号同为 `0x143`）。

#### 4.1.3 核心理解

1. **为什么先恢复 `sstatus`/`sepc`**：`sret` 的返回地址来自 `sepc`，特权级/中断使能来自 `sstatus.SPP/SPIE`；两者必须在 `sret` 前写好。它们不依赖通用寄存器，所以先做最安全。恢复 GPR 时用 `s1/s2` 作临时寄存器，最后 `LOAD x9/x18` 会把它们从 Trap frame 原样恢复，不会污染现场。
2. **`x2(sp)` 为什么特殊**：`SAVE_ALL` 先把原 `sp` 写进 `sscratch`，再 `addi sp, sp, -36*REGBYTES` 开帧；最后用 `csrrw s0, sscratch, x0` 把原 `sp` 取回并存到第 2 项。因此第 2 项存的是“开栈之前”的 `sp`，等于恢复后的 `sp + 36*REGBYTES`。`RESTORE_ALL` 用 `addi sp, sp, 36*REGBYTES` 与它等价，而且不必在恢复过程中提前改基址；如果像其它寄存器一样先 `LOAD sp, 2*REGBYTES(sp)`，后面的 `LOAD` 基址全错，Test 1 会失败。
3. **`sscratch` 的语义**：`SAVE_ALL` 末尾把 `sscratch` 清 0，沿用 `idt_init()` 的约定——内核态运行期间 `sscratch` 为 0，若内核里再次发生 Trap，入口可据此判断不是从用户态进来的。本实验全程内核态，`RESTORE_ALL` 不恢复 `sscratch`，返回后仍为 0，与约定一致；用户态切换是后续实验的内容。
4. **`a0` 与调用约定**：`SAVE_ALL` 之后 `sp` 正好是 Trap frame 首地址，用 `addi a0, sp, 0` 放进第一个参数。`jal trap` 会写 `ra`，但 `ra` 已在第 1 项保存过，`RESTORE_ALL` 会恢复；`trap()` 作为被调用者遵守调用约定，返回后 `sp` 不变，可以直接落入 `__trapret`。
5. **`sret` 的返回语义**：`sret` 做三件事——`pc <- sepc`；特权级 <- `sstatus.SPP`；`sstatus.SIE <- sstatus.SPIE`。所以 `RESTORE_ALL` 必须把 `sstatus` 原样写回，不能只恢复 `sepc`。本实验 `SPP=1`，返回 S 模式；不要按指导书的 U 模式文字去清 `SPP`。
6. **对齐**：`__alltraps` 必须 4 字节对齐（`stvec` 的低两位是模式位），原骨架的 `.align(2)` 不动。汇编器会生成一个 `R_RISCV_ALIGN` 重定位，链接时由 relaxation 调整到 4 字节边界。
7. **工具链兼容**：`SAVE_ALL` 里的 `csrr s3, sbadaddr` 在新 binutils 上会报 `unknown CSR 'sbadaddr'`；用 `#define sbadaddr stval` 把旧别名映射到标准名，CSR 编号都是 `0x143`，不改变 `SAVE_ALL` 的宏体逻辑。

#### 4.1.4 spec 摘要

- **Pre**：Trap 已进入 `__alltraps`，`sp` 指向由 `SAVE_ALL` 建好的 36 项 Trap frame；`RA` 等寄存器已保存。
- **Post**：`sstatus`、`sepc` 和 `x1`、`x3`–`x31` 已恢复；`sp` 回到 Trap 前；`a0` 在调用 `trap()` 前等于 tf 首地址；`trap()` 返回后顺序进入 `__trapret`。
- **不变量**：偏移与 `struct trapframe` 一致；`x0` 不恢复；`x2` 最后恢复；不改 `SAVE_ALL`/`trap()` 签名。
- **验收**：Test 1 中 `ebreak` 返回后，被检查的通用寄存器全部原值；`epc` 前移由模块二负责。

#### 4.1.5 prompt 摘要与迭代

完整 prompt 见 `report/prompt.md` 的 P1，这里只摘录关键点：

- RELY 里列出 `SAVE_ALL` 的 36 项偏移、`struct trapframe` 的字段顺序、`trap()` 的参数约定；
- GUARANTEE 明确不改 `SAVE_ALL`、`struct trapframe`、`trap()`、`__trapret`；
- SPECIFICATION 单列“`sp` 最后恢复”“`a0 = sp`”“`jal trap` 后 `sp` 不变”。

迭代中实际出现并修掉的问题：

1. **第一版把 `sp` 当普通寄存器恢复**：`LOAD sp, 2*REGBYTES(sp)` 之后，后续 `LOAD` 全部错位，Test 1 失败。改为最后 `addi sp, sp, 36*REGBYTES`。
2. **第二版 `__alltraps` 忘记设 `a0`**：`trap()` 收到野指针。补 `addi a0, sp, 0`。
3. **新 binutils 不识别 `sbadaddr`**：汇编报 `unknown CSR 'sbadaddr'`。在文件顶部加 `#define sbadaddr stval`，保持 `SAVE_ALL` 宏体不变。

#### 4.1.6 Test 1 结果

```text
Breakpoint at 0xffffffffc02019a8
[Test 1] Trap entry and breakpoint return: PASS (35/35)
```

说明：`ebreak` 被编成 2 字节 `c.ebreak`，模块二按低 2 位判断长度后 `epc += 2`；返回后寄存器全部原值。该地址随构建/工具链变化，最终报告以丙（C）截图的同一构建为准。

---

### 4.3.1 `page_init()`（Lab 2 回填）

#### 物理内存布局

```text
[0x80000000, 0x80200000)   OpenSBI，保留
[0x80200000, end)          内核镜像（代码/数据/bss），保留
[end, pages)               补齐到页边界的空隙，保留
[pages, pages_end)         Page 描述符数组，保留
[free_start, free_end)     交给 pmm_manager 的完整空闲页
```

#### 实现步骤

1. `dtb_init()` 已解析设备树，`get_memory_base()`/`get_memory_size()` 给出物理内存范围；
2. 上界截断：Lab 3 骨架没有 `PHYSICAL_MEMORY_END`，用 `(uint64_t)KERNTOP - PHYSICAL_MEMORY_OFFSET`（0x88000000）；
3. 设 `va_pa_offset = PHYSICAL_MEMORY_OFFSET`，`npage = PPN(mem_end)`；
4. `pages = ROUNDUP(end, PGSIZE)`，描述符数组覆盖 `[nbase, npage)`；
5. 先把 `[nbase, npage)` 全部标 `PageReserved`；
6. 计算描述符数组之后的完整空闲页 `[free_start_pa, free_end_pa)`，调用 `init_memmap(pa2page(free_start_pa), n)`。

#### 核心理解

- **三套地址不能混用**：`pages`/`end` 是虚拟地址；算物理布局要先用 `PADDR()` 转成物理地址；`pa2page()` 再用 `nbase` 转成描述符下标。
- **为什么先全标 reserved**：描述符数组还没建好之前，不能让任何页面被误分配；`init_memmap()` 内部会 `assert(PageReserved(p))` 再清标志，所以“先 reserved、后交给管理器”正好满足它的前置条件。
- **为什么不把 OpenSBI/内核/描述符数组下发**：它们已被占用，必须从空闲区排除；`free_start_pa` 取描述符数组末尾向上对齐，`free_end_pa` 取内存末尾向下对齐，不足一页的尾巴不下发。

#### 遇到的 bug

Lab 2 的实现用了 `PHYSICAL_MEMORY_END`，但 Lab 3 的 `memlayout.h` 只有 `KERNTOP`、`PHYSICAL_MEMORY_OFFSET`，直接编译报：

```text
error: 'PHYSICAL_MEMORY_END' undeclared
```

改为：

```c
uint64_t manageable_end = (uint64_t)KERNTOP - PHYSICAL_MEMORY_OFFSET;
if (mem_end > manageable_end) mem_end = manageable_end;
```

#### 验证

```text
memory management: best_fit_pmm_manager
physical memory map:
  memory: 0x0000000008000000, [0x0000000080000000, 0x0000000087ffffff].
check_alloc_page() succeeded!
```

`check_alloc_page()` 通过说明描述符布局、保留区和空闲区划分正确；随后 `make grade` 为 100/100。
