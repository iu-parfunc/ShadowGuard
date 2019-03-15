.intel_syntax noprefix

#if defined(__linux) && defined(__GNUC__) && defined(__x86_64__)
# define ASM_HIDDEN(symbol) .hidden symbol
# define ASM_TYPE_FUNCTION(symbol) .type symbol, @function
# define ASM_SIZE(symbol) .size symbol, .-symbol
# define ASM_SYMBOL(symbol) symbol
# define NO_EXEC_STACK_DIRECTIVE .section .note.GNU-stack,"",%progbits

# define CFI_DEF_CFA_OFFSET(n) .cfi_def_cfa_offset n
# define CFI_STARTPROC .cfi_startproc
# define CFI_ENDPROC .cfi_endproc
#else
# We only support x86_64 on Linux currently
#endif

  .globl ASM_SYMBOL(litecfi_avx2_stack_init)
  ASM_TYPE_FUNCTION(litecfi_avx2_stack_init)
ASM_SYMBOL(litecfi_avx2_stack_init):
CFI_STARTPROC
lea r11, litecfi_avx2_stack_push[rip]
pinsrq xmm15, r11, 0
lea r11, litecfi_avx2_stack_pop[rip]
pinsrq xmm15, r11, 1
ret
.align 64
ASM_SIZE(litecfi_avx2_stack_init)
CFI_ENDPROC

  ASM_HIDDEN(litecfi_avx2_stack_push)
  ASM_TYPE_FUNCTION(litecfi_avx2_stack_push)
ASM_SYMBOL(litecfi_avx2_stack_push):
CFI_STARTPROC
lea r11, [r11+64]
pinsrq xmm15, r11, 0
pinsrq xmm13, [r10], 0
ret
.align 64
lea r11, [r11+64]
pinsrq xmm15, r11, 0
pinsrq xmm13, [r10], 1
vpextrq r11, xmm15, 1
lea r11, [r11+64]
pinsrq xmm15, r11, 1
ret
.align 64
lea r11, [r11+64]
pinsrq xmm15, r11, 0
pinsrq xmm12, [r10], 0
vpextrq r11, xmm15, 1
lea r11, [r11+64]
pinsrq xmm15, r11, 1
ret
.align 64
lea r11, [r11+64]
pinsrq xmm15, r11, 0
pinsrq xmm12, [r10], 1
vpextrq r11, xmm15, 1
lea r11, [r11+64]
pinsrq xmm15, r11, 1
ret
.align 64
push rax
push rdx
push rcx
push rdi
mov rdi, r10
call litecfi_overflow_stack_push@plt
pop rdi
pop rcx
pop rdx
pop rax
ret
.align 64
ASM_SIZE(litecfi_avx2_stack_push)
CFI_ENDPROC

  ASM_HIDDEN(litecfi_avx2_stack_pop)
  ASM_TYPE_FUNCTION(litecfi_avx2_stack_pop)
ASM_SYMBOL(litecfi_avx2_stack_pop):
CFI_STARTPROC
vpextrq r11, xmm15, 0
lea r11, [r11-64]
pinsrq xmm15, r11, 0
vpextrq r11, xmm13, 0
cmp r11, [r10]
jne L0
ret
L0:
int3
.align 64
lea r11, [r11-64]
pinsrq xmm15, r11, 1
vpextrq r11, xmm15, 0
lea r11, [r11-64]
pinsrq xmm15, r11, 0
vpextrq r11, xmm13, 1
cmp r11, [r10]
jne L1
ret
L1:
int3
.align 64
lea r11, [r11-64]
pinsrq xmm15, r11, 1
vpextrq r11, xmm15, 0
lea r11, [r11-64]
pinsrq xmm15, r11, 0
vpextrq r11, xmm12, 0
cmp r11, [r10]
jne L2
ret
L2:
int3
.align 64
lea r11, [r11-64]
pinsrq xmm15, r11, 1
vpextrq r11, xmm15, 0
lea r11, [r11-64]
pinsrq xmm15, r11, 0
vpextrq r11, xmm12, 1
cmp r11, [r10]
jne L3
ret
L3:
int3
.align 64
push rax
push rdx
push rcx
push rdi
mov rdi, r10
call litecfi_overflow_stack_pop@plt
pop rdi
pop rcx
pop rdx
pop rax
ret
.align 64
ASM_SIZE(litecfi_avx2_stack_pop)
CFI_ENDPROC

NO_EXEC_STACK_DIRECTIVE
