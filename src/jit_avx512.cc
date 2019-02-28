
#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit.h"
#include "jit_internal.h"
#include "register_usage.h"
#include "register_utils.h"

using namespace asmjit::x86;

void JitAvx512StackInit(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx512Register(info);
  a->pxor(meta.xmm, meta.xmm);
}

#define JIT_MOV_RA_TO_ZMM(a, zmm_reg, end, write_mask) \
  a->mov(rax, asmjit::imm(write_mask));                \
  a->kmovb(k5, rax);                                   \
  a->k(k5).vpbroadcastq(zmm_reg, ptr(rsp));            \
  a->jmp(end);                                         \
  NOP_PAD(a, 19)

#define JIT_PUSH_AVX512(a, end, zmm_reg, i1, i2, i3, i4, i5, i6, i7, i8) \
  case i1: {                                                             \
    JIT_MOV_RA_TO_ZMM(a, zmm_reg, end, 1);                               \
    break;                                                               \
  }                                                                      \
  case i2: {                                                             \
    JIT_MOV_RA_TO_ZMM(a, zmm_reg, end, 2);                               \
    break;                                                               \
  }                                                                      \
  case i3: {                                                             \
    JIT_MOV_RA_TO_ZMM(a, zmm_reg, end, 4);                               \
    break;                                                               \
  }                                                                      \
  case i4: {                                                             \
    JIT_MOV_RA_TO_ZMM(a, zmm_reg, end, 8);                               \
    break;                                                               \
  }                                                                      \
  case i5: {                                                             \
    JIT_MOV_RA_TO_ZMM(a, zmm_reg, end, 16);                              \
    break;                                                               \
  }                                                                      \
  case i6: {                                                             \
    JIT_MOV_RA_TO_ZMM(a, zmm_reg, end, 32);                              \
    break;                                                               \
  }                                                                      \
  case i7: {                                                             \
    JIT_MOV_RA_TO_ZMM(a, zmm_reg, end, 64);                              \
    break;                                                               \
  }                                                                      \
  case i8: {                                                             \
    JIT_MOV_RA_TO_ZMM(a, zmm_reg, end, 128);                             \
    break;                                                               \
  }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
void JitAvx512StackPush(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx512Register(info);

  AvxRegister scratch = GetNextUnusedAvx2Register(info);

  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  // Unused quadword element indices in AVX512 register file
  std::vector<uint16_t> quad_words = GetUnusedAvx512QuadWords(info);

  // Jump table dispatch
  JIT_DISPATCH_PUSH(a, sp, quad_words.size());

  asmjit::Label end = a->newLabel();
  for (unsigned int i = 0; i < quad_words.size(); i++) {
    switch (quad_words[i]) {
      JIT_PUSH_AVX512(a, end, zmm0, 0, 1, 2, 3, 4, 5, 6, 7)
      JIT_PUSH_AVX512(a, end, zmm1, 8, 9, 10, 11, 12, 13, 14, 15)
      JIT_PUSH_AVX512(a, end, zmm2, 16, 17, 18, 19, 20, 21, 22, 23)
      JIT_PUSH_AVX512(a, end, zmm3, 24, 25, 26, 27, 28, 29, 30, 31)
      JIT_PUSH_AVX512(a, end, zmm4, 32, 33, 34, 35, 36, 37, 38, 39)
      JIT_PUSH_AVX512(a, end, zmm5, 40, 41, 42, 43, 44, 45, 46, 47)
      JIT_PUSH_AVX512(a, end, zmm6, 48, 49, 50, 51, 52, 53, 54, 55)
      JIT_PUSH_AVX512(a, end, zmm7, 56, 57, 58, 59, 60, 61, 62, 63)
      JIT_PUSH_AVX512(a, end, zmm8, 64, 65, 66, 67, 68, 69, 70, 71)
      JIT_PUSH_AVX512(a, end, zmm9, 72, 73, 74, 75, 76, 77, 78, 79)
      JIT_PUSH_AVX512(a, end, zmm10, 80, 81, 82, 83, 84, 85, 86, 87)
      JIT_PUSH_AVX512(a, end, zmm11, 88, 89, 90, 91, 92, 93, 94, 95)
      JIT_PUSH_AVX512(a, end, zmm12, 96, 97, 98, 99, 100, 101, 102, 103)
      JIT_PUSH_AVX512(a, end, zmm13, 104, 105, 106, 107, 108, 109, 110, 111)
      JIT_PUSH_AVX512(a, end, zmm14, 112, 113, 114, 115, 116, 117, 118, 119)
      JIT_PUSH_AVX512(a, end, zmm15, 120, 121, 122, 123, 124, 125, 126, 127)
      JIT_PUSH_AVX512(a, end, zmm16, 128, 129, 130, 131, 132, 133, 134, 135)
      JIT_PUSH_AVX512(a, end, zmm17, 136, 137, 138, 139, 140, 141, 142, 143)
      JIT_PUSH_AVX512(a, end, zmm18, 144, 145, 146, 147, 148, 149, 150, 151)
      JIT_PUSH_AVX512(a, end, zmm19, 152, 153, 154, 155, 156, 157, 158, 159)
      JIT_PUSH_AVX512(a, end, zmm20, 160, 161, 162, 163, 164, 165, 166, 167)
      JIT_PUSH_AVX512(a, end, zmm21, 168, 169, 170, 171, 172, 173, 174, 175)
      JIT_PUSH_AVX512(a, end, zmm22, 176, 177, 178, 179, 180, 181, 182, 183)
      JIT_PUSH_AVX512(a, end, zmm23, 184, 185, 186, 187, 188, 189, 190, 191)
      JIT_PUSH_AVX512(a, end, zmm24, 192, 193, 194, 195, 196, 197, 198, 199)
      JIT_PUSH_AVX512(a, end, zmm25, 200, 201, 202, 203, 204, 205, 206, 207)
      JIT_PUSH_AVX512(a, end, zmm26, 208, 209, 210, 211, 212, 213, 214, 215)
      JIT_PUSH_AVX512(a, end, zmm27, 216, 217, 218, 219, 220, 221, 222, 223)
      JIT_PUSH_AVX512(a, end, zmm28, 224, 225, 226, 227, 228, 229, 230, 231)
      JIT_PUSH_AVX512(a, end, zmm29, 232, 233, 234, 235, 236, 237, 238, 239)
      JIT_PUSH_AVX512(a, end, zmm30, 240, 241, 242, 243, 244, 245, 246, 247)
      JIT_PUSH_AVX512(a, end, zmm31, 248, 249, 250, 251, 252, 253, 254, 255)
    }
  }

  a->bind(end);
}
#pragma GCC diagnostic pop

#define JIT_POP_AVX512(a, end, error, scratch, xmm_reg, zmm_reg, i1, i2, i3, \
                       i4, i5, i6, i7, i8)                                   \
  case i1: {                                                                 \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(0));                                \
    JIT_POP_RET_SEQ(a, end, error)                                           \
    break;                                                                   \
  }                                                                          \
  case i2: {                                                                 \
    a->vpextrq(rdi, xmm_reg, asmjit::imm(1));                                \
    JIT_POP_RET_SEQ(a, end, error)                                           \
    break;                                                                   \
  }                                                                          \
  case i3: {                                                                 \
    a->vextracti64x2(scratch.xmm, zmm_reg, asmjit::imm(1));                  \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(0));                            \
    JIT_POP_RET_SEQ(a, end, error)                                           \
    break;                                                                   \
  }                                                                          \
  case i4: {                                                                 \
    a->vextracti64x2(scratch.xmm, zmm_reg, asmjit::imm(1));                  \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(1));                            \
    JIT_POP_RET_SEQ(a, end, error)                                           \
    break;                                                                   \
  }                                                                          \
  case i5: {                                                                 \
    a->vextracti64x2(scratch.xmm, zmm_reg, asmjit::imm(2));                  \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(0));                            \
    JIT_POP_RET_SEQ(a, end, error)                                           \
  }                                                                          \
  case i6: {                                                                 \
    a->vextracti64x2(scratch.xmm, zmm_reg, asmjit::imm(2));                  \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(1));                            \
    JIT_POP_RET_SEQ(a, end, error)                                           \
    break;                                                                   \
  }                                                                          \
  case i7: {                                                                 \
    a->vextracti64x2(scratch.xmm, zmm_reg, asmjit::imm(3));                  \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(0));                            \
    JIT_POP_RET_SEQ(a, end, error)                                           \
  }                                                                          \
  case i8: {                                                                 \
    a->vextracti64x2(scratch.xmm, zmm_reg, asmjit::imm(3));                  \
    a->vpextrq(rdi, scratch.xmm, asmjit::imm(1));                            \
    JIT_POP_RET_SEQ(a, end, error)                                           \
    break;                                                                   \
  }

void JitAvx512StackPop(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  AvxRegister scratch = GetNextUnusedAvx2Register(info);

  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  // Unused quadword elements in AVX512 register file
  std::vector<uint16_t> quad_words = GetUnusedAvx512QuadWords(info);

  // Jump table dispatch
  JIT_DISPATCH_POP(a, sp, quad_words.size());

  asmjit::Label error = a->newLabel();
  asmjit::Label end = a->newLabel();
  for (unsigned int i = 0; i < quad_words.size(); i++) {
    switch (quad_words[i]) {
      JIT_POP_AVX512(a, end, error, scratch, xmm0, zmm0, 0, 1, 2, 3, 4, 5, 6, 7)
      JIT_POP_AVX512(a, end, error, scratch, xmm1, zmm1, 8, 9, 10, 11, 12, 13,
                     14, 15)
      JIT_POP_AVX512(a, end, error, scratch, xmm2, zmm2, 16, 17, 18, 19, 20, 21,
                     22, 23)
      JIT_POP_AVX512(a, end, error, scratch, xmm3, zmm3, 24, 25, 26, 27, 28, 29,
                     30, 31)
      JIT_POP_AVX512(a, end, error, scratch, xmm4, zmm4, 32, 33, 34, 35, 36, 37,
                     38, 39)
      JIT_POP_AVX512(a, end, error, scratch, xmm5, zmm5, 40, 41, 42, 43, 44, 45,
                     46, 47)
      JIT_POP_AVX512(a, end, error, scratch, xmm6, zmm6, 48, 49, 50, 51, 52, 53,
                     54, 55)
      JIT_POP_AVX512(a, end, error, scratch, xmm7, zmm7, 56, 57, 58, 59, 60, 61,
                     62, 63)
      JIT_POP_AVX512(a, end, error, scratch, xmm8, zmm8, 64, 65, 66, 67, 68, 69,
                     70, 71)
      JIT_POP_AVX512(a, end, error, scratch, xmm9, zmm9, 72, 73, 74, 75, 76, 77,
                     78, 79)
      JIT_POP_AVX512(a, end, error, scratch, xmm10, zmm10, 80, 81, 82, 83, 84,
                     85, 86, 87)
      JIT_POP_AVX512(a, end, error, scratch, xmm11, zmm11, 88, 89, 90, 91, 92,
                     93, 94, 95)
      JIT_POP_AVX512(a, end, error, scratch, xmm12, zmm12, 96, 97, 98, 99, 100,
                     101, 102, 103)
      JIT_POP_AVX512(a, end, error, scratch, xmm13, zmm13, 104, 105, 106, 107,
                     108, 109, 110, 111)
      JIT_POP_AVX512(a, end, error, scratch, xmm14, zmm14, 112, 113, 114, 115,
                     116, 117, 118, 119)
      JIT_POP_AVX512(a, end, error, scratch, xmm15, zmm15, 120, 121, 122, 123,
                     124, 125, 126, 127)

      JIT_POP_AVX512(a, end, error, scratch, xmm16, zmm16, 128, 129, 130, 131,
                     132, 133, 134, 135)
      JIT_POP_AVX512(a, end, error, scratch, xmm17, zmm17, 136, 137, 138, 139,
                     140, 141, 142, 143)
      JIT_POP_AVX512(a, end, error, scratch, xmm18, zmm18, 144, 145, 146, 147,
                     148, 149, 150, 151)
      JIT_POP_AVX512(a, end, error, scratch, xmm19, zmm19, 152, 153, 154, 155,
                     156, 157, 158, 159)
      JIT_POP_AVX512(a, end, error, scratch, xmm20, zmm20, 160, 161, 162, 163,
                     164, 165, 166, 167)
      JIT_POP_AVX512(a, end, error, scratch, xmm21, zmm21, 168, 169, 170, 171,
                     172, 173, 174, 175)
      JIT_POP_AVX512(a, end, error, scratch, xmm22, zmm22, 176, 177, 178, 179,
                     180, 181, 182, 183)
      JIT_POP_AVX512(a, end, error, scratch, xmm23, zmm23, 184, 185, 186, 187,
                     188, 189, 190, 191)
      JIT_POP_AVX512(a, end, error, scratch, xmm24, zmm24, 192, 193, 194, 195,
                     196, 197, 198, 199)
      JIT_POP_AVX512(a, end, error, scratch, xmm25, zmm25, 200, 201, 202, 203,
                     204, 205, 206, 207)
      JIT_POP_AVX512(a, end, error, scratch, xmm26, zmm26, 208, 209, 210, 211,
                     212, 213, 214, 215)
      JIT_POP_AVX512(a, end, error, scratch, xmm27, zmm27, 216, 217, 218, 219,
                     220, 221, 222, 223)
      JIT_POP_AVX512(a, end, error, scratch, xmm28, zmm28, 224, 225, 226, 227,
                     228, 229, 230, 231)
      JIT_POP_AVX512(a, end, error, scratch, xmm29, zmm29, 232, 233, 234, 235,
                     236, 237, 238, 239)
      JIT_POP_AVX512(a, end, error, scratch, xmm30, zmm30, 240, 241, 242, 243,
                     244, 245, 246, 247)
      JIT_POP_AVX512(a, end, error, scratch, xmm31, zmm31, 248, 249, 250, 251,
                     252, 253, 254, 255)
    }
  }

  a->bind(error);
  a->int3();
  a->bind(end);
}
