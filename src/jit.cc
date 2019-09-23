
#include "jit.h"

#include <vector>

#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit_internal.h"
#include "utils.h"

using namespace asmjit::x86;

/**************** AssemblerHolder  *****************/

// Error handler that just prints the error and lets AsmJit ignore it.
class PrintErrorHandler : public asmjit::ErrorHandler {
 public:
  // Return `true` to set last error to `err`, return `false` to do nothing.
  void handleError(asmjit::Error err, const char* message,
                   asmjit::BaseEmitter* origin) override {
    fprintf(stderr, "ERROR: %s\n", message);
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

/********************** End ***********************/

asmjit::x86::Gp GetRaHolder() { return asmjit::x86::r10; }

std::string JitStackPush(FuncSummary* s, AssemblerHolder& ah) {
  return JitMemoryStackPush(s, ah);
}

std::string JitStackPop(FuncSummary* s, AssemblerHolder& ah) {
  return JitMemoryStackPop(s, ah);
}
