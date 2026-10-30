# Lab3 甲部分提示词记录（草稿）

> 甲（成员 A，2413541-赖鸿毅）负责的提示词：
> P1 功能模块一（`RESTORE_ALL`/`__alltraps`）、P-lab2-page_init（Lab2 `page_init()` 回填）。
> 最终合并进 `report/prompt.md`。

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
Breakpoint at 0xffffffffc02019a8
[Test 1] Trap entry and breakpoint return: PASS (35/35)
```

`ebreak` 是 2 字节 `c.ebreak`，模块二按指令长度推进 `epc` 后 `sret` 正常返回；该地址随构建/工具链变化。

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
