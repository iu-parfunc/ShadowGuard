
#ifndef LITECFI_JIT_H_
#define LITECFI_JIT_H_

#include <string>

#include "asmjit/asmjit.h"
#include "assembler.h"
#include "pass_manager.h"

std::string JitStackPush(FuncSummary* s, AssemblerHolder& ah);

std::string JitStackPop(FuncSummary* s, AssemblerHolder& ah);

#endif  // LITECFI_JIT_H_
