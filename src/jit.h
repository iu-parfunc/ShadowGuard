
#ifndef LITECFI_JIT_H_
#define LITECFI_JIT_H_

#include "asmjit/asmjit.h"
#include "register_usage.h"

bool JitStackPush(RegisterUsageInfo info, asmjit::X86Assembler* a);

bool JitStackPop(RegisterUsageInfo info, asmjit::X86Assembler* a);

#endif  // LITECFI_JIT_H_
