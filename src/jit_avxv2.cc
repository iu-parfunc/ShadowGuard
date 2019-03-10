
#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit.h"
#include "jit_internal.h"
#include "register_utils.h"

using namespace asmjit::x86;

void JitAvxV2CallStackPush(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  a->vpextrq(r10, sp, asmjit::imm(0));
  a->jmp(r10);
}

void JitAvxV2CallStackPop(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  a->vpextrq(r10, sp, asmjit::imm(1));
  a->jmp(r10);
}

void JitAvxV2StackInit(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  a->pxor(meta.xmm, meta.xmm);
}

#define JIT_PUSH_AVX2(a, sp, first, scratch, xmm_reg, ymm_reg, i1, i2, i3, i4) \
  case i1: {                                                                   \
    a->pinsrq(xmm_reg, ptr(GetRaHolder()), asmjit::imm(0));                    \
    if (!first) {                                                              \
      a->jmp(8);                                                               \
      a->dq(64);                                                               \
      a->paddq(sp, ptr(rip, -16));                                             \
    }                                                                          \
    a->lea(GetRaHolder(), ptr(rip, 8));                                        \
    a->pinsrq(sp, GetRaHolder(), asmjit::imm(0));                              \
    a->ret();                                                                  \
    break;                                                                     \
  }                                                                            \
  case i2: {                                                                   \
    a->pinsrq(xmm_reg, ptr(GetRaHolder()), asmjit::imm(1));                    \
    if (!first) {                                                              \
      a->jmp(8);                                                               \
      a->dq(64);                                                               \
      a->paddq(sp, ptr(rip, -16));                                             \
    }                                                                          \
    a->lea(GetRaHolder(), ptr(rip, 8));                                        \
    a->pinsrq(sp, GetRaHolder(), asmjit::imm(0));                              \
    a->ret();                                                                  \
    break;                                                                     \
  }                                                                            \
  case i3: {                                                                   \
    a->vmovq(scratch.xmm, ptr(GetRaHolder()));                                 \
    a->vinserti128(scratch.ymm, ymm_reg, scratch.xmm, asmjit::imm(1));         \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(48));               \
    if (!first) {                                                              \
      a->jmp(8);                                                               \
      a->dq(64);                                                               \
      a->paddq(sp, ptr(rip, -16));                                             \
    }                                                                          \
    a->lea(GetRaHolder(), ptr(rip, 8));                                        \
    a->pinsrq(sp, GetRaHolder(), asmjit::imm(0));                              \
    a->ret();                                                                  \
    break;                                                                     \
  }                                                                            \
  case i4: {                                                                   \
    a->vmovq(scratch.xmm, ptr(GetRaHolder()));                                 \
    a->vpbroadcastq(scratch.ymm, scratch.xmm);                                 \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(192));              \
    if (!first) {                                                              \
      a->jmp(8);                                                               \
      a->dq(64);                                                               \
      a->paddq(sp, ptr(rip, -16));                                             \
    }                                                                          \
    a->lea(GetRaHolder(), ptr(rip, 8));                                        \
    a->pinsrq(sp, GetRaHolder(), asmjit::imm(0));                              \
    a->ret();                                                                  \
    break;                                                                     \
  }

void JitAvxV2StackPush(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  AvxRegister scratch = GetNextUnusedAvx2Register(info);

  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  // Unused quadword element indices in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);

  // Jump table dispatch
  // JIT_DISPATCH_PUSH(a, sp, quad_words.size());

  // If this is the first jump table slot
  bool first = false;
  for (unsigned int i = 0; i < quad_words.size(); i++) {
    if (i == 0) {
      first = true;
    }
    switch (quad_words[i]) {
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm0, ymm0, 0, 1, 2, 3)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm1, ymm1, 4, 5, 6, 7)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm2, ymm2, 8, 9, 10, 11)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm3, ymm3, 12, 13, 14, 15)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm4, ymm4, 16, 17, 18, 19)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm5, ymm5, 20, 21, 22, 23)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm6, ymm6, 24, 25, 26, 27)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm7, ymm7, 28, 29, 30, 31)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm8, ymm8, 32, 33, 34, 35)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm9, ymm9, 36, 37, 38, 39)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm10, ymm10, 40, 41, 42, 43)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm11, ymm11, 44, 45, 46, 47)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm12, ymm12, 48, 49, 50, 51)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm13, ymm13, 52, 53, 54, 55)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm14, ymm14, 56, 57, 58, 59)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm15, ymm15, 60, 61, 62, 63)
    }
  }
}

#define JIT_POP_AVX2(a, sp, first, error, scratch, xmm_reg, ymm_reg, i1, i2,   \
                     i3, i4)                                                   \
  case i1: {                                                                   \
    a->align(0 /* code-alignment */, 64);                                      \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(0));                                  \
    JIT_POP_RET_SEQ_V2(a, error, first)                                        \
    break;                                                                     \
  }                                                                            \
  case i2: {                                                                   \
    a->align(0 /* code-alignment */, 64);                                      \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(1));                                  \
    JIT_POP_RET_SEQ_V2(a, error, first)                                        \
    break;                                                                     \
  }                                                                            \
  case i3: {                                                                   \
    a->align(0 /* code-alignment */, 64);                                      \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(0));                              \
    JIT_POP_RET_SEQ_V2(a, error, first)                                        \
    break;                                                                     \
  }                                                                            \
  case i4: {                                                                   \
    a->align(0 /* code-alignment */, 64);                                      \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(1));                              \
    JIT_POP_RET_SEQ_V2(a, error, first)                                        \
    break;                                                                     \
  }

void JitAvxV2StackPop(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  AvxRegister scratch = GetNextUnusedAvx2Register(info);

  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  // Unused quadword elements in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);

  // Jump table dispatch
  // JIT_DISPATCH_POP(a, sp, quad_words.size());

  // If this is the first slot
  bool first = false;
  for (unsigned int i = 0; i < quad_words.size(); i++) {
    if (i == 0) {
      first = true;
    }

    asmjit::Label error = a->newLabel();
    switch (quad_words[i]) {
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm0, ymm0, 0, 1, 2, 3)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm1, ymm1, 4, 5, 6, 7)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm2, ymm2, 8, 9, 10, 11)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm3, ymm3, 12, 13, 14, 15)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm4, ymm4, 16, 17, 18, 19)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm5, ymm5, 20, 21, 22, 23)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm6, ymm6, 24, 25, 26, 27)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm7, ymm7, 28, 29, 30, 31)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm8, ymm8, 32, 33, 34, 35)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm9, ymm9, 36, 37, 38, 39)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm10, ymm10, 40, 41, 42, 43)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm11, ymm11, 44, 45, 46, 47)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm12, ymm12, 48, 49, 50, 51)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm13, ymm13, 52, 53, 54, 55)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm14, ymm14, 56, 57, 58, 59)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm15, ymm15, 60, 61, 62, 63)
    }
  }
}
