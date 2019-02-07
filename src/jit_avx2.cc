
#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit.h"
#include "jit_internal.h"
#include "register_utils.h"

using namespace asmjit::x86;

void JitAvx2StackInit(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  a->pxor(meta.xmm, meta.xmm);
}

#define JIT_PUSH_AVX2(a, end, scratch, xmm_reg, ymm_reg, i1, i2, i3, i4) \
  case i1: {                                                             \
    a->align(0 /* code-alignment */, 32);                                \
    a->pinsrq(xmm_reg, ptr(rsp), asmjit::imm(0));                        \
    a->jmp(end);                                                         \
    break;                                                               \
  }                                                                      \
  case i2: {                                                             \
    a->align(0 /* code-alignment */, 32);                                \
    a->pinsrq(xmm_reg, ptr(rsp), asmjit::imm(1));                        \
    a->jmp(end);                                                         \
    break;                                                               \
  }                                                                      \
  case i3: {                                                             \
    a->align(0 /* code-alignment */, 32);                                \
    a->vmovq(scratch.xmm, ptr(rsp));                                     \
    a->vinserti128(scratch.ymm, ymm_reg, scratch.xmm, asmjit::imm(1));   \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(48));         \
    a->jmp(end);                                                         \
    break;                                                               \
  }                                                                      \
  case i4: {                                                             \
    a->align(0 /* code-alignment */, 32);                                \
    a->vmovq(scratch.xmm, ptr(rsp));                                     \
    a->vpbroadcastq(scratch.ymm, scratch.xmm);                           \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(192));        \
    a->jmp(end);                                                         \
    break;                                                               \
  }

void JitAvx2StackPush(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  // Jump table dispatch
  JIT_DISPATCH_PUSH(a, sp);

  AvxRegister scratch = GetNextUnusedAvx2Register(info);
  asmjit::Label end = a->newLabel();
  // Unused quadword element indices in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);
  for (unsigned int i = 0; i < quad_words.size(); i++) {
    switch (quad_words[i]) {
      JIT_PUSH_AVX2(a, end, scratch, xmm0, ymm0, 0, 1, 2, 3)
      JIT_PUSH_AVX2(a, end, scratch, xmm1, ymm1, 4, 5, 6, 7)
      JIT_PUSH_AVX2(a, end, scratch, xmm2, ymm2, 8, 9, 10, 11)
      JIT_PUSH_AVX2(a, end, scratch, xmm3, ymm3, 12, 13, 14, 15)
      JIT_PUSH_AVX2(a, end, scratch, xmm4, ymm4, 16, 17, 18, 19)
      JIT_PUSH_AVX2(a, end, scratch, xmm5, ymm5, 20, 21, 22, 23)
      JIT_PUSH_AVX2(a, end, scratch, xmm6, ymm6, 24, 25, 26, 27)
      JIT_PUSH_AVX2(a, end, scratch, xmm7, ymm7, 28, 29, 30, 31)
      JIT_PUSH_AVX2(a, end, scratch, xmm8, ymm8, 32, 33, 34, 35)
      JIT_PUSH_AVX2(a, end, scratch, xmm9, ymm9, 36, 37, 38, 39)
      JIT_PUSH_AVX2(a, end, scratch, xmm10, ymm10, 40, 41, 42, 43)
      JIT_PUSH_AVX2(a, end, scratch, xmm11, ymm11, 44, 45, 46, 47)
      JIT_PUSH_AVX2(a, end, scratch, xmm12, ymm12, 48, 49, 50, 51)
      JIT_PUSH_AVX2(a, end, scratch, xmm13, ymm13, 52, 53, 54, 55)
      JIT_PUSH_AVX2(a, end, scratch, xmm14, ymm14, 56, 57, 58, 59)
      JIT_PUSH_AVX2(a, end, scratch, xmm15, ymm15, 60, 61, 62, 63)
    }
  }

  a->bind(end);
}

#define JIT_POP_AVX2(a, end, error, scratch, xmm_reg, ymm_reg, i1, i2, i3, i4) \
  case i1: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(0));                                  \
    JIT_POP_RET_SEQ(a, end, error)                                             \
    break;                                                                     \
  }                                                                            \
  case i2: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(1));                                  \
    JIT_POP_RET_SEQ(a, end, error)                                             \
    break;                                                                     \
  }                                                                            \
  case i3: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(0));                              \
    JIT_POP_RET_SEQ(a, end, error)                                             \
    break;                                                                     \
  }                                                                            \
  case i4: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(1));                              \
    JIT_POP_RET_SEQ(a, end, error)                                             \
    break;                                                                     \
  }

void JitAvx2StackPop(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  // Jump table dispatch
  JIT_DISPATCH_POP(a, sp);

  AvxRegister scratch = GetNextUnusedAvx2Register(info);
  asmjit::Label error = a->newLabel();
  asmjit::Label end = a->newLabel();
  // Unused quadword elements in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);
  for (unsigned int i = 0; i < quad_words.size(); i++) {
    switch (quad_words[i]) {
      JIT_POP_AVX2(a, end, error, scratch, xmm0, ymm0, 0, 1, 2, 3)
      JIT_POP_AVX2(a, end, error, scratch, xmm1, ymm1, 4, 5, 6, 7)
      JIT_POP_AVX2(a, end, error, scratch, xmm2, ymm2, 8, 9, 10, 11)
      JIT_POP_AVX2(a, end, error, scratch, xmm3, ymm3, 12, 13, 14, 15)
      JIT_POP_AVX2(a, end, error, scratch, xmm4, ymm4, 16, 17, 18, 19)
      JIT_POP_AVX2(a, end, error, scratch, xmm5, ymm5, 20, 21, 22, 23)
      JIT_POP_AVX2(a, end, error, scratch, xmm6, ymm6, 24, 25, 26, 27)
      JIT_POP_AVX2(a, end, error, scratch, xmm7, ymm7, 28, 29, 30, 31)
      JIT_POP_AVX2(a, end, error, scratch, xmm8, ymm8, 32, 33, 34, 35)
      JIT_POP_AVX2(a, end, error, scratch, xmm9, ymm9, 36, 37, 38, 39)
      JIT_POP_AVX2(a, end, error, scratch, xmm10, ymm10, 40, 41, 42, 43)
      JIT_POP_AVX2(a, end, error, scratch, xmm11, ymm11, 44, 45, 46, 47)
      JIT_POP_AVX2(a, end, error, scratch, xmm12, ymm12, 48, 49, 50, 51)
      JIT_POP_AVX2(a, end, error, scratch, xmm13, ymm13, 52, 53, 54, 55)
      JIT_POP_AVX2(a, end, error, scratch, xmm14, ymm14, 56, 57, 58, 59)
      JIT_POP_AVX2(a, end, error, scratch, xmm15, ymm15, 60, 61, 62, 63)
    }
  }

  a->bind(error);
  a->int3();
  a->bind(end);
}
