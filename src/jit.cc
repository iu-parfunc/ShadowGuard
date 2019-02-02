
#include "jit.h"

#include <vector>

#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit_internal.h"
#include "utils.h"

/**************** AssemblerHolder  *****************/

// Error handler that just prints the error and lets AsmJit ignore it.
class PrintErrorHandler : public asmjit::ErrorHandler {
 public:
  // Return `true` to set last error to `err`, return `false` to do nothing.
  bool handleError(asmjit::Error err, const char* message,
                   asmjit::CodeEmitter* origin) override {
    fprintf(stderr, "ERROR: %s\n", message);
    return false;
  }
};

AssemblerHolder::AssemblerHolder() {
  rt_ = new asmjit::JitRuntime();

  code_ = new asmjit::CodeHolder;
  code_->init(rt_->getCodeInfo());
  code_->setErrorHandler(new PrintErrorHandler());

  logger_ = new asmjit::StringLogger();
  code_->setLogger(logger_);

  assembler_ = new asmjit::X86Assembler(code_);
}

asmjit::X86Assembler* AssemblerHolder::GetAssembler() { return assembler_; }

asmjit::StringLogger* AssemblerHolder::GetStringLogger() { return logger_; }

asmjit::CodeHolder* AssemblerHolder::GetCode() { return code_; }

/********************** End ***********************/

bool HasEnoughStorage(const RegisterUsageInfo& info) {
  int n_unused_avx_regs = 0;
  for (auto unused : info.unused_avx2_mask) {
    if (unused) {
      n_unused_avx_regs++;
    }
  }

  // We need at least two unused registers to hold stack state
  DCHECK(n_unused_avx_regs > 2) << "No free registers available for the stack";

  return true;
}

bool JitStackInit(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (FLAGS_shadow_stack == "avx2") {
    JitAvx2StackInit(info, ah);
  } else if (FLAGS_shadow_stack == "avx512") {
    // TODO(chamibuddhika) Test this
    JitAvx512StackPush(info, ah);
  } else if (FLAGS_shadow_stack == "mem") {
    // TODO(chamibuddhika) Implement this
    // JitMemoryStackPush(info, a);
  }
  return true;
}

bool JitStackPush(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (!HasEnoughStorage(info)) {
    return false;
  }

  if (FLAGS_shadow_stack == "avx2") {
    JitAvx2StackPush(info, ah);
  } else if (FLAGS_shadow_stack == "avx512") {
    // TODO(chamibuddhika) Test this
    JitAvx512StackPush(info, ah);
  } else if (FLAGS_shadow_stack == "mem") {
    // TODO(chamibuddhika) Implement this
    // JitMemoryStackPush(info, a);
  }

  return true;
}

bool JitStackPop(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (!HasEnoughStorage(info)) {
    return false;
  }

  if (FLAGS_shadow_stack == "avx2") {
    JitAvx2StackPop(info, ah);
  } else if (FLAGS_shadow_stack == "avx512") {
    // TODO(chamibuddhika) Test this
    JitAvx512StackPop(info, ah);
  } else if (FLAGS_shadow_stack == "mem") {
    // TODO(chamibuddhika) Implement this
    // JitMemoryStackPop(info, a);
  }

  return true;
}
