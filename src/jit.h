
#ifndef LITECFI_JIT_H_
#define LITECFI_JIT_H_

#include <string>

#include "Point.h"
#include "asmjit/asmjit.h"
#include "assembler.h"
#include "pass_manager.h"

std::string JitStackPush(Dyninst::PatchAPI::Point* pt, FuncSummary* s,
                         AssemblerHolder& ah, bool, int);

std::string JitStackPop(Dyninst::PatchAPI::Point* pt, FuncSummary* s,
                        AssemblerHolder& ah, bool, int);

std::string JitRegisterPush(Dyninst::PatchAPI::Point* pt, FuncSummary* s,
                            AssemblerHolder& ah, bool, int);

std::string JitRegisterPop(Dyninst::PatchAPI::Point* pt, FuncSummary* s,
                           AssemblerHolder& ah, bool, int);

#endif  // LITECFI_JIT_H_
