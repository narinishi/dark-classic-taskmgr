.intel_syntax noprefix
.section .text.entry,"ax"
.extern orig_ctor
.extern orig_ret
.extern dark_init
.global entry_stub

entry_stub:
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    push rbx
    sub rsp, 0x28
    call orig_ctor
    call dark_init
    add rsp, 0x28
    pop rbx
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    jmp orig_ret
