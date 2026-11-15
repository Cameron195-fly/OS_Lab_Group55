# Challenge：`local_intr_save` / `local_intr_restore` 的实现与嵌套

> 本挑战由丙（2412440-王朝阳）完成。结合 `kern/sync/sync.h`、`kern/driver/intr.c`
> 以及 `schedule()` / `proc_run()` 的实际调用关系，说明“保存并关闭中断、再恢复”的原理，
> 重点解释**为什么嵌套调用时不会把中断提前打开**。

## 4.1 代码与硬件基础

`local_intr_save` / `local_intr_restore` 不是两个普通函数，而是对 `sstatus.SIE` 位的一次
“读—改—写”封装：

```c
// kern/sync/sync.h
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

#define local_intr_save(x)    do { x = __intr_save(); } while (0)
#define local_intr_restore(x) __intr_restore(x);
```

```c
// kern/driver/intr.c
void intr_enable(void)  { set_csr(sstatus, SSTATUS_SIE); }    // SIE = 1
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }  // SIE = 0
```

`SSTATUS_SIE` 是 S 模式中断使能位：

- `SIE == 1`：S 模式可以响应已使能的中断（本实验主要就是 S 模式时钟中断）；
- `SIE == 0`：S 模式屏蔽一切可屏蔽中断。

`read_csr(sstatus)` 读的是**当前**的使能状态，而不是某个“引用计数”。
`__intr_save()` 的返回值恰好回答了一个问题：**“在我关中断之前，中断本来是开着的吗？”**

- 本来是开的：`intr_disable()` 后返回 `true`；
- 本来就是关的：不做任何事，返回 `false`。

`__intr_restore(flag)` 只在 `flag == true` 时重新开中断。**恢复的是调用者进入临界区之前的状态，
而不是无条件开中断**，这是嵌套正确的关键。

## 4.2 为什么这样就能嵌套

设初始 `SIE = 1`，函数 A 先进入临界区，内部调用函数 B，B 也进入临界区：

| 步骤 | 操作 | 保存的 flag | 当前 `SIE` |
|---|---|---|---|
| 1 | A: `local_intr_save(flagA)` | `flagA = true` | 0 |
| 2 | B: `local_intr_save(flagB)` | `flagB = false`（读到的已经是 0） | 0 |
| 3 | B 的临界区 | — | 0 |
| 4 | B: `local_intr_restore(flagB)` | — | 0（`false`，不动作） |
| 5 | A 的临界区继续 | — | 0 |
| 6 | A: `local_intr_restore(flagA)` | — | 1（`true`，重新开中断） |

关键在第 2 步：内层 `__intr_save()` 读到 `SIE == 0`，返回 `false`。
第 4 步内层恢复时看到 `false`，**不会**执行 `intr_enable()`，所以中断不会在 A 的临界区中间被提前打开。
只有最外层 A 恢复时，才会把中断真正打开。

这就是“保存的是**旧状态**而不是‘关中断次数’”带来的效果：

- 不需要维护嵌套计数器；
- 每一层各存一个 `bool`，天然支持任意深度嵌套；
- 只要 `save` / `restore` 成对出现，最外层恢复后的状态一定等于进入临界区之前的状态。

反过来，如果写成“保存时无条件 `intr_disable()`、恢复时无条件 `intr_enable()`”，
内层恢复就会在 A 的临界区里把中断打开，A 的原子性被破坏——这正是很多并发 bug 的来源。

## 4.3 在 `schedule()` / `proc_run()` 中的实际嵌套

`schedule()` 先关中断，然后在关中断状态下调用 `proc_run()`，而 `proc_run()` 自己也会
`local_intr_save`：

```text
schedule()
  local_intr_save(flag1)      // flag1 = true（假设原本开着）
    current->need_resched = 0
    遍历 proc_list ...
    proc_run(next)
      local_intr_save(flag2)  // 读到 SIE=0，flag2 = false
        current = next
        write_csr(satp, ...)
        flush_tlb()
        switch_to(...)
      local_intr_restore(flag2)  // false，不动作
  local_intr_restore(flag1)      // true，恢复开中断
```

这段嵌套有两个容易忽略的细节：

1. **`proc_run` 是“可被单独调用”的。** 它的调用者不一定已经关中断，
   所以它自己必须 `save/restore`，不能假设“调用者会帮我关”。
2. **`switch_to` 在关中断状态下完成。** 切换过程中 `current`、`satp`、TLB、寄存器栈帧
   正在被改写，此时如果来中断，中断处理会看到半更新的状态。
   `switch_to` 返回后，`proc_run` 内层恢复看到 `false` 不动作，直到回到 `schedule` 外层才恢复中断。
   因此整个“选择 + 切换”是一个不可分割的临界区。

## 4.4 与 Trap 返回路径的关系

`local_intr_restore` 只是把 `sstatus.SIE` 置 1。真正的“从中断返回并恢复中断使能”由 `sret` 完成：

- 进入 Trap 时，硬件把 `sstatus.SIE` 保存到 `sstatus.SPIE`，并清 `SIE`；
- Trap 处理期间 `SIE == 0`，不会被同类中断重入（除非显式开中断）；
- `sret` 时，硬件用 `SPIE` 恢复 `SIE`，回到被打断代码的中断使能状态。

所以 `local_intr_save/restore` 和 Trap 是两套互补的机制：

- Trap 管“被动进入内核时”的现场保存与恢复；
- `local_intr_save/restore` 管“内核主动进入临界区时”的短暂屏蔽。

要注意：`local_intr_restore(true)` 之后，如果当前仍在 Trap 处理上下文中，
中断也可能立即被响应（取决于 `SPIE` 与 `SIE` 的组合）。
因此临界区应当尽量短，恢复后不要假设“还会再执行很多条指令才被中断”。

## 4.5 这套实现的边界与不足

1. **只关 S 模式中断。** `SSTATUS.SIE` 只影响 S 模式；M 模式中断、不可屏蔽中断
   （如硬件错误）不受影响。本实验运行在 S 模式，够用；真实内核还要考虑 NMI。
2. **只适合单核。** 它保护的是“本 hart 不被中断”，不能阻止另一个 hart 同时访问共享数据。
   多核需要自旋锁（`spinlock`）或每 CPU 的 `irqsave` 标志。
3. **没有内存屏障。** `clear_csr` / `set_csr` 只改 CSR，不保证编译器和 CPU 不重排临界区内的访存。
   在弱内存模型下，共享数据的发布/获取还需要 `fence` 或 `__atomic` 语义。
4. **flag 必须由调用者保存，且不能跨函数丢失。** `local_intr_save(x)` 把状态写进局部变量 `x`；
   如果 `x` 被覆盖、或者 `save` 与 `restore` 不成对，状态就会错乱。
5. **临界区过长会恶化中断延迟。** 本实验的遍历很短，影响可忽略；真实调度器应把
   “选择进程”和“切换进程”尽量拆细，只在必要的短区间关中断。
6. **不能在“中断本来就没开”的场景下误以为已经保护了并发。** 它只防中断，
   不防异常、不防其他 hart、也不防编译器优化带来的重排。

## 4.6 可行的改进方向

- 用 `spinlock` + `local_intr_save` 组合，既防本核中断也防多核竞争；
- 把中断状态保存为每 CPU 变量或 `unsigned long flags`（类似 Linux 的 `irqsave`），
  支持在更深的调用层次中传递；
- 临界区内避免 `cprintf`、`kmalloc` 等可能睡眠或耗时的操作；
- 对共享数据使用 `READ_ONCE` / `WRITE_ONCE` 或 `fence`，补齐内存序语义；
- 在调试构建里增加 `SIE` 状态断言，检测 `save/restore` 是否配对。

## 4.7 小结

`local_intr_save/restore` 的精髓只有一句话：
**保存“进入临界区之前的中断状态”，恢复时只恢复到那个状态。**

- 内层读到“已经关中断”，就返回 `false`，恢复时保持关中断；
- 外层读到“原本开中断”，就返回 `true`，恢复时重新开中断。

因此它不需要嵌套计数，就能正确支持多层嵌套；配合 `schedule()`/`proc_run()` 的调用结构，
保证了“选择进程 + 切换地址空间 + 切换寄存器”这一整段操作的原子性。
