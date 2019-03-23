
#include "jit.h"

#include <vector>

#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit_internal.h"
#include "register_utils.h"
#include "utils.h"

using namespace asmjit::x86;

const std::string kStackInitFunction = "litecfi_avx2_stack_init";
const std::string kStackPushFunction = "litecfi_avx2_stack_push";
const std::string kStackPopFunction = "litecfi_avx2_stack_pop";

const std::string kStackFunction = "litecfi_avx2_stack";

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

asmjit::X86Gp GetRaHolder() { return asmjit::x86::r10; }

bool HasEnoughStorage(RegisterUsageInfo& info) {
  int n_unused_avx_regs = 0;
  const std::vector<bool>& mask = info.GetUnusedAvx2Mask();
  for (auto unused : mask) {
    if (unused) {
      n_unused_avx_regs++;
    }
  }

  // We need at least two unused registers to hold stack state
  DCHECK(n_unused_avx_regs > 2) << "No free registers available for the stack";

  return true;
}

std::string JitCallStackPush(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (FLAGS_shadow_stack == "avx_v2") {
    return JitAvxV2CallStackPush(info, ah);
  } else if (FLAGS_shadow_stack == "avx_v3") {
    return JitAvxV3CallStackPush(info, ah);
  }

  return "";
}

std::string JitCallStackPush2(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (FLAGS_shadow_stack == "avx_v2") {
    return JitAvxV2CallStackPush2(info, ah);
  } else if (FLAGS_shadow_stack == "avx_v3") {
    return JitAvxV3CallStackPush2(info, ah);
  }

  return "";
}

std::string JitCallStackPop(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (FLAGS_shadow_stack == "avx_v2") {
    return JitAvxV2CallStackPop(info, ah);
  } else if (FLAGS_shadow_stack == "avx_v3") {
    return JitAvxV3CallStackPop(info, ah);
  }

  return "";
}

std::string JitCallStackPop2(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (FLAGS_shadow_stack == "avx_v2") {
    return JitAvxV2CallStackPop2(info, ah);
  } else if (FLAGS_shadow_stack == "avx_v3") {
    return JitAvxV3CallStackPop2(info, ah);
  }

  return "";
}

std::string JitStackInit(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (FLAGS_shadow_stack == "avx_v2") {
    return JitAvxV2StackInit(info, ah);
  } else if (FLAGS_shadow_stack == "avx_v3") {
    return JitAvxV3StackInit(info, ah);
  } else if (FLAGS_shadow_stack == "avx512") {
    // TODO(chamibuddhika) Test this
    return JitAvx512StackInit(info, ah);
  } else if (FLAGS_shadow_stack == "mem") {
    // TODO(chamibuddhika) Implement this
    // JitMemoryStackPush(info, a);
  } else if (FLAGS_shadow_stack == "dispatch") {
    return JitNopInit(info, ah);
  }
  return "";
}

std::string JitEmpty(AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  a->mov(rax, asmjit::imm(1));
  a->nop();

  return ah.GetStringLogger()->getString();
}

std::string JitStackPush(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (!HasEnoughStorage(info)) {
    return "";
  }

  if (FLAGS_shadow_stack == "avx_v2") {
    return JitAvxV2StackPush(info, ah);
  } else if (FLAGS_shadow_stack == "avx512") {
    // TODO(chamibuddhika) Test this
    return JitAvx512StackPush(info, ah);
  } else if (FLAGS_shadow_stack == "mem") {
    // TODO(chamibuddhika) Implement this
    // JitMemoryStackPush(info, a);
  } else if (FLAGS_shadow_stack == "dispatch") {
    return JitNopPush(info, ah);
  } else if (FLAGS_shadow_stack == "empty") {
    return JitEmpty(ah);
  }

  return "";
}

std::string JitStackPop(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (!HasEnoughStorage(info)) {
    return "";
  }

  if (FLAGS_shadow_stack == "avx_v2") {
    return JitAvxV2StackPop(info, ah);
  } else if (FLAGS_shadow_stack == "avx512") {
    // TODO(chamibuddhika) Test this
    return JitAvx512StackPop(info, ah);
  } else if (FLAGS_shadow_stack == "mem") {
    // TODO(chamibuddhika) Implement this
    // JitMemoryStackPop(info, a);
  } else if (FLAGS_shadow_stack == "dispatch") {
    return JitNopPop(info, ah);
  } else if (FLAGS_shadow_stack == "empty") {
    return JitEmpty(ah);
  }

  return "";
}

std::string JitStack(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (FLAGS_shadow_stack == "avx_v3") {
    return JitAvxV3Stack(info, ah);
  }
  return "";
}
