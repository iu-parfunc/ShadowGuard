
#include <iostream>

#include "assembler.h"

// Error handler that just prints the error and lets AsmJit ignore it.
class PrintErrorHandler : public asmjit::ErrorHandler {
 public:
  // Return `true` to set last error to `err`, return `false` to do nothing.
  void handleError(asmjit::Error err, const char* message,
                   asmjit::BaseEmitter* origin) override {
    std::cerr << "ERROR : " << message;
  }
};

AssemblerHolder::AssemblerHolder() {
  rt_ = new asmjit::JitRuntime();

  code_ = new asmjit::CodeHolder;
  code_->init(rt_->codeInfo());
  code_->setErrorHandler(new PrintErrorHandler());

  logger_ = new asmjit::StringLogger();
  code_->setLogger(logger_);

  assembler_ = new asmjit::x86::Assembler(code_);
}

asmjit::x86::Assembler* AssemblerHolder::GetAssembler() { return assembler_; }

asmjit::StringLogger* AssemblerHolder::GetStringLogger() { return logger_; }

asmjit::CodeHolder* AssemblerHolder::GetCode() { return code_; }
