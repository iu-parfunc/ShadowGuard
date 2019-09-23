
#include <string>

#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "jit_internal.h"
#include "utils.h"

using namespace asmjit::x86;

DECLARE_bool(optimize_regs);

static std::map<std::string, Gp> kRegisterMap = {
    {"x86_64::rax", rax}, {"x86_64::rbx", rbx}, {"x86_64::rcx", rcx},
    {"x86_64::rdx", rdx}, {"x86_64::rsp", rsp}, {"x86_64::rbp", rbp},
    {"x86_64::rsi", rsi}, {"x86_64::rdi", rdi}, {"x86_64::r8", r8},
    {"x86_64::r9", r9},   {"x86_64::r10", r10}, {"x86_64::r11", r11},
    {"x86_64::r12", r12}, {"x86_64::r13", r13}, {"x86_64::r14", r14},
    {"x86_64::r15", r15}};

struct TempRegisters {
  Gp tmp1;
  Gp tmp2;
  bool tmp1_saved;
  bool tmp2_saved;

  TempRegisters() : tmp1(rax), tmp2(rcx), tmp1_saved(true), tmp2_saved(true) {}
};

TempRegisters SaveTempRegisters(Assembler* a,
                                std::set<std::string>& dead_registers) {
  TempRegisters t;
  if ((FLAGS_optimize_regs && dead_registers.empty()) || !FLAGS_optimize_regs) {
    a->push(t.tmp1);
    a->push(t.tmp2);
  } else {
    auto reg = dead_registers.begin();
    auto it = kRegisterMap.find(*reg);
    if (it != kRegisterMap.end()) {
      t.tmp1 = it->second;
      t.tmp1_saved = false;
    } else {
      a->push(t.tmp1);
    }

    ++reg;
    if (reg != dead_registers.end()) {
      auto it = kRegisterMap.find(*reg);
      if (it != kRegisterMap.end()) {
        t.tmp2 = it->second;
        t.tmp2_saved = false;
      } else {
        a->push(t.tmp2);
      }
    } else {
      a->push(t.tmp2);
    }
  }

  return t;
}

void RestoreTempRegisters(Assembler* a, TempRegisters t) {
  if (t.tmp2_saved) {
    a->pop(t.tmp2);
  }

  if (t.tmp1_saved) {
    a->pop(t.tmp1);
  }
}

std::string JitMemoryStackPush(FuncSummary* s, AssemblerHolder& ah) {
  Assembler* a = ah.GetAssembler();

  TempRegisters t = SaveTempRegisters(a, s->dead_at_entry);

  Gp s_ptr = t.tmp1;
  Gp ra_holder = t.tmp2;

  a->mov(ra_holder, ptr(rsp, 16));

  // Get gs:ptr to rax
  asmjit::x86::Mem shadow_ptr;
  shadow_ptr.setSize(8);
  shadow_ptr.setSegment(gs);
  shadow_ptr = shadow_ptr.cloneAdjusted(0);
  a->mov(s_ptr, shadow_ptr);
  a->add(shadow_ptr, asmjit::imm(8));

  a->mov(ptr(s_ptr), ra_holder);

  RestoreTempRegisters(a, t);

  return "";
}

std::string JitMemoryStackPop(FuncSummary* s, AssemblerHolder& ah) {
  Assembler* a = ah.GetAssembler();
  asmjit::Label error = a->newLabel();
  asmjit::Label success = a->newLabel();
  asmjit::Label loop = a->newLabel();

  TempRegisters t = SaveTempRegisters(a, s->dead_at_entry);

  Gp s_ptr = t.tmp1;
  Gp ra_holder = t.tmp2;

  // Get gs:ptr to rax
  asmjit::x86::Mem shadow_ptr;
  shadow_ptr.setSize(8);
  shadow_ptr.setSegment(gs);
  shadow_ptr = shadow_ptr.cloneAdjusted(0);
  a->mov(s_ptr, shadow_ptr);

  a->bind(loop);
  a->mov(ra_holder, ptr(s_ptr, -8));
  a->sub(shadow_ptr, asmjit::imm(8));
  a->cmp(ra_holder, ptr(rsp, 16));
  a->je(success);

  a->sub(s_ptr, asmjit::imm(8));
  a->cmp(dword_ptr(s_ptr), 0);
  a->je(error);
  a->jmp(loop);

  a->bind(error);
  a->int3();

  a->bind(success);
  RestoreTempRegisters(a, t);

  return "";
}
