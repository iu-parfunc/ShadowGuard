#ifndef LITECFI_PASSES_H_
#define LITECFI_PASSES_H_

#include <algorithm>
#include <cstdlib>

#include "Absloc.h"
#include "AbslocInterface.h"
#include "CFG.h"
#include "CodeObject.h"
#include "CodeSource.h"
#include "Expression.h"
#include "Instruction.h"
#include "InstructionDecoder.h"
#include "Location.h"
#include "Register.h"
#include "Visitor.h"
#include "bitArray.h"
#include "glog/logging.h"
#include "liveness.h"
#include "pass_manager.h"
#include "register_utils.h"
#include "utils.h"
#include "stackanalysis.h"

using Dyninst::Absloc;
using Dyninst::AbsRegion;
using Dyninst::Address;
using Dyninst::Offset;
using Dyninst::InstructionAPI::Instruction;
using Dyninst::InstructionAPI::RegisterAST;
using Dyninst::InstructionAPI::Visitor;
using Dyninst::ParseAPI::Block;
using Dyninst::ParseAPI::Function;
using Dyninst::ParseAPI::Location;
using Dyninst::ParseAPI::Loop;

class CallGraphAnalysis : public Pass {
 public:
  CallGraphAnalysis()
      : Pass("Call Graph Generation", "Generates the application call graph.") {
  }

  void RunGlobalAnalysis(CodeObject* co,
                         std::map<Function*, FuncSummary*>& summaries,
                         PassResult* result) override {
    std::set<Function*> visited;
    for (auto f : co->funcs()) {
      auto it = visited.find(f);
      if (it == visited.end()) {
        VisitFunction(co, summaries[f], summaries, visited);
      }
    }
  }

 private:
  void UpdateCallees(CodeObject* co, Function* f, FuncSummary* s) {
    for (auto b : f->blocks()) {
      for (auto e : b->targets()) {
        if (e->sinkEdge() && e->type() == ParseAPI::INDIRECT && e->interproc()) continue;
        if (e->sinkEdge() && e->type() != ParseAPI::RET) {
          s->has_unknown_cf = true;
          s->assume_unsafe = true;
          continue;
        }
        if (e->type() == ParseAPI::INDIRECT) {
          s->has_indirect_cf = true;
          //s->assume_unsafe = true;
          continue;
        }

        if (e->type() != ParseAPI::CALL)
          continue;
        if (co->cs()->linkage().find(e->trg()->start()) !=
            co->cs()->linkage().end()) {
          s->has_plt_call = true;
          s->assume_unsafe = true;
          continue;
        }
        std::vector<Function*> funcs;
        e->trg()->getFuncs(funcs);
        Function* callee = nullptr;
        for (auto call_trg : funcs) {
          if (call_trg->entry() == e->trg()) {
            callee = call_trg;
            break;
          }
        }
        if (callee)
          s->callees.insert(callee);
      }
    }
  }

  void VisitFunction(CodeObject* co, FuncSummary* s,
                     std::map<Function*, FuncSummary*>& summaries,
                     std::set<Function*>& visited) {
    visited.insert(s->func);
    UpdateCallees(co, s->func, s);
    for (auto f : s->callees) {
      summaries[f]->callers.insert(s->func);
      auto it = visited.find(f);
      if (it == visited.end()) {
        VisitFunction(co, summaries[f], summaries, visited);
      }
    }
  }
};

class LargeFunctionFilter : public Pass {
 public:
  LargeFunctionFilter()
      : Pass("Large Function Filter",
             "Filters out large functions from static anlaysis.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->assume_unsafe)
      return;

    Address start = f->addr();
    Address end = 0;
    for (auto b : f->exitBlocks()) {
      end = std::max(end, b->end());
    }

    for (auto b : f->returnBlocks()) {
      end = std::max(end, b->end());
    }

    if (end < start) {
      return;
    }

    if (end - start > kCutoffSize_) {
      StdOut(Color::RED, FLAGS_vv)
          << "    Skipping large function " << f->name()
          << " with size : " << end - start << Endl;

      // Bail out and conservatively assume the function is unsafe.
      s->assume_unsafe = true;
    }
  }

  static constexpr unsigned int kCutoffSize_ = 20000;
};

class IntraProceduralMemoryAnalysis : public Pass {
 public:
  IntraProceduralMemoryAnalysis()
      : Pass("Intra-procedural Memory Write and Stack Pointer Overwrite "
             "Analysis",
             "Analyzes the memory writes within a function. Also detects stack"
             " pointer overwrites.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {

    if (s->assume_unsafe)
      return;

    StackAnalysis sa(f);

    AssignmentConverter converter(true /* cache results*/,
                                  true /* use stack analysis*/);

    std::map<Offset, Instruction> insns;
    for (auto b : f->blocks()) {
      StackAnalysis::Height h = sa.findSP(b, b->end());
      if (!h.isTop() && !h.isBottom()) {
          s->blockEndSPHeight[b->start()] = -8 - h.height();
      }
      h = sa.findSP(b, b->start());
      if (!h.isTop() && !h.isBottom()) {
          s->blockEntrySPHeight[b->start()] = -8 - h.height();
      }

      insns.clear();
      b->getInsns(insns);

      bool unsafe = false;
      for (auto const& ins : insns) {
        // Ignore writes due to frame switching instructions such as call/ ret.
        if (IsFrameSwitchingInstruction(ins.second))
          continue;

        if (ins.second.writesMemory()) {
          // Assignment is essentially SSA.
          // So, an instruction may have multiple SSAs.
          std::vector<Assignment::Ptr> assigns;
          converter.convert(ins.second, ins.first, f, b, assigns);

          MemoryWrite* write = new MemoryWrite;
          write->function = f;
          write->block = b;
          write->ins = ins.second;
          write->addr = ins.first;

          for (auto a : assigns) {
            AbsRegion& out = a->out();  // The lhs.
            Absloc loc = out.absloc();
            switch (loc.type()) {
            case Absloc::Stack: {
              // If the stack location is in the previous frame,
              // it is a dangerous write.
              write->stack = true;
              s->stack_writes[loc.off()] = write;
              if (loc.off() >= -8) {
                s->self_writes = true;
                unsafe = true;
              }
              break;
            }
            case Absloc::Unknown: {
              // Unknown memory writes.
              s->self_writes = true;
              unsafe = true;
              break;
            }
            case Absloc::Heap:
              // This is actually a write to a global variable.
              // That's why it will have a statically determined address.
              break;
            case Absloc::Register:
              // Not a memory write.
              break;
            }
          }

          s->all_writes[write->addr] = write;
        }
      }

      if (unsafe) {
        s->unsafe_blocks.insert(b);
      }
    }
  }

  bool IsSafeFunction(FuncSummary* s) override {
    return !s->self_writes && !s->assume_unsafe && s->callees.empty();
  }

 private:
  bool IsFrameSwitchingInstruction(const Instruction& ins) {
    // Call or return instructions switch frames.
    if (ins.getCategory() == Dyninst::InstructionAPI::c_CallInsn ||
        ins.getCategory() == Dyninst::InstructionAPI::c_ReturnInsn) {
      return true;
    }
    return false;
  }
};

class InterProceduralMemoryAnalysis : public Pass {
 public:
  InterProceduralMemoryAnalysis()
      : Pass("Inter-procedural Memory Write Analysis",
             "Analyses memory writes across functions.") {}

  void RunGlobalAnalysis(CodeObject* co,
                         std::map<Function*, FuncSummary*>& summaries,
                         PassResult* result) override {
    std::set<Function*> visited;
    for (auto f : co->funcs()) {
      auto it = visited.find(f);
      if (it == visited.end()) {
        VisitFunction(co, summaries[f], summaries, visited);
      }
    }
  }

 private:
  bool VisitFunction(CodeObject* co, FuncSummary* s,
                     std::map<Function*, FuncSummary*>& summaries,
                     std::set<Function*>& visited) {
    visited.insert(s->func);

    for (auto f : s->callees) {
      auto it = visited.find(f);
      if (it == visited.end()) {
        s->child_writes |= VisitFunction(co, summaries[f], summaries, visited);
      } else {
        s->child_writes |= summaries[f]->writes;
      }
    }

    s->writes = s->self_writes || s->child_writes || s->assume_unsafe;
    return s->writes;
  }
};

class CFGAnalysis : public Pass {
 public:
  CFGAnalysis()
      : Pass("CFG Analysis",
             "Analyses the CFG for unsafe basic blocks and generates a flow "
             "graph with strongly connected components.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->assume_unsafe || s->has_unknown_cf || s->has_indirect_cf) {
      s->cfg = nullptr;
      return;
    }

    // Pre-Process loops within the function.
    std::vector<Loop*> loops;
    f->getOuterLoops(loops);

    std::map<Block*, SCComponent*> block_to_sc;
    for (auto l : loops) {
      std::vector<Block*> blocks;
      l->getLoopBasicBlocks(blocks);

      SCComponent* sc = new SCComponent;
      for (auto b : blocks) {
        sc->blocks.insert(b);
        if (s->unsafe_blocks.find(b) != s->unsafe_blocks.end()) {
          sc->unsafe = true;
        }
        block_to_sc[b] = sc;
      }
    }

    std::set<Block*> visited;
    auto it = block_to_sc.find(f->entry());
    if (it != block_to_sc.end()) {
      s->cfg = it->second;
    } else {
      s->cfg = new SCComponent;
    }
    VisitBlock(f->entry(), s->cfg, s, visited, block_to_sc);
  }

 private:
  void VisitBlock(Block* b, SCComponent* sc, FuncSummary* s,
                  std::set<Block*>& visited,
                  std::map<Block*, SCComponent*>& block_to_sc) {
    visited.insert(b);
    sc->blocks.insert(b);
    block_to_sc[b] = sc;
    if (s->unsafe_blocks.find(b) != s->unsafe_blocks.end())
      sc->unsafe = true;

    for (auto e : b->targets()) {
      Block* target = e->trg();
      if (e->type() == ParseAPI::CALL) {
        sc->unsafe = true;
        continue;
      }

      if (e->type() == ParseAPI::RET) {
        sc->returns.insert(b);
        continue;
      }

      auto it = block_to_sc.find(target);
      SCComponent* new_sc = nullptr;
      if (it == block_to_sc.end()) {
        new_sc = new SCComponent;
      } else {
        new_sc = it->second;
      }

      // We may be in a loop and the target may also be in the current loop. If
      // that's the case skip adding a child component.
      if (sc != new_sc) {
        sc->children.insert(new_sc);

        sc->targets[new_sc] = target;
        sc->outgoing[target] = new_sc;
      }

      if (visited.find(target) == visited.end()) {
        VisitBlock(target, new_sc, s, visited, block_to_sc);
      }
    }
  }
};

class CFGStatistics : public Pass {
 public:
  CFGStatistics()
      : Pass("CFG Statistics",
             "Calculates statistics about the control flow graph.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->cfg == nullptr)
      return;

    s->stats = new CFGStats;
    std::set<SCComponent*> visited;
    VisitComponent(s->cfg, s->stats, visited, s->func->name());
  }

 private:
  void VisitComponent(SCComponent* sc, CFGStats* stats,
                      std::set<SCComponent*>& visited, std::string name) {
    visited.insert(sc);
    stats->n_original_nodes += sc->blocks.size();

    for (auto child : sc->children) {
      if (visited.find(child) == visited.end()) {
        VisitComponent(child, stats, visited, name);
      }
    }
  }
};

class LowerInstrumentation : public Pass {
 public:
  LowerInstrumentation()
      : Pass("Lower Instrumentation",
             "Lowers stack push instrumentation to cover unsafe portions of "
             "SCC based CFG by generating stack push nodes") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->cfg == nullptr)
      return;

    std::map<SCComponent*, Components*> components;
    SCComponent* new_root = new SCComponent;
    // Process non safe root separately.
    if (s->cfg->unsafe) {
      new_root->stack_push = true;

      SCComponent* child = new SCComponent;
      *child = *(s->cfg);
      new_root->children.insert(child);
    } else {
      *new_root = *(s->cfg);
    }

    // We don't track visited nodes in this traversal since by now the loops
    // should have been each flattened to strongly connected components. Only
    // edges we should encounter are forward and cross edges, not back edges.
    // Also we need to repeatedly traverse cross edges since we may have to do
    // block duplication if the cross edge traverses both safe and unsafe
    // control flow paths.
    VisitComponent(s->cfg, new_root, components, true /* in_safe_region */);
    s->cfg = new_root;
  }

 private:
  struct Components {
    SCComponent* safe;
    SCComponent* unsafe;

    Components() : safe(nullptr), unsafe(nullptr) {}
  };

  void VisitComponent(SCComponent* sc, SCComponent* new_sc,
                      std::map<SCComponent*, Components*>& components,
                      bool in_safe_region) {
    if (components.find(sc) == components.end())
      components[sc] = new Components;

    // If we are encountering a generated stack push node just pass it through.
    if (new_sc->stack_push) {
      DCHECK(new_sc->children.size() == 1);
      for (auto c : new_sc->children) {
        VisitComponent(sc, c, components, false /* in_safe_region */);
      }
      return;
    }

    for (auto c : sc->children) {
      if (c->unsafe && in_safe_region) {
        SCComponent* child = new SCComponent;
        child->stack_push = true;

        SCComponent* grand_child = nullptr;
        // We need a safe to unsafe region transition.
        // First check if there is already existing unsafe version and if so
        // reuse it.
        auto it = components.find(grand_child);
        if (it != components.end() && it->second->unsafe != nullptr) {
          grand_child = it->second->unsafe;
        } else {
          grand_child = new SCComponent;
          *grand_child = *c;

          Components* new_component = new Components;
          new_component->unsafe = grand_child;
          components[grand_child] = new_component;
        }

        Block* target = sc->targets[c];
        new_sc->targets[child] = target;
        new_sc->outgoing[target] = child;
        child->outgoing[target] = grand_child;

        child->children.insert(grand_child);
        new_sc->children.insert(child);
        VisitComponent(c, child, components, false /* in_safe_region */);
        continue;
      }

      SCComponent* child = nullptr;
      if (c->unsafe) {
        auto it = components.find(c);
        if (it != components.end() && it->second->unsafe != nullptr) {
          child = it->second->unsafe;
        } else {
          child = new SCComponent;
          *child = *c;

          Components* new_component = new Components;
          new_component->unsafe = child;
          components[c] = new_component;
        }
      }

      if (!c->unsafe && !in_safe_region) {
        auto it = components.find(c);
        if (it != components.end() && it->second->unsafe != nullptr) {
          child = it->second->unsafe;
        } else {
          child = new SCComponent;
          *child = *c;

          Components* new_component = new Components;
          new_component->unsafe = child;
          components[c] = new_component;
        }
      }

      if (!c->unsafe && in_safe_region) {
        auto it = components.find(c);
        if (it != components.end() && it->second->safe != nullptr) {
          child = it->second->safe;
        } else {
          child = new SCComponent;
          *child = *c;

          Components* new_component = new Components;
          new_component->safe = child;
          components[c] = new_component;
        }
      }

      Block* target = sc->targets[c];
      new_sc->targets[child] = target;
      new_sc->outgoing[target] = child;
      new_sc->children.insert(child);
      VisitComponent(c, child, components, in_safe_region);
    }
  }
};

class LinkParentsOfCFG : public Pass {
 public:
  LinkParentsOfCFG()
      : Pass("Link parent nodes", "Creates parent node links in the CFG DAG.") {
  }

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->cfg == nullptr)
      return;

    std::set<SCComponent*> visited;
    VisitComponent(s->cfg, visited);
  }

 private:
  void VisitComponent(SCComponent* sc, std::set<SCComponent*>& visited) {
    visited.insert(sc);

    for (auto child : sc->children) {
      child->parents.insert(sc);
      if (visited.find(child) == visited.end()) {
        VisitComponent(child, visited);
      }
    }
  }
};

class CoalesceIngressInstrumentation : public Pass {
 public:
  CoalesceIngressInstrumentation()
      : Pass("Coalesce Ingress Instrumentation",
             "Coalesces generated stack push nodes at incoming edges where "
             "possible.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->cfg == nullptr)
      return;

    std::map<SCComponent*, SCComponent*> components;
    SCComponent* new_root = new SCComponent;
    *new_root = *(s->cfg);
    VisitComponent(s->cfg, new_root, components);

    s->cfg = new_root;
  }

 private:
  SCComponent* GetOnlyChild(SCComponent* sc) {
    DCHECK(sc->children.size() == 1);

    auto it = sc->children.begin();
    return *it;
  }

  bool IsAllInstrumentation(std::set<SCComponent*>& nodes) {
    for (auto node : nodes) {
      if (!node->stack_push) {
        return false;
      }
    }
    return true;
  }

  bool GetOrCopyNode(SCComponent* sc,
                     std::map<SCComponent*, SCComponent*>& components,
                     SCComponent** copy) {
    auto it = components.find(sc);
    if (it != components.end()) {
      *copy = it->second;
      return true;
    }

    *copy = new SCComponent;
    **copy = *sc;
    return false;
  }

  void VisitComponent(SCComponent* sc, SCComponent* new_sc,
                      std::map<SCComponent*, SCComponent*>& components) {
    components[sc] = new_sc;

    if (sc->stack_push) {
      DCHECK(sc->children.size() == 1);
      for (auto child : sc->children) {
        SCComponent* new_child = nullptr;
        bool visited = GetOrCopyNode(child, components, &new_child);

        Block* target = sc->targets[child];
        new_sc->targets[new_child] = target;
        new_sc->outgoing[target] = new_child;
        new_sc->children.insert(new_child);

        if (!visited) {
          VisitComponent(child, new_child, components);
        }
      }
      return;
    }

    for (auto child : sc->children) {
      if (child->stack_push) {
        // Any child stack_push (i.e instrumentation) node is dominated by its
        // parent node. So if this is the first time the parent is being visited
        // then it must be the first the child is being visited as well. So the
        // visited check is not necessary for recursion on stack_push nodes.
        SCComponent* grand_child = GetOnlyChild(child);
        if (grand_child->header_instrumentation) {
          // If the 'header_instrumentation' of grand_child is set it means that
          // the grand child has already been processed and had its entry
          // instrumentation coaleseced.
          SCComponent* new_grand_child = nullptr;
          GetOrCopyNode(grand_child, components, &new_grand_child);
          new_sc->children.insert(new_grand_child);

          Block* target = sc->targets[child];
          new_sc->targets[new_grand_child] = target;
          new_sc->outgoing[target] = new_grand_child;
          continue;
        }

        if (IsAllInstrumentation(grand_child->parents) &&
            grand_child->blocks.size() == 1) {
          SCComponent* new_grand_child = nullptr;
          GetOrCopyNode(grand_child, components, &new_grand_child);
          new_grand_child->header_instrumentation = true;
          new_sc->children.insert(new_grand_child);

          Block* target = sc->targets[child];
          new_sc->targets[new_grand_child] = target;
          new_sc->outgoing[target] = new_grand_child;

          // Mark grand_child as entry instrumentation coalesced for future
          // traversals from cross edges.
          grand_child->header_instrumentation = true;

          VisitComponent(grand_child, new_grand_child, components);
        } else {
          SCComponent* copy = nullptr;
          GetOrCopyNode(child, components, &copy);
          new_sc->children.insert(copy);

          Block* target = sc->targets[child];
          new_sc->targets[copy] = target;
          new_sc->outgoing[target] = copy;

          VisitComponent(child, copy, components);
        }
        continue;
      }

      SCComponent* copy = nullptr;
      bool visited = GetOrCopyNode(child, components, &copy);
      new_sc->children.insert(copy);

      Block* target = sc->targets[child];
      new_sc->targets[copy] = target;
      new_sc->outgoing[target] = copy;

      if (!visited) {
        VisitComponent(child, copy, components);
      }
    }
  }
};

class CoalesceEgressInstrumentation : public Pass {
 public:
  CoalesceEgressInstrumentation()
      : Pass("Coalesce Egress Instrumentation",
             "Coalesces generated stack push nodes at outgoing edges where "
             "possible.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->cfg == nullptr)
      return;

    std::set<SCComponent*> visited;
    VisitComponent(s->cfg, visited);
  }

 private:
  SCComponent* GetOnlyChild(SCComponent* sc) {
    DCHECK(sc->children.size() == 1);

    auto it = sc->children.begin();
    return *it;
  }

  void VisitComponent(SCComponent* sc, std::set<SCComponent*>& visited) {
    visited.insert(sc);

    bool all_instrumentation = true;
    for (auto child : sc->children) {
      all_instrumentation &= child->stack_push;
    }

    if (all_instrumentation && sc->blocks.size() == 1) {
      std::set<SCComponent*> new_children;
      for (auto child : sc->children) {
        SCComponent* grand_child = GetOnlyChild(child);
        new_children.insert(grand_child);

        Block* target = sc->targets[child];
        sc->targets[grand_child] = target;
        sc->targets.erase(child);
        sc->outgoing[target] = grand_child;
      }
      sc->children.clear();
      sc->children = new_children;
      sc->header_instrumentation = true;
    }

    for (auto child : sc->children) {
      if (visited.find(child) == visited.end()) {
        VisitComponent(child, visited);
      }
    }
  }
};

class ValidateCFG : public Pass {
 public:
  ValidateCFG()
      : Pass("Validate CFG", "Validate lowered CFG for correctness.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->cfg == nullptr)
      return;

    std::set<SCComponent*> visited;
    VisitComponent(s->cfg, true /* safe_flow */, visited);
  }

 private:
  void VisitComponent(SCComponent* sc, bool safe_flow,
                      std::set<SCComponent*>& visited) {
    visited.insert(sc);
    if (!safe_flow && (sc->stack_push || sc->header_instrumentation)) {
      StdOut(Color::RED, FLAGS_vv) << "Invalid lowering found." << Endl;
      abort();
    }

    if (safe_flow && (sc->stack_push || sc->header_instrumentation)) {
      for (auto child : sc->children) {
        VisitComponent(child, false /* safe_flow */, visited);
      }
    }
  }
};

class LoweringStatistics : public Pass {
 public:
  LoweringStatistics()
      : Pass("Lowering Statistics",
             "Generates graph statistics for the final lowered CFG.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->cfg == nullptr)
      return;

    std::set<SCComponent*> visited;
    CFGStats* stats = s->stats;
    VisitComponent(s->cfg, stats, true /* safe_flow */, visited);

    stats->safe_ratio = (static_cast<double>(stats->safe_paths) /
                         (stats->safe_paths + stats->unsafe_paths)) *
                        100;
    stats->increase = ((static_cast<double>(stats->n_lowered_nodes -
                                            stats->n_original_nodes)) /
                       stats->n_original_nodes) *
                      100;
  }

 private:
  void VisitComponent(SCComponent* sc, CFGStats* stats, bool safe_flow,
                      std::set<SCComponent*>& visited) {
    visited.insert(sc);
    if (sc->stack_push) {
      stats->n_lowered_nodes++;
    } else {
      stats->n_lowered_nodes += sc->blocks.size();
    }

    // This is a return or sink node.
    if (sc->children.size() == 0) {
      if (safe_flow) {
        stats->safe_paths++;
      } else {
        stats->unsafe_paths++;
      }
      return;
    }

    bool safe = safe_flow && !sc->stack_push;
    for (auto child : sc->children) {
      if (visited.find(child) == visited.end()) {
        VisitComponent(child, stats, safe, visited);
      }
    }
  }
};

class RedZoneAccessVisitor : public Visitor {

 public:
  int disp;
  bool plusOp;
  bool findSP;
  bool onlySP;

  RedZoneAccessVisitor()
      : disp(0), plusOp(false), findSP(false), onlySP(true) {}

  virtual void visit(BinaryFunction* b) {
    if (b->isAdd())
      plusOp = true;
  }

  virtual void visit(Immediate* i) {
    const Result& r = i->eval();
    disp = r.convert<int>();
  }

  virtual void visit(RegisterAST* r) {
    if (r->getID() == x86_64::rsp)
      findSP = true;
    else
      onlySP = false;
  }
  virtual void visit(Dereference* d) {}

  bool isRedZoneAccess() { return plusOp && findSP && (disp < 0) && onlySP; }
};

class UnusedRegisterAnalysis : public Pass {
 public:
  UnusedRegisterAnalysis()
      : Pass("Unused Register Analysis", "Analyses register unused "
                                         "saved registers of leaf functions.") {
  }

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->assume_unsafe) {
      return;
    }

    if (s->callees.size() > 0) {
      return;
    }

    std::set<std::string> all = {
        "x86_64::rax", "x86_64::rbx", "x86_64::rcx", "x86_64::rdx",
        "x86_64::rsi", "x86_64::rdi", "x86_64::r8",  "x86_64::r9",
        "x86_64::r10", "x86_64::r11", "x86_64::r12", "x86_64::r13",
        "x86_64::r14", "x86_64::r15",
    };

    StackAnalysis sa(f);

    std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;
    std::set<std::string> used;
    for (auto b : f->blocks()) {
      b->getInsns(insns);


      for (auto const& ins : insns) {
          StackAnalysis::Height h = sa.findSP(b, ins.first);
          if (!h.isTop() && !h.isBottom()) {
              int height = -8 - h.height();
              if (height >= 128) s->moveDownSP = true;

          }

        std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> read;
        std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> written;

        ins.second.getReadSet(read);
        ins.second.getWriteSet(written);
        
        for (auto const& r : read) {
          used.insert(NormalizeRegisterName(r->getID().name()));
        }

        for (auto const& w : written) {
          used.insert(NormalizeRegisterName(w->getID().name()));
        }

        // See if this instruction accesses to red zone
        if (!ins.second.writesMemory() && !ins.second.readsMemory())
          continue;

        std::set<Expression::Ptr> accessors;
        ins.second.getMemoryReadOperands(accessors);
        ins.second.getMemoryWriteOperands(accessors);
        for (auto e : accessors) {
          RedZoneAccessVisitor v;
          e->apply(&v);
          if (v.isRedZoneAccess()) {
            s->redZoneAccess.insert(v.disp);
          }
        }
      }
    }

    for (auto r : all) {
      auto it = used.find(r);
      if (it == used.end()) {
        s->unused_regs.insert(r);
      }
    }
  }
};

class DeadRegisterAnalysis : public Pass {
 public:
  DeadRegisterAnalysis()
      : Pass("Dead Register Analysis",
             "Analyses dead registers at function entry and exit.") {}

  std::set<std::string> GetDeadRegisters(Function* f, Block* b,
                                         LivenessAnalyzer::Type type) {
    // Construct a liveness analyzer based on the address width of the
    // mutatee. 32bit code and 64bit code have different ABI.
    LivenessAnalyzer la(f->obj()->cs()->getAddressWidth());
    // Construct a liveness query location.
    Location loc(f, b);

    std::set<std::string> dead;

    // Query live registers.
    bitArray live;
    if (!la.query(loc, type, live)) {
      return dead;
    }

    // Check all dead caller-saved registers.
    std::vector<MachRegister> regs;
    regs.push_back(x86_64::rsi);
    regs.push_back(x86_64::rdi);
    regs.push_back(x86_64::rdx);
    regs.push_back(x86_64::rcx);
    regs.push_back(x86_64::r8);
    regs.push_back(x86_64::r9);
    regs.push_back(x86_64::r10);
    regs.push_back(x86_64::r11);

    for (auto reg : regs) {
      if (!live.test(la.getIndex(reg))) {
        dead.insert(reg.name());
      }
    }
    return dead;
  }

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->assume_unsafe) {
      return;
    }

    s->dead_at_entry =
        GetDeadRegisters(f, f->entry(), LivenessAnalyzer::Before);

    for (auto b : f->exitBlocks()) {
      s->dead_at_exit[b->end()] =
          GetDeadRegisters(f, b, LivenessAnalyzer::After);
    }
  }
};

class BlockDeadRegisterAnalysis : public Pass {
 public:
  BlockDeadRegisterAnalysis()
      : Pass("Dead Register Analysis in a basic block",
             "Looking for concrete envidence that registers are dead") {}

  void CalculateBlockLevelLiveness(std::map<Offset, Instruction> &insns,
          std::map<Offset, std::set<std::string> > &d) {
      std::set<std::string> cur;
      for (auto it = insns.rbegin(); it != insns.rend(); ++it) {
          auto& o = it->first;
          Instruction& i = it->second;
          std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> read;
          i.getReadSet(read);

          std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> written;
          i.getWriteSet(written);
          for (auto const& w: written) {
            MachRegister actualReg = w->getID();
            // Writing to lower 32-bit portion of a register
            // will automatically clean the upper 32-bit.
            //
            // We cannot treat a partial writes to a register as a kill
            if (actualReg.size() != 4 && actualReg.size() != 8) {
                continue;
            }
            MachRegister baseReg = actualReg.getBaseRegister();
            if (baseReg == x86_64::rsp) continue;
            if (baseReg.regClass() != x86_64::GPR) continue;
            std::string name = NormalizeRegisterName(baseReg.name());
            cur.insert(name);
          }
          for (auto const& r : read) {
            MachRegister baseReg = r->getID().getBaseRegister();
            cur.erase(NormalizeRegisterName(baseReg.name()));
          }
          d[o] = cur;
      }
  }

  bool MoveSP(Instruction &i) {
    std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> written;
    i.getWriteSet(written);
    for (auto const& w: written) {
      if (w->getID() == x86_64::rsp) return true;
    }
    return false;
  }

  bool ReadFlags(Instruction &i) {
    std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> read;
    i.getReadSet(read);
    for (auto const &r : read) {
        if (r->getID().isFlag()) return true;
    }
    return false;
  }

  void CalculateEntryInstPoint(std::map<Offset, Instruction> &insns,
          std::map<Offset, std::set<std::string> > &d,
          FuncSummary *s,
          Address blockEntry) {
      Address newAddr = 0;
      int saveCount = 0;
      int raOffset = 0;
      int curHeight = 0;
      for (auto it = insns.begin(); it != insns.end(); ++it) {
          auto& deadRegs = d[it->first];
          if ((int)deadRegs.size() > saveCount) {
              saveCount = deadRegs.size();
              if (saveCount > 2) saveCount = 2;
              raOffset = curHeight;
              newAddr = it->first;
          }

          if (saveCount > 0 && it == insns.begin()) {
              MoveInstData *mid = new MoveInstData;
              mid->newInstAddress = it->first;
              mid->raOffset = raOffset;
              mid->saveCount = saveCount;
              
              auto& deadRegs = d[it->first];
              if (deadRegs.size() == 1) {
                  mid->reg1 = *(deadRegs.begin());
              } else {
                  auto it = deadRegs.begin();
                  mid->reg1 = *it;
                  ++it;
                  mid->reg2 = *it;
              }
              s->entryFixedData[blockEntry] = mid;
          }

          if (saveCount == 2) break;
          if (it->second.getOperation().getID() == e_push) {
              curHeight += 8;
              continue;
          }
          if (it->second.writesMemory()) break;
          if (MoveSP(it->second)) break;
      }

      if (saveCount > 0 && newAddr > 0) {
          MoveInstData *mid = new MoveInstData;
          mid->newInstAddress = newAddr;
          mid->raOffset = raOffset;
          mid->saveCount = saveCount;

          auto& deadRegs = d[newAddr];
          if (deadRegs.size() == 1) {
              mid->reg1 = *(deadRegs.begin());
          } else {
              auto it = deadRegs.begin();
              mid->reg1 = *it;
              ++it;
              mid->reg2 = *it;
          }
          s->entryData[blockEntry] = mid;
      }
  }

  void CalculateExitInstPoint(std::map<Offset, Instruction> &insns,
          std::map<Offset, std::set<std::string> > &d,
          FuncSummary* s,
          Address blockEntry) {
      Address newAddr = 0;
      int saveCount = 0;
      int raOffset = 0;
      int curHeight = 0;
      for (auto it = insns.rbegin(); it != insns.rend(); ++it) {
          entryID e = it->second.getOperation().getID();
          if (e == e_ret || e == e_ret_near || e == e_ret_far) continue;
          // pop can writes to a memory location
          if (it->second.writesMemory()) break;

          if (e == e_pop) {
              curHeight += 8;
          } else {
              if (MoveSP(it->second)) break;
              if (ReadFlags(it->second)) break;
          }

          auto & deadRegs = d[it->first];
          if ((int)deadRegs.size() > saveCount) {
              saveCount = deadRegs.size();
              if (saveCount > 2) saveCount = 2;
              raOffset = curHeight;
              newAddr = it->first;
          }
          if (saveCount == 2) break;

      }

      if (saveCount > 0 && newAddr > 0) {
          MoveInstData *mid = new MoveInstData;
          mid->newInstAddress = newAddr;
          mid->raOffset = raOffset;
          mid->saveCount = saveCount;

          auto& deadRegs = d[newAddr];
          if (deadRegs.size() == 1) {
              mid->reg1 = *(deadRegs.begin());
          } else {
              auto it = deadRegs.begin();
              mid->reg1 = *it;
              ++it;
              mid->reg2 = *it;
          }
          s->exitData[blockEntry] = mid;
      }

  }

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {

    for (auto b : f->blocks()) {
      std::map<Offset, Instruction> insns;
      b->getInsns(insns);
      std::map<Offset, std::set<std::string> > deadReg;

      CalculateBlockLevelLiveness(insns, deadReg);
      CalculateEntryInstPoint(insns, deadReg, s, b->start());
      CalculateExitInstPoint(insns, deadReg, s, b->start());
    }
  }
};

class SafePathsCounting : public Pass {
 public:
  SafePathsCounting()
      : Pass("Count the maximal number of safe control flow paths in a function",
             "Do not collapse SCC to get as many safe CF paths as possible") {}

  int countPaths(Block* cur, FuncSummary* s, std::set<ParseAPI::Edge*> &visited, std::set<Block*> &exitBlocks) {
      if (s->unsafe_blocks.find(cur) != s->unsafe_blocks.end()) {
          return 0;
      }
      if (exitBlocks.find(cur) != exitBlocks.end()) {
          return 1;
      }
      int c = 0;
      for (auto e : cur->targets()) {
          if (e->sinkEdge()) continue;
          if (e->interproc()) continue;
          if (e->type() == ParseAPI::CATCH) continue;
          if (visited.find(e) != visited.end()) continue;
          visited.insert(e);
          c += countPaths(e->trg(), s, visited, exitBlocks);
      }
      return c;
  }

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    std::set<ParseAPI::Edge*> visited;
    std::set<Block*> exitBlocks;
    for (auto b : f->exitBlocks())
        exitBlocks.insert(b);
    s->safe_paths = countPaths(f->entry(), s, visited, exitBlocks);
  }

};

class UnsafeCallBlockAnalysis : public Pass {
 public:
  UnsafeCallBlockAnalysis()
      : Pass("Identify unsafe call blocks",
             "A call block to safe function is safe") {}
  void RunGlobalAnalysis(CodeObject* co,
                         std::map<Function*, FuncSummary*>& summaries,
                         PassResult* result) override {
      std::set<Address> safe_func;
      for (auto f : co->funcs()) {
          if (!summaries[f]->writes) {
              safe_func.insert(f->addr());
          }
      }

      for (auto f : co->funcs()) 
          for (auto b: f->blocks()) {
              Address callee = 0;
              for (auto e : b->targets()) {
                  if (e->sinkEdge() && e->type() == ParseAPI::CALL) {
                      summaries[f]->unsafe_blocks.insert(b);
                      break;
                  }
                  if (e->type() == ParseAPI::CALL) {
                      callee = e->trg()->start();
                      if (safe_func.find(callee) == safe_func.end()) summaries[f]->unsafe_blocks.insert(b);
                      break;
                  }
              }
          }
  }

};

class FunctionExceptionAnalysis : public Pass {
 public:
  FunctionExceptionAnalysis()
      : Pass("Inter-procedural Exception Analysis",
             "Assume a plt call may throw an exception.") {}

  void RunGlobalAnalysis(CodeObject* co,
                         std::map<Function*, FuncSummary*>& summaries,
                         PassResult* result) override {
    std::set<Function*> visited;
    for (auto f : co->funcs()) {
      auto it = visited.find(f);
      if (it == visited.end()) {
        VisitFunction(co, summaries[f], summaries, visited);
      }
    }
    for (auto f : co->funcs())
        if (summaries[f]->func_exception_safe)
            exception_free_func.insert(f->addr());
  }

 private:
  bool VisitFunction(CodeObject* co, FuncSummary* s,
                     std::map<Function*, FuncSummary*>& summaries,
                     std::set<Function*>& visited) {
    visited.insert(s->func);
    if (s->has_plt_call) {
        s->func_exception_safe = false;
        return false;
    }
    if (s->has_unknown_cf) {
        s->func_exception_safe = false;
        return false;
    }
    
    bool ret = true;
    for (auto f : s->callees) {
      auto it = visited.find(f);
      if (it == visited.end()) {
        ret &= VisitFunction(co, summaries[f], summaries, visited);
      } else {
        ret &= summaries[f]->writes;
      }
    }

    s->func_exception_safe = ret;
    return ret;
  }
};

#endif  // LITECFI_PASSES_H
