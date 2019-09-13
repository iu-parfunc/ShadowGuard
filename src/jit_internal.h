
#ifndef LITECFI_JIT_INTERNAL_H_
#define LITECFI_JIT_INTERNAL_H_

#include <string>

#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "jit.h"
#include "pass_manager.h"
#include "register_usage.h"

std::string JitMemoryStackPush(RegisterUsageInfo& info, FuncSummary* s,
                               AssemblerHolder& ah);
std::string JitMemoryStackPop(RegisterUsageInfo& info, FuncSummary* s,
                              AssemblerHolder& ah);

#endif  // LITECFI_JIT_INTERNAL_H_
