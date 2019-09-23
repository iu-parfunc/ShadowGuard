
#ifndef LITECFI_JIT_INTERNAL_H_
#define LITECFI_JIT_INTERNAL_H_

#include <string>

#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "jit.h"
#include "pass_manager.h"

std::string JitMemoryStackPush(FuncSummary* s, AssemblerHolder& ah);
std::string JitMemoryStackPop(FuncSummary* s, AssemblerHolder& ah);

#endif  // LITECFI_JIT_INTERNAL_H_
