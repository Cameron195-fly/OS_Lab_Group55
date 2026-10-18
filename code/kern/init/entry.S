#include <mmu.h>
#include <memlayout.h>

    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    # a0: hartid
    # a1: dtb physical address
    # save hartid and dtb address
    la t0, boot_hartid
    sd a0, 0(t0)
    la t0, boot_dtb
    sd a1, 0(t0)

    # t0 := 三级页表的虚拟地址
    lui     t0, %hi(boot_page_table_sv39)
    # t1 := 0xffffffff40000000 即虚实映射偏移量
    li      t1, 0xffffffffc0000000 - 0x80000000
    # t0 减去虚实映射偏移量 0xffffffff40000000，变为三级页表的物理地址
    sub     t0, t0, t1
    # t0 >>= 12，变为三级页表的物理页号
    srli    t0, t0, 12

    # t1 := 8 << 60，设置 satp 的 MODE 字段为 Sv39
    li      t1, 8 << 60
    # 将刚才计算出的预设三级页表物理页号附加到 satp 中
    or      t0, t0, t1
    # 将算出的 t0(即新的MODE|页表基址物理页号) 覆盖到 satp 中
    csrw    satp, t0
    # 使用 sfence.vma 指令刷新 TLB
    sfence.vma
    # 从此，我们给内核搭建出了一个完美的虚拟内存空间！
    #nop # 可能映射的位置有些bug。。插入一个nop
    
    # 我们在虚拟内存空间中：随意将 sp 设置为虚拟地址！
    lui sp, %hi(bootstacktop)

    # 我们在虚拟内存空间中：随意跳转到虚拟地址！
    # 跳转到 kern_init
    lui t0, %hi(kern_init)
    addi t0, t0, %lo(kern_init)
    jr t0

.section .data
    # .align 2^12
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:

.section .data
    # 由于我们要把这个页表放到一个页里面，因此必须 12 位对齐
    .align PGSHIFT
    .global boot_page_table_sv39
# 分配 4KiB 内存给预设的三级页表
boot_page_table_sv39:
    # [511] 0xffffffff_c0000000 -> 0x80000000 (1G)  内核运行用的高地址映射
    # [2]   0x00000000_80000000 -> 0x80000000 (1G)  过渡期用的低地址恒等映射
    #
    # 为什么需要 [2]：csrw satp 之后 MMU 立刻开始翻译"当前 PC"，而此刻 PC 还是
    # 物理地址 0x80200038（sfence.vma 那一条），只映射高地址就会 exec_page_fault。
    # QEMU 4.1.1 里写 satp 不打断当前 translation block，靠"一条 TB 直接跑到
    # jr t0"侥幸能过；QEMU >= 5.x 修正了 TB 刷新，就必须让 0x80xxxxxx 这段在
    # 过渡期也可取指。该映射对 QEMU 4.1.1 同样安全。
    #
    # 页表项标志位（RISC-V Sv39 PTE）：
    #   bit0 V, bit1 R, bit2 W, bit3 X, bit4 U, bit5 G, bit6 A, bit7 D
    #   0xcf = 0b1100_1111 = V|R|W|X|A|D —— 可读写可执行、A/D 已置位、U=0、G=0。
    #   U 必须为 0：S 模式不允许从 U=1 的页面取指（新版 QEMU 的
    #   access_type == MMU_INST_FETCH 分支会直接 TRANSLATE_FAIL）。
    .zero 8 * 2                          # [0], [1] : unmapped
    .quad (0x80000 << 10) | 0xcf         # [2]   : 低 1GiB 恒等映射（过渡用）
    .zero 8 * (511 - 3)                  # [3] .. [510] : unmapped
    .quad (0x80000 << 10) | 0xcf         # [511] : 0xffffffff_c0000000 -> 0x80000000

    .global boot_hartid
boot_hartid:
    .quad 0
    .global boot_dtb
boot_dtb:
    .quad 0
