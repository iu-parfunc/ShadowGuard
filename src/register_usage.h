
#ifndef LITECFI_REGISTER_USAGE_H_
#define LITECFI_REGISTER_USAGE_H_

#include <map>
#include <string>
#include <vector>

#include "parse.h"

class RegisterUsageInfo {
 public:
  const std::vector<bool>& GetUnusedAvx2Mask();
  const std::vector<bool>& GetUnusedAvx512Mask();
  const std::vector<bool>& GetUnusedMmxMask();

  // Used register set
  std::set<std::string> used_;

  // Number of unused AVX2 registers
  int n_unused_avx2_regs_;
  // Number of unused AVX512 registers
  int n_unused_avx512_regs_;
  // Number of unused MMX registers
  int n_unused_mmx_regs_;
  // Whether a function writes memory or not
  bool writesMemory_;
  // Whether a function writes to SP or not (excluding ret)
  bool writesSP_;
  // Whether a function calls other function
  bool containsCall_;

  // Name of the function
  std::string name_;

  bool ShouldSkip();

 private:
  // Register masks
  //
  // Mask of unused AVX and AVX2 registers
  std::vector<bool> unused_avx2_mask_;
  // Mask of unused AVX, AVX2 and AVX512 registers
  std::vector<bool> unused_avx512_mask_;
  // Mask of unused MMX registers
  std::vector<bool> unused_mmx_mask_;
};

struct Code {
  // Fully qualified path to the code object
  std::string path;
  // Register usage at each function in the code object
  std::map<std::string, RegisterUsageInfo*> register_usage;
};

// Find unused registers across all functions in the application and update the
// call graph with the register usage information. This also includes linked
// shared library functions.
std::map<std::string, Code*>* AnalyseRegisterUsage(std::string binary,
                                                   const Parser& parser);

#endif  // LITECFI_REGISTER_USAGE_H_
