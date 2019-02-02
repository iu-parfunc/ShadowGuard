
#ifndef LITECFI_JIT_INTERNAL_H_
#define LITECFI_JIT_INTERNAL_H_

#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "jit.h"
#include "register_usage.h"

// Shadow stack implementation flag
DECLARE_string(shadow_stack);

// Generates a NOP pad with byte size n
#define NOP_PAD(a, n) \
  int iters = n;      \
  while (iters > 0) { \
    a->nop();         \
    iters--;          \
  }

// Jump table dispatch logic for push operation
// Parameters :
//   a - asmjit::X86Assembler* instance
//   sp - Stackpointer XMM register
#define JIT_DISPATCH_PUSH(a, sp)       \
  /* Calculate jump target */          \
  a->vpextrq(r11, sp, asmjit::imm(0)); \
  a->imul(r11, asmjit::imm(32));       \
  a->lea(rax, ptr(rip, 22));           \
  asmjit::X86Mem c = ptr(rax, r11);    \
  a->lea(rax, c);                      \
                                       \
  /* Increment stack pointer */        \
  a->vpextrq(r11, sp, asmjit::imm(0)); \
  a->inc(r11);                         \
  a->pinsrq(sp, r11, asmjit::imm(0));  \
                                       \
  /* Dispatch to jump table */         \
  a->jmp(rax);

// Jump table dispatch logic for pop operation
// Parameters:
//   a - asmjit::X86Assemblre* instance
//   sp - Stackpointer XMM register
#define JIT_DISPATCH_POP(a, sp)        \
  /* Decrement stack pointer */        \
  a->vpextrq(r11, sp, asmjit::imm(0)); \
  a->sub(r11, asmjit::imm(1));         \
  a->pinsrq(sp, r11, asmjit::imm(0));  \
                                       \
  /* Calculate jump target */          \
  a->imul(r11, asmjit::imm(32));       \
  a->lea(rdi, ptr(rip, 6));            \
                                       \
  /* Dispatch to jump table */         \
  asmjit::X86Mem c = ptr(rdi, r11);    \
  a->lea(rdi, c);                      \
  a->jmp(rdi);

// Check and return sequence which follows a stack pop. Here we assume stack has
// been pop'd at rdi
#define JIT_POP_RET_SEQ(a, end, error) \
  a->cmp(rdi, ptr(rsp));               \
  a->jne(error);                       \
  a->jmp(end);

void JitAvx2StackInit(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitAvx2StackPush(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitAvx2StackPop(RegisterUsageInfo& info, AssemblerHolder& ah);

void JitAvx512StackPush(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitAvx512StackPop(RegisterUsageInfo& info, AssemblerHolder& ah);

void JitMemoryStackPush(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitMemoryStackPop(RegisterUsageInfo& info, AssemblerHolder& ah);

void JitXorProlog(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitXorEpilog(RegisterUsageInfo& info, AssemblerHolder& ah);

#endif  // LITECFI_JIT_INTERNAL_H_
