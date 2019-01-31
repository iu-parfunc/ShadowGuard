
#ifndef LITECFI_JIT_H_
#define LITECFI_JIT_H_

#include <string>

#include "asmjit/asmjit.h"
#include "register_usage.h"

const std::string kStackInitFunction = "__litecfi_avx2_stack_init";
const std::string kStackPushFunction = "__litecfi_avx2_stack_push";
const std::string kStackPopFunction = "__litecfi_avx2_stack_pop";

class AssemblerHolder {
 public:
  AssemblerHolder();

  asmjit::X86Assembler* GetAssembler();

  asmjit::StringLogger* GetStringLogger();

  asmjit::CodeHolder* GetCode();

 private:
  asmjit::JitRuntime* rt_;
  asmjit::CodeHolder* code_;
  asmjit::X86Assembler* assembler_;
  asmjit::StringLogger* logger_;
};

bool JitStackInit(RegisterUsageInfo info, AssemblerHolder& ah);

bool JitStackPush(RegisterUsageInfo info, AssemblerHolder& ah);

bool JitStackPop(RegisterUsageInfo info, AssemblerHolder& ah);

#endif  // LITECFI_JIT_H_
