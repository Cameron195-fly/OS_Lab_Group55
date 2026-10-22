# Lab2 · 丙（Buddy System + SLUB）交付包

本目录是 ucore Lab2「物理内存与页表」中**丙**负责部分的完整交付：代码、设计笔记、实验报告、AI 协作 prompt。

三人分工（详见教程第 1.6 节）：

| 角色 | 负责内容 | 落点 |
|---|---|---|
| 甲 | `page_init()`（页描述符初始化） | `kern/mm/pmm.c` |
| 乙 | Best-Fit 管理器（`make grade` 的 Test 2 判定它） | `kern/mm/best_fit_pmm.c`、`pmm.c` 第 37 行切换 |
| **丙（本包）** | **Buddy System + SLUB 两层分配器，各含代码与两份 prompt** | `kern/mm/buddy_pmm.c/.h`、`kern/mm/slub.c/.h`、`kern/init/init.c` |

---

## 1. 目录结构

```text
Lab2_丙_Buddy_SLUB/
├── README.md                     ← 本文件：怎么合并、怎么编译、怎么验证
└── code/                         ← 基于官方 lab2 骨架，含丙的改动 + 本机跑通所需的甲/乙最小实现
    ├── Makefile                  ← 启动参数改为 -kernel（本机 QEMU 6.2 需要，见第 4 节说明）
    ├── kern/
    │   ├── mm/
    │   │   ├── buddy_pmm.c       ★ 新增：伙伴系统 + 7 组自检
    │   │   ├── buddy_pmm.h       ★ 新增
    │   │   ├── slub.c            ★ 新增：SLUB + 4 组自检
    │   │   ├── slub.h            ★ 新增
    │   │   ├── pmm.c             ＋ 补 page_init()（甲的最小实现）、切换到 best_fit
    │   │   ├── best_fit_pmm.c    ＋ 补 best_fit 管理器（乙的最小实现）
    │   │   └── ...
    │   └── init/
    │       └── init.c            ★ 修改：pmm_init() 后接入三个自检
    ├── libs/                     ← 原样
    ├── tools/                    ← grade.sh 启动参数改为 -kernel，其余未改动
    ├── docs/
    │   └── buddy-slub-design.md  ← 设计笔记：不变量、借页-归还协议
    └── report/
        ├── report.md             ← 实验报告（丙的三个章节）
        └── prompt.md             ← P3–P6 四份 prompt + v1→v2 迭代记录
```

★ = 丙新增/修改的文件，共 5 个；＋ = 为了让这个包在本机能直接编译运行而补的接口实现，共 2 个。
其余文件与官方 `lab2.zip` 骨架一致（`Makefile` / `grade.sh` 仅改了 QEMU 启动参数）。

---

## 2. 合并到小组仓库（重要）

本包 `code/` 现在可以直接编译运行（第 4 节是本机实测结果）。其中 `pmm.c` 的 `page_init()` 和
`best_fit_pmm.c` 的 best_fit 管理器是**为了跑通而补的最小实现**，正式提交时必须以甲、乙的版本为准，
合并时不要把这两个文件覆盖回组内仓库。

正确顺序（与教程 6.1 一致，甲 → 乙 → 丙）：

```bash
git checkout -b feature/lab2-buddy-slub
# 1) 先合并甲：pmm.c 的 page_init() 与 init_memmap 调用链
# 2) 再合并乙：best_fit_pmm.c 实现 + pmm.c 第 37 行
#    pmm_manager = &best_fit_pmm_manager;
# 3) 最后叠加丙：只覆盖下面这 5 个文件（★ 文件）
cp -r Lab2_丙_Buddy_SLUB/code/kern/mm/buddy_pmm.[ch] <repo>/kern/mm/
cp -r Lab2_丙_Buddy_SLUB/code/kern/mm/slub.[ch]      <repo>/kern/mm/
cp    Lab2_丙_Buddy_SLUB/code/kern/init/init.c        <repo>/kern/init/
```

> 切记：**不要**把本包的 `kern/mm/pmm.c`、`kern/mm/best_fit_pmm.c` 复制进组内仓库，
> 否则会覆盖甲、乙的正式实现。

`init.c` 的改动只有两处，如果组内 `init.c` 也被别人改过，可以手工打补丁：

```c
#include <buddy_pmm.h>      /* 加在 include 区 */
#include <slub.h>

    pmm_init();

    /* lab2 扩展任务自检：放在 pmm_init() 之后，
       这样 make grade 注入的 Test 1 / Test 2 已经打印完，不影响主线的 20/20 */
    buddy_check();
    slub_init();
    slub_check();
```

---

## 3. 构建与运行

环境：Linux + `riscv64-unknown-elf-` 工具链 + `qemu-system-riscv64` + `python3`（`make grade` 需要）。

```bash
# 编译
make -j$(nproc)

# 跑 QEMU（Ctrl+A 然后 X 退出）
make qemu

# 官方评分（会自动 tar 到 /tmp/lab2_grade、inject.py 注入 test_lab2.c、跑 grade.sh）
make grade
```

本机验证用的是 WSL 里的副本 `/root/lab2work`，在 PowerShell 中进入：

```powershell
wsl -d Ubuntu-22.04 -u root
cd /root/lab2work
make qemu
```

---

## 4. 运行输出（本机实测）

实测环境：WSL2 + Ubuntu 22.04.5，`riscv64-unknown-elf-gcc 10.2.0`，`qemu-system-riscv64 6.2.0`。

主线程（甲 + 乙）跑完后：

```text
memory management: best_fit_pmm_manager
check_alloc_page() succeeded!
satp virtual address: 0xffffffffc0206000
satp physical address: 0x0000000080206000
```

接着是丙的自检（Buddy + SLUB，全部断言通过才会走到 `while (1)`）：

```text
[Test] Buddy: test area = 256 pages at index 1024
[Test] Buddy: alloc(1/2/4/3) ok, nr_free = 245
[Test] Buddy: split/merge round trip ok, blocks = 1
[Test] Buddy: 256 single page allocations and releases ok
[Test] Buddy: PASS (free pages restored to 31927)
[Test] SLUB: size 16 -> 256 objects, 2 slab(s), all distinct
[Test] SLUB: size 40 -> 256 objects, 3 slab(s), all distinct
[Test] SLUB: size 96 -> 127 objects, 4 slab(s), all distinct
[Test] SLUB: size 256 -> 46 objects, 4 slab(s), all distinct
[Test] SLUB: size 1024 -> 10 objects, 4 slab(s), all distinct
[Test] SLUB: 24 interleaved objects from 3 caches released
[Test] SLUB: PASS (free pages restored to 31927)
```

其中 `index` 与 `nr_free` 的具体数字随内核镜像大小变化，`nr_free = 245 = 256 - (1+2+4+4)` 与
`blocks = 1` 是不随环境变化的判定点。Buddy 与 SLUB 两处 `restored to` 的数字一致，说明自检前后
`nr_free_pages()` 完全复原。

`make grade` 的汇总：

```text
  -check page init:                          OK
  -check best fit:                           OK
Total Score: 20/20
```

> **验证说明（如实记录）**：
> 1. 本包是丙的分支快照，缺甲的 `page_init()` 和乙的 best_fit 管理器。本机验证时**只在 WSL 的工作副本**里
>    补了这两处最小实现（`pmm_manager` 指向 `&best_fit_pmm_manager`，这是 Test 2 的要求），桌面交付包
>    里丙的 5 个文件没有任何改动。
> 2. 本机 QEMU 6.2 + 自带 OpenSBI 0.9 下，`-device loader` 启动时 OpenSBI 打印
>    `Domain0 Next Address: 0x0`，不会跳进内核；把启动参数换成 `-kernel bin/ucore.img` 即可正常运行。
>    这是 QEMU 版本差异，与本实验代码无关，评卷环境若使用原 `-device loader` 参数无需改动。
> 3. 代码已在 `-Wall -Werror -O2` 下编译通过，无告警。

---

## 5. 丙的 commit 清单（对应教程 丙-01 ~ 丙-11）

| 编号 | 类型 | 内容 |
|---|---|---|
| 丙-01 | docs | 设计笔记：Buddy 三条不变量、为何不切换活动管理器 |
| 丙-02 | feat(mm) | `buddy_pmm.h`：导出 `buddy_pmm_manager` / `buddy_check` |
| 丙-03 | feat(mm) | `buddy_init` + `buddy_init_memmap`（贪心切成阶对齐块） |
| 丙-04 | feat(mm) | `buddy_alloc_pages`（向上取阶 + 逐级分裂，上半块挂回） |
| 丙-05 | feat(mm) | `buddy_free_pages`（同阶伙伴反复合并，`released` 先存，计数守恒） |
| 丙-06 | test(mm) | `buddy_check` 前 4 组：切分、乱序合并、整段分配、单页循环 |
| 丙-07 | test(mm) | `buddy_check` 扩到 7 组：未对齐请求、容量上限、`nr_free` 复原 |
| 丙-08 | feat(mm) | `slub.h` / `slub.c`：页层 + 对象层，`kmem_cache_create/alloc/free` |
| 丙-09 | fix(mm) | SLUB 三处修正：`SLUB_OBJ_OFFSET` 对齐、`slub_page_to_kva` 取代 `PADDR`、`was_full` 提前取 |
| 丙-10 | feat(init) | `init.c` 在 `pmm_init()` 后接入 `buddy_check` / `slub_init` / `slub_check` |
| 丙-11 | docs | 报告 Buddy 章、SLUB 章、六.2；P3–P6 prompt 与迭代记录 |

建议的提交粒度就是上表一行一个 commit，`feat` / `fix` / `test` / `docs` 分开，方便 review 时定位。

---

## 6. 自检清单（提交前逐条确认）

- [ ] `make grade` 仍是 **20/20**（丙的改动没有影响甲、乙的用例）
- [ ] QEMU 里 Buddy 7 组、SLUB 4 组全部 PASS
- [ ] `grep -n "pmm_manager =" kern/mm/pmm.c` 仍指向 `best_fit_pmm_manager`（丙没有偷偷切换）
- [ ] `git diff --stat` 里只有 5 个文件：`buddy_pmm.c/.h`、`slub.c/.h`、`init.c`
- [ ] 报告的三章已按 `report.md` 头部说明的顺序拼进小组报告
- [ ] prompt.md 的 P3–P6 已贴进报告对应小节

---

## 7. 关键设计一句话总结

Buddy 解决「**按 2 的幂批量拿连续页**」，SLUB 解决「**同一尺寸的小对象反复申请释放**」；
两者都以**自检函数**形态挂在 `pmm_init()` 之后，通过「从当前管理器借一段私有页 → 自己管 → 复位描述符 → 整段归还 → 断言 `nr_free_pages()` 复原」接入，因此**不需要、也不允许**替换活动的 `pmm_manager`。
