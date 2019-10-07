
#include <iostream>
#include <string>

#include "CodeObject.h"
#include "Expression.h"
#include "Instruction.h"
#include "InstructionDecoder.h"
#include "Operand.h"
#include "Register.h"
#include "Visitor.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "slicing.h"

using namespace Dyninst;
using namespace ParseAPI;
using namespace InstructionAPI;
using namespace DataflowAPI;

using std::string;

class WriteMatchVisitor : public Visitor {
 public:
  explicit WriteMatchVisitor(string reg)
      : reg_(reg), is_dereference_(false), is_reg_match_(false){};

  ~WriteMatchVisitor(){};

  virtual void visit(BinaryFunction *b) override{};

  virtual void visit(Immediate *i) override{};

  virtual void visit(RegisterAST *r) override {
    std::cout << "Register : " << r->getID().name() << "\n";
    is_reg_match_ = (r->getID().name() == reg_);
    std::cout << "is_reg_match : " << is_reg_match_ << "\n";
  }

  virtual void visit(Dereference *d) override {
    std::cout << "Dereference found\n";
    is_dereference_ = true;
  };

  bool is_write_matched() { return is_dereference_ && is_reg_match_; }

  string reg_;
  bool is_dereference_;
  bool is_reg_match_;
};

Function *FindFunctionByName(CodeObject *co, string name) {
  const CodeObject::funclist &all = co->funcs();
  auto fit = all.begin();
  for (; fit != all.end(); ++fit) {
    Function *f = *fit;
    if (f->name().find(name) != std::string::npos) {
      return f;
    }
  }

  return nullptr;
}

CodeObject *GetCodeObject(string binary) {
  SymtabAPI::Symtab *symTab;
  if (SymtabAPI::Symtab::openFile(symTab, binary) == false) {
    std::cout << "error: file can not be parsed";
    exit(-1);
  }

  SymtabCodeSource *sts =
      new SymtabCodeSource(const_cast<char *>(binary.c_str()));
  CodeObject *co = new CodeObject(sts);
  co->parse();
  return co;
}

/**
   0x00000000000006aa <+0>:	push   %rbp
   0x00000000000006ab <+1>:	mov    %rsp,%rbp
   0x00000000000006ae <+4>:	mov    %rdi,-0x8(%rbp)
   0x00000000000006b2 <+8>:	mov    -0x8(%rbp),%rax
   0x00000000000006b6 <+12>:	movl   $0x2a,(%rax)
   0x00000000000006bc <+18>:	nop
   0x00000000000006bd <+19>:	pop    %rbp
   0x00000000000006be <+20>:	retq
**/

Instruction::Ptr GetMemoryWrite(Function *f, std::string reg) {
  std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;

  for (auto b : f->blocks()) {
    b->getInsns(insns);

    for (auto const &ins : insns) {
      auto category = ins.second.getCategory();
      // Call instructions always write to memory.
      // But they create new frames, so the writes are ignored.
      bool isCallOrRet = (category == Dyninst::InstructionAPI::c_CallInsn ||
                          category == Dyninst::InstructionAPI::c_ReturnInsn);
      if (isCallOrRet)
        continue;

      WriteMatchVisitor v(reg);
      if (ins.second.writesMemory()) {
        std::cout << ins.second.format() << "\n";
        std::set<Expression::Ptr> exprs;
        ins.second.getMemoryWriteOperands(exprs);

        if (exprs.size() > 1)
          continue;

        for (auto expr : exprs) {
          expr->apply(&v);

          if (v.is_write_matched()) {
            return Instruction::Ptr(new Instruction(ins.second));
          }
        }
      }
    }
  }

  return nullptr;
}

int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binary(argv[1]);
  string name = "simple_write";
  string reg = "x86_64::rax";

  Function *f = FindFunctionByName(GetCodeObject(binary), name);
  CHECK(f != nullptr);

  Instruction::Ptr ins = GetMemoryWrite(f, reg);
  CHECK(ins != nullptr);
}
