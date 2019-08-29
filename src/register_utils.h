
#ifndef LITECFI_REGISTER_UTILS_H_
#define LITECFI_REGISTER_UTILS_H_

#include "asmjit/asmjit.h"
#include "register_usage.h"

struct AvxRegister {
  // AVX portion
  asmjit::x86::Xmm xmm;
  // AVX2 portion
  asmjit::x86::Ymm ymm;
  // AVX512 portion
  asmjit::x86::Zmm zmm;
};

/*
std::string GetAvx2Register(asmjit::x86::Xmm reg);
*/

AvxRegister GetNextUnusedAvx2Register(RegisterUsageInfo& info);
AvxRegister GetNextUnusedAvx512Register(RegisterUsageInfo& info);

std::vector<uint8_t> GetUnusedAvx2QuadWords(RegisterUsageInfo& info);

std::vector<uint16_t> GetUnusedAvx512QuadWords(RegisterUsageInfo& info);

#endif  // LITECFI_REGISTER_UTILS_H_
