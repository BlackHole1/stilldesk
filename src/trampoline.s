    .section __TEXT,__text,regular,pure_instructions

// ---------------------------------------------------------------------------
// gate_trampoline
//
// 由写在镜像偏移 0x44cb34 的 16 字节分支岛（"br x9"）进入，
// 位置在 MainDlgPresetDialogLogic::HandlePresetDialogCandidate(owner, type,
// errorcode) 内部。此时函数自己的栈帧已经分配好，寄存器为
// x0=this, x1=&owner, x2=type, x3=errorcode。
//
// 分支岛替换掉的就是这四条指令：
//      mov x20, x3        ; errorcode
//      mov x21, x2        ; type
//      mov x24, x1        ; owner
//      mov x22, x0        ; this
// ---------------------------------------------------------------------------
    .globl _gate_trampoline
    .p2align 2
_gate_trampoline:
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    stp  x0, x1, [sp, #-16]!
    stp  x2, x3, [sp, #-16]!
    bl   _gate_should_block
    mov  w8, w0
    ldp  x2, x3, [sp], #16
    ldp  x0, x1, [sp], #16
    ldp  x29, x30, [sp], #16

    // 重放被覆盖的四条指令
    mov  x20, x3
    mov  x21, x2
    mov  x24, x1
    mov  x22, x0

    cbnz w8, 1f
    // 放行：紧接这四条指令继续执行原函数体
    adrp x9, _gate_continue_addr@PAGE
    ldr  x9, [x9, _gate_continue_addr@PAGEOFF]
    br   x9
1:
    // 拦截：执行函数自己的尾声（所有出口都走这里）
    add  sp, sp, #0x340
    ldp  x29, x30, [sp, #0x50]
    ldp  x20, x19, [sp, #0x40]
    ldp  x22, x21, [sp, #0x30]
    ldp  x24, x23, [sp, #0x20]
    ldp  x26, x25, [sp, #0x10]
    ldp  x28, x27, [sp], #0x60
    ret

// ---------------------------------------------------------------------------
// cfg_trampoline（仅诊断用）
//
// 镜像偏移 0x0c90b6c，CSettingLogic::DealPresetDialogConfig(std::string const&)
// 的前八条指令，在下面重放；之后从 0x0c90b8c 继续执行。
// ---------------------------------------------------------------------------
    .globl _cfg_trampoline
    .p2align 2
_cfg_trampoline:
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    stp  x0, x1, [sp, #-16]!
    mov  x0, x1
    bl   _cfg_dump
    ldp  x0, x1, [sp], #16
    ldp  x29, x30, [sp], #16

    stp  x28, x27, [sp, #-0x60]!
    stp  x26, x25, [sp, #0x10]
    stp  x24, x23, [sp, #0x20]
    stp  x22, x21, [sp, #0x30]
    stp  x20, x19, [sp, #0x40]
    stp  x29, x30, [sp, #0x50]
    add  x29, sp, #0x50
    sub  sp, sp, #0x2b0

    adrp x9, _cfg_continue_addr@PAGE
    ldr  x9, [x9, _cfg_continue_addr@PAGEOFF]
    br   x9
