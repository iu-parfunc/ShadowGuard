#include <string>

#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit.h"
#include "utils.h"

#include "PatchCFG.h"

using namespace asmjit::x86;

DECLARE_bool(optimize_regs);
DECLARE_bool(validate_frame);
DECLARE_string(shadow_stack);
DECLARE_string(dry_run);

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
  Gp tmp3;
  bool tmp1_saved;
  bool tmp2_saved;
  bool tmp3_saved;
  int sp_offset;

  TempRegisters(std::set<std::string> exclude = {"x86_64::rsp"}, int height = 0)
      : tmp1_saved(true), tmp2_saved(true), tmp3_saved(true),
        sp_offset(height /* flag saving always takes 8 bytes */) {
    int count = 0;
    for (auto it : kRegisterMap) {
      auto rit = exclude.find(it.first);
      if (rit == exclude.end()) {
        switch (count) {
        case 0:
          tmp1 = it.second;
          break;
        case 1:
          tmp2 = it.second;
          break;
        case 2:
          tmp3 = it.second;
          break;
        }
        count++;
      }

      if (count == 2)
        break;
    }

    DCHECK(count == 2);
  }

  TempRegisters(MoveInstData * mid, int height) {
    sp_offset = mid->raOffset + height;
    tmp1_saved = false;
    tmp1 = kRegisterMap[mid->reg1];
    if (mid->saveCount == 2) {
      tmp2_saved = false;
      tmp2 = kRegisterMap[mid->reg2];
    } else {
      tmp2_saved = true;
      auto it = kRegisterMap.begin();
      if (it->first == mid->reg1) ++it;
      tmp2 = it->second;
    }
    tmp3_saved = true;
  }
};

void SaveOrSkip(Assembler* a, TempRegisters* t, std::string reg_str, Gp* reg,
                bool* saved) {
  auto it = kRegisterMap.find(reg_str);
  if (it != kRegisterMap.end()) {
    *reg = it->second;
    *saved = false;
  } else {
    a->push(*reg);
    t->sp_offset += 8;
  }
}

inline void Save(Assembler* a, TempRegisters* t, Gp* reg) {
  a->push(*reg);
  t->sp_offset += 8;
}

TempRegisters UseSpecifiedRegisters(Assembler *a, MoveInstData* mid, int height = 0) {
    TempRegisters t(mid, height);
    if (t.tmp1_saved) Save(a, &t, &t.tmp1);
    if (t.tmp2_saved) Save(a, &t, &t.tmp2);
    return t;
}

TempRegisters SaveTempRegisters(Assembler* a,
                                std::set<std::string>& dead_registers,
                                std::set<std::string> exclude = {"x86_64::rsp"},
                                int height = 0) {
  TempRegisters t(exclude, height);
  if ((FLAGS_optimize_regs && dead_registers.empty()) || !FLAGS_optimize_regs) {
    Save(a, &t, &t.tmp1);
    Save(a, &t, &t.tmp2);
    // Save(a, &t, &t.tmp3);
  } else {
    auto reg = dead_registers.begin();
    if (reg++ != dead_registers.end()) {
      SaveOrSkip(a, &t, *reg, &t.tmp1, &t.tmp1_saved);
    } else {
      Save(a, &t, &t.tmp1);
    }

    if (reg++ != dead_registers.end()) {
      SaveOrSkip(a, &t, *reg, &t.tmp2, &t.tmp2_saved);
    } else {
      Save(a, &t, &t.tmp2);
    }

    if (reg++ != dead_registers.end()) {
      SaveOrSkip(a, &t, *reg, &t.tmp3, &t.tmp3_saved);
    } else {
      Save(a, &t, &t.tmp3);
    }
  }

  return t;
}

void RestoreTempRegisters(Assembler* a, TempRegisters t) {
  if (t.tmp3_saved) {
    // a->pop(t.tmp3);
  }

  if (t.tmp2_saved) {
    a->pop(t.tmp2);
  }

  if (t.tmp1_saved) {
    a->pop(t.tmp1);
  }
}

void SaveRa(const asmjit::x86::Mem& shadow_ptr, const Gp& sp_reg,
            const Gp& ra_reg, const TempRegisters& t, Assembler* a) {
  // Assembly:
  //
  //   pushfq
  //   mov 0x16(%rsp),%rcx
  //   mov %gs:0x0, %rax
  //   addq $0x8, %gs:0x0
  //   mov %rcx, (%rax)
  //   popfq
  a->mov(ra_reg, ptr(rsp, t.sp_offset));
  a->mov(sp_reg, shadow_ptr);
  a->mov(ptr(sp_reg), ra_reg);
  a->lea(sp_reg, ptr(sp_reg, 8));
  a->mov(shadow_ptr, sp_reg);
}

void SaveRaAndFrame(const asmjit::x86::Mem& shadow_ptr, const Gp& sp_reg,
                    const Gp& ra_reg, const TempRegisters& t, Assembler* a) {
  // Assembly:
  //
  //   pushfq
  //   mov 0x16(%rsp),%rcx
  //   mov %gs:0x0, %rax
  //   addq $0x16, %gs:0x0
  //   mov %rcx, (%rax)
  //   leaq rcx, 0x10(%rsp)
  //   mov %rcx, 0x8(%rax)
  //   popfq
  a->pushfq();
  a->mov(ra_reg, ptr(rsp, t.sp_offset));
  a->mov(sp_reg, shadow_ptr);
  a->add(shadow_ptr, asmjit::imm(16));
  a->mov(ptr(sp_reg), ra_reg);
  a->lea(ra_reg, ptr(rsp, 24));
  a->mov(ptr(sp_reg, 8), ra_reg);
  a->popfq();
}

std::string JitStackPush(Dyninst::PatchAPI::Point* pt, FuncSummary* s,
                         AssemblerHolder& ah, bool useOriginalCode, int height, bool useOriginalCodeFixed) {
  if (FLAGS_dry_run == "empty") return "";
  Assembler* a = ah.GetAssembler();
  TempRegisters t;
  MoveInstData* mid = nullptr;
  if (useOriginalCode) {
      Address blockEntry = pt->block()->start();
      mid = s->getMoveInstDataAtEntry(blockEntry);
  } else if (useOriginalCodeFixed) {
      Address blockEntry = pt->edge()->trg()->start();
      mid = s->getMoveInstDataFixedAtEntry(blockEntry);
  }
  
  if (mid != nullptr) {
      t = UseSpecifiedRegisters(a, mid, height);
  } else if (s != nullptr) {
    t = SaveTempRegisters(a, s->dead_at_entry, {}, height);
  } else {
    std::set<std::string> dead;
    t = SaveTempRegisters(a, dead);
  }

  Gp sp_reg = t.tmp1;
  Gp ra_reg = t.tmp2;

  asmjit::x86::Mem shadow_ptr;
  shadow_ptr.setSize(8);
  shadow_ptr.setSegment(gs);
  shadow_ptr = shadow_ptr.cloneAdjusted(0);
  if (FLAGS_dry_run != "only-save") {
    if (FLAGS_validate_frame) {
      SaveRaAndFrame(shadow_ptr, sp_reg, ra_reg, t, a);
    } else {
      SaveRa(shadow_ptr, sp_reg, ra_reg, t, a);
    }
  }

  RestoreTempRegisters(a, t);

  return "";
}

void ValidateRa(const asmjit::x86::Mem& shadow_ptr, const Gp& sp_reg,
                const Gp& ra_reg, const TempRegisters& t, Assembler* a,
                bool save_flags = true) {
  asmjit::Label error = a->newLabel();
  asmjit::Label done = a->newLabel();
  asmjit::Label loop = a->newLabel();

  // Assembly:
  //
  //   [pushfq]
  //   mov %gs:0x0,%rax
  // loop:
  //   mov -0x8(%rax), %rcx
  //   subq $0x8, %gs:0x0
  //   cmp 0x16(%rsp), %rcx
  //   je done
  //   sub $0x8, %rax
  //   cmpl $0x0, (%rax)
  //   je error
  //   jmp loop
  // error:
  //   int3 | sigill
  // done:
  //   [popfq]

  a->mov(sp_reg, shadow_ptr);

  a->bind(loop);
  a->lea(sp_reg, ptr(sp_reg, -8));
  a->mov(ra_reg, ptr(sp_reg));
  a->mov(shadow_ptr, sp_reg);
  a->cmp(ra_reg, ptr(rsp, t.sp_offset));
  a->je(done);

  a->cmp(dword_ptr(sp_reg), 0);
  a->je(error);

  a->jmp(loop);

  a->bind(error);
  // Cause a SIGILL instead of SIGTRAP to ease debuggability with GDB.
  const char sigill = 0x62;
  a->embed(&sigill, sizeof(char));

  a->bind(done);
}

void ValidateRaAndFrame(const asmjit::x86::Mem& shadow_ptr, const Gp& sp_reg,
                        const Gp& ra_reg, const TempRegisters& t,
                        Assembler* a) {
  asmjit::Label error = a->newLabel();
  asmjit::Label done = a->newLabel();
  asmjit::Label loop = a->newLabel();
  asmjit::Label unwind = a->newLabel();

  // Assembly:
  //
  //   pushfq
  //   mov %gs:0x0,%rax
  // loop:
  //   mov -0x10(%rax), %rcx
  //   subq $0x10, %gs:0x0
  //   cmp 0x16(%rsp), %rcx
  //   jne unwind
  //   leaq 0x16(%rsp), %rcx
  //   cmp -0x8(%rax), %rcx
  //   je success
  // unwind:
  //   sub $0x10, %rax
  //   cmpl $0x0, (%rax)
  //   je error
  //   jmp loop
  // error:
  //   int3 | sigill
  // done:
  //   popfq
  //   pop ...
  //   retq
  a->pushfq();
  a->mov(sp_reg, shadow_ptr);

  a->bind(loop);
  a->mov(ra_reg, ptr(sp_reg, -16));
  a->sub(shadow_ptr, asmjit::imm(16));
  a->cmp(ra_reg, ptr(rsp, t.sp_offset));
  a->jne(unwind);
  a->lea(ra_reg, ptr(rsp, t.sp_offset));
  a->cmp(ra_reg, ptr(sp_reg, -8));
  a->je(done);

  a->bind(unwind);
  a->sub(sp_reg, asmjit::imm(16));
  a->cmp(dword_ptr(sp_reg), 0);
  a->je(error);
  a->jmp(loop);

  a->bind(error);
  // Cause a SIGILL instead of SIGTRAP to ease debuggability with GDB.
  const char sigill = 0x62;
  a->embed(&sigill, sizeof(char));

  a->bind(done);
  a->popfq();
}

std::string JitStackPop(Dyninst::PatchAPI::Point* pt, FuncSummary* s,
                        AssemblerHolder& ah, bool useOriginalCode, int, bool) {
  if (FLAGS_dry_run == "empty") return "";
  Assembler* a = ah.GetAssembler();

  TempRegisters t;
  MoveInstData* mid = nullptr;
  if (useOriginalCode) {
      Address blockEntry = pt->block()->start();
      mid = s->getMoveInstDataAtExit(blockEntry);
  }
  
  if (mid != nullptr) {
      t = UseSpecifiedRegisters(a, mid);
  } else if (s != nullptr) {
    auto it = s->dead_at_exit.find(pt->addr());
    if (it != s->dead_at_exit.end()) {
      t = SaveTempRegisters(a, it->second);
    } else {
      std::set<std::string> dead;
      t = SaveTempRegisters(a, dead);
    }
  } else {
    std::set<std::string> dead;
    t = SaveTempRegisters(a, dead);
  }

  Gp sp_reg = t.tmp1;
  Gp ra_reg = t.tmp2;

  asmjit::x86::Mem shadow_ptr;
  shadow_ptr.setSize(8);
  shadow_ptr.setSegment(gs);
  shadow_ptr = shadow_ptr.cloneAdjusted(0);
  if (FLAGS_dry_run != "only-save") {
    if (FLAGS_validate_frame) {
      ValidateRaAndFrame(shadow_ptr, sp_reg, ra_reg, t, a);
    } else {
      ValidateRa(shadow_ptr, sp_reg, ra_reg, t, a);
    }
  }

  RestoreTempRegisters(a, t);

  return "";
}

std::pair<std::string, Gp> GetUnusedRegister(FuncSummary* s) {
  DCHECK(s->unused_regs.size() > 0);
  auto it = s->unused_regs.cbegin();

  auto rit = kRegisterMap.find(*it);
  DCHECK(rit != kRegisterMap.end());

  return std::make_pair(*it, rit->second);
}

std::string JitRegisterPush(Dyninst::PatchAPI::Point* pt, FuncSummary* s,
                            AssemblerHolder& ah, bool, int height, bool) {
  if (FLAGS_dry_run == "empty") return "";
  auto pair = GetUnusedRegister(s);
  Gp reg = pair.second;

  // Assembly:
  //
  //   push %<unused_reg>
  //   pushfq
  //   mov 0x10(%rsp),%<unused_reg>
  //   popfq
  Assembler* a = ah.GetAssembler();
  //a->push(reg);
  //a->pushfq();
  asmjit::x86::Mem scratch;
  scratch.setSize(8);
  scratch.setSegment(gs);
  scratch = scratch.cloneAdjusted(8);

  a->mov(scratch, reg);
  if (FLAGS_dry_run != "only-save")
    a->mov(reg, ptr(rsp, height));
  //a->popfq();

  return "";
}

std::string JitRegisterPop(Dyninst::PatchAPI::Point* pt, FuncSummary* s,
                           AssemblerHolder& ah, bool, int, bool) {
  if (FLAGS_dry_run == "empty") return "";
  // Here we rely on the fact that stl::set iteration order is deterministic
  // across multiple invocations (i.e: we will get the same register that we got
  // during stack push using the iterator).
  auto pair = GetUnusedRegister(s);
  std::string reg_str = pair.first;
  Gp reg = pair.second;

  // Assembly:
  //
  //   pushfq
  //   cmp %<unused_reg>, 0x10(%rsp)
  //   je success
  //   push %rcx                        ; begin stack unwinding
  //   push %rax
  //   mov %gs:0x0,%rax
  // loop:
  //   mov -0x8(%rax), %rcx
  //   subq $0x8, %gs:0x0
  //   cmp 0x20(%rsp), %rcx
  //   je done
  //   sub $0x8, %rax
  //   cmpl $0x0, (%rax)
  //   je error
  //   jmp loop
  // error:
  //   int3 | sigill
  // done:
  //   pop %rax
  //   pop %rcx                         ; end stack unwinding
  // success:
  //   popfq
  //   pop %<unused_reg>
  Assembler* a = ah.GetAssembler();
  if (FLAGS_dry_run != "only-save") {
    asmjit::Label success = a->newLabel();

    a->cmp(reg, ptr(rsp));
    a->je(success);

    // Fall through for stack unwind scenario.
    std::set<std::string> dead;
    TempRegisters t = SaveTempRegisters(a, dead, {reg_str});

    Gp sp_reg = t.tmp1;
    Gp ra_reg = t.tmp2;

    asmjit::x86::Mem shadow_ptr;
    shadow_ptr.setSize(8);
    shadow_ptr.setSegment(gs);
    shadow_ptr = shadow_ptr.cloneAdjusted(0);
    ValidateRa(shadow_ptr, sp_reg, ra_reg, t, a, false /* save_flags */);
    RestoreTempRegisters(a, t);

    a->bind(success);
  }
  asmjit::x86::Mem scratch;
  scratch.setSize(8);
  scratch.setSegment(gs);
  scratch = scratch.cloneAdjusted(8);
  a->mov(reg, scratch);
  return "";
}

#include "Instruction.h"
#include "BinaryFunction.h"
#include "Immediate.h"
#include "Register.h"
#include "Dereference.h"
#include "Visitor.h"
#include "Result.h"
#include "register_utils.h"

class AddressingModeVisitor: public Dyninst::InstructionAPI::Visitor {

public:
  AddressingModeVisitor():
    scale(0), disp(0), hasBaseReg(false), hasIndexReg(false) {}

  Dyninst::MachRegister baseReg;
  Dyninst::MachRegister indexReg;
  int scale;
  int64_t disp;

  bool hasBaseReg;
  bool hasIndexReg;

  std::vector<Dyninst::InstructionAPI::RegisterAST*> regStack;
  std::vector<Dyninst::InstructionAPI::Immediate*> immStack;

  void finalize() {
    if (regStack.size() > 0) {
      assert(regStack.size() == 1);
      hasBaseReg = true;
      baseReg = regStack[0]->getID().getBaseRegister();
      regStack.clear();
    }
    if (immStack.size() > 0) {
      assert(immStack.size() == 1);
      disp = getImmediate(immStack[0]);
      immStack.clear();
    }
  }

  int64_t getImmediate(Dyninst::InstructionAPI::Immediate* imm) {
    const Dyninst::InstructionAPI::Result& r = imm->eval();
    return r.convert<int64_t>();
  }

  void visit(Dyninst::InstructionAPI::BinaryFunction *b) {
    std::vector<Dyninst::InstructionAPI::Expression::Ptr> children;
    b->getChildren(children);
    if (b->isAdd()) {
      if (immStack.size() > 0) {
        Dyninst::InstructionAPI::Immediate *imm = *immStack.rbegin();
        immStack.pop_back();
        disp = getImmediate(imm);
      }
      if (regStack.size() >= 1) {
        hasBaseReg = true;
        Dyninst::InstructionAPI::RegisterAST* reg = regStack[0];
        baseReg = reg->getID().getBaseRegister();
        if (regStack.size() == 2) {
          hasIndexReg = true;
          reg = regStack[1];
          indexReg = reg->getID().getBaseRegister();
        }
        regStack.clear();
      }
    } else if (b->isMultiply()) {
      Dyninst::InstructionAPI::RegisterAST* reg = *regStack.rbegin();
      regStack.pop_back();
      hasIndexReg = true;
      indexReg = reg->getID().getBaseRegister();

      Dyninst::InstructionAPI::Immediate *imm = *immStack.rbegin();
      immStack.pop_back();
      int64_t val = getImmediate(imm);
      switch (val) {
        case 1: {
          scale = 0;
          break;
        }
        case 2: {
          scale = 1;
          break;
        }
        case 4: {
          scale = 2;
          break;
        }
        case 8: {
          scale = 3;
          break;
        }
        default:
          fprintf(stderr, "Unhandled scale %d\n", (int)val);
      }
    }
  }
  void visit(Dyninst::InstructionAPI::Immediate* i) {
    immStack.push_back(i);
  }
  void visit(Dyninst::InstructionAPI::RegisterAST* r) {
    regStack.push_back(r);
  }
  void visit(Dyninst::InstructionAPI::Dereference * d) {
  }
};

void JitConditionalBranch(Assembler* a, Gp& eff_reg, int off, asmjit::Label& label) {
  asmjit::x86::Mem bound;
  bound.setSize(8);
  bound.setSegment(gs);
  bound = bound.cloneAdjusted(off);
  a->cmp(eff_reg, bound);
  a->jb(label);
}

void JitBoundCheck1(Assembler* a, Gp& eff_reg, asmjit::Label& done, asmjit::Label& error) {
  /*  First version of bound check 
   *  if (effAddr < global_stack_lower_bound) goto done; // Heap access
   *  if (effAddr < local_stack_bottom) goto error; // Not heap and smaller than local stack
   *  if (effAddr < local_stack_top) goto done; // In local stack
   *  error as it beyond local stack
   */
  JitConditionalBranch(a, eff_reg, 32, done);
  JitConditionalBranch(a, eff_reg, 24, error);
  JitConditionalBranch(a, eff_reg, 16, done);
}

void JitBoundCheck2(Assembler* a, Gp& eff_reg, asmjit::Label& done, asmjit::Label&) {
  a->sub(eff_reg, rsp);
  a->sar(eff_reg, 24);
  a->cmp(eff_reg, -128);
  a->jl(done);
  a->cmp(eff_reg, 0);
  a->jz(done);
}
std::string JitSFI(Dyninst::PatchAPI::Point* point, FuncSummary *s, AssemblerHolder& ah) {
  if (FLAGS_dry_run == "empty") return "";
  Assembler* a = ah.GetAssembler();

  // Get the instruction that writes memory
  const Dyninst::InstructionAPI::Instruction& insn = point->insn();

  // Get effective address expression
  std::set<Dyninst::InstructionAPI::Expression::Ptr> effectiveAddressExprs;
  insn.getMemoryWriteOperands(effectiveAddressExprs);
  assert(effectiveAddressExprs.size() == 1);
  Dyninst::InstructionAPI::Expression::Ptr expr = *(effectiveAddressExprs.begin());

  // Analyze the addressing mode to determine whether we need a scratch register
  // to compute the effective address
  bool needScratchReg = false;
  AddressingModeVisitor amv;
  expr->apply(&amv);
  amv.finalize();

  Gp eff_reg;
  Gp base;
  Gp index;
  bool baseIsPC = false;

  std::set<std::string> exclude;
  exclude.insert("x86_64::rsp");
  if (amv.hasBaseReg) {
    std::string baseName = NormalizeRegisterName(amv.baseReg.name());
    if (baseName == "x86_64::rip") {
      baseIsPC = true;
    } else {
      assert(kRegisterMap.find(baseName) != kRegisterMap.end());
      base = kRegisterMap[baseName];
      exclude.insert(baseName);
    }
  }
  if (amv.hasIndexReg) {
    std::string indexName = NormalizeRegisterName(amv.indexReg.name());
    assert(kRegisterMap.find(indexName) != kRegisterMap.end());
    assert(indexName != "x86_64::rip");
    index = kRegisterMap[indexName];
    exclude.insert(indexName);
  }
  TempRegisters t(exclude);
  eff_reg = t.tmp1; 
  if (baseIsPC) return "";
  asmjit::Label done = a->newLabel();
  asmjit::Label error = a->newLabel();

  // If the function uses red zone,
  // move down SP to avoid overwriting it 
  if (s->redZoneAccess.size() > 0)
    a->lea(rsp, ptr(rsp, -128));
  // Save flags
  a->pushfq();
  a->push(eff_reg);
  
  // Emit a lea to calcualte the effective address
  asmjit::x86::Mem mem;
  mem.setSize(8);
  if (baseIsPC) {
    mem.setBase(rip);
  } else {
    mem.setBase(base);
  }
  mem.setIndex(index);
  mem.setOffset(amv.disp);
  mem.setShift(amv.scale);
  a->lea(eff_reg, mem);

  JitBoundCheck1(a, eff_reg, done, error);
  //JitBoundCheck2(a, eff_reg, done, error);
  a->bind(error);
  // Cause a SIGILL instead of SIGTRAP to ease debuggability with GDB.
  const char sigill = 0x62;
  a->embed(&sigill, sizeof(char));

  a->bind(done);
  a->pop(eff_reg);
  a->popfq();
  if (s->redZoneAccess.size() > 0)
    a->lea(rsp, ptr(rsp, 128));
  return "";
}

std::string JitInit(Dyninst::Address curAddr, Dyninst::Address shadowStart, AssemblerHolder& ah) {
  Assembler* a = ah.GetAssembler();
  a->push(rax);
  a->push(rdi);
  a->push(rsi);
  a->push(rdx);
  a->push(r8);
  a->push(r9);
  a->push(r10);
  a->push(r15);
  a->lea(r15, ptr(rip, shadowStart - (curAddr + ah.GetCode()->codeSize() + 7)));
  a->mov(rax, 0x9e);
  a->mov(rdi, 0x1001);
  a->mov(rsi, r15);
  a->mov(rdx, 0);
  a->mov(r8, 0);
  a->mov(r9, 0);
  a->mov(r10, 0);
  a->syscall();
  a->mov(qword_ptr(r15, 0x0), 0);
  a->mov(qword_ptr(r15, 0x8), 0);
  a->mov(qword_ptr(r15, 0x10), 0);
  a->mov(qword_ptr(r15, 0x18), 0);
  a->mov(qword_ptr(r15, 0x20), 0);
  a->mov(qword_ptr(r15, 0x28), 0);
  a->mov(qword_ptr(r15, 0x30), 0);
  a->lea(r15, ptr(r15, 0x38));  
  asmjit::x86::Mem shadow_ptr;
  shadow_ptr.setSize(8);
  shadow_ptr.setSegment(gs);
  shadow_ptr = shadow_ptr.cloneAdjusted(0);
  a->mov(shadow_ptr, r15);

  a->pop(r15);
  a->pop(r10);
  a->pop(r9);
  a->pop(r8);
  a->pop(rdx);
  a->pop(rsi);
  a->pop(rdi);
  a->pop(rax);
  return "";
}

