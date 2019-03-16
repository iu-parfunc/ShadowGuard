
#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit.h"
#include "jit_internal.h"
#include "register_utils.h"

const int alignment = 64;
using namespace asmjit::x86;

std::string JitAvxV2CallStackPush(RegisterUsageInfo& info,
                                  AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  a->lea(r10, ptr(rsp));
  a->vpextrq(r11, sp, asmjit::imm(0));
  a->call(r11);
  return "";
}

std::string JitAvxV2CallStackPush2(RegisterUsageInfo& info,
                                  AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  a->vpextrq(r11, sp, asmjit::imm(0));
  a->call(r11);
  return "";
}



std::string JitAvxV2CallStackPop(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  a->lea(r10, ptr(rsp));
  a->vpextrq(r11, sp, asmjit::imm(1));
  a->call(r11);
  return "";
}

std::string JitAvxV2CallStackPop2(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  a->vpextrq(r11, sp, asmjit::imm(1));
  a->call(r11);
  return "";
}

std::string JitAvxV2StackInit(RegisterUsageInfo& info, AssemblerHolder& ah) {
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;

  std::string stack_init = "";
  stack_init += "lea r11, " + kStackPushFunction + "[rip]\n";
  stack_init += "pinsrq " + GetAvx2Register(sp) + ", r11, 0\n";
  stack_init += "lea r11, " + kStackPopFunction + "[rip]\n";
  stack_init += "pinsrq " + GetAvx2Register(sp) + ", r11, 1\n";

  return stack_init;
}

#define JIT_PUSH_AVX2(a, sp, first, scratch, xmm_reg, ymm_reg, i1, i2, i3, i4, A) \
  case i1: {                                                                   \
    a->lea(r11, ptr(r11, A)); \
    a->pinsrq(sp, r11, asmjit::imm(0));                              \
    a->pinsrq(xmm_reg, ptr(GetRaHolder()), asmjit::imm(0));                    \
    if (!first) {                                                              \
      a->vpextrq(r11, sp, asmjit::imm(1)); \
      a->lea(r11, ptr(r11, A)); \
      a->pinsrq(sp, r11, asmjit::imm(1)); \
    }\
    a->ret();                                                                  \
    a->align(0, A); \
    break;                                                                     \
  }                                                                            \
  case i2: {                                                                   \
    a->lea(r11, ptr(r11, A)); \
    a->pinsrq(sp, r11, asmjit::imm(0));                              \
    a->pinsrq(xmm_reg, ptr(GetRaHolder()), asmjit::imm(1));                    \
    if (!first) {                                                              \
      a->vpextrq(r11, sp, asmjit::imm(1)); \
      a->lea(r11, ptr(r11, A)); \
      a->pinsrq(sp, r11, asmjit::imm(1)); \
    }                                                                          \
    a->ret();                                                                  \
    a->align(0, A); \
    break;                                                                     \
  }                                                                            \
  case i3: {                                                                   \
    a->lea(r11, ptr(r11, A)); \
    a->pinsrq(sp, r11, asmjit::imm(0));                              \
    a->vmovq(scratch.xmm, ptr(GetRaHolder()));                                 \
    a->vinserti128(scratch.ymm, ymm_reg, scratch.xmm, asmjit::imm(1));         \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(48));               \
    if (!first) {                                                              \
      a->vpextrq(r11, sp, asmjit::imm(1)); \
      a->lea(r11, ptr(r11, A)); \
      a->pinsrq(sp, r11, asmjit::imm(1)); \
    }                                                                          \
    a->ret();                                                                  \
    a->align(0, A); \
    break;                                                                     \
  }                                                                            \
  case i4: {                                                                   \
    a->lea(r11, ptr(r11, A)); \
    a->pinsrq(sp, r11, asmjit::imm(0));                              \
    a->vmovq(scratch.xmm, ptr(GetRaHolder()));                                 \
    a->vpbroadcastq(scratch.ymm, scratch.xmm);                                 \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(192));              \
    if (!first) {                                                              \
      a->vpextrq(r11, sp, asmjit::imm(1)); \
      a->lea(r11, ptr(r11, A)); \
      a->pinsrq(sp, r11, asmjit::imm(1)); \
    }                                                                          \
    a->ret();                                                                  \
    a->align(0, A); \
    break;                                                                     \
  }

std::string JitAvxV2StackPush(RegisterUsageInfo& info, AssemblerHolder& ah) {
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
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm0, ymm0, 0, 1, 2, 3, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm1, ymm1, 4, 5, 6, 7, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm2, ymm2, 8, 9, 10, 11, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm3, ymm3, 12, 13, 14, 15, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm4, ymm4, 16, 17, 18, 19, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm5, ymm5, 20, 21, 22, 23, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm6, ymm6, 24, 25, 26, 27, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm7, ymm7, 28, 29, 30, 31, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm8, ymm8, 32, 33, 34, 35, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm9, ymm9, 36, 37, 38, 39, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm10, ymm10, 40, 41, 42, 43, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm11, ymm11, 44, 45, 46, 47, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm12, ymm12, 48, 49, 50, 51, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm13, ymm13, 52, 53, 54, 55, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm14, ymm14, 56, 57, 58, 59, alignment)
      JIT_PUSH_AVX2(a, sp, first, scratch, xmm15, ymm15, 60, 61, 62, 63, alignment)
    }
    first = false;
  }
  return ah.GetStringLogger()->getString();
}

#define JIT_POP_AVX2(a, sp, first, error, scratch, xmm_reg, ymm_reg, i1, i2,   \
                     i3, i4, A)                                                   \
  case i1: {                                                                   \
    if (!first) { \
      a->lea(r11, ptr(r11, -A)); \
      a->pinsrq(sp, r11, asmjit::imm(1));\
    }\
    a->vpextrq(r11, sp, asmjit::imm(0)); \
    a->lea(r11, ptr(r11, -A)); \
    a->pinsrq(sp, r11, asmjit::imm(0)); \
    a->vpextrq(r11, xmm_reg, asmjit::imm(0));                                  \
    JIT_POP_RET_SEQ_V2(a, error, first)                                        \
    a->align(0 /* code-alignment */, A);                                      \
    break;                                                                     \
  }                                                                            \
  case i2: {                                                                   \
    if (!first) { \
      a->lea(r11, ptr(r11, -A)); \
      a->pinsrq(sp, r11, asmjit::imm(1));\
    }\
    a->vpextrq(r11, sp, asmjit::imm(0)); \
    a->lea(r11, ptr(r11, -A)); \
    a->pinsrq(sp, r11, asmjit::imm(0)); \
    a->vpextrq(r11, xmm_reg, asmjit::imm(1));                                  \
    JIT_POP_RET_SEQ_V2(a, error, first)                                        \
    a->align(0 /* code-alignment */, A);                                      \
    break;                                                                     \
  }                                                                            \
  case i3: {                                                                   \
    if (!first) { \
      a->lea(r11, ptr(r11, -A)); \
      a->pinsrq(sp, r11, asmjit::imm(1));\
    }\
    a->vpextrq(r11, sp, asmjit::imm(0)); \
    a->lea(r11, ptr(r11, -A)); \
    a->pinsrq(sp, r11, asmjit::imm(0)); \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(r11, scratch.xmm, asmjit::imm(0));                              \
    JIT_POP_RET_SEQ_V2(a, error, first)                                        \
    a->align(0 /* code-alignment */, A);                                      \
    break;                                                                     \
  }                                                                            \
  case i4: {                                                                   \
    if (!first) { \
      a->lea(r11, ptr(r11, -A)); \
      a->pinsrq(sp, r11, asmjit::imm(1));\
    }\
    a->vpextrq(r11, sp, asmjit::imm(0)); \
    a->lea(r11, ptr(r11, -A)); \
    a->pinsrq(sp, r11, asmjit::imm(0)); \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(r11, scratch.xmm, asmjit::imm(1));                              \
    JIT_POP_RET_SEQ_V2(a, error, first)                                        \
    a->align(0 /* code-alignment */, A);                                      \
    break;                                                                     \
  }

std::string JitAvxV2StackPop(RegisterUsageInfo& info, AssemblerHolder& ah) {
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
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm0, ymm0, 0, 1, 2, 3, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm1, ymm1, 4, 5, 6, 7, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm2, ymm2, 8, 9, 10, 11, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm3, ymm3, 12, 13, 14, 15, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm4, ymm4, 16, 17, 18, 19, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm5, ymm5, 20, 21, 22, 23, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm6, ymm6, 24, 25, 26, 27, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm7, ymm7, 28, 29, 30, 31, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm8, ymm8, 32, 33, 34, 35, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm9, ymm9, 36, 37, 38, 39, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm10, ymm10, 40, 41, 42, 43, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm11, ymm11, 44, 45, 46, 47, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm12, ymm12, 48, 49, 50, 51, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm13, ymm13, 52, 53, 54, 55, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm14, ymm14, 56, 57, 58, 59, alignment)
      JIT_POP_AVX2(a, sp, first, error, scratch, xmm15, ymm15, 60, 61, 62, 63, alignment)
    }
    first = false;
  }

  return ah.GetStringLogger()->getString();
}
