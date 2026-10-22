#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

/* Buddy System 物理内存管理器（lab2 任务三）。
 * 管理器注册为 buddy_pmm_manager，但**不**被设为活动管理器：
 * make grade 要求活动管理器是 Best-Fit，因此 buddy 通过 buddy_check() 自检。
 */
extern const struct pmm_manager buddy_pmm_manager;

/* 自检：从当前活动管理器借一段私有页区间跑测试，结束后原样归还 */
void buddy_check(void);

#endif /* !__KERN_MM_BUDDY_PMM_H__ */
