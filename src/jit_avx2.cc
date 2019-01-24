
#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit_internal.h"

using namespace asmjit::x86;

std::vector<uint8_t> GetUnusedMmxRegisterIndices(
    const RegisterUsageInfo& info) {
  std::vector<uint8_t> unused;
  for (unsigned int i = 0; i < info.unused_mmx_mask.size(); i++) {
    if (info.unused_mmx_mask[i]) {
      unused.push_back(i);
    }
  }
  return unused;
}

std::vector<uint8_t> GetUnusedAvxRegisterIndices(
    const RegisterUsageInfo& info) {
  std::vector<uint8_t> unused;
  for (unsigned int i = 0; i < info.unused_avx_mask.size(); i++) {
    if (info.unused_avx_mask[i]) {
      unused.push_back(i);
    }
  }
  return unused;
}

struct Avx2Register {
  // AVX portion
  asmjit::X86Xmm xmm;
  // AVX2 portion
  asmjit::X86Ymm ymm;
};

Avx2Register GetAvx2Register(uint8_t index) {
  switch (index) {
    case 0:
      return {asmjit::x86::xmm0, asmjit::x86::ymm0};
    case 1:
      return {asmjit::x86::xmm1, asmjit::x86::ymm1};
    case 2:
      return {asmjit::x86::xmm2, asmjit::x86::ymm2};
    case 3:
      return {asmjit::x86::xmm3, asmjit::x86::ymm3};
    case 4:
      return {asmjit::x86::xmm4, asmjit::x86::ymm4};
    case 5:
      return {asmjit::x86::xmm5, asmjit::x86::ymm5};
    case 6:
      return {asmjit::x86::xmm6, asmjit::x86::ymm6};
    case 7:
      return {asmjit::x86::xmm7, asmjit::x86::ymm7};
    case 8:
      return {asmjit::x86::xmm8, asmjit::x86::ymm8};
    case 9:
      return {asmjit::x86::xmm9, asmjit::x86::ymm9};
    case 10:
      return {asmjit::x86::xmm10, asmjit::x86::ymm10};
    case 11:
      return {asmjit::x86::xmm11, asmjit::x86::ymm11};
    case 12:
      return {asmjit::x86::xmm12, asmjit::x86::ymm12};
    case 13:
      return {asmjit::x86::xmm13, asmjit::x86::ymm13};
    case 14:
      return {asmjit::x86::xmm14, asmjit::x86::ymm14};
    case 15:
      return {asmjit::x86::xmm15, asmjit::x86::ymm15};
  }

  DCHECK(false);
}

asmjit::X86Mm GetMmxRegister(uint8_t index) {
  switch (index) {
    case 0:
      return asmjit::x86::mm0;
    case 1:
      return asmjit::x86::mm1;
    case 2:
      return asmjit::x86::mm2;
    case 3:
      return asmjit::x86::mm3;
    case 4:
      return asmjit::x86::mm4;
    case 5:
      return asmjit::x86::mm5;
    case 6:
      return asmjit::x86::mm6;
    case 7:
      return asmjit::x86::mm7;
  }

  DCHECK(false);
}

// Get an unused ymm register
Avx2Register GetScratchAvx2Register(std::vector<uint8_t>& avx_indices) {
  for (unsigned int i = 0; i < avx_indices.size(); i++) {
    if (avx_indices[i] % 2 == 0 && avx_indices[i + 1] == avx_indices[i] + 1) {
      avx_indices.erase(avx_indices.begin() + i);
      return GetAvx2Register(avx_indices[i] / 2);
    }
  }
  DCHECK(false);
}

asmjit::X86Mm GetStackPointerRegister(std::vector<uint8_t>& avx_indices,
                                      std::vector<uint8_t>& mmx_indices) {
  if (mmx_indices.size() > 0) {
    asmjit::X86Mm mmx = GetMmxRegister(mmx_indices[0]);
    mmx_indices.erase(mmx_indices.begin());
    return mmx;
  }

  DCHECK(false);
}

std::vector<uint8_t> GetUnusedAvxQuadWords(
    const std::vector<uint8_t>& avx_indices) {
  std::vector<uint8_t> quad_words;
  for (unsigned int i = 0; i < avx_indices.size(); i++) {
    quad_words.push_back(2 * i);
    quad_words.push_back(2 * i + 1);
  }
  return quad_words;
}

#define NOP_PAD(a, n) \
  int iters = n;      \
  while (iters > 0) { \
    a->nop();         \
    iters--;          \
  }

// Precision loss by incrementing %r11d instead of %r11 limits the stack depth
// to a max(uint32_t). This tradeoff was due to the fact that using full 64 bit
// inc (or add for that matter) makes the jump table slot of case 'i3' to spill
// over 32 bytes in size. With 32 bit inc we keep the maximum jump table slot
// size to 32 bytes and hence makes it aligned w.r.t. cacheline sizes which
// hopefully will yield better memory performance.
#define JIT_PUSH_END_SEQ(a, end, sp) \
  if (sp.isMm()) {                   \
    a->movd(r11d, sp);               \
    a->inc(r11d);                    \
    a->movd(sp, r11d);               \
    a->jmp(end);                     \
  }

#define JIT_PUSH_AVX2(a, end, sp, scratch, xmm_reg, ymm_reg, i1, i2, i3, i4) \
  case i1: {                                                                 \
    a->pinsrq(xmm_reg, ptr(rsp), asmjit::imm(0));                            \
    JIT_PUSH_END_SEQ(a, end, sp)                                             \
    NOP_PAD(a, 9)                                                            \
    break;                                                                   \
  }                                                                          \
  case i2: {                                                                 \
    a->pinsrq(xmm_reg, ptr(rsp), asmjit::imm(1));                            \
    JIT_PUSH_END_SEQ(a, end, sp)                                             \
    NOP_PAD(a, 9)                                                            \
    break;                                                                   \
  }                                                                          \
  case i3: {                                                                 \
    a->vmovq(scratch.xmm, ptr(rsp));                                         \
    a->vinserti128(scratch.ymm, ymm_reg, scratch.xmm, asmjit::imm(1));       \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(48));             \
    JIT_PUSH_END_SEQ(a, end, sp)                                             \
    break;                                                                   \
  }                                                                          \
  case i4: {                                                                 \
    a->vmovq(scratch.xmm, ptr(rsp));                                         \
    a->vpbroadcastq(scratch.ymm, scratch.xmm);                               \
    a->vpblendd(ymm_reg, ymm_reg, scratch.ymm, asmjit::imm(192));            \
    JIT_PUSH_END_SEQ(a, end, sp)                                             \
    NOP_PAD(a, 1)                                                            \
    break;                                                                   \
  }

#define JIT_DISPATCH_PUSH(a, sp)    \
  a->movq(r11, sp);                 \
  a->imul(r11, asmjit::imm(33));    \
  a->lea(rax, ptr(rip, 6));         \
                                    \
  asmjit::X86Mem c = ptr(rax, r11); \
  a->lea(rax, c);                   \
  a->jmp(rax);

void JitAvx2StackPush(const RegisterUsageInfo& info, asmjit::X86Assembler* a) {
  std::vector<uint8_t> avx_indices = GetUnusedAvxRegisterIndices(info);
  std::vector<uint8_t> mmx_indices = GetUnusedMmxRegisterIndices(info);

  Avx2Register scratch = GetScratchAvx2Register(avx_indices);
  asmjit::X86Mm sp = GetStackPointerRegister(avx_indices, mmx_indices);

  std::vector<uint8_t> quad_words = GetUnusedAvxQuadWords(avx_indices);

  asmjit::Label end = a->newLabel();

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

#define JIT_POP_RET_SEQ(a, end, error, sp) \
  if (sp.isMm()) {                         \
    a->cmp(rdi, ptr(rsp));                 \
    a->jne(error);                         \
    a->jmp(end);                           \
  }

#define JIT_POP_AVX2(a, end, error, sp, scratch, xmm_reg, ymm_reg, i1, i2, i3, \
                     i4)                                                       \
  case i1: {                                                                   \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(0));                                  \
    JIT_POP_RET_SEQ(a, end, error, sp)                                         \
    NOP_PAD(a, 11)                                                             \
    break;                                                                     \
  }                                                                            \
  case i2: {                                                                   \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(1));                                  \
    JIT_POP_RET_SEQ(a, end, error, sp)                                         \
    NOP_PAD(a, 11)                                                             \
    break;                                                                     \
  }                                                                            \
  case i3: {                                                                   \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(0));                              \
    JIT_POP_RET_SEQ(a, end, error, sp)                                         \
    NOP_PAD(a, 5)                                                              \
    break;                                                                     \
  }                                                                            \
  case i4: {                                                                   \
    a->vextracti128(scratch.xmm, ymm_reg, asmjit::imm(1));                     \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(1));                              \
    JIT_POP_RET_SEQ(a, end, error, sp)                                         \
    NOP_PAD(a, 5)                                                              \
    break;                                                                     \
  }

#define JIT_DISPATCH_POP(a, sp)     \
  a->movq(r11, sp);                 \
  a->sub(r11, asmjit::imm(1));      \
  a->movq(sp, r11);                 \
  a->imul(r11, asmjit::imm(32));    \
  a->lea(rdi, ptr(rip, 6));         \
                                    \
  asmjit::X86Mem c = ptr(rdi, r11); \
  a->lea(rdi, c);                   \
  a->jmp(rdi);

void JitAvx2StackPop(const RegisterUsageInfo& info, asmjit::X86Assembler* a) {
  std::vector<uint8_t> avx_indices = GetUnusedAvxRegisterIndices(info);
  std::vector<uint8_t> mmx_indices = GetUnusedMmxRegisterIndices(info);

  Avx2Register scratch = GetScratchAvx2Register(avx_indices);
  asmjit::X86Mm sp = GetStackPointerRegister(avx_indices, mmx_indices);

  std::vector<uint8_t> quad_words = GetUnusedAvxQuadWords(avx_indices);

  asmjit::Label error = a->newLabel();
  asmjit::Label end = a->newLabel();

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
