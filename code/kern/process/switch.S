//LAB4 function3 2412440
    .text
    .globl switch_to
    .align 2
switch_to:
    # switch_to(from = a0, to = a1)
    # struct context: ra, sp, s0~s11，共 14 个 8 字节字段
    # 只保存 callee-saved 寄存器，调用者保存寄存器由编译器负责

    # 1) 把当前（from）的上下文保存到 a0 指向的 context
    sd ra,  0*8(a0)
    sd sp,  1*8(a0)
    sd s0,  2*8(a0)
    sd s1,  3*8(a0)
    sd s2,  4*8(a0)
    sd s3,  5*8(a0)
    sd s4,  6*8(a0)
    sd s5,  7*8(a0)
    sd s6,  8*8(a0)
    sd s7,  9*8(a0)
    sd s8, 10*8(a0)
    sd s9, 11*8(a0)
    sd s10, 12*8(a0)
    sd s11, 13*8(a0)

    # 2) 从 a1 指向的 context 恢复目标（to）的上下文
    ld ra,  0*8(a1)
    ld sp,  1*8(a1)
    ld s0,  2*8(a1)
    ld s1,  3*8(a1)
    ld s2,  4*8(a1)
    ld s3,  5*8(a1)
    ld s4,  6*8(a1)
    ld s5,  7*8(a1)
    ld s6,  8*8(a1)
    ld s7,  9*8(a1)
    ld s8, 10*8(a1)
    ld s9, 11*8(a1)
    ld s10, 12*8(a1)
    ld s11, 13*8(a1)

    ret
