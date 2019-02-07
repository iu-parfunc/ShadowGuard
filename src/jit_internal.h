
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

// Jump table dispatch logic for push operation.
//
// We align jump table start and each jump table slot to 32 byte boundaries
// using assembler align directives. It is assumed that each jump table slot
// takes less than 32 bytes. If this assumption becomes false the alignments and
// jump table dispatch will require changing. Currently jump table dispatch
// takes more than 32 bytes so we align the first jump table slot to the next 64
// byte boundary and adjust the jump target offset by incrementing it by 2 to
// skip over the dispatch code.
//
// Parameters :
//   a - asmjit::X86Assembler* instance
//   sp - Stackpointer XMM register
#define JIT_DISPATCH_PUSH(a, sp)                                       \
  a->align(0 /* code-alignment */, 32);                                \
  /* Make jump base address to be the start of dispatch code. */       \
  /* We deduct the size of lea instruction (7) from $rip to get it. */ \
  a->lea(rax, ptr(rip, -7));                                           \
  a->vpextrq(r11, sp, asmjit::imm(0));                                 \
                                                                       \
  /* Account for dispatch in the offset index */                       \
  a->add(r11, 2);                                                      \
                                                                       \
  /* Calculate jump target */                                          \
  a->imul(r11, asmjit::imm(32));                                       \
  a->lea(rax, ptr(rax, r11));                                          \
                                                                       \
  /* Increment stack pointer */                                        \
  a->vpextrq(r11, sp, asmjit::imm(0));                                 \
  a->inc(r11);                                                         \
  a->pinsrq(sp, r11, asmjit::imm(0));                                  \
                                                                       \
  /* Dispatch to jump table */                                         \
  a->jmp(rax);                                                         \
  /* Align first jump table slot to next 64 byte boundary */           \
  a->align(0 /* code-alignment */, 64);

// Jump table dispatch logic for pop operation
//
// We align jump table start and each jump table slot to 32 byte boundaries
// using assembler align directives. It is assumed that each jump table slot
// takes less than 32 bytes. If this assumption becomes false the alignments and
// jump table dispatch will require changing. Currently jump table dispatch
// takes more than 32 bytes so we align the first jump table slot to the next 64
// byte boundary and adjust the jump target offset index by incrementing it by 2
// to skip over the dispatch code.
//
// Parameters:
//   a - asmjit::X86Assembler* instance
//   sp - Stackpointer XMM register
#define JIT_DISPATCH_POP(a, sp)                                        \
  a->align(0 /* code-alignment */, 32);                                \
  /* Make jump base address to be the start of dispatch code. */       \
  /* We deduct the size of lea instruction (7) from $rip to get it. */ \
  a->lea(rdi, ptr(rip, -7));                                           \
                                                                       \
  /* Decrement stack pointer */                                        \
  a->vpextrq(r11, sp, asmjit::imm(0));                                 \
  a->sub(r11, asmjit::imm(1));                                         \
  a->pinsrq(sp, r11, asmjit::imm(0));                                  \
                                                                       \
  /* Account for dispatch in the offset index */                       \
  a->add(r11, 2);                                                      \
                                                                       \
  /* Calculate jump target */                                          \
  a->imul(r11, asmjit::imm(32));                                       \
  a->lea(rdi, ptr(rdi, r11));                                          \
                                                                       \
  /* Dispatch to jump table */                                         \
  a->jmp(rdi);                                                         \
  /* Align first jump table slot to next 64 byte boundary */           \
  a->align(0 /* code-alignment */, 64);

// Check and return sequence which follows a stack pop. Here we assume stack has
// been pop'd at rdi
#define JIT_POP_RET_SEQ(a, end, error) \
  a->cmp(rdi, ptr(rsp));               \
  a->jne(error);                       \
  a->jmp(end);

void JitAvx2StackInit(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitAvx2StackPush(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitAvx2StackPop(RegisterUsageInfo& info, AssemblerHolder& ah);

void JitAvx512StackInit(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitAvx512StackPush(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitAvx512StackPop(RegisterUsageInfo& info, AssemblerHolder& ah);

void JitMemoryStackPush(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitMemoryStackPop(RegisterUsageInfo& info, AssemblerHolder& ah);

void JitXorProlog(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitXorEpilog(RegisterUsageInfo& info, AssemblerHolder& ah);

#endif  // LITECFI_JIT_INTERNAL_H_
