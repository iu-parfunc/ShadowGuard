#ifndef LITECFI_HEAP_H_
#define LITECFI_HEAP_H_

#include <map>
#include <set>
#include <stack>

#include "CFG.h"
#include "Instruction.h"
#include "Register.h"
#include "dyn_regs.h"
#include "glog/logging.h"
#include "pass_manager.h"

namespace heap {

using Dyninst::MachRegister;
using Dyninst::Offset;
using Dyninst::InstructionAPI::BinaryFunction;
using Dyninst::InstructionAPI::Dereference;
using Dyninst::InstructionAPI::Expression;
using Dyninst::InstructionAPI::Immediate;
using Dyninst::InstructionAPI::Instruction;
using Dyninst::InstructionAPI::RegisterAST;
using Dyninst::InstructionAPI::Visitor;
using Dyninst::ParseAPI::Block;
using Dyninst::ParseAPI::Function;

enum class Location { HEAP, STACK, ARG, HEAP_OR_ARG, TOP, BOTTOM };

char buf[128];

struct AbstractLocation {
  AbstractLocation()
      : type(Location::BOTTOM), stack_height(INT_MAX), points_to(nullptr) {}

  AbstractLocation(const AbstractLocation& l)
      : type(l.type), stack_height(l.stack_height) {
    if (l.points_to == nullptr) {
      points_to = nullptr;
    } else {
      points_to = new AbstractLocation;
      *points_to = *l.points_to;
    }
  }

  ~AbstractLocation() {
    if (points_to != nullptr) delete points_to;
    points_to = nullptr;
  }

  Location type;
  int stack_height;
  AbstractLocation* points_to;

  std::string format() const {
    std::string ret = "AbsLoc: ";
    switch (type) {
      case Location::HEAP:
        ret += "HEAP";
        break;
      case Location::STACK:
        ret += "STACK(";
        snprintf(buf, 128, "%d", stack_height);
        ret += buf;
        ret += ")";
        break;
      case Location::ARG:
        ret += "ARG";
        break;
      case Location::HEAP_OR_ARG:
        ret += "HEAP_OR_ARG";
        break;
      case Location::TOP:
        ret += "TOP";
        break;
      case Location::BOTTOM:
        ret += "BOTTOM";
        break;
      default:
        ret += "ERROR TYPE";
    }
    if (points_to != nullptr) {
      ret += ", point to [";
      ret += points_to->format();
      ret += "]";
    }
    return ret;
  }

  AbstractLocation pointToLocation() const {
    if (points_to == nullptr)
      return GetTop();
    else
      return *points_to;
  }

  void setPointToLocation(const AbstractLocation& l) {
    if (points_to != nullptr) delete points_to;
    if (l == GetTop()) {
      points_to = nullptr;
    } else {
      points_to = new AbstractLocation;
      *points_to = l;
    }
  }

  bool operator==(const AbstractLocation& l) const {
    if (type != l.type)
      return false;

    if (type == Location::STACK) {
      if (stack_height != l.stack_height)
        return false;

      if ((points_to == nullptr && l.points_to != nullptr) ||
          (points_to != nullptr && l.points_to == nullptr))
        return false;

      if (points_to != nullptr && l.points_to != nullptr)
        return *points_to == *(l.points_to);
    }

    return true;
  }

  bool operator!=(const AbstractLocation& l) const { return !(*this == l); }

  AbstractLocation& operator=(const AbstractLocation& l) {
    type = l.type;
    stack_height = l.stack_height;
    if (l.points_to == nullptr) {
      points_to = nullptr;
    } else {
      points_to = new AbstractLocation;
      assert(l.points_to->points_to == nullptr);
      points_to->type = l.points_to->type;
      points_to->stack_height = l.points_to->stack_height;
      points_to->points_to = nullptr;
    }
    return *this;
  }

  void Meet(const AbstractLocation& other) {
    if (type == Location::TOP || other.type == Location::TOP) {
      Reset();
      type = Location::TOP;
      return;
    }

    if (type == Location::BOTTOM) {
      *this = other;
      return;
    }

    if (other.type == Location::BOTTOM)
      return;

    if (other.type == type) {
      if (type == Location::STACK) {
        if (stack_height != other.stack_height) {
          Reset();
          type = Location::TOP;
          return;
        }

        if (points_to != nullptr) {
          points_to->Meet(other.pointToLocation());
        }
      }
      return;
    }

    if ((type == Location::ARG && other.type == Location::HEAP) ||
        (type == Location::HEAP && other.type == Location::ARG) ||
        (type == Location::HEAP_OR_ARG && other.type == Location::HEAP) ||
        (type == Location::HEAP_OR_ARG && other.type == Location::ARG) ||
        (type == Location::HEAP && other.type == Location::HEAP_OR_ARG) ||
        (type == Location::ARG && other.type == Location::HEAP_OR_ARG)) {
      Reset();
      type = Location::HEAP_OR_ARG;
      return;
    }
    DCHECK(false);
  }

  void Reset() {
    type = Location::BOTTOM;
    stack_height = INT_MAX;
    if (points_to != nullptr) delete points_to;
    points_to = nullptr;
  }

  static AbstractLocation GetStackLocation(int height) {
    AbstractLocation loc;
    loc.type = Location::STACK;
    loc.stack_height = height;
    return loc;
  }

  static AbstractLocation GetArgLocation() {
    AbstractLocation loc;
    loc.type = Location::ARG;
    return loc;
  }

  static AbstractLocation GetHeapLocation() {
    AbstractLocation loc;
    loc.type = Location::HEAP;
    return loc;
  }

  static AbstractLocation GetHeapOrArgLocation() {
    AbstractLocation loc;
    loc.type = Location::HEAP_OR_ARG;
    return loc;
  }

  static AbstractLocation GetTop() {
    AbstractLocation loc;
    loc.type = Location::TOP;
    return loc;
  }

  static AbstractLocation GetBottom() {
    AbstractLocation loc;
    loc.type = Location::BOTTOM;
    return loc;
  }
};

struct HeapContext {
  Address addr;
  Block* block;
  Instruction ins;

  std::map<MachRegister, AbstractLocation> regs;
  std::map<int, AbstractLocation> stack;

  HeapContext* successor;

  HeapContext() : successor(nullptr) {
    regs[Dyninst::x86_64::rax] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::rbx] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::rcx] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::rdx] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::rsp] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::rbp] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::rsi] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::rdi] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::r8] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::r9] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::r10] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::r11] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::r12] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::r13] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::r14] = AbstractLocation::GetBottom();
    regs[Dyninst::x86_64::r15] = AbstractLocation::GetBottom();
  }

  // This copy constructor is used to compare whether
  // data flow fact has changed. The successor field is
  // not part of the data flow fact.
  HeapContext(const HeapContext& c) {
    regs = c.regs;
    stack = c.stack;
    successor = nullptr;
  }

  ~HeapContext() {
    regs.clear();
    stack.clear();
  }

  void Meet(HeapContext* other) {
    if (this == other) return;
    PointWiseMeet<MachRegister>(regs, other->regs);
    PointWiseMeet<int>(stack, other->stack);
  }

  template <class T>
  void PointWiseMeet(std::map<T, AbstractLocation>& m1,
                     std::map<T, AbstractLocation>& m2) {
    for (auto& it1 : m1) {
      auto it2 = m2.find(it1.first);
      if (it2 != m2.end()) {
        it1.second.Meet(it2->second);
      }
    }

    for (auto& it2 : m2) {
      auto it1 = m1.find(it2.first);
      if (it1 == m1.end()) {
        m1.insert(std::make_pair(it2.first, it2.second));
      }
    }
  }

  bool operator==(const HeapContext& c) const {
    return (regs == c.regs) && (stack == c.stack);
  }

  bool operator!=(const HeapContext& c) const {
    return !(*this == c);
  }
};

class HeapAnalysis {
 public:
  HeapAnalysis(FuncSummary* s) : s_(s) {
    InitFlowInfo();
    Analyse();
    UpdateFunctionSummary();
    Cleanup();
  }

 private:
  using FlowInfo = std::map<Address, std::map<Address, HeapContext*>>;
  using WorkList = std::deque<std::pair<HeapContext*, std::set<HeapContext*>>>;

  void InitFlowInfo() {
    Function* f = s_->func;
    auto blocks = f->blocks();

    for (auto b : blocks) {
      std::map<Offset, Instruction> insns;
      b->getInsns(insns);

      Address start = b->start();
      auto& ctxs = info_[start];
      HeapContext* prev = nullptr;
      for (auto const& ins : insns) {
        Address addr = ins.first;
        auto it = ctxs.find(addr);
        HeapContext* ctx;
        if (it == ctxs.end()) {
          ctx = new HeapContext;
          ctx->addr = addr;
          ctx->block = b;
          ctx->ins = ins.second;
        } else {
          ctx = it->second;
          ctx->ins = ins.second;
        }

        ctxs[ctx->addr] = ctx;

        if (prev != nullptr) {
          prev->successor = ctx;
        }
        prev = ctx;
      }
    }
  }

  void Analyse() {
    std::stack<SCComponent*> rpo;
    std::set<SCComponent*> visited;
    PostOrderTraverse(s_->cfg, rpo, visited);

    while (!rpo.empty()) {
      AnalyseBlocks(rpo.top()->blocks, s_->func->entry());
      rpo.pop();
    }
  }

  void PostOrderTraverse(SCComponent* sc, std::stack<SCComponent*>& rpo, std::set<SCComponent*> &visited) {
    if (visited.find(sc) != visited.end()) return;
    visited.insert(sc);
    for (auto child : sc->children) {
      PostOrderTraverse(child, rpo, visited);
    }
    rpo.push(sc);
  }

  void AnalyseBlocks(std::set<Block*>& blocks, Block* func_entry) {
    WorkList worklist;
    Block* b = *(blocks.begin());
    UpdateWorkList(worklist, b, func_entry);

    while (!worklist.empty()) {
      auto work = worklist.front();
      HeapContext* ctx = work.first;
      std::set<HeapContext*>& predecessors = work.second;

      HeapContext old_fact(*ctx);

      for (auto pred : predecessors) {
        assert(pred);
        ctx->Meet(pred);
      }
      TransferFunction(ctx);
      if (old_fact != *ctx) {
        if (ctx->successor != nullptr) {
          std::set<HeapContext*> predecessors;
          predecessors.insert(ctx);
          worklist.push_back(std::make_pair(ctx->successor, predecessors));
        } else {
          for (auto& e : ctx->block->targets()) {
            Block* target = e->trg();
            // We only process intra component targets. Inter component edges
            // will be processed subsequently in reverse post order.
            if (blocks.find(target) != blocks.end()) {
              UpdateWorkList(worklist, target, func_entry);
            }
          }
        }
      }
      worklist.pop_front();
    }
  }

  void UpdateWorkList(WorkList& worklist, Block* b, Block* func_entry) {
    auto& ctxs = info_[b->start()];
    HeapContext* ctx = ctxs[b->start()];
    DCHECK(ctx != nullptr);

    std::set<HeapContext*> predecessors;
    if (b->start() == func_entry->start()) {
      ctx->regs[Dyninst::x86_64::rdi].type = Location::ARG;
      ctx->regs[Dyninst::x86_64::rsi].type = Location::ARG;
      ctx->regs[Dyninst::x86_64::rdx].type = Location::ARG;
      ctx->regs[Dyninst::x86_64::rcx].type = Location::ARG;
      ctx->regs[Dyninst::x86_64::r8].type = Location::ARG;
      ctx->regs[Dyninst::x86_64::r9].type = Location::ARG;
    } else {
      for (auto& e : b->sources()) {
        if (e->interproc()) continue;
        if (e->type() == Dyninst::ParseAPI::CATCH) continue;
        Block* src = e->src();
        Address start = src->start();
        // A block can be shared by multiple functions.
        // An intra-procedural source edge can come from other functions
        if (info_.find(start) == info_.end()) continue;
        auto& src_ctxs = info_[start];
        predecessors.insert(src_ctxs[src->last()]);
      }
    }
    worklist.push_back(std::make_pair(ctx, predecessors));
  }

  // Pattern match any memory operand to get the base register.
  // e.g:
  //   (%rbx) -> %rbx
  //   4(%rax, %rcx, 2) -> %rax
  //   (,%rdx, 2) -> %rdx
  class RegisterVisitor : public Visitor {
   public:
    RegisterVisitor() : add_found_(false), complex_operand_(false) {}

    void visit(BinaryFunction* f) override {
      complex_operand_ = true;
      if (f->isAdd()) {
        add_found_ = true;
      }
    }

    void visit(Immediate* i) override {}

    void visit(Dereference* d) override {}

    void visit(RegisterAST* r) override {
      if (complex_operand_ && !add_found_)
        return;

      if (!base_.isValid())
        base_ = r->getID().getBaseRegister();
    }

    void reset() { base_ = MachRegister(); }

    bool add_found_;
    bool complex_operand_;
    MachRegister base_;
  };

  void ParseOperand(Expression::Ptr expr, RegisterVisitor* v,
                    MachRegister* reg) {
    if (expr != nullptr) {
      expr->apply(v);

      if (v->base_.isValid()) {
        *reg = v->base_;
      }
    }
  }

  bool TransferFunction(HeapContext* ctx) {
    Instruction& ins = ctx->ins;
    entryID id = ins.getOperation().getID();
    switch (id) {
    case e_mov:
      return HandleMov(ctx);
    case e_call:
      return HandleCall(ctx);
    //case e_lea:
      //return HandleLea(ctx);
    default:
      break;
    }
    return HandleUnknown(ctx);
  }

  bool HandleUnknown(HeapContext *ctx) {
    std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> written;
    ctx->ins.getWriteSet(written);
    bool modified = false;
    for (auto const& w : written) {
      Dyninst::MachRegister reg = w->getID().getBaseRegister();
      if (reg.regClass() == Dyninst::x86_64::GPR) {
        AbstractLocation top = AbstractLocation::GetTop();
        modified = modified || (ctx->regs[reg] != top);
        ctx->regs[reg] = top;
      }
    }
    return modified;
  }

  bool HandleLea(HeapContext* ctx) { return false; }

  bool HandleCall(HeapContext* ctx) {
    // Pattern match the call to determine if the call is heap
    // allocation function or not. If so ctx->regs[rax] == HEAP.
    // Otherwise ctx->regs[rax] == TOP.
    Block* b = ctx->block;
    Address target = 0;
    for (auto e : b->targets())
      if (e->type() == Dyninst::ParseAPI::CALL && !e->sinkEdge()) {
        target = e->trg()->start();
      }
    bool setToHeap = false;
    if (target != 0 && b->obj()->cs()->linkage().find(target) != b->obj()->cs()->linkage().end()) {
      std::string name = b->obj()->cs()->linkage()[target];
      if (name == "malloc" || name.find("_Zna") == 0) {
        setToHeap = true;
      }
    }
    if (!setToHeap) {
      AbstractLocation top = AbstractLocation::GetTop();
      bool modified = (ctx->regs[Dyninst::x86_64::rax] != top);
      ctx->regs[Dyninst::x86_64::rax] = top;
      return modified;
    } else {
      AbstractLocation h = AbstractLocation::GetHeapLocation();
      bool modified = (ctx->regs[Dyninst::x86_64::rax] != h);
      ctx->regs[Dyninst::x86_64::rax] = h;
      return modified;
    }
  }

  bool HandleMov(HeapContext* ctx) {
    MachRegister src_reg;
    MachRegister dest_reg;
    RegisterVisitor v = RegisterVisitor();

    Instruction ins = ctx->ins;
    Expression::Ptr expr = ins.getOperand(0).getValue();
    ParseOperand(expr, &v, &dest_reg);

    if (!dest_reg.isValid()) {
      fprintf(stderr, " Handle move, unknown dest reg, %s at %lx\n", ins.format().c_str(), ctx->addr);
      return true;
    }

    v.reset();
    expr = ins.getOperand(1).getValue();
    ParseOperand(expr, &v, &src_reg);

    bool modified = false;
    if (ins.readsMemory()) {
      auto it = s_->stack_heights.find(ctx->addr);
      if (it != s_->stack_heights.end()) {
        // Stack read.
        int height = it->second.src;
        auto sit = ctx->stack.find(height);
        if (sit == ctx->stack.end()) {
          ctx->stack[height] = AbstractLocation::GetStackLocation(height);
          modified = true;
        }
        AbstractLocation old = ctx->regs[dest_reg];
        ctx->regs[dest_reg] = ctx->stack[height].pointToLocation();
        modified = modified || (old != ctx->regs[dest_reg]);
        return modified;
      }

      // Non stack read.
      AbstractLocation old = ctx->regs[dest_reg];
      ctx->regs[dest_reg] = AbstractLocation::GetTop();
      modified = (old != ctx->regs[dest_reg]);
      return modified;
    }

    if (ins.writesMemory()) {
      auto it = s_->stack_heights.find(ctx->addr);
      if (it != s_->stack_heights.end()) {
        // Stack write.
        int height = it->second.dest;
        auto sit = ctx->stack.find(height);
        if (sit == ctx->stack.end()) {
          ctx->stack[height] = AbstractLocation::GetStackLocation(height);
          modified = true;
        }
        AbstractLocation &loc = ctx->stack[height];
        AbstractLocation old = loc.pointToLocation();
        loc.setPointToLocation(ctx->regs[src_reg]);
        modified = modified || (old != loc);
      }
      return modified;
    }

    if (src_reg.isValid() && dest_reg.isValid()) {
      AbstractLocation old = ctx->regs[dest_reg];
      ctx->regs[dest_reg] = ctx->regs[src_reg];
      modified = (old != ctx->regs[dest_reg]);
      return modified;
    }

    AbstractLocation top = AbstractLocation::GetTop();
    modified = (ctx->regs[dest_reg] != top);
    ctx->regs[dest_reg] = top;
    return modified;
  }

  void UpdateFunctionSummary() {
    std::set<Block*> to_remove_blocks;
    for (auto it : s_->unknown_writes) {
      Address block = it.first->start();
      std::set<Address>& addrs = it.second;

      auto& ctxs = info_[block];
      std::set<Address> to_remove;
      for (auto addr : addrs) {
        auto cit = ctxs.find(addr);
        if (cit != ctxs.end()) {
          HeapContext* ctx = cit->second;
          MachRegister dest_reg;
          RegisterVisitor v = RegisterVisitor();

          Instruction ins = ctx->ins;
          Expression::Ptr expr = ins.getOperand(0).getValue();
          ParseOperand(expr, &v, &dest_reg);

          // Push instructions that do have a known stack height
          // can lead to unvalid dest register
          if (!dest_reg.isValid()) {
              fprintf(stderr, " UpdateFunctionSummary, unknown dest reg, %s at %lx\n", ins.format().c_str(), ctx->addr);
              continue;
          }

          AbstractLocation& loc = ctx->regs[dest_reg];
          switch (loc.type) {
          case Location::HEAP:
            s_->heap_writes[block].insert(addr);
            assert(s_->all_writes.find(addr) != s_->all_writes.end());
            s_->all_writes[addr]->heap = true;
            to_remove.insert(addr);
            break;
          case Location::ARG:
            s_->arg_writes[block].insert(addr);
            assert(s_->all_writes.find(addr) != s_->all_writes.end());
            s_->all_writes[addr]->arg = true;
            //to_remove.insert(addr);
            break;
          case Location::HEAP_OR_ARG:
            s_->heap_or_arg_writes[block].insert(addr);
            assert(s_->all_writes.find(addr) != s_->all_writes.end());
            s_->all_writes[addr]->heap_or_arg = true;
            //to_remove.insert(addr);
            break;
          default:
            break;
          }
        }
      }

      for (auto addr : to_remove) {
        addrs.erase(addr);
      }

      if (addrs.empty()) {
        to_remove_blocks.insert(it.first);
      }
    }

    for (auto b : to_remove_blocks) {
      s_->unknown_writes.erase(b);
      s_->unsafe_blocks.erase(b);
    }
  }

  void Cleanup() {
    for (auto it : info_) {
      auto& ctxs = it.second;

      for (auto& cit : ctxs) {
        delete cit.second;
      }
    }
  }

  FuncSummary* s_;
  FlowInfo info_;
};

}  // namespace heap

#endif  // LITECFI_HEAP_H
