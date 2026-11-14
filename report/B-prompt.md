# Lab4 Prompt 记录（B：2412386-唐港）

> 本文档记录 B 负责的功能模块二与 `cpu_idle`/`proc_run` 使用的提示词。
> 每个提示词沿用课程的四段式框架：`[PROMPT]` / `[RELY]` / `[GUARANTEE]` / `[SPECIFICATION]`。
> 与 A 的 `prompt.md` 分开存放，避免多人同时修改同一文件；R-2 拼接报告时再合并。

---

## B-1：`alloc_proc`

### 最终提示词

```text
[PROMPT]
**任务**：修改文件 code/kern/process/proc.c，在文件末尾的 `//LAB4 function2 YOUR CODE`
标记处实现 alloc_proc() 函数，完成进程控制块的分配与初始字段设置。本任务只实现
alloc_proc() 这一个函数；同属 function2 的 copy_mm、copy_thread、do_fork、
kernel_thread、proc_init 留给后续任务，本次不要实现。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码片段。保留 `//LAB4 function2`
标记注释，并把 `YOUR CODE` 替换为学号 2412386。不要改动本文件已有的其它函数
（set_proc_name、get_proc_name、get_pid、forkret、hash_proc、find_proc、
setup_kstack、put_kstack、do_exit、init_main），不要新增全局变量。

**输出要求**：直接进行文件操作，代码插入到标记位置。使用下面 [RELY]、[GUARANTEE]、
[SPECIFICATION] 提供的信息，以工程真实代码为准，不要引入与本函数无关的功能
（不要分配内核栈、不要分配 PID、不要挂链表、不要修改 current/nr_process）。

[RELY]
// 宏定义
#define PROC_NAME_LEN 15
#define MAX_PROCESS   4096
#define KSTACKPAGE    2          // 内核栈页数，仅供理解，本函数不使用

// 枚举与结构体（真实定义，勿改）
enum proc_state {
    PROC_UNINIT = 0,
    PROC_SLEEPING,
    PROC_RUNNABLE,
    PROC_ZOMBIE,
};

struct context {                 // 14 个 8 字节字段
    uintptr_t ra, sp, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};

struct proc_struct {
    enum proc_state state;
    int pid;
    int runs;
    uintptr_t kstack;
    volatile bool need_resched;
    struct proc_struct *parent;
    struct mm_struct *mm;
    struct context context;
    struct trapframe *tf;
    uintptr_t pgdir;              // 页表根（物理地址）
    uint32_t flags;
    char name[PROC_NAME_LEN + 1];
    list_entry_t list_link;
    list_entry_t hash_link;
};

// 可用全局变量
extern pde_t *boot_pgdir_va;      // 内核页表根的虚拟地址
extern uintptr_t boot_pgdir_pa;   // 内核页表根的物理地址

// 可用函数（签名以仓库为准）
void *kmalloc(size_t n);          // SLOB 页内分配，8 字节对齐，内容不保证为 0
void kfree(void *objp);
void *memset(void *s, int c, size_t n);

[GUARANTEE]
必须实现或修改的接口：
static struct proc_struct *alloc_proc(void);

你可以根据需要添加：
- 辅助函数（建议设为 static）
- 内部使用的宏定义
- 模块内部的静态变量

[SPECIFICATION]
## alloc_proc
**Pre-Condition**:
- 内核堆已初始化（kmalloc_init 已在 pmm_init 中完成）
- 调用者可能是 proc_init（创建 idleproc）或 do_fork（创建子线程）
- 调用时不要求关闭中断；本函数不访问任何全局链表或计数器

**Post-Condition**:
- 成功时返回新 PCB 指针，每个字段都处于 Case 1 规定的确定初值；
  PCB 尚未挂入 proc_list 和 hash_list，不属于任何调度队列
- 失败时返回 NULL，系统状态与调用前完全一致

**Case 1**（kmalloc 成功）:
- PCB 先整体 memset 清零，随后显式设置为：state = PROC_UNINIT；pid = -1；
  pgdir = boot_pgdir_pa。其余字段保持清零值：runs = 0、kstack = 0、
  need_resched = 0、parent = NULL、mm = NULL、context 全 0（特别是 ra = 0）、
  tf = NULL、flags = 0、name 全 0、list_link 与 hash_link 为 NULL。返回该 PCB。

**Case 2**（kmalloc 返回 NULL）:
- 直接返回 NULL，不做任何其他操作。

**Requirements**:
- 函数必须是 static，不改变任何公开接口
- 不得分配内核栈、分配 PID、初始化 proc_list/hash_list、把状态设成 RUNNABLE
  或调用 wakeup_proc
- 不得设置 parent、mm、context、tf 的业务值；这些分别由 do_fork 和 copy_thread 负责
- 必须用 memset 整体清零，不能依赖 kmalloc 返回内存的初始内容
```

### 迭代记录

- 初版提示词缺少 `[SPECIFICATION]`，AI 需要猜测失败返回值、`pgdir` 用 PA 还是 VA、是否 memset。
- 补充后明确了三个“非 0 初值”字段和失败路径；生成代码一次编译通过。

---

## B-2：`copy_mm` 与 `copy_thread`

### 最终提示词

```text
[PROMPT]
**任务**：修改文件 code/kern/process/proc.c，在 `//LAB4 function2 2412386` 标记区域、
alloc_proc() 之后，实现 copy_mm() 和 copy_thread() 两个函数。本任务只实现这两个函数；
do_fork、kernel_thread、proc_init 留给后续任务，本次不要实现。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码片段。保留 `//LAB4 function2`
标记注释与已有注释。不要改动本文件中 alloc_proc 及之前的所有函数（含 forkret、
setup_kstack、hash_proc 等），不要新增全局变量。

**输出要求**：直接编辑文件。使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 的信息，
以工程真实代码为准；不要引入与本任务无关的功能（不要分配内核栈、不要分配 PID、
不要挂链表、不要写 satp）。

[RELY]
// 宏与类型
#define KSTACKPAGE 2
#define KSTACKSIZE (KSTACKPAGE * PGSIZE)   // 内核栈大小，栈从高地址向低地址增长
typedef uintptr_t ...;                      // 见 defs.h，用于指针与整数互转

// 结构体（真实定义）
struct trapframe {
    struct pushregs gpr;   // 含 gpr.a0、gpr.sp 等 32 个通用寄存器槽位
    uintptr_t status;      // sstatus 快照
    uintptr_t epc;         // 恢复后 sret 跳转到的地址
    uintptr_t badvaddr;
    uintptr_t cause;
};                         // 共 36 个 8 字节槽位

struct proc_struct 关键字段：
    uintptr_t kstack;              // 内核栈基址（低地址端）
    struct context context;        // { ra, sp, s0..s11 } 共 14 个 8 字节字段
    struct trapframe *tf;          // 首次运行使用的中断帧
    struct mm_struct *mm;          // 地址空间；内核线程为 NULL

// 本文件中已存在、可直接调用
static void forkret(void) { forkrets(current->tf); }
struct proc_struct *current;       // 本文件内定义，不是外部符号

// 外部符号（已在其它文件中实现，这里只引用，不要重新定义）
void forkrets(struct trapframe *tf);   // kern/trap/trapentry.S：sp=a0; j __trapret
void kernel_thread_entry(void);        // kern/process/entry.S：move a0,s1; jalr s0

[GUARANTEE]
必须实现或修改的接口：
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);

[SPECIFICATION]
## copy_mm
**Pre-Condition**:
- proc 的 PCB 已由 alloc_proc 初始化，kstack 已分配
- 本实验创建的是内核线程，current->mm 恒为 NULL

**Post-Condition**:
- proc->mm 被赋为 NULL（clone_flags 含 CLONE_VM 时为 current->mm，
  本实验两条路径结果相同）
- 返回 0，本实验没有失败路径

**Requirements**:
- 不做真正的地址空间复制；不得调用 mm_create 或分配页表
- 函数形态保持完整，供后续实验扩展

## copy_thread
**Pre-Condition**:
- proc->kstack 已由 setup_kstack 指向一段 KSTACKSIZE 大小的内核栈低地址端
- tf 指向调用者（kernel_thread）栈上的临时中断帧模板，函数返回后该模板失效

**Post-Condition**:
- proc->tf 指向 proc 内核栈顶部的 trapframe 槽位，内容为 tf 的完整拷贝
- proc->context 已设置好，可被 switch_to 直接使用

**Case 1**（正常创建内核线程）:
- 把 proc->tf 定位到 kstack + KSTACKSIZE - sizeof(struct trapframe)
- 整体深拷贝 *tf；将 proc->tf->gpr.a0 置 0（子线程返回值）
- esp 为 0 时 gpr.sp 指向 proc->tf 自身，否则使用 esp
- context.ra = forkret，context.sp = proc->tf

**Requirements**:
- 必须深拷贝，不能保存 tf 指针，因为模板位于调用者栈帧
- 不能设置 context 的 s0~s11；不能在此处写 satp
```

### 迭代记录

- 初版缺少 `[SPECIFICATION]`，没有说明深拷贝、栈顶定位和 `esp == 0` 的回退。
- 补充后明确 `(trapframe *)(kstack + KSTACKSIZE) - 1`，并用编译期断言核对
  `sizeof(trapframe) == 288`、`sizeof(context) == 112`。

---

## B-3：`do_fork`

### 最终提示词

```text
[PROMPT]
**任务**：修改文件 code/kern/process/proc.c，在 `//LAB4 function2 2412386` 标记区域内、
copy_thread() 之后，实现 do_fork() 函数，完成“把新线程登记进系统并置为就绪态”的功能。
本任务只实现 do_fork()；kernel_thread、proc_init 留给后续任务，本次不要实现。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留 `//LAB4 function2`
标记与已有注释。不要改动 alloc_proc、copy_mm、copy_thread 及其之前的函数；
不要新增全局变量；不要修改 nr_process 的声明。

**输出要求**：直接编辑文件。使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 的信息，
以工程真实代码为准。不要添加与本任务无关的功能（不要切换 current、不要写 satp、
不要调用 schedule、不要设置 need_resched）。

[RELY]
// 宏与错误码
#define MAX_PROCESS 4096
#define E_NO_FREE_PROC 5
#define E_NO_MEM 4
#define MAX_PID (MAX_PROCESS * 2)

// 全局变量（已在本文件定义）
list_entry_t proc_list;                 // 所有 PCB 的双向链表（带哨兵头）
static int nr_process = 0;
static list_entry_t hash_list[HASH_LIST_SIZE];
struct proc_struct *current;            // 本文件内定义

// 本文件中已存在的函数（直接调用，不要重写）
static struct proc_struct *alloc_proc(void);
static int setup_kstack(struct proc_struct *proc);      // 成功 0，失败 -E_NO_MEM
static void put_kstack(struct proc_struct *proc);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int get_pid(void);                               // 依赖 proc_list 遍历
static void hash_proc(struct proc_struct *proc);        // 按 pid 挂 hash_list
void wakeup_proc(struct proc_struct *proc);             // 置 PROC_RUNNABLE
void kfree(void *objp);

// 链表操作（libs/list.h）
void list_add(list_entry_t *listelm, list_entry_t *elm); // 插到 listelm 之后

// 开关中断（kern/sync/sync.h）
#define local_intr_save(x)    do { x = __intr_save(); } while (0)
#define local_intr_restore(x) __intr_restore(x);

[GUARANTEE]
必须实现或修改的接口：
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);

[SPECIFICATION]
## do_fork
**Pre-Condition**:
- proc_list 与 hash_list 已初始化（proc_init 的第一步）
- current 指向正在执行的线程
- tf 指向调用者栈上的临时中断帧；stack 为 0 表示内核线程

**Post-Condition**:
- 成功：系统中新增一个 PROC_RUNNABLE 线程，已分配 PID、已挂 hash_list
  和 proc_list，parent 指向创建者，返回其 PID
- 失败：返回负错误码，调用前的系统状态完全恢复，无资源泄漏

**Case 1**（进程数已达 MAX_PROCESS）:
- 不分配任何资源，返回 -E_NO_FREE_PROC

**Case 2**（alloc_proc 失败）:
- 返回 -E_NO_MEM，无资源需要回滚

**Case 3**（setup_kstack 失败）:
- 释放 PCB，然后返回 -E_NO_MEM

**Case 4**（copy_mm 失败，本实验不会触发）:
- 先释放内核栈，再释放 PCB，返回错误码

**Case 5**（正常创建）:
- 设置 parent = current
- 依次完成内核栈、mm、trapframe/context
- 在关中断临界区内完成 PID 分配、hash 挂接、proc_list 挂接、nr_process++
- 临界区外调用 wakeup_proc 把状态置为 PROC_RUNNABLE
- 返回新线程 PID

**Requirements**:
- setup_kstack 必须先于 copy_thread（copy_thread 依赖 kstack）
- PID 分配与链表挂接必须在同一临界区，避免并发 fork 竞争
- 不得在此切换 current 或写 satp（属于 proc_run）
```

### 迭代记录

- 初版缺少 `[SPECIFICATION]`，最危险的是漏写 `proc->parent = current`；测试直接断言
  `initproc->parent == idleproc`。
- 补充失败标签的贯穿顺序：`bad_fork_cleanup_kstack → put_kstack → 落入
  bad_fork_cleanup_proc → kfree`，并说明顺序反了会 use-after-free。

---

## B-4：`kernel_thread`

### 最终提示词

```text
[PROMPT]
**任务**：修改文件 code/kern/process/proc.c，在 `//LAB4 function2 2412386` 标记区域内、
do_fork() 之后，实现 kernel_thread() 函数：为“把一个普通函数包装成内核线程”构造
临时 trapframe 模板，并交给 do_fork 完成创建。本任务只实现 kernel_thread()；
proc_init 留给后续任务，本次不要实现。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留 `//LAB4 function2`
标记与已有注释。不要改动 alloc_proc、copy_mm、copy_thread、do_fork 及其之前的函数；
不要新增全局变量。

**输出要求**：直接编辑文件。使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 的信息，
以工程真实代码为准。不要添加与本任务无关的功能（不要分配资源、不要动 current、
不要设置 need_resched、不要调用 schedule）。

[RELY]
// 结构体（真实定义，共 36 个 8 字节槽位）
struct pushregs {
    uintptr_t zero, ra, sp, gp, tp, t0, t1, t2;
    uintptr_t s0, s1;            // 注意：s0 在 a0 之前，按字段名访问即可
    uintptr_t a0, a1, a2, a3, a4, a5, a6, a7;
    uintptr_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uintptr_t t3, t4, t5, t6;
};
struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};

// 状态位（libs/riscv.h）
#define SSTATUS_SIE   0x00000002
#define SSTATUS_SPIE  0x00000020
#define SSTATUS_SPP   0x00000100
#define read_csr(reg)  ({ unsigned long __tmp; asm volatile("csrr %0, " #reg : "=r"(__tmp)); __tmp; })

// 已存在的符号 / 函数
void kernel_thread_entry(void);   // kern/process/entry.S：move a0,s1; jalr s0
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
void *memset(void *s, int c, size_t n);

[GUARANTEE]
必须实现或修改的接口：
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

[SPECIFICATION]
## kernel_thread
**Pre-Condition**:
- current 是正在执行的线程（idleproc 或 initproc）
- fn 指向内核函数，签名为 int fn(void *)

**Post-Condition**:
- 成功：返回新线程 PID，系统中多一个 PROC_RUNNABLE 的内核线程，
  首次被调度时从 kernel_thread_entry 进入并调用 fn(arg)
- 失败：返回 do_fork 的负错误码，自身不残留任何资源

**Case 1**（正常创建）:
- 在栈上定义 trapframe 模板并整体清零
- gpr.s0 = fn、gpr.s1 = arg、gpr.a0 = 0、gpr.sp 保持 0
- epc = kernel_thread_entry
- status 取 sstatus 快照后：清 SIE、置 SPIE、置 SPP
- 以 stack = 0 调用 do_fork(clone_flags, 0, &tf) 并返回其结果

**Case 2**（do_fork 失败）:
- 直接返回其错误码；栈上模板随函数返回自然消失

**Requirements**:
- tf 必须是栈上局部变量；不得 static 或全局，否则多次调用会互相覆盖
- 不得在这里分配内核栈、PID 或修改 current
- gpr.sp 必须保持 0，让 copy_thread 将栈指针指向自己的 trapframe
```

### 迭代记录

- 初版只说要“设置函数和参数”，没有绑定 `kernel_thread_entry` 的 `s0/s1` 约定。
- 补充 `[RELY]` 中的汇编实现和 `[SPECIFICATION]` 中的寄存器对应关系后，生成代码与
  `entry.S` 完全一致。

---

## B-5：`proc_init`

### 最终提示词

```text
[PROMPT]
**任务**：修改文件 code/kern/process/proc.c，在 `//LAB4 function2 2412386` 标记区域内、
kernel_thread() 之后，实现 proc_init() 函数：把当前执行现场包装成 idleproc（0 号线程），
再创建 initproc（1 号线程），并让 current 指向 idleproc。这是 function2 的最后一个函数。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留 `//LAB4 function2`
标记与已有注释。不要改动 alloc_proc、copy_mm、copy_thread、do_fork、kernel_thread；
不要新增全局变量；不要修改 HASH_LIST_SIZE、hash_list、proc_list、nr_process 的声明。

**输出要求**：直接编辑文件。使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 的信息，
以工程真实代码为准。不要添加与本任务无关的功能（不要调用 schedule、不要写 satp、
不要分配额外的内核栈）。

[RELY]
// 宏与常量
#define HASH_LIST_SIZE (1 << 10)
#define PROC_NAME_LEN  15

// 全局变量（已在本文件定义）
list_entry_t proc_list;
static list_entry_t hash_list[HASH_LIST_SIZE];
struct proc_struct *idleproc, *initproc, *current;
static int nr_process;

// 外部符号
extern char bootstack[], bootstacktop[];    // kern/init/entry.S：启动栈

// 本文件中已存在的函数
static struct proc_struct *alloc_proc(void);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);  // 返回 pid
struct proc_struct *find_proc(int pid);
char *set_proc_name(struct proc_struct *proc, const char *name);
static int init_main(void *arg);
void panic(const char *fmt, ...);

// 枚举
enum proc_state { PROC_UNINIT, PROC_SLEEPING, PROC_RUNNABLE, PROC_ZOMBIE };

// 链表（libs/list.h）
void list_init(list_entry_t *elm);

[GUARANTEE]
必须实现或修改的接口：
void proc_init(void);

[SPECIFICATION]
## proc_init
**Pre-Condition**:
- pmm_init / kmalloc_init / vmm_init 已完成
- 当前执行现场是 kern_init，正在使用 bootstack

**Post-Condition**:
- proc_list 与 1024 个 hash 桶全部初始化为空
- idleproc 指向一个 PROC_RUNNABLE、pid=0、kstack=bootstack、need_resched=1
  的 PCB，current 指向它，但它不在任何链表中
- initproc 指向 pid=1 的 PCB，parent=idleproc，name="init"，已挂入
  proc_list 与 hash_list
- nr_process == 2

**Case 1**（正常）:
- 初始化链表结构
- alloc_proc 得到 idleproc 并补齐身份字段；不 list_init 它的 list_link、
  不 hash_proc、不 list_add
- 用 kernel_thread(init_main, "Hello world!!", 0) 创建 initproc
- 用返回的 pid 调 find_proc 取回 PCB，命名为 "init"

**Case 2**（alloc_proc 失败 / kernel_thread 失败）:
- panic 终止启动；不留下半初始化的进程表

**Requirements**:
- idleproc 必须留在链表之外，它是调度器的兜底指针，不是可调度节点
- 所有初始化必须在函数返回前完成（测试在 proc_init 末尾注入）
```

### 迭代记录

- 初版没有说明 idleproc 不能挂链表，也没说明 `kernel_thread` 返回的是 int PID。
- 补充后使用 `find_proc(pid)` 取回 PCB；`nm`/编译验证通过。

---

## B-6：`cpu_idle` 与 `proc_run`

### 最终提示词

```text
[PROMPT]
**任务**：修改文件 code/kern/process/proc.c，在 `//LAB4 function3 2412386` 标记区域实现
cpu_idle() 与 proc_run()。cpu_idle 是 idleproc 的执行主体与唯一调度点；proc_run
负责把 current 切换到目标线程、切换页表并调用 switch_to 完成上下文切换。
本任务只实现这两个函数。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留 `//LAB4 function3`
标记与已有注释。不要改动 function2 区域的任何函数；不要修改 schedule()（在 sched.c）、
switch_to()（在 switch.S）或 trap 相关文件；不要新增全局变量。

**输出要求**：直接编辑文件。使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 的信息，
以工程真实代码为准。不要添加与本任务无关的功能（不要在这里清 need_resched、
不要实现调度策略、不要碰 trapframe）。

[RELY]
// 宏（libs/riscv.h）
#define SATP_MODE_SV39   8          // MODE 字段的值，不是整个 satp
#define RISCV_PGSHIFT    12

// satp 布局（Sv39，RV64）：
//   bit 63..60 = MODE，bit 59..44 = ASID，bit 43..0 = PPN（页表根物理地址 >> 12）

#define write_csr(reg, val)  asm volatile("csrw " #reg ", %0" ::"rK"(val))

// 全局变量
struct proc_struct *current;

// 结构体字段
struct proc_struct {
    unsigned long pgdir;            // 页表根【物理地址】
    volatile bool need_resched;
    struct context context;         // { ra, sp, s0..s11 }
    enum proc_state state;
};

// 已存在的外部函数（只调用，不要重写）
void schedule(void);                                    // kern/schedule/sched.c（C）
void switch_to(struct context *from, struct context *to); // kern/process/switch.S（C）
void flush_tlb(void);
#define local_intr_save(x)    do { x = __intr_save(); } while (0)
#define local_intr_restore(x) __intr_restore(x);

// 参考：kern/mm/pmm.c 中 enable_paging() 的正确写法
//   write_csr(satp, 0x8000000000000000 | (boot_pgdir_pa >> RISCV_PGSHIFT));

[GUARANTEE]
必须实现或修改的接口：
void cpu_idle(void) __attribute__((noreturn));
void proc_run(struct proc_struct *proc);

[SPECIFICATION]
## cpu_idle
**Pre-Condition**:
- current 指向 idleproc，其 need_resched 已由 proc_init 置 1

**Post-Condition**:
- 函数永不返回
- 每当 current->need_resched 为真就调用 schedule()，否则空转等待

**Requirements**:
- 不得在这里修改 need_resched、不得实现调度策略
- 函数体必须是死循环，满足 noreturn

## proc_run
**Pre-Condition**:
- proc 是已构造完成、状态为 PROC_RUNNABLE 的 PCB
- proc->pgdir 是页表根物理地址

**Post-Condition**:
- 返回时 current 已指向 proc，satp 已指向 proc->pgdir，
  from 与 proc 的上下文已完成交换
- 若 proc == current，函数什么都不做

**Case 1**（proc == current）:
- 只关闭并恢复中断，不写 satp、不调用 switch_to

**Case 2**（proc != current）:
- 关中断 → 保存 from = current → current = proc →
  写 satp（MODE=8 在 bit63..60，PPN 在 bit43..0）→ flush_tlb →
  switch_to(&from->context, &proc->context) → 恢复中断

**Requirements**:
- 必须用 satp 的完整 64 位形式，8UL << 60 或等价常量；不得用 lsatp()
- current 必须在 switch_to 之前更新
- from 必须在 current 被改写之前保存
- 不得在 switch_to 前后做额外调度判断
```

### 迭代记录

- 初版没有提醒 `SATP_MODE_SV39 << 60` 在 32 位 `int` 上是未定义行为。
- 最终采用 `enable_paging()` 的 `0x8000000000000000 | ...` 写法；并明确了
  `from` 的保存时机和 `current` 的更新时机。

---

## 通用验证提示词

```text
[验证]
请检查 code/kern/process/proc.c：
1. 两条 //LAB4 function 标记是否保留，YOUR CODE 是否已替换为 2412386；
2. 所有新增函数是否为 static（alloc_proc/copy_mm/copy_thread 必须是）；
3. boot_pgdir_pa 与 boot_pgdir_va 是否用对；
4. do_fork 的失败标签顺序是否为 kstack → PCB；
5. 是否存在未使用的全局变量或多余的 include。
最后运行 make obj/kern/process/proc.o，报告编译结果。
```

## 备注

- 六轮提示词均遵循四段式框架；初版普遍缺少 `[SPECIFICATION]`，补齐后才交给 AI 落盘。
- 提示词与最终代码保持同步：任何手动修改都会回写到对应的 `[SPECIFICATION]`。
- 与 A 的 `prompt.md` 分开存放，R-2 拼接报告时再合并为一个文档。
