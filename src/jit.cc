
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

DECLARE_string(instrument);

asmjit::X86Gp GetRaHolder() {
  if (FLAGS_instrument == "shared") {
    return asmjit::x86::r10;
  }

  // Inlined instrumentation
  return asmjit::x86::rsp;
}

#define DUMMY_DISPATCH_PUSH(a, sp, sz)                                         \
  a->align(0 /* code-alignment */, 32);                                        \
  /* Make jump base address to be the start of dispatch code. */               \
  /* We deduct the size of lea instruction (7) from $rip to get it. */         \
  a->lea(rax, ptr(rip, -7));                                                   \
  a->vmovdqu(ptr(rsp, 8), sp);                                                 \
  a->pxor(sp, sp);                                                             \
  a->vpextrq(r11, sp, asmjit::imm(0));                                         \
  a->cmp(r11d, asmjit::imm(sz));                                               \
                                                                               \
  {                                                                            \
    /* Make the macro hygenic with locals in a separate scope */               \
    asmjit::Label no_overflow = a->newLabel();                                 \
    a->jb(no_overflow);                                                        \
                                                                               \
    /* Increment stack pointer */                                              \
    a->vpextrq(r11, sp, asmjit::imm(0));                                       \
    a->inc(r11);                                                               \
    a->pinsrq(sp, r11, asmjit::imm(0));                                        \
                                                                               \
    /* Return false to indicate push operation failed */                       \
    a->mov(rax, asmjit::imm(0));                                               \
    a->ret();                                                                  \
    a->bind(no_overflow);                                                      \
  }                                                                            \
                                                                               \
  /* Account for dispatch in the offset index */                               \
  a->add(r11, 3);                                                              \
                                                                               \
  /* Calculate jump target */                                                  \
  a->imul(r11, asmjit::imm(32));                                               \
  a->lea(rax, ptr(rax, r11));                                                  \
                                                                               \
  /* Increment stack pointer */                                                \
  a->vpextrq(r11, sp, asmjit::imm(0));                                         \
  a->inc(r11);                                                                 \
  a->pinsrq(sp, r11, asmjit::imm(0));                                          \
  a->vmovdqu(sp, ptr(rsp, 8));

#define DUMMY_DISPATCH_POP(a, sp, sz)                                          \
  a->align(0 /* code-alignment */, 32);                                        \
  /* Make jump base address to be the start of dispatch code. */               \
  /* We deduct the size of lea instruction (7) from $rip to get it. */         \
  a->lea(rax, ptr(rip, -7));                                                   \
  a->vmovdqu(ptr(rsp, 8), sp);                                                 \
  a->pxor(sp, sp);                                                             \
  a->vpextrq(r11, sp, asmjit::imm(0));                                         \
  a->vpextrq(r11, sp, asmjit::imm(0));                                         \
  a->cmp(r11d, asmjit::imm(sz));                                               \
                                                                               \
  {                                                                            \
    /* Make the macro hygenic with locals in a separate scope */               \
    asmjit::Label no_overflow = a->newLabel();                                 \
    a->jb(no_overflow);                                                        \
                                                                               \
    /* Increment stack pointer */                                              \
    a->vpextrq(r11, sp, asmjit::imm(0));                                       \
    a->inc(r11);                                                               \
    a->pinsrq(sp, r11, asmjit::imm(0));                                        \
                                                                               \
    /* Return false to indicate push operation failed */                       \
    a->mov(rax, asmjit::imm(0));                                               \
    a->ret();                                                                  \
    a->bind(no_overflow);                                                      \
  }                                                                            \
                                                                               \
  /* Account for dispatch in the offset index */                               \
  a->add(r11, 3);                                                              \
                                                                               \
  /* Calculate jump target */                                                  \
  a->imul(r11, asmjit::imm(32));                                               \
  a->lea(rax, ptr(rax, r11));                                                  \
                                                                               \
  /* Increment stack pointer */                                                \
  a->vpextrq(r11, sp, asmjit::imm(0));                                         \
  a->inc(r11);                                                                 \
  a->pinsrq(sp, r11, asmjit::imm(0));                                          \
  a->vmovdqu(sp, ptr(rsp, 8));

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

bool JitStackInit(RegisterUsageInfo info, AssemblerHolder& ah) {
  if (FLAGS_shadow_stack == "avx2") {
    JitAvx2StackInit(info, ah);
  } else if (FLAGS_shadow_stack == "avx512") {
    // TODO(chamibuddhika) Test this
    JitAvx512StackInit(info, ah);
  } else if (FLAGS_shadow_stack == "mem") {
    // TODO(chamibuddhika) Implement this
    // JitMemoryStackPush(info, a);
  }
  return true;
}

void JitNopPush(RegisterUsageInfo info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  AvxRegister scratch = GetNextUnusedAvx2Register(info);

  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  // Unused quadword element indices in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);

  // Jump table dispatch
  DUMMY_DISPATCH_PUSH(a, xmm15, quad_words.size());
}

void JitNopPop(RegisterUsageInfo info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  AvxRegister meta = GetNextUnusedAvx2Register(info);
  AvxRegister scratch = GetNextUnusedAvx2Register(info);

  // Stack pointer register
  asmjit::X86Xmm sp = meta.xmm;
  // Unused quadword element indices in AVX2 register file
  std::vector<uint8_t> quad_words = GetUnusedAvx2QuadWords(info);

  // Jump table dispatch
  DUMMY_DISPATCH_POP(a, xmm15, quad_words.size());
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
  } else if (FLAGS_shadow_stack == "nop") {
    JitNopPush(info, ah);
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
  } else if (FLAGS_shadow_stack == "nop") {
    JitNopPop(info, ah);
  }

  return true;
}
