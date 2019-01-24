
#ifndef LITECFI_JIT_INTERNAL_H_
#define LITECFI_JIT_INTERNAL_H_

#include "asmjit/asmjit.h"
#include "register_usage.h"

void JitAvx2StackPush(const RegisterUsageInfo& info, asmjit::X86Assembler* a);
void JitAvx2StackPop(const RegisterUsageInfo& info, asmjit::X86Assembler* a);

void JitAvx512StackPush(const RegisterUsageInfo& info, asmjit::X86Assembler* a);
void JitAvx512StackPop(const RegisterUsageInfo& info, asmjit::X86Assembler* a);

void JitMemoryStackPush(const RegisterUsageInfo& info, asmjit::X86Assembler* a);
void JitMemoryStackPop(const RegisterUsageInfo& info, asmjit::X86Assembler* a);

#endif  // LITECFI_JIT_INTERNAL_H_
