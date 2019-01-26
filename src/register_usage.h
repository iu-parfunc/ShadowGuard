
#ifndef LITECFI_REGISTER_USAGE_H_
#define LITECFI_REGISTER_USAGE_H_

#include <string>
#include <vector>

struct RegisterUsageInfo {
  // Mask of unused AVX and AVX2 registers
  std::vector<bool> unused_avx2_mask;
  // Mask of unused AVX, AVX2 and AVX512 registers
  std::vector<bool> unused_avx512_mask;
  // Mask of unused MMX registers
  std::vector<bool> unused_mmx_mask;
  // Mask of unused general purpose registers
  std::vector<bool> unused_gpr;
};

// Find unused registers across all functions in the application. This
// includes linked shared library functions.
RegisterUsageInfo GetUnusedRegisterInfo(std::string binary);

#endif  // LITECFI_REGISTER_USAGE_H_
