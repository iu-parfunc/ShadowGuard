
#ifndef LITECFI_JIT_H_
#define LITECFI_JIT_H_

#include <string>

#include "asmjit/asmjit.h"
#include "register_usage.h"

extern const std::string kStackInitFunction;
extern const std::string kStackPushFunction;
extern const std::string kStackPopFunction;

extern const std::string kRegisterSpillFunction;
extern const std::string kRegisterRestoreFunction;

extern const std::string kContextSaveFunction;
extern const std::string kContextRestoreFunction;

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
