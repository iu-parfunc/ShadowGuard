
#ifndef LITECFI_ASSEMBLER_H_
#define LITECFI_ASSEMBLER_H_

#include "asmjit/asmjit.h"

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

#endif  // LITECFI_ASSEMBLER_H_
