
#ifndef LITECFI_JIT_INTERNAL_H_
#define LITECFI_JIT_INTERNAL_H_

#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "jit.h"
#include "register_usage.h"

// Shadow stack implementation flag
DECLARE_string(shadow_stack);

void JitAvx2StackInit(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitAvx2StackPush(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitAvx2StackPop(RegisterUsageInfo& info, AssemblerHolder& ah);

void JitAvx512StackPush(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitAvx512StackPop(RegisterUsageInfo& info, AssemblerHolder& ah);

void JitMemoryStackPush(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitMemoryStackPop(RegisterUsageInfo& info, AssemblerHolder& ah);

void JitXorProlog(RegisterUsageInfo& info, AssemblerHolder& ah);
void JitXorEpilog(RegisterUsageInfo& info, AssemblerHolder& ah);

#endif  // LITECFI_JIT_INTERNAL_H_
