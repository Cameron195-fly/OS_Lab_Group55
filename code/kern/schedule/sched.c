#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2412440
// schedule - 从 proc_list 中轮转选择一个 PROC_RUNNABLE 进程投入运行
void
schedule(void) {
    bool intr_flag;
    list_entry_t *head = &proc_list;
    list_entry_t *le, *start;
    struct proc_struct *next = NULL;

    local_intr_save(intr_flag);

    // 主动让出 CPU 后清除重调度标志
    current->need_resched = 0;

    // 当前是 idleproc 时它不在 proc_list 中，从链表头开始；
    // 否则从当前进程的下一个节点开始，实现轮转
    start = (current == idleproc) ? list_next(head) : list_next(&(current->list_link));
    le = start;

    // 遍历整圈（含跨过哨兵 head）：跳过哨兵，找第一个可运行进程
    if (!list_empty(head)) {
        do {
            if (le != head) {
                struct proc_struct *proc = le2proc(le, list_link);
                if (proc->state == PROC_RUNNABLE && proc != current) {
                    next = proc;
                    break;
                }
            }
            le = list_next(le);
        } while (le != start);
    }

    // 没有其它可运行进程时回退到 idleproc
    if (next == NULL) {
        next = idleproc;
    }

    proc_run(next);

    local_intr_restore(intr_flag);
}
