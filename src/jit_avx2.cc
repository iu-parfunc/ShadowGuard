
#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit_internal.h"
#include "register_utils.h"

using namespace asmjit::x86;

#define NOP_PAD(a, n) \
  int iters = n;      \
  while (iters > 0) { \
    a->nop();         \
    iters--;          \
  }

#define JIT_PUSH_AVX2(a, end, sp, scratch, xmm_reg, ymm_reg, i1, i2, i3, i4) \
  case i1: {                                                                 \
    a->pinsrq(xmm_reg, ptr(rsp), asmjit::imm(0));                            \
    a->jmp(end);                                                             \
    NOP_PAD(a, 9)                                                            \
    break;                                                                   \
  }                                                                          \
  case i2: {                                                                 \
    a->pinsrq(xmm_reg, ptr(rsp), asmjit::imm(1));                            \
    a->jmp(end);                                                             \
    NOP_PAD(a, 9)                                                            \
    break;                                                                   \
  }                                                                          \
  case i3: {                                                                 \
    a->vmovq(scratch.xmm, ptr(rsp));                                         \
    a->vinserti128(scratch.ymm, ymm_reg, scratch.xmm, asmjit::imm(1));       \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(48));             \
    a->jmp(end);                                                             \
    break;                                                                   \
  }                                                                          \
  case i4: {                                                                 \
    a->vmovq(scratch.xmm, ptr(rsp));                                         \
    a->vpbroadcastq(scratch.ymm, scratch.xmm);                               \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(192));            \
    a->jmp(end);                                                             \
    NOP_PAD(a, 1)                                                            \
    break;                                                                   \
  }

#define JIT_DISPATCH_PUSH(a, sp)       \
  /* Calculate jump target */          \
  a->vpextrq(r11, sp, asmjit::imm(0)); \
  a->imul(r11, asmjit::imm(33));       \
  a->lea(rax, ptr(rip, 6));            \
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

void JitAvx2StackPush(RegisterUsageInfo& info, asmjit::X86Assembler* a) {
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  AvxRegister scratch = GetNextUnusedAvx2Register(info);

  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;

  // Unused quadword element indices in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);

  asmjit::Label end = a->newLabel();

  // Jump table dispatch
  JIT_DISPATCH_PUSH(a, sp);

  for (unsigned int i = 0; i < quad_words.size(); i++) {
    switch (quad_words[i]) {
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm0, ymm0, 0, 1, 2, 3)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm1, ymm1, 4, 5, 6, 7)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm2, ymm2, 8, 9, 10, 11)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm3, ymm3, 12, 13, 14, 15)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm4, ymm4, 16, 17, 18, 19)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm5, ymm5, 20, 21, 22, 23)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm6, ymm6, 24, 25, 26, 27)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm7, ymm7, 28, 29, 30, 31)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm8, ymm8, 32, 33, 34, 35)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm9, ymm9, 36, 37, 38, 39)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm10, ymm10, 40, 41, 42, 43)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm11, ymm11, 44, 45, 46, 47)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm12, ymm12, 48, 49, 50, 51)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm13, ymm13, 52, 53, 54, 55)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm14, ymm14, 56, 57, 58, 59)
      JIT_PUSH_AVX2(a, end, sp, scratch, xmm15, ymm15, 60, 61, 62, 63)
    }
  }

  a->bind(end);
}

#define JIT_POP_RET_SEQ(a, end, error) \
  a->cmp(rdi, ptr(rsp));               \
  a->jne(error);                       \
  a->jmp(end);

#define JIT_POP_AVX2(a, end, error, sp, scratch, xmm_reg, ymm_reg, i1, i2, i3, \
                     i4)                                                       \
  case i1: {                                                                   \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(0));                                  \
    JIT_POP_RET_SEQ(a, end, error)                                             \
    NOP_PAD(a, 11)                                                             \
    break;                                                                     \
  }                                                                            \
  case i2: {                                                                   \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(1));                                  \
    JIT_POP_RET_SEQ(a, end, error)                                             \
    NOP_PAD(a, 11)                                                             \
    break;                                                                     \
  }                                                                            \
  case i3: {                                                                   \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(0));                              \
    JIT_POP_RET_SEQ(a, end, error)                                             \
    NOP_PAD(a, 5)                                                              \
    break;                                                                     \
  }                                                                            \
  case i4: {                                                                   \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(1));                              \
    JIT_POP_RET_SEQ(a, end, error)                                             \
    NOP_PAD(a, 5)                                                              \
    break;                                                                     \
  }

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

void JitAvx2StackPop(RegisterUsageInfo& info, asmjit::X86Assembler* a) {
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  AvxRegister scratch = GetNextUnusedAvx2Register(info);

  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;

  // Unused quadword elements in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);

  asmjit::Label error = a->newLabel();
  asmjit::Label end = a->newLabel();

  // Jump table dispatch
  JIT_DISPATCH_POP(a, sp);

  for (unsigned int i = 0; i < quad_words.size(); i++) {
    switch (quad_words[i]) {
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm0, ymm0, 0, 1, 2, 3)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm1, ymm1, 4, 5, 6, 7)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm2, ymm2, 8, 9, 10, 11)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm3, ymm3, 12, 13, 14, 15)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm4, ymm4, 16, 17, 18, 19)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm5, ymm5, 20, 21, 22, 23)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm6, ymm6, 24, 25, 26, 27)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm7, ymm7, 28, 29, 30, 31)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm8, ymm8, 32, 33, 34, 35)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm9, ymm9, 36, 37, 38, 39)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm10, ymm10, 40, 41, 42, 43)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm11, ymm11, 44, 45, 46, 47)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm12, ymm12, 48, 49, 50, 51)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm13, ymm13, 52, 53, 54, 55)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm14, ymm14, 56, 57, 58, 59)
      JIT_POP_AVX2(a, end, error, sp, scratch, xmm15, ymm15, 60, 61, 62, 63)
    }
  }

  a->bind(error);
  a->int3();
  a->bind(end);
}
