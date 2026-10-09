# 操作系统实验报告

## 实验基本信息

|项目|内容|
|-|-|
|实验名称|Lab 1: 比麻雀更小的麻雀（最小可执行内核）|
|小组成员|2413541-赖鸿毅、2412386-唐港、2412440-王朝阳|
|完成日期|2026-10-07|

### 小组分工

|成员|负责的练习/模块|
|-|-|
|2413541-赖鸿毅|练习1（entry.S 入口分析）、环境搭建与编译链接|
|2412386-唐港|练习2（GDB 跟踪启动流程）|
|2412440-王朝阳|运行验证、截图整理与报告撰写|

\---

## 一、实验目的

1. 搞清楚 QEMU 模拟的 RISC-V 从上电到执行内核第一条指令之间发生了什么。
2. 学会用链接脚本描述内存布局，理解内核入口为什么必须是 `0x80200000`。
3. 走通交叉编译的完整流程：源码 → ELF → `objcopy` → 裸二进制镜像。
4. 理解 RISC-V 的 M/S/U 特权级和 `ecall`，明白内核怎么借 OpenSBI 打印出第一个字符。
5. 学会用 GDB + QEMU 远程调试内核。

\---

## 二、实验环境

|项目|内容|
|-|-|
|宿主系统|Windows + WSL2（Ubuntu 22.04）|
|交叉编译器|`riscv64-unknown-elf-gcc` 15.1.0|
|调试器|`riscv64-unknown-elf-gdb` 16.3|
|模拟器|`qemu-system-riscv64` 7.0.0，内置 OpenSBI v1.0|

工具链装在 `\~/riscv-elf-toolchains`，QEMU 装在 `/opt/qemu/bin`，两者都通过 `\~/.bashrc` 加进 `PATH`。

各成员使用的 AI 工具：

|成员|AI 工具|底层模型|
|-|-|-|
|2413541-赖鸿毅|DeepSeek Harness|DeepSeek-V4.1-Flash|
|2412386-唐港|Claude Code|DeepSeek-v4-pro|
|2412440-王朝阳|DeepSeek|DeepSeek-v4-pro|

\---

## 三、实验整体逻辑分析

### 3.1 本章的逻辑主线

Lab1 要做的东西其实很小：一个能打印一行字符串、然后死循环的内核。之所以从这么小的地方开始，是因为 ucore 本体也有几千行，不砍到最小根本看不透；先把这个骨架跑通，后面 lab2\~lab9 再一点点把功能加回去。

虽然功能小，但它逼着我们回答三个绕不开的问题：

1. **CPU 上电后第一条指令在哪？** 不在我们的内核里，而在复位向量 `0x1000`。
2. **控制权怎么从固件交到内核？** OpenSBI 把镜像放到 `0x80200000`，然后跳过去。
3. **内核在一无所有的环境里怎么输出？** 用 `ecall` 请求 OpenSBI 帮忙打印一个字符，再往上层层封装成 `cprintf()`。

这三个问题分别对应本章的三块内容：链接与内存布局、启动流程、从 SBI 到 stdio。它们是一环扣一环的——内存布局决定了内核被放在哪、入口是谁；启动流程解释了为什么非得放在那个地址；而 `cprintf()` 能打出字，才说明内核真的拿到执行权了。

### 3.2 功能的逐步实现

1. **先写链接脚本 `tools/kernel.ld`。** 编译时用了 `-mcmodel=medany`，生成的是地址相关代码，指令里写死了绝对地址。所以必须先用 `ENTRY(kern\_entry)` 和 `BASE\_ADDRESS = 0x80200000` 把入口定死，否则 OpenSBI 不知道往哪跳。
2. **再写汇编入口 `kern/init/entry.S`。** C 代码跑起来之前必须有合法的栈，而 `kern\_entry` 就干两件事：设好 `sp`、跳到 `kern\_init`。这是从汇编进入 C 的唯一一跳。
3. **用 Makefile 把编译流程自动化。** 要手动敲编译、链接、`objcopy` 太容易出错。`make` 负责把源码编成 `.o`、链接成 ELF `bin/kernel`，再用 `objcopy --strip-all -O binary` 转成裸二进制 `bin/ucore.img`（OpenSBI 不认识复杂的 ELF）。
4. **用 QEMU 跑起来。** 这一步是在验证前面全做对了：如果 `0x80200000` 处不是 `kern\_entry`，或者栈没设对，立刻就会出问题。
5. **最后用 GDB 把过程看一遍。** 能跑起来不等于理解了。用 `-S -s` 把 CPU 冻住，从 `0x1000` 一直观察到 `0x80200000`，整条启动链才能看清楚。

\---

## 四、实验内容与实现

### 练习1：理解内核启动中的程序入口操作

**负责人：** 2413541-赖鸿毅

先看要分析的代码：

```asm
# kern/init/entry.S
    .section .text,"ax",%progbits
    .globl kern\_entry
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
```

#### 1\. `la sp, bootstacktop` 做了什么？为什么？

**它把 `bootstacktop` 这个符号的地址装进了栈指针 `sp`。**

`la` 是伪指令（load address），在 `-mcmodel=medany` 下展开成 `auipc` + `addi` 两条指令。我们在 GDB 里反汇编 `kern\_entry` 看到的就是这样：

```
0x80200000 <kern\_entry>:      auipc  sp,0x3      # sp = 0x80200000 + 0x3000 = 0x80203000
0x80200004 <kern\_entry+4>:    mv     sp,sp       # 即 addi sp,sp,0
```

用 `nm` 查符号表能对上：`bootstack = 0x80201000`，`bootstacktop = 0x80203000`，两者相差 `0x2000`，也就是 8192 字节，正好等于 `KSTACKSIZE = KSTACKPAGE(2) × PGSIZE(4096)`。

**为什么要这么做：**

* 在 `kern\_entry` 之前，CPU 一直跑在 OpenSBI 里，`sp` 指的是 OpenSBI 自己的栈。我们在 GDB 里看到进入 `kern\_entry` 时 `sp = 0x8003def0`，这个地址落在 `0x80000000` 段——那是固件的地盘，内核不能一直用。
* C 函数调用要靠栈来存返回地址、局部变量和保存的寄存器，所以**在跳进 C 代码之前，必须有一个属于自己的合法栈**。
* 栈是从高地址往低地址长的，因此 `sp` 要指向这块 8KB 空间的**高地址端**（也就是 `bootstacktop`），初始时栈为空。这样压栈时它会在 `\[0x80201000, 0x80203000)` 里生长，不会踩到别的数据。
* `.align PGSHIFT` 让栈按页对齐，方便 lab2 建页表时把这块区域整块管理。

#### 2\. `tail kern\_init` 做了什么？为什么？

**它跳转到 `kern\_init`。** `tail` 是尾调用伪指令，展开成的是一条无条件跳转 `j`，**不是 `call`**：

```
0x80200008 <kern\_entry+8>:    j  0x8020000a <kern\_init>
```

可以看到 `kern\_init` 紧跟在后面（`0x8020000a`），说明链接脚本用 `\*(.text.kern\_entry)` 把入口代码排在了 `.text` 最前面，跟 `ENTRY(kern\_entry)` 和 `0x80200000` 是对得上的。

**为什么用跳转而不是调用：**

* `j` 不碰 `ra`、也不压栈。因为 `kern\_init` 声明了 `\_\_attribute\_\_((noreturn))`，函数体最后是 `while (1) ;`，它压根不会返回，存返回地址没有意义。
* 而且此时 `ra` 是 OpenSBI 跳过来时留下的值（GDB 里看到是 `0x8000966a`，指向固件内部）。如果这里用 `call`，就等于往刚建好的内核栈里压一个"回不去也不该回去"的地址。
* 用尾调用也表达了"内核入口之后没有调用者"这个意思，`kern\_entry` 因此不需要栈帧，整个入口只有两行。

#### 小结

`kern\_entry` 用两条指令完成了从固件到内核、从汇编到 C 的交接：**先备好栈，再跳进 `kern\_init`**。之后 `kern\_init` 清空 `.bss`（`memset(edata, 0, end - edata)`）、调用 `cprintf` 打印一行字，最后进入死循环。

\---

### 练习2：使用 GDB 验证启动流程

**负责人：** 2412386-唐港

#### 调试方法

开两个终端（我们用 `tmux` 分屏）：一边跑 QEMU 并冻结 CPU、开放调试端口，另一边用 GDB 连上去。

```bash
# 左边：-S 表示启动即暂停，-s 表示在 1234 端口等 GDB
make debug

# 右边：
make gdb
```

`make gdb` 展开后是：

```bash
riscv64-unknown-elf-gdb \\
    -ex 'file bin/kernel' \\
    -ex 'set arch riscv:rv64' \\
    -ex 'target remote localhost:1234'
```

#### 调试过程与观察

**（1）连上之后先看 PC。**

```
Remote debugging using localhost:1234
0x0000000000001000 in ?? ()

(gdb) info registers pc
pc    0x1000    0x1000
```

PC 停在 `0x1000`，说明这时候内核还没开始跑，我们停在 QEMU 内置的固件代码里。

**（2）反汇编 `0x1000` 处的指令。**

```
(gdb) x/8i 0x1000
=> 0x1000:  auipc  t0,0x0
   0x1004:  addi   a2,t0,40
   0x1008:  csrr   a0,mhartid
   0x100c:  ld     a1,32(t0)
   0x1010:  ld     t0,24(t0)
   0x1014:  jr     t0
   0x1018:  unimp
   0x101a:  .insn  2, 0x8000
```

后两条 `ld` 到底读的是指令还是数据？我们把那块内存打出来看：

```
(gdb) x/2gx 0x1018
0x1018: 0x0000000080000000    0x0000000087000000
```

`0x1018` 存的是 `0x80000000`，`0x1020` 存的是 `0x87000000`。可以看出 `0x1018` 之后的 `unimp` / `.insn` 并不是指令，而是**数据**。

**（3）在内核入口下断点，看它的确被执行了。**

```
(gdb) break \*0x80200000
Breakpoint 1 at 0x80200000: file kern/init/entry.S, line 7.
(gdb) continue
Continuing.

Breakpoint 1, kern\_entry () at kern/init/entry.S:7
7       la sp, bootstacktop
```

断点命中了，说明 OpenSBI 确实把控制权交到了物理地址 `0x80200000`，跟链接脚本里的 `BASE\_ADDRESS` 一致。

**（4）看进入内核时的寄存器。**

```
(gdb) info registers pc sp ra a0 a1 a2
pc    0x80200000   0x80200000 <kern\_entry>
sp    0x8003def0   0x8003def0        # 还是 OpenSBI 的栈
ra    0x8000966a   0x8000966a        # OpenSBI 内部地址
a0    0x0          0                 # hart id
a1    0x87000000   2264924160        # 设备树 FDT 地址
a2    0x7          7
```

这里的 `a0`、`a1` 正好是复位代码在 `0x1000` 处替 OpenSBI 准备好的参数，能看出参数是一路传下来的。

**（5）单步执行，看栈有没有换过来。**

```
(gdb) x/4i $pc
=> 0x80200000 <kern\_entry>:      auipc  sp,0x3
   0x80200004 <kern\_entry+4>:    mv     sp,sp
   0x80200008 <kern\_entry+8>:    j      0x8020000a <kern\_init>
   0x8020000a <kern\_init>:       auipc  a0,0x3

(gdb) si
0x0000000080200004 in kern\_entry () at kern/init/entry.S:7
7       la sp, bootstacktop
(gdb) si
9       tail kern\_init
(gdb) info registers pc sp
pc    0x80200008   0x80200008 <kern\_entry+8>
sp    0x80203000   0x80203000        # 已经切到 bootstacktop
```

`sp` 从 `0x8003def0` 变成 `0x80203000`，正是 `bootstacktop`，跟练习1 的分析对上了。

#### 问题的答案

**问：RISC-V 硬件加电后最初执行的几条指令位于什么地址？它们主要完成了哪些功能？**

**答：位于物理地址 `0x1000`。**

QEMU 模拟的这款 RISC-V 把复位向量地址设成 `0x1000`，PC 也初始化为该值，所以处理器从这里开始执行。这段代码在 QEMU 内置的 MROM 里，是整个启动过程的第一棒，代码很少，只负责把控制权交给 OpenSBI。它做的事按顺序是：

1. `auipc t0, 0x0` —— 把当前 PC 取到 `t0`，即 `t0 = 0x1000`，作为后面读 MROM 内嵌数据的基址。
2. `addi a2, t0, 40` —— `a2 = 0x1028`，指向 MROM 里内嵌的 `fw\_dynamic\_info` 结构体（OpenSBI 的固件信息块，里面有 magic、version 和下一阶段入口 `next\_addr` 等字段）。
3. `csrr a0, mhartid` —— 读当前 hart（硬件线程）编号到 `a0`，作为第 1 个参数。
4. `ld a1, 32(t0)` —— 读 `0x1020` 处的 8 字节，得到**设备树 FDT 的物理地址** `0x87000000`，作为第 2 个参数。
5. `ld t0, 24(t0)` —— 读 `0x1018` 处的 8 字节，得到 `next\_addr = 0x80000000`，也就是 OpenSBI 被加载到的地址。
6. `jr t0` —— 跳到 `0x80000000`，进入 OpenSBI（M 模式）。

所以这几条指令本身没做任何复杂的初始化，就是**准备好参数（hart id、FDT 地址、`fw\_dynamic\_info` 指针），然后按 `next\_addr` 跳过去**。这也解释了为什么 `0x1018` 之后是 `unimp` / `.insn`——那些位置放的是跳转地址和 FDT 地址，是数据不是指令。

之后 OpenSBI 在 M 模式完成硬件初始化，把内核镜像加载到 `0x80200000`，跳转过去，控制权才交到我们的内核手上。

#### 遇到的问题：`make qemu` 没有输出

第一次 `make qemu` 时，屏幕上只有一大段 OpenSBI 的启动信息，接着就停住了，始终等不到内核打印的那行 `(THU.CST) os is loading ...`。

排查时注意到 OpenSBI 输出里有这么一行：

```
Domain0 Next Address      : 0x0000000000000000
```

`Next Address` 是 0，说明 OpenSBI 拿到的是"跳到地址 0"这个指令。回头看 Makefile，原来的 `qemu` 目标是用 `-device loader,file=...,addr=0x80200000` 来放内核的——这条命令只是把镜像**写进**物理内存，并没有告诉 OpenSBI 该跳到哪，于是 `fw\_dynamic\_info.next\_addr` 保持为 0（正好对应练习2 里 `ld t0,24(t0)` 读到的那一项）。

改成用 `-kernel` 指定内核后：

```make
qemu:
	$(QEMU) -machine virt -nographic -bios default \\
		-kernel $(UCOREIMG)
```

`Domain0 Next Address` 就变成了 `0x0000000080200000`，内核也能正常打印了。`debug` 目标做了同样的修改，否则断点设在 `0x80200000` 也永远不会命中。

\---

## 五、测试与验证

本实验共 7 张截图，与各小节的对应关系如下：

|截图文件|所属小节|对应操作|验证内容|
|-|-|-|-|
|`images/build\_result.png`|5.1 编译与镜像生成|`make clean \&\& make`、`ls -l bin/`|8 个源文件全部编译通过，链接出 `bin/kernel`，并转成裸二进制 `bin/ucore.img`|
|`images/build\_layout.png`|5.1 编译与镜像生成|`readelf -h`、`nm`|内核入口地址为 `0x80200000`；`bootstacktop - bootstack = 0x2000`，即 8KB 内核栈|
|`images/qemu\_result1.png`|5.2 make qemu|`make qemu` 的上半屏|OpenSBI 初始化信息，`Domain0 Next Address = 0x80200000`，说明固件会把控制权交给内核|
|`images/qemu\_result2.png`|5.2 make qemu|`make qemu` 的下半屏|内核打印 `(THU.CST) os is loading ...`，证明最小内核真正跑起来了|
|`images/gdb\_boot.png`|5.3 GDB 调试|`info registers pc`、`x/8i 0x1000`|加电后 PC = `0x1000`，以及复位代码的 6 条指令（练习2 的答案依据）|
|`images/gdb\_break.png`|5.3 GDB 调试|`break \*0x80200000`、`continue`|断点命中 `kern\_entry`，证明控制权已交给内核；此时 `sp` 还是 OpenSBI 的|
|`images/gdb\_sp.png`|5.3 GDB 调试|`x/4i $pc`、`si`、`info registers pc sp`|`la` / `tail` 的展开形式，以及 `sp` 切换到 `bootstacktop`（练习1 的证据）|

### 5.1 编译与镜像生成

```bash
$ make
+ cc kern/init/entry.S
+ cc kern/init/init.c
+ cc kern/libs/stdio.c
+ cc kern/driver/console.c
+ cc libs/printfmt.c
+ cc libs/readline.c
+ cc libs/sbi.c
+ cc libs/string.c
+ ld bin/kernel
riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img
```

用 `readelf` 确认入口地址是 `0x80200000`：

```
Entry point address:  0x80200000

\[ 1] .text     PROGBITS  0000000080200000
\[ 2] .rodata   PROGBITS  00000000802004a8
\[ 3] .data     PROGBITS  0000000080201000
\[ 4] .sdata    PROGBITS  0000000080203000
```

`nm` 查出的关键符号：

```
0000000080200000 T kern\_entry
000000008020000a T kern\_init
0000000080201000 D bootstack
0000000080203000 D bootstacktop
0000000080203008 D edata
0000000080203008 D end
```

**图 5-1　`build\_result.png`（编译与镜像生成）**：`make clean \&\& make` 的完整编译输出。8 个源文件全部编译通过（编译器开了 `-Werror`，有任何警告都会失败），最后链接成 `bin/kernel`，再用 `objcopy` 转成裸二进制 `bin/ucore.img`；下方 `ls -l bin/` 确认两个产物都已生成。

!\[图 5-1 编译与镜像生成](./images/build\_result.png)

**图 5-2　`build\_layout.png`（内存布局与符号地址）**：`readelf -h` 显示的 `Entry point address: 0x80200000` 与链接脚本的 `BASE\_ADDRESS` 一致；`nm` 显示 `bootstack = 0x80201000`、`bootstacktop = 0x80203000`、`edata = end = 0x80203008`。`bootstacktop - bootstack = 0x2000`，正好是 `KSTACKSIZE = 2 × 4096 = 8KB`，是练习1 中"内核栈"说法的来源。

!\[图 5-2 内存布局与符号地址](./images/build\_layout.png)

### 5.2 make qemu

```
OpenSBI v1.0
   \_\_\_\_                    \_\_\_\_\_ \_\_\_\_ \_\_\_\_\_
  / \_\_ \\                  / \_\_\_\_|  \_ \_   \_|
 | |  | |\_ \_\_   \_\_\_ \_ \_\_ | (\_\_\_ | |\_) || |
 | |  | | '\_ \\ / \_ \\ '\_ \\ \_\_\_ |  \_ < | |
 | |\_\_| | |\_) |  \_\_/ | | |\_\_\_\_) | |\_) || |\_
  \_\_\_\_/| .\_\_/ \_\_\_|\_| |\_|\_\_\_\_\_/|\_\_\_\_/\_\_\_\_\_|
        | |
        |\_|
Platform Name             : riscv-virtio,qemu
Firmware Base             : 0x80000000
Runtime SBI Version       : 0.3
Domain0 Next Address      : 0x0000000080200000
Domain0 Next Mode         : S-mode
...

(THU.CST) os is loading ...
```

打出 `(THU.CST) os is loading ...` 之后内核进入死循环，符合预期。

**图 5-3　`qemu\_result1.png`（OpenSBI 启动信息）**：`make qemu` 输出的上半屏。重点是 `Domain0 Next Address : 0x0000000080200000`——固件把下一阶段的入口设成了内核入口地址，说明它会跳到我们的内核；`Domain0 Next Arg1 : 0x87000000` 是设备树地址，与练习2 中 `a1` 寄存器的值对得上。

!\[图 5-3 OpenSBI 启动信息](./images/qemu\_result1.png)

**图 5-4　`qemu\_result2.png`（内核输出）**：`make qemu` 输出的下半屏。内核获得执行权后打印出 `(THU.CST) os is loading ...`，说明最小可执行内核跑起来了；之后进入 `while(1)` 死循环，屏幕不再变化。

!\[图 5-4 内核输出](./images/qemu\_result2.png)

### 5.3 GDB 调试

**图 5-5　`gdb\_boot.png`（复位向量与最初执行的指令）**：GDB 连上 QEMU 后，`info registers pc` 显示 `pc = 0x1000`，即加电后的复位地址；`x/8i 0x1000` 反汇编出复位代码的 6 条指令；`x/2gx 0x1018` 显示这两条 `ld` 读到的是 OpenSBI 入口 `0x80000000` 和 FDT 地址 `0x87000000`，证明 `0x1018` 之后是数据而非指令。对应练习2 的问题。

!\[图 5-5 复位向量与最初执行的指令](./images/gdb\_boot.png)

**图 5-6　`gdb\_break.png`（断点命中内核入口）**：`break \*0x80200000` 后 `continue`，断点命中 `kern\_entry`（`kern/init/entry.S:7`），证明 OpenSBI 确实把控制权交给了内核。此时 `sp = 0x8003def0` 仍指向 OpenSBI 的栈、`ra = 0x8000966a` 是固件内部的地址——这正是练习1 里"必须先建立自己的栈"的原因。

!\[图 5-6 断点命中内核入口](./images/gdb\_break.png)

**图 5-7　`gdb\_sp.png`（单步换栈）**：`x/4i $pc` 显示 `la sp, bootstacktop` 被展开成 `auipc sp,0x3`、`tail kern\_init` 被展开成 `j 0x8020000a <kern\_init>`（是跳转不是 `call`）；连按两次 `si` 后 `sp` 从 `0x8003def0` 变成 `0x80203000`，正是 `bootstacktop`。这是练习1 最直接的证据。

!\[图 5-7 单步执行后 sp 切换到内核栈](./images/gdb\_sp.png)

> \*\*关于图 5-7 中 GDB 显示 `<SBI\_CONSOLE\_PUTCHAR>` 的说明\*\*
>
> 图 5-7 里 GDB 把 `sp = 0x80203000` 标成了 `<SBI\_CONSOLE\_PUTCHAR>`，一开始我们以为是栈设错了。后来用 `nm` 查发现 `0x80203000` 上确实有两个符号：
>
> ```
> 0000000080203000 D SBI\_CONSOLE\_PUTCHAR     # libs/sbi.c 的全局变量，在 .sdata
> 0000000080203000 D bootstacktop            # entry.S 的栈顶，在 .data 末尾
> ```
>
> 原因是 `bootstack` 从 `0x80201000` 开始、长 `0x2000`，所以栈顶是 `0x80203000`；而链接脚本让 `.sdata` 紧接着也从这里开始，两个符号地址重合了，GDB 只是随便挑了其中一个显示。`sp` 的真实含义以 `nm` 为准，就是 `bootstacktop`。



\---

## 六、实验总结

### 1\. 本实验知识点与 OS 原理的对应

|本实验|OS 原理|说明|
|-|-|-|
|链接脚本指定 `BASE\_ADDRESS = 0x80200000` 和 `ENTRY(kern\_entry)`|编译、链接与装入|都是把符号地址固定下来。区别是原理课讲的是通用概念（静态/动态重定位），这里是在没有操作系统的裸机上用链接脚本硬约定加载地址，因为此时还没有页表。|
|`-mcmodel=medany` 生成的 `auipc + addi`|地址空间与重定位|原理课强调位置无关代码（PIC）以便灵活装入；这里正相反，为了让 OpenSBI 用最简单的方式跳转，故意用固定地址。|
|`elf` 与 `bin` 的区别、`objcopy -O binary`|可执行文件格式与加载器|原理课讲 ELF 的段和程序头。这里 OpenSBI 解析不了 ELF，只能吃裸二进制，所以内存布局得我们自己拍平。|
|`la sp, bootstacktop` 建内核栈|栈管理、函数调用约定|原理课里栈是和进程、现场保护一起讲的；这里展示了最原始的一步：还没有进程概念，就得先手工划一块内存当栈，否则 C 函数都调不了。|
|S 模式下 `ecall` 陷入 M 模式调 OpenSBI|系统调用、异常、特权级|`ecall` 既能让用户态陷入内核（系统调用），也能让内核态陷入固件（SBI 调用），同一条指令、不同特权级。调用约定（功能号放 `a7`、参数放 `a0`\~`a2`、返回值在 `a0`）跟 lab5 的系统调用是一样的。|
|`sbi\_call` → `cons\_putc` → `cputch` → `cprintf`|设备驱动、I/O 分层|原理课讲 I/O 分层的完整结构；这里用一个很小的规模把这条链走了一遍，从最底层的 `ecall` 一直封到跟 glibc 用法一致的 `cprintf`。区别是我们还没用中断驱动 I/O，`cons\_getc` 是轮询的。|
|`memset(edata, 0, end - edata)` 清 `.bss`|内存管理与运行时初始化|平时 `.bss` 是加载器帮忙清零的，这里没有加载器，内核得自己给自己清零，算是自举的一个体现。|
|`make debug` + GDB 远程调试|内核调试|原理课不太讲调试手段。通过 `-S -s` 把 QEMU 当成一块可以下断点、可以看寄存器的"假硬件"，抽象的过程就变成能观察的了。|

### 2\. OS 原理里重要、但本实验没覆盖的

* **进程与线程、调度**：本实验只有一条执行流，`kern\_init` 最后是 `while(1)`，没有并发和上下文切换。
* **虚拟内存与分页**：全程用物理地址，没有页表，也没有地址空间隔离。
* **中断与异常处理**：没有设置 `stvec`，没有中断入口，时钟中断也没开。
* **同步互斥**：没有并发，就谈不上临界区、信号量、管程。
* **文件系统与存储**：没有任何存储抽象和设备管理框架。
* **系统调用的完整语义**：虽然用到了 `ecall` 和调用约定，但没有用户态进程，所以还没有"用户程序请求内核服务"这层含义。
* **内存分配算法**：内核栈是链接期静态分配的一个固定大小数组，没有动态分配器。

### 3\. AI 协作开发的经验

这次实验里 AI 帮上的忙，主要集中在"看懂底层细节"上，比如 `la`、`tail` 这类伪指令到底展开成什么、`a0`/`a1` 这些寄存器在启动时分别代表什么——这些东西光看代码不容易确定，让 AI 解释之后再用 GDB 验证，效率比硬啃快很多。

但有一次 AI 的判断没能直接用： `make qemu` 跑不出内核输出。我们一开始只是笼统地问"为什么没输出"，得到的都是些泛泛的可能原因。后来把 OpenSBI 的完整输出贴过去，尤其是 `Domain0 Next Address : 0x0` 这一行，问题才定位到 Makefile 用 `-device loader` 没有告诉 OpenSBI 跳转地址。**把实际输出、报错、文件内容这些具体信息给 AI，比只描述现象有用得多。**

总的体会是：AI 更适合用来解释和核对，结论还是得自己在 QEMU/GDB 里跑一遍才算数。特别是练习1、练习2 这种要求"说明你的理解"的题，直接问答案没什么意义，先用 GDB 把数据跑出来，再让 AI 帮忙检查有没有说错、有没有漏掉，效果比较好。

