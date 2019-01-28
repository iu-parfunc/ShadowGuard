
#ifndef LITECFI_JIT_INTERNAL_H_
#define LITECFI_JIT_INTERNAL_H_

#include "asmjit/asmjit.h"
#include "register_usage.h"

// Shadow stack implementation flag
DECLARE_string(shadow_stack);

void JitAvx2StackInit(RegisterUsageInfo& info, asmjit::X86Assembler* a);
void JitAvx2StackPush(RegisterUsageInfo& info, asmjit::X86Assembler* a);
void JitAvx2StackPop(RegisterUsageInfo& info, asmjit::X86Assembler* a);

void JitAvx512StackPush(RegisterUsageInfo& info, asmjit::X86Assembler* a);
void JitAvx512StackPop(RegisterUsageInfo& info, asmjit::X86Assembler* a);

void JitMemoryStackPush(RegisterUsageInfo& info, asmjit::X86Assembler* a);
void JitMemoryStackPop(RegisterUsageInfo& info, asmjit::X86Assembler* a);

void JitXorProlog(RegisterUsageInfo& info, asmjit::X86Assembler* a);
void JitXorEpilog(RegisterUsageInfo& info, asmjit::X86Assembler* a);

#endif  // LITECFI_JIT_INTERNAL_H_
