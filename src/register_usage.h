
#ifndef LITECFI_REGISTER_USAGE_H_
#define LITECFI_REGISTER_USAGE_H_

#include <string>
#include <vector>

#include "call_graph.h"
#include "parse.h"

struct RegisterUsageInfo {
  // Used register set
  std::set<std::string> used;

  // If the analysis is precise or not. It may be imprecise due to the existence
  // of indirect calls
  bool is_precise = true;

  // Register masks
  //
  // Mask of unused AVX and AVX2 registers
  std::vector<bool> unused_avx2_mask;
  // Mask of unused AVX, AVX2 and AVX512 registers
  std::vector<bool> unused_avx512_mask;
  // Mask of unused MMX registers
  std::vector<bool> unused_mmx_mask;
  // Mask of unused general purpose registers
  std::vector<bool> unused_gpr;

  // Number of unused AVX2 registers
  int n_unused_avx2_regs;
  // Number of unused AVX512 registers
  int n_unused_avx512_regs;
  // Number of unused MMX registers
  int n_unused_mmx_regs;
};

// Find unused registers across all functions in the application and update the
// call graph with the register usage information. This also includes linked
// shared library functions.
void AnalyseRegisterUsage(std::string binary,
                          LazyCallGraph<RegisterUsageInfo>* call_graph,
                          const Parser& parser);

#endif  // LITECFI_REGISTER_USAGE_H_
