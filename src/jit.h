
#ifndef LITECFI_JIT_H_
#define LITECFI_JIT_H_

#include <string>

#include "Point.h"
#include "asmjit/asmjit.h"
#include "assembler.h"
#include "pass_manager.h"

std::string JitStackPush(Dyninst::PatchAPI::Point* pt, FuncSummary* s,
                         AssemblerHolder& ah);

std::string JitStackPop(Dyninst::PatchAPI::Point* pt, FuncSummary* s,
                        AssemblerHolder& ah);

#endif  // LITECFI_JIT_H_
