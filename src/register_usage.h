
#ifndef LITECFI_REGISTER_USAGE_H_
#define LITECFI_REGISTER_USAGE_H_

#include <string>
#include <vector>

struct RegisterUsageInfo {
  std::vector<bool> unused_avx_mask;  // Mask of unused AVX and AVX2 registers
  std::vector<bool> unused_mmx_mask;  // Mask of unused MMX registers
  std::vector<bool> unused_gpr;  // Mask of unused general purpose registers
};

// Find unused registers across all functions in the application. This
// includes linked shared library functions.
RegisterUsageInfo GetUnusedRegisterInfo(std::string binary);

#endif  // LITECFI_REGISTER_USAGE_H_
