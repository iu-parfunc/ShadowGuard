
#include "CFG.h"
#include "Register.h"
#include "glog/logging.h"
#include "pass_manager.h"

namespace heap {

using Dyninst::InstructionAPI::Instruction;
using Dyninst::ParseAPI::Block;
using Dyninst::ParseAPI::Function;

enum class Location { HEAP, STACK, ARG, HEAP_OR_ARG, TOP, BOTTOM };

struct AbstractLocation {
  AbstractLocation() : type(BOTTOM) {}

  Location type;
  int stack_height;
  AbstractLocation points_to;

  AbstractLocation Meet(AbstractLocation other) {
    if (type == Location::TOP || other.type == Location::TOP) {
      return GetTop();
    }

    if (type == Location::BOTTOM)
      return other;

    if (other.type == Location::BOTTOM)
      return *this;

    if (other.type == type) {
      if (type == Location::STACK) {
        if (stack_height != other.stack_height)
          return GetTop();

        points_to = points_to.Meet(other.points_to);
        return *this;
      }
      return *this;
    }

    if ((type == Location::ARG && other.type == Location::HEAP) ||
        (type == Location::HEAP && other.type == Location::ARG)) {
      return GetHeapOrArgLocation();
    }

    DCHECK(false);
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

struct InstructionContext {
  Address addr;
  Address block;
  Instruction ins;
};

struct HeapContext {
  InstructionContext ins;
  std::map<MachRegister, AbstractLoction> regs;
  std::map<int, AbstractLocation> stack;

  std::set<HeapContext*> successors;

  void Meet(HeapContext& other) {
    PointWiseMeet<MachRegister>(regs, other.regs);
    PointWiseMeet<int>(stack, other.stack);
  }

  template <class T>
  void PointWiseMeet(std::map<T, AbstractLocation>& m1,
                     std::map<T, AbstractLocation>& m2) {
    for (auto& it1 : m1) {
      auto it2 = m2.find(it1.first);
      if (it2 != m2.end()) {
        m1[it1.first] = it1.second.meet(it2.second);
      }
    }

    for (auto& it2 : m2) {
      auto it1 = m1.find(it2.first);
      if (it1 == m1.end()) {
        m1[it2.first] = it2.second;
      }
    }
  }
};

class HeapAnalysis {
 public:
  HeapAnalysis(FunctionSummary* s) : s_(s) {
    InitFlowInfo();
    Analyse();
  }

 private:
  using FlowInfo = std::map<Address, std::map<Address, HeapContext*>>;

  void Analyse() {
    std::set<SCComponent*> visited;
    AnalyseComponent(s_->cfg, visited, s, true);
  }

  void AnalyseComponent(SCComponent* sc, std::set<SCComponent*>& visited,
                        FunctionSummary* s, bool root = false) {
    visited.insert(sc);

    AnalyseBlocks(sc->blocks, root);

    for (auto child : sc->children) {
      if (visited.find(child) == visited.end()) {
        AnalyseComponent(child, visited);
      }
    }
  }

  void AnalyseBlocks(std::set<Block*>& blocks, bool root) {
    Block* b;
    if (root) {
      b = s_->func->entry();
    } else {
      b = *(blocks.begin())
    }

    auto& ctxs = info_[b->start()];
    HeapContext* ctx = ctxs[b->start()];
    DCHECK(ctx != nullptr);

    std::deque<std::pair<HeapContext*, HeapContext*>> worklist;
    if (root) {
      ctx->regs[x86_64::rdi] = GetArgLocation();
      ctx->regs[x86_64::rsi] = GetArgLocation();
      ctx->regs[x86_64::rdx] = GetArgLocation();
      ctx->regs[x86_64::rcx] = GetArgLocation();
      ctx->regs[x86_64::r8] = GetArgLocation();
      ctx->regs[x86_64::r9] = GetArgLocation();
    }
    worklist.push_back(std::make_pair(ctx, nullptr));

    while (!worklist.empty()) {
      auto work = worklist.pop_front();
      HeapContext* ctx = work.first;
      HeapContext* predecessor = work.second;

      if (predecessor != nullptr) {
        ctx.meet(predecessor);
      }

      if (TransferFunction(ctx)) {
        for (auto successor : ctx->successors) {
          worklist.push_back(std::make_pair(successor, ctx));
        }
      }
    }
  }

  bool TransferFunction(HeapContext* ctx) { return false; }

  void InitFlowInfo() {
    SCComponent* root = s_->cfg;

    for (auto b : blocks) {
      std::map<Offset, Instruction> insns;
      b->getInstructions(insns);

      auto& ctxs = info_[start];

      // Handle instructions within a block.
      Address start = b->start();
      HeapContext* prev = nullptr;
      for (auto const& ins : insns) {
        Address addr = start + ins.first;
        auto it = ctxs->find(addr);
        if (it == ctxs.end()) {
          HeapContext* ctx = new HeapContext;
          ctx->ins.addr = addr;
          ctx->ins.block = start;
          ctx->ins.ins = ins;
        } else {
          it->second->ins.ins = ins;
        }

        ctxs[ins.addr] = ctx;

        if (prev != nullptr) {
          prev->successors.insert(ctx);
        }
        prev = ctx;
      }

      // Handle inter-block edges.
      HeapContext* first = ctxs[b->start()];
      HeapContext* last = ctxs[b->last()];
      for (auto& e : b->sources) {
        Block* src = e->src();
        Address src_start = e->src->start();

        auto& src_ctxs = info_[src_start];
        auto it = src_ctxs.find(src->last());
        if (it == src_ctxs.end()) {
          HeapContext* ctx = new HeapContext;
          ctx->ins.addr = src->last();
          ctx->ins.block = src_start;
          ctx->successors.insert(first);
          src_ctxs[src->last()] = ctx;
        }
      }
    }
  }

  FunctionSummary* s_;
  FlowInfo info_;
};

}  // namespace heap
