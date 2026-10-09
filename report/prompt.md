# Prompt 记录

本文件汇总 lab1 实验过程中向 AI 提出的关键提示词。每条按 \[PROMPT] / \[RELY] / \[GUARANTEE] / \[SPECIFICATION] 四段记录：我们问了什么、当时提供了哪些实测事实、对回答施加了什么约束、期望得到什么样的输出。

\---

## Prompt 1：kern\_entry 两条指令的作用与"为什么必须先设栈"

\[PROMPT]
阅读以下 kern/init/entry.S 片段，解释 kern\_entry 两条指令各自完成什么、目的是什么，并说明为什么必须先设置栈才能进入 C 代码。

\[RELY]
kern\_entry:
la sp, bootstacktop
tail kern\_init

.section .data
.align PGSHIFT
.global bootstack
bootstack:
.space KSTACKSIZE
.global bootstacktop
bootstacktop:

* kern/mm/memlayout.h：KSTACKPAGE = 2，KSTACKSIZE = KSTACKPAGE \* PGSIZE
* kern/mm/mmu.h：PGSIZE = 4096，PGSHIFT = 12
* kern\_init 声明为 **attribute**((noreturn))，函数体末尾是 while (1);
* 调试中观察到的 OpenSBI 跳转前的 sp = 0x8003def0

\[GUARANTEE]

* 不要泛泛而谈"设置栈是为了保存变量"，要具体说明在此上下文里为什么必须换栈
* 不要凭空推测 OpenSBI 的行为，所有结论要与源码和调试观察一致

\[SPECIFICATION]

* 逐条解释 la sp, bootstacktop 与 tail kern\_init
* 明确给出 KSTACKSIZE = 2 × 4096 = 8192 字节
* 说明栈从高地址向低地址增长，sp 初始应指向 bootstacktop
* 说明 .align PGSHIFT 的作用

\---

## Prompt 2：la / tail 伪指令展开后的真实机器指令

\[PROMPT]
entry.S 中写的是 la sp, bootstacktop 和 tail kern\_init，但 GDB 反汇编显示的是 auipc sp, 0x3 / mv sp, sp / j 0x8020000a <kern\_init>。请解释这两条伪指令分别被展开成了什么，以及为什么。

\[RELY]

* GDB x/4i 0x80200000 实测输出：

=> 0x80200000 <kern\_entry>:      auipc  sp, 0x3
0x80200004 <kern\_entry+4>:    mv     sp, sp
0x80200008 <kern\_entry+8>:    j      0x8020000a <kern\_init>
0x8020000a <kern\_init>:       auipc  a0, 0x3

* 编译参数：-mcmodel=medany
* 符号表：bootstack = 0x80201000，bootstacktop = 0x80203000

\[GUARANTEE]

* 必须逐条对应源码与反汇编，不能只说"la 是伪指令"这种笼统结论
* 不得把 mv sp, sp 解释成"无意义指令"，要说明它在 PC 相对寻址中的作用

\[SPECIFICATION]

* 给出 la 展开为 auipc + addi（此处 addi 偏移为 0，被显示为 mv）的对应关系
* 给出 tail 展开为无条件跳转 j（不是 jal）的对应关系
* 说明 tail 不写 ra、不压栈，是因为 kern\_init 永不返回

\---

## Prompt 3：链接脚本如何决定内核入口地址

\[PROMPT]
解释 tools/kernel.ld 里的 ENTRY(kern\_entry) 和 . = 0x80200000 两行如何决定内核的入口符号与链接起始地址。

\[RELY]

* 链接脚本片段（BASE\_ADDRESS = 0x80200000，ENTRY(kern\_entry)）
* readelf -h bin/kernel 显示 Entry point address: 0x80200000
* nm bin/kernel 显示 kern\_entry = 0x80200000
* QEMU 启动参数把内核加载到 0x80200000

\[GUARANTEE]

* 不把链接脚本当作"配置文件"泛泛描述，要说明它是链接器决定段布局的依据
* 结论要能解释为什么 kern\_entry 恰好位于 0x80200000

\[SPECIFICATION]

* 说明 ENTRY(kern\_entry) 指定入口符号
* 说明 . = 0x80200000 把定位计数器设到该地址，.text 段从此开始
* 说明链接脚本地址、QEMU 装载地址、OpenSBI 跳转目标三者必须一致

\---

## Prompt 4：用 GDB 从 0x1000 跟踪到 0x80200000

\[PROMPT]
给出用 GDB + QEMU 跟踪 RISC-V 从加电到内核第一条指令的完整操作流程：如何启动 QEMU 并冻结 CPU、如何在 GDB 中连接、如何观察复位向量、如何在内核入口下断点并验证控制权移交。

\[RELY]

* Makefile 中已有 debug 目标（QEMU 带 -s -S）和 gdb 目标（启动 riscv64-unknown-elf-gdb）
* 内核入口约定为 0x80200000
* 复位地址为 0x1000

\[GUARANTEE]

* 不修改内核源码
* 所有命令必须能在当前环境下复现
* GDB 侧不得省略 file bin/kernel（符号表）与 target remote :1234

\[SPECIFICATION]

* 给出两个终端的完整命令：make debug 与 make gdb
* 给出关键 GDB 命令：target remote :1234、p/x $pc、x/8i 0x1000、b \*0x80200000、c、x/4i $pc、si、info registers pc sp ra
* 成功判据：断点命中时 GDB 显示停在 kern\_entry（entry.S:7）

\---

## Prompt 5：单步换栈是否符合预期

\[PROMPT]
在 0x80200000 断点命中后单步执行，sp 从 0x8003def0 变为 0x80203000，请核对这是否符合 entry.S 的设计。

\[RELY]

* 断点命中时 info registers pc sp ra：

pc    0x80200000   <kern\_entry>
sp    0x8003def0
ra    0x8000966a

* 单步两次后：

pc    0x80200008   <kern\_entry+8>
sp    0x80203000

* 符号表：bootstack = 0x80201000，bootstacktop = 0x80203000

\[GUARANTEE]

* 不要只看 sp 是否变化，要核对它是否等于 bootstacktop
* 不得推测，必须与符号表和 entry.S 对应

\[SPECIFICATION]

* 确认 sp = 0x80203000 = bootstacktop，栈顶正确
* 说明此时栈为空（sp 指向栈顶，向低地址增长）
* 说明 ra = 0x8000966a 是 OpenSBI 留下的值，tail 不覆盖它

\---



