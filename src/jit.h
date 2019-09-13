
#ifndef LITECFI_JIT_H_
#define LITECFI_JIT_H_

#include <string>

#include "asmjit/asmjit.h"
#include "pass_manager.h"
#include "register_usage.h"

class AssemblerHolder {
 public:
  AssemblerHolder();

  asmjit::x86::Assembler* GetAssembler();

  asmjit::StringLogger* GetStringLogger();

  asmjit::CodeHolder* GetCode();

 private:
  asmjit::JitRuntime* rt_;
  asmjit::CodeHolder* code_;
  asmjit::x86::Assembler* assembler_;
  asmjit::StringLogger* logger_;
};

std::string JitStackPush(RegisterUsageInfo info, FuncSummary* s,
                         AssemblerHolder& ah);

std::string JitStackPop(RegisterUsageInfo info, FuncSummary* s,
                        AssemblerHolder& ah);

#endif  // LITECFI_JIT_H_
