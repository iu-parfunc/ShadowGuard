
#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit.h"
#include "jit_internal.h"
#include "register_utils.h"

using namespace asmjit::x86;

DECLARE_string(shadow_stack);

static std::vector<bool> reserved;

static std::vector<bool> GetReservedAvxMask() {
  if (reserved.size() > 0) {
    return reserved;
  }

  int n_regs = 16;
  int reserved_from = 8;

  for (int i = 0; i < n_regs; i++) {
    // We reserve extended avx2 registers for the stack
    if (i >= reserved_from) {
      reserved.push_back(true);
      continue;
    }
    reserved.push_back(false);
  }
  return reserved;
}

void JitNopInit(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  a->pxor(xmm14, xmm14);
  a->pxor(xmm15, xmm15);
}

#define JIT_NOP_DISPATCH_PUSH(a, sp, sz)                                       \
  a->align(0 /* code-alignment */, 32);                                        \
  a->vpextrq(r11, sp, asmjit::imm(0));                                         \
  a->cmp(r11d, asmjit::imm(sz));                                               \
  {                                                                            \
    /* Make the macro hygenic with locals in a separate scope */               \
    asmjit::Label overflow = a->newLabel();                                    \
    a->jg(overflow);                                                           \
                                                                               \
    /* Account for dispatch in the offset index */                             \
    a->add(r11, 3);                                                            \
                                                                               \
    /* Calculate jump target */                                                \
    a->imul(r11, asmjit::imm(32));                                             \
    a->lea(rax, ptr(rax, r11));                                                \
                                                                               \
    /* Increment stack pointer */                                              \
    a->vpextrq(r11, sp, asmjit::imm(0));                                       \
    a->inc(r11);                                                               \
    a->pinsrq(sp, r11, asmjit::imm(0));                                        \
                                                                               \
    /* Dispatch to jump table */                                               \
    a->nop();                                                                  \
    a->ret();                                                                  \
    a->bind(overflow);                                                         \
  }                                                                            \
  /* Increment stack pointer */                                                \
  a->vpextrq(r11, sp, asmjit::imm(0));                                         \
  a->inc(r11);                                                                 \
  a->pinsrq(sp, r11, asmjit::imm(0));                                          \
                                                                               \
  /* Return false to indicate push operation failed */                         \
  a->mov(rax, asmjit::imm(0));                                                 \
  a->ret();                                                                    \
  /* Align first jump table slot to next 32 byte boundary */                   \
  a->align(0 /* code-alignment */, 32);

#define JIT_NOP_PUSH(a, end, scratch, xmm_reg, ymm_reg, i1, i2, i3, i4)        \
  case i1: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->pinsrq(xmm_reg, ptr(GetRaHolder()), asmjit::imm(0));                    \
    a->jmp(end);                                                               \
    break;                                                                     \
  }                                                                            \
  case i2: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->pinsrq(xmm_reg, ptr(GetRaHolder()), asmjit::imm(1));                    \
    a->jmp(end);                                                               \
    break;                                                                     \
  }                                                                            \
  case i3: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->vmovq(scratch.xmm, ptr(GetRaHolder()));                                 \
    a->vinserti128(scratch.ymm, ymm_reg, scratch.xmm, asmjit::imm(1));         \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(48));               \
    a->jmp(end);                                                               \
    break;                                                                     \
  }                                                                            \
  case i4: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->vmovq(scratch.xmm, ptr(GetRaHolder()));                                 \
    a->vpbroadcastq(scratch.ymm, scratch.xmm);                                 \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(192));              \
    a->jmp(end);                                                               \
    break;                                                                     \
  }

void JitNopPush(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();

  // Stack pointer register
  asmjit::X86Xmm sp = xmm14;
  AvxRegister scratch = {xmm15, ymm15, zmm15};

  // Clobbered register for stack content
  asmjit::X86Xmm reg_xmm = xmm13;
  asmjit::X86Ymm reg_ymm = ymm13;

  std::vector<bool>& mask =
      const_cast<std::vector<bool>&>(info.GetUnusedAvx2Mask());
  mask = GetReservedAvxMask();

  // Unused quadword element indices in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);

  // Dummy Jump table dispatch. Only calculates and update the stack pointer.
  // Does not jump.
  JIT_NOP_DISPATCH_PUSH(a, sp, 24);

  /*
  // Jump table
  asmjit::Label end = a->newLabel();
  for (unsigned int i = 0; i < quad_words.size(); i++) {
    switch (quad_words[i]) {
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 0, 1, 2, 3)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 4, 5, 6, 7)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 8, 9, 10, 11)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 12, 13, 14, 15)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 16, 17, 18, 19)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 20, 21, 22, 23)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 24, 25, 26, 27)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 28, 29, 30, 31)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 32, 33, 34, 35)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 36, 37, 38, 39)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 40, 41, 42, 43)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 44, 45, 46, 47)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 48, 49, 50, 51)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 52, 53, 54, 55)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 56, 57, 58, 59)
      JIT_NOP_PUSH(a, end, scratch, reg_xmm, reg_ymm, 60, 61, 62, 63)
    }
  }

  a->bind(end);
  */
  // Returns true to denote the push was successful
  a->mov(rax, asmjit::imm(1));
}

#define JIT_NOP_DISPATCH_POP(a, sp, sz)                                        \
  a->align(0 /* code-alignment */, 32);                                        \
  /* Make jump base address to be the start of dispatch code. */               \
  /* We deduct the size of lea instruction (7) from $rip to get it. */         \
  a->lea(rdi, ptr(rip, -7));                                                   \
                                                                               \
  /* Decrement stack pointer */                                                \
  a->vpextrq(r11, sp, asmjit::imm(0));                                         \
  a->sub(r11, asmjit::imm(1));                                                 \
  a->pinsrq(sp, r11, asmjit::imm(0));                                          \
  a->cmp(r11d, asmjit::imm(sz));                                               \
                                                                               \
  {                                                                            \
    /* Make the macro hygenic with locals in a separate scope */               \
    asmjit::Label overflow = a->newLabel();                                    \
    a->jg(overflow);                                                           \
                                                                               \
    /* Account for dispatch in the offset index */                             \
    a->add(r11, 2);                                                            \
                                                                               \
    /* Calculate jump target */                                                \
    a->imul(r11, asmjit::imm(32));                                             \
    a->lea(rdi, ptr(rdi, r11));                                                \
                                                                               \
    /* Dispatch to jump table */                                               \
    a->nop();                                                                  \
    a->ret();                                                                  \
    a->bind(overflow);                                                         \
  }                                                                            \
  /* Return false to indicate pop operation failed */                          \
  a->mov(rax, asmjit::imm(0));                                                 \
  /* Prepare the input parameter for overflow call */                          \
  a->mov(rdi, ptr(GetRaHolder()));                                             \
  a->ret();                                                                    \
                                                                               \
  /* Align first jump table slot to next 64 byte boundary */                   \
  a->align(0 /* code-alignment */, 64);

#define JIT_NOP_POP(a, end, error, scratch, xmm_reg, ymm_reg, i1, i2, i3, i4)  \
  case i1: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(0));                                  \
    break;                                                                     \
  }                                                                            \
  case i2: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(1));                                  \
    break;                                                                     \
  }                                                                            \
  case i3: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(0));                              \
    break;                                                                     \
  }                                                                            \
  case i4: {                                                                   \
    a->align(0 /* code-alignment */, 32);                                      \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(1));                              \
    break;                                                                     \
  }

void JitNopPop(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();

  // Stack pointer register
  asmjit::X86Xmm sp = xmm14;
  AvxRegister scratch = {xmm15, ymm15, zmm15};

  // Clobbered register for stack content
  asmjit::X86Xmm reg_xmm = xmm13;
  asmjit::X86Ymm reg_ymm = ymm13;

  std::vector<bool>& mask =
      const_cast<std::vector<bool>&>(info.GetUnusedAvx2Mask());
  mask = GetReservedAvxMask();

  // Unused quadword element indices in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);

  // Dummy Jump table dispatch. Only calculates and update the stack pointer.
  // Does not jump.
  JIT_NOP_DISPATCH_POP(a, sp, 24);

  /*
  asmjit::Label error = a->newLabel();
  asmjit::Label end = a->newLabel();
  for (unsigned int i = 0; i < quad_words.size(); i++) {
    switch (quad_words[i]) {
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 0, 1, 2, 3)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 4, 5, 6, 7)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 8, 9, 10, 11)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 12, 13, 14, 15)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 16, 17, 18, 19)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 20, 21, 22, 23)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 24, 25, 26, 27)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 28, 29, 30, 31)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 32, 33, 34, 35)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 36, 37, 38, 39)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 40, 41, 42, 43)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 44, 45, 46, 47)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 48, 49, 50, 51)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 52, 53, 54, 55)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 56, 57, 58, 59)
      JIT_NOP_POP(a, end, error, scratch, reg_xmm, reg_ymm, 60, 61, 62, 63)
    }
  }

  a->bind(error);
  a->nop();

  a->bind(end);
  */
  // Returns true to denote the pop was successful
  a->mov(rax, asmjit::imm(1));
}
