
#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit.h"
#include "jit_internal.h"
#include "register_utils.h"

//--------------- Jump Table Version v3 : Threaded Jump Table -----------------
//
// Here we implement a threaded jump table where push and pop slots are
// alternating with each other. Advantage of this approach over two separate
// jump tables for push and pop is that, with this approach we only need to
// maintain one stack pointer. This cuts down the vector operations at each push
// and pop by half.
//
//   push_0 |        |  |
//          |--------|  |
//   pop_0  |        |  v
//          |--------| <-- sp (%xmm15[0])
//   push_1 |        |
//          |--------|
//   pop_1  |        |
//             ...
//
// Notes:
//
// 1. We always position the pointer to a start of a push slot. So during a
// push it will bump the pointer to the next push slot. A pop will position the
// pointer to the previous push slot. Pop dispatch will then calculate the jump
// target for the pop slot relative to that previous push slot.
// 2. Overflow handling is bit complicated since we need to handle overflow
// stack with the same stack pointer arithmetic. We use a second overflow
// slot for push to achieve that. More details can be found in thread_local.c.
// 3. This implementation moves the stack pointer updates to happen at the
// callsite of stack functions rather than within the jump table slots. This
// considerably trim the jump table slots making more of them fit to a
// instruction cache line.

const int alignment = 32;
using namespace asmjit::x86;

std::string JitAvxV3CallStackPush(RegisterUsageInfo& info,
                                  AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;

  a->push(r10);
  a->push(r11);

  a->lea(r10, ptr(rsp, 16));

  // a->vmovq(r11, sp);
  // a->lea(r11, ptr(r11, /* 2 * A */ 64));
  // a->vmovq(sp, r11);

  // Bump the pointer to the next push slot
  a->vmovq(r11, sp);
  a->lea(r11, ptr(r11, /* 2 * A */ 64));
  a->vmovq(sp, r11);
  a->lea(r11, ptr(r11, -64));

  // a->call(ptr(r11, -32));
  a->call(r11);

  a->pop(r11);
  a->pop(r10);
  return "";
}

std::string JitAvxV3CallStackPush2(RegisterUsageInfo& info,
                                   AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;

  // a->vmovq(r11, sp);
  // a->lea(r11, ptr(r11, /* 2 * A */ 64));
  // a->vmovq(sp, r11);

  // Bump the pointer to the next push slot
  a->vmovq(r11, sp);
  a->lea(r11, ptr(r11, /* -(2 * A) */ 64));
  a->vmovq(sp, r11);
  // Readjust the dispatch target to be the original push slot
  a->lea(r11, ptr(r11, -64));

  // a->and_(rsp, asmjit::imm(-16));
  a->call(r11);

  return "";
}

std::string JitAvxV3CallStackPop(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;

  a->push(r10);
  a->push(r11);

  a->lea(r10, ptr(rsp, 16));

  // Decrement the pointer to the previous push slot
  a->vmovq(r11, sp);
  a->lea(r11, ptr(r11, /* -(2 * A) */ -64));
  a->vmovq(sp, r11);
  // Readjust the dispatch target to be the corresponding pop slot
  a->lea(r11, ptr(r11, 32));

  a->call(r11);

  a->pop(r11);
  a->pop(r10);
  return "";
}

std::string JitAvxV3CallStackPop2(RegisterUsageInfo& info,
                                  AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;

  a->vmovq(r11, sp);
  a->lea(r11, ptr(r11, /* -(2 * A) */ -64));
  a->vmovq(sp, r11);
  a->lea(r11, ptr(r11, 32));

  a->call(r11);

  return "";
}

std::string JitAvxV3StackInit(RegisterUsageInfo& info, AssemblerHolder& ah) {
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;

  std::string stack_init = "";
  stack_init += "lea r11, " + kStackFunction + "[rip]\n";
  stack_init += "vmovq " + GetAvx2Register(sp) + ", r11\n";

  return stack_init;
}

#define JIT_AVX2_STACK(a, error, scratch, xmm_reg, ymm_reg, i1, i2, i3, i4, A) \
  case i1: {                                                                   \
    /* Push slot */                                                            \
    a->vmovq(scratch.xmm, ptr(GetRaHolder()));                                 \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(3));                \
    a->ret();                                                                  \
    a->align(0, A);                                                            \
    /* Pop slot */                                                             \
    a->vmovq(r11, xmm_reg);                                                    \
    JIT_POP_RET_SEQ_V3(a, error)                                               \
    a->align(0 /* code-alignment */, A);                                       \
    break;                                                                     \
  }                                                                            \
  case i2: {                                                                   \
    /* Push slot */                                                            \
    a->vmovq(scratch.xmm, ptr(GetRaHolder()));                                 \
    a->vpbroadcastq(scratch.xmm, scratch.xmm);                                 \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(12));               \
    a->ret();                                                                  \
    a->align(0 /* code-alignment */, A);                                       \
    /* Pop slot */                                                             \
    a->vpextrq(r11, xmm_reg, asmjit::imm(1));                                  \
    JIT_POP_RET_SEQ_V3(a, error)                                               \
    a->align(0 /* code-alignment */, A);                                       \
    break;                                                                     \
  }                                                                            \
  case i3: {                                                                   \
    /* Push slot */                                                            \
    a->vmovq(scratch.xmm, ptr(GetRaHolder()));                                 \
    a->vinserti128(scratch.ymm, ymm_reg, scratch.xmm, asmjit::imm(1));         \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(48));               \
    a->ret();                                                                  \
    a->align(0, A);                                                            \
    /* Pop slot */                                                             \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(r11, scratch.xmm, asmjit::imm(0));                              \
    JIT_POP_RET_SEQ_V3(a, error)                                               \
    a->align(0 /* code-alignment */, A);                                       \
    break;                                                                     \
  }                                                                            \
  case i4: {                                                                   \
    /* Push slot */                                                            \
    a->vmovq(scratch.xmm, ptr(GetRaHolder()));                                 \
    a->vpbroadcastq(scratch.ymm, scratch.xmm);                                 \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(192));              \
    a->ret();                                                                  \
    a->align(0, A);                                                            \
    /* Pop slot */                                                             \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(r11, scratch.xmm, asmjit::imm(1));                              \
    JIT_POP_RET_SEQ_V3(a, error)                                               \
    a->align(0 /* code-alignment */, A);                                       \
    break;                                                                     \
  }

std::string JitAvxV3Stack(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  AvxRegister scratch = GetNextUnusedAvx2Register(info);

  // Unused quadword elements in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);

  for (unsigned int i = 0; i < quad_words.size(); i++) {
    asmjit::Label error = a->newLabel();
    switch (quad_words[i]) {
      JIT_AVX2_STACK(a, error, scratch, xmm0, ymm0, 0, 1, 2, 3, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm1, ymm1, 4, 5, 6, 7, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm2, ymm2, 8, 9, 10, 11, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm3, ymm3, 12, 13, 14, 15, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm4, ymm4, 16, 17, 18, 19, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm5, ymm5, 20, 21, 22, 23, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm6, ymm6, 24, 25, 26, 27, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm7, ymm7, 28, 29, 30, 31, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm8, ymm8, 32, 33, 34, 35, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm9, ymm9, 36, 37, 38, 39, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm10, ymm10, 40, 41, 42, 43, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm11, ymm11, 44, 45, 46, 47, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm12, ymm12, 48, 49, 50, 51, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm13, ymm13, 52, 53, 54, 55, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm14, ymm14, 56, 57, 58, 59, alignment)
      JIT_AVX2_STACK(a, error, scratch, xmm15, ymm15, 60, 61, 62, 63, alignment)
    }
  }

  return ah.GetStringLogger()->getString();
}
