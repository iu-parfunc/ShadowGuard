
#include <string>

#include "asmjit/asmjit.h"
#include "jit_internal.h"
#include "register_utils.h"

using namespace asmjit::x86;

std::string JitMemoryStackPush(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();

  a->push(rax);
  a->push(rcx);
  a->mov(rcx, ptr(rsp, 16));

  // Get gs:ptr to rax
  asmjit::X86Mem shadow_ptr;
  shadow_ptr.setSize(8);
  shadow_ptr.setSegment(gs);
  shadow_ptr = shadow_ptr.adjusted(0);
  a->mov(rax, shadow_ptr);
  a->add(shadow_ptr, asmjit::imm(8));

  a->mov(ptr(rax), rcx);
  a->pop(rcx);
  a->pop(rax);
  return "";
}

std::string JitMemoryStackPop(RegisterUsageInfo& info, AssemblerHolder& ah) {
  asmjit::X86Assembler* a = ah.GetAssembler();
  asmjit::Label error = a->newLabel();

  a->push(rax);
  a->push(rcx);

  // Get gs:ptr to rax
  asmjit::X86Mem shadow_ptr;
  shadow_ptr.setSize(8);
  shadow_ptr.setSegment(gs);
  shadow_ptr = shadow_ptr.adjusted(0);
  a->mov(rax, shadow_ptr);

  a->mov(rcx, ptr(rax, -8));
  a->sub(shadow_ptr, asmjit::imm(8));

  a->cmp(rcx, ptr(rsp, 16));
  a->jnz(error);
  a->pop(rcx);
  a->pop(rax);
  a->ret();
  a->bind(error);
  a->int3();
  return "";
}
