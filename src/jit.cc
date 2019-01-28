
#include "jit.h"

#include <vector>

// #include "Point.h"
// #include "Snippet.h"
#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit_internal.h"
#include "utils.h"

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

bool JitStackInit(RegisterUsageInfo info, asmjit::X86Assembler* a) {
  if (FLAGS_shadow_stack == "avx2") {
    JitAvx2StackInit(info, a);
  } else if (FLAGS_shadow_stack == "avx512") {
    // TODO(chamibuddhika) Implement this
    // JitAvx512StackPush(info, a);
  } else if (FLAGS_shadow_stack == "mem") {
    // TODO(chamibuddhika) Implement this
    // JitMemoryStackPush(info, a);
  }
  return true;
}

bool JitStackPush(RegisterUsageInfo info, asmjit::X86Assembler* a) {
  if (!HasEnoughStorage(info)) {
    return false;
  }

  if (FLAGS_shadow_stack == "avx2") {
    JitAvx2StackPush(info, a);
  } else if (FLAGS_shadow_stack == "avx512") {
    // TODO(chamibuddhika) Implement this
    // JitAvx512StackPush(info, a);
  } else if (FLAGS_shadow_stack == "mem") {
    // TODO(chamibuddhika) Implement this
    // JitMemoryStackPush(info, a);
  }

  return true;
}

bool JitStackPop(RegisterUsageInfo info, asmjit::X86Assembler* a) {
  if (!HasEnoughStorage(info)) {
    return false;
  }

  if (FLAGS_shadow_stack == "avx2") {
    JitAvx2StackPop(info, a);
  } else if (FLAGS_shadow_stack == "avx512") {
    // TODO(chamibuddhika) Implement this
    // JitAvx512StackPop(info, a);
  } else if (FLAGS_shadow_stack == "mem") {
    // TODO(chamibuddhika) Implement this
    // JitMemoryStackPop(info, a);
  }

  return true;
}
