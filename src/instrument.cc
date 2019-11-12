
#include <algorithm>
#include <iostream>
#include <vector>

#include "BPatch.h"
#include "BPatch_basicBlock.h"
#include "BPatch_edge.h"
#include "BPatch_flowGraph.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "BPatch_point.h"

#include "InstSpec.h"
#include "PatchMgr.h"
#include "Point.h"
#include "Snippet.h"
#include "PatchModifier.h"

#include "asmjit/asmjit.h"
#include "assembler.h"
#include "codegen.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit.h"
#include "parse.h"
#include "pass_manager.h"
#include "passes.h"
#include "utils.h"

#include "Module.h"
#include "Symbol.h"

using namespace Dyninst;
using namespace Dyninst::PatchAPI;

DECLARE_bool(libs);
DECLARE_bool(vv);

DECLARE_string(output);
DECLARE_string(shadow_stack);
DECLARE_string(threat_model);
DECLARE_string(stats);
DECLARE_string(skip_list);

// Thread local shadow stack initialization function name.
static constexpr char kShadowStackInitFn[] = "litecfi_init_mem_region";
// Init function which needs to be instrumented with a call the thread local
// shadow stack initialzation function above.
static constexpr char kInitFn[] = "_start";

// Trampoline specifications.
static InstSpec is_init;
static InstSpec is_empty;
static std::set<Address> skip_addrs;
static CFGMaker* cfgMaker;

struct InstrumentationResult {
  std::vector<std::string> safe_fns;
  std::vector<std::string> lowered_fns;
  std::vector<std::string> reg_stack_fns;
};

class StackOpSnippet : public Dyninst::PatchAPI::Snippet {
 public:
  explicit StackOpSnippet(FuncSummary* summary, bool u, int h) : summary_(summary), useOriginalCode(u), height(h) {}

  bool generate(Dyninst::PatchAPI::Point* pt, Dyninst::Buffer& buf) override {
    AssemblerHolder ah;
    jit_fn_(pt, summary_, ah, useOriginalCode, height);

    size_t size = ah.GetCode()->codeSize();
    char* temp_buf = (char*)malloc(size);

    ah.GetCode()->relocateToBase((uint64_t)temp_buf);

    size = ah.GetCode()->codeSize();
    ah.GetCode()->copyFlattenedData(temp_buf, size,
                                    asmjit::CodeHolder::kCopyWithPadding);

    buf.copy(temp_buf, size);
    return true;
  }

 protected:
  std::string (*jit_fn_)(Dyninst::PatchAPI::Point* pt, FuncSummary* summary,
                         AssemblerHolder&, bool, int);

 private:
  FuncSummary* summary_;
  bool useOriginalCode;
  int height;
};

class StackPushSnippet : public StackOpSnippet {
 public:
  explicit StackPushSnippet(FuncSummary* summary, bool u, int h = 0) : StackOpSnippet(summary, u, h) {
    jit_fn_ = JitStackPush;
  }
};

class StackPopSnippet : public StackOpSnippet {
 public:
  explicit StackPopSnippet(FuncSummary* summary, bool u) : StackOpSnippet(summary, u, 0) {
    jit_fn_ = JitStackPop;
  }
};

class RegisterPushSnippet : public StackOpSnippet {
 public:
  explicit RegisterPushSnippet(FuncSummary* summary) : StackOpSnippet(summary, false, 0) {
    jit_fn_ = JitRegisterPush;
  }
};

class RegisterPopSnippet : public StackOpSnippet {
 public:
  explicit RegisterPopSnippet(FuncSummary* summary) : StackOpSnippet(summary, false, 0) {
    jit_fn_ = JitRegisterPop;
  }
};

bool IsNonreturningCall(Point* point) {
  PatchBlock* exitBlock = point->block();
  assert(exitBlock);
  bool call = false;
  bool callFT = false;
  for (auto e : exitBlock->targets()) {
    if (e->type() == CALL)
      call = true;
    if (e->type() == CALL_FT)
      callFT = true;
  }
  return (call && !callFT);
}

void InsertSnippet(BPatch_function* function, Point::Type location,
                   Snippet::Ptr snippet, PatchMgr::Ptr patcher) {
  std::vector<Point*> points;
  patcher->findPoints(Scope(Dyninst::PatchAPI::convert(function)), location,
                      back_inserter(points));

  for (auto it = points.begin(); it != points.end(); ++it) {
    Point* point = *it;
    // Do not instrument function exits that are calls to non-returning
    // functions because the stack frame may still not be tear down, causing the
    // instrumentation to get wrong return address.
    if (location == Point::FuncExit && IsNonreturningCall(point))
      continue;
    point->pushBack(snippet);
  }
}

bool CheckFastPathFunction(BPatch_basicBlock*& entry,
                           vector<BPatch_basicBlock*>& exits,
                           BPatch_function* f) {
  BPatch_flowGraph* cfg = f->getCFG();

  // Should have only one function entry block.
  std::vector<BPatch_basicBlock*> eb;
  cfg->getEntryBasicBlock(eb);
  if (eb.size() != 1)
    return false;
  BPatch_basicBlock* func_entry = eb[0];

  // The function entry block should not contain memory writes.
  std::vector<Dyninst::InstructionAPI::Instruction> insns;
  func_entry->getInstructions(insns);
  for (auto i : insns) {
    // Here we should reuse stack analysis to allow writes to known stack
    // location. But it is going to be a future optimization.
    if (i.writesMemory())
      return false;
  }

  eb.clear();
  cfg->getExitBasicBlock(eb);
  // The function entry block should have two edges,
  // one cond-taken, one cond-not-taken.
  //
  // One of the edge should point to an exit block
  // as the fast path exit block.
  std::vector<BPatch_edge*> edges;
  func_entry->getOutgoingEdges(edges);
  if (edges.size() != 2)
    return false;
  bool condTaken = false;
  bool condNotTaken = false;
  BPatch_basicBlock* fastExitBlock = NULL;
  for (auto e : edges) {
    if (e->getType() == CondJumpTaken)
      condTaken = true;
    if (e->getType() == CondJumpNottaken)
      condNotTaken = true;
    BPatch_basicBlock* b = e->getTarget();
    if (std::find(eb.begin(), eb.end(), b) != eb.end()) {
      fastExitBlock = b;
    } else {
      entry = b;
    }
  }
  if (!condTaken || !condNotTaken || fastExitBlock == NULL || entry == NULL)
    return false;

  // Function entry block cannot have intra-procedural incoming edges.
  edges.clear();
  func_entry->getIncomingEdges(edges);
  if (edges.size() > 0)
    return false;

  // The slow path entry should only have entry block as source.
  // Otherwise, the stack push instrumentation will be executed multiple times.
  edges.clear();
  entry->getIncomingEdges(edges);
  if (edges.size() != 1)
    return false;

  // The fast path exit block should contain one return instruction.
  // This condition makes sure that our shadow stack instrumentation can
  // find the correct location for return address.
  // TODO: relax this condition later.
  insns.clear();
  fastExitBlock->getInstructions(insns);
  if (insns.size() != 1)
    return false;
  if (insns[0].getCategory() != InstructionAPI::c_ReturnInsn)
    return false;

  // Excluding the function entry block,
  // each block that is a source block of the fast exit block
  // should be instrumented with shadow stack pop.
  edges.clear();
  fastExitBlock->getIncomingEdges(edges);
  for (auto e : edges) {
    if (e->getSource() != func_entry) {
      BPatch_basicBlock* slowPathExit = e->getSource();
      // If the slow path exit has more than one outgoing edges,
      // we cannot instrument at its exit, because stack pop operation
      // can potentially be performed mulitple times.
      std::vector<BPatch_edge*> out_edges;
      slowPathExit->getOutgoingEdges(out_edges);
      if (out_edges.size() != 1)
        return false;
      exits.push_back(e->getSource());
    }
  }

  // In addition, we need to instrument all non-fast-exit exit blocks.
  for (auto b : eb)
    if (b != fastExitBlock)
      exits.push_back(b);
  return true;
}

bool DoStackOpsUsingRegisters(BPatch_function* function, FuncSummary* summary,
                              const litecfi::Parser& parser,
                              PatchMgr::Ptr patcher) {
  if (summary->shouldUseRegisterFrame()) {
    fprintf(stdout, "[Register Stack] Function : %s\n",
            Dyninst::PatchAPI::convert(function)->name().c_str());
    Snippet::Ptr stack_push =
        RegisterPushSnippet::create(new RegisterPushSnippet(summary));
    InsertSnippet(function, Point::FuncEntry, stack_push, patcher);

    Snippet::Ptr stack_pop =
        RegisterPopSnippet::create(new RegisterPopSnippet(summary));
    InsertSnippet(function, Point::FuncExit, stack_pop, patcher);

    BPatch_nullExpr nopSnippet;
    vector<BPatch_point*> points;
    function->getEntryPoints(points);
    BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

    binary_edit->insertSnippet(nopSnippet, points, BPatch_callBefore,
                               BPatch_lastSnippet, &is_empty);

    points.clear();
    function->getExitPoints(points);
    binary_edit->insertSnippet(nopSnippet, points, BPatch_callAfter,
                               BPatch_lastSnippet, &is_empty);
    return true;
  }
  return false;
}

Block* CopyBlock(Block* block) {
  // Do CFGMaker::copyBlock magic here to copy the block.
  return nullptr;
}

Block* StackPopAndCopyBlock(Block* block) {
  // Insert a stack pop to the copied block.
  return nullptr;
}

Block* CreateStackPushBlock() {
  // Create and return a basic block with just stack push instrumentation.
  return nullptr;
}

void RewireBlock(Block* src, Block* target) {
  // Do PatchModifier::redirect magic here and rewire blocks.
}

void VisitAndCopyCFG(SCComponent* sc, std::set<SCComponent*>& visited,
                     bool unsafe_flow) {
  if (visited.find(sc) != visited.end())
    return;

  for (auto child : sc->children) {
    VisitAndCopyCFG(child, visited, sc->stack_push || unsafe_flow);
  }

  if (sc->stack_push) {
    Block* src = CreateStackPushBlock();
    sc->blocks.insert(src);

    DCHECK(sc->outgoing.size() == 1);
    for (auto it : sc->outgoing) {
      SCComponent* target_sc = it.second;
      auto itt = target_sc->block_remappings.find(it.first);
      DCHECK(itt != target_sc->block_remappings.end());
      RewireBlock(src, itt->second);
    }

    visited.insert(sc);
    return;
  }

  // Copy over the blocks.
  for (auto b : sc->blocks) {
    if (unsafe_flow && sc->returns.find(b) != sc->returns.end()) {
      // Insert stack pop instrumentation while copying the nodes.
      sc->block_remappings[b] = StackPopAndCopyBlock(b);
      continue;
    }
    sc->block_remappings[b] = CopyBlock(b);
  }

  // Now rewire the blocks.
  for (auto b : sc->blocks) {
    Block* src = sc->block_remappings[b];
    for (auto e : b->targets()) {
      Block* target = e->trg();

      // Check if this is an intra component edge and skip if so.
      auto it = sc->block_remappings.find(target);
      if (it != sc->block_remappings.end()) {
        RewireBlock(src, it->second);
        continue;
      }

      SCComponent* target_sc = sc->outgoing[target];
      if (target_sc->stack_push) {
        DCHECK(target_sc->blocks.size() == 1);
        for (auto tgt : target_sc->blocks) {
          RewireBlock(src, tgt);
        }
      } else {
        auto it = target_sc->block_remappings.find(target);
        DCHECK(it != target_sc->block_remappings.end());

        RewireBlock(src, it->second);
      }
    }
  }

  visited.insert(sc);
}


bool skipPatchEdges(PatchEdge *e) {
    if (e->sinkEdge() || e->interproc()) return true;
    if (e->type() == ParseAPI::CATCH) return true;
    return false;
}

void CloneFunctionCFG(PatchFunction* f, 
        PatchMgr::Ptr patcher, 
        std::map<PatchBlock*, PatchBlock*> &cloneBlockMap) {    
    // Clone all blocks
    std::vector<PatchBlock*> newBlocks;
    for (auto b : f->blocks()) {
        PatchBlock* cloneB = cfgMaker->cloneBlock(b, b->object());
        cloneBlockMap[b] = cloneB;
        newBlocks.push_back(cloneB);
        fprintf(stderr, "old block %p, new block %p\n", b, cloneB);
    }
    fprintf(stderr, "%d new cloned blocks to %p\n", newBlocks.size(), f);
    for (auto b: newBlocks) {
        PatchModifier::addBlockToFunction(f, b);
    }

    // The edges of the cloned blocks are still from/to original blocks
    // Now we redirect all edges
    for (auto b : newBlocks) {
        for (auto e : b->targets()) {
            if (skipPatchEdges(e)) continue;
            PatchBlock* newTarget = cloneBlockMap[e->trg()];
            assert (PatchModifier::redirect(e , newTarget) ); 
        }
    }
}

void RedirectTransitionEdges(PatchBlock* cur, 
                            FuncSummary* summary, 
                            std::map<PatchBlock*, PatchBlock*> &cloneBlockMap,
                            std::set<PatchEdge*>& redirect,
                            std::set<PatchEdge*>& visited) {
    for (auto e : cur->targets()) {
        if (skipPatchEdges(e)) continue;
        if (visited.find(e) != visited.end()) continue;
        visited.insert(e);
        PatchBlock* target = e->trg();
        if (summary->unsafe_blocks.find(target->block()) != summary->unsafe_blocks.end()) {
            assert(PatchModifier::redirect(e , cloneBlockMap[target]));
            redirect.insert(e);
        } else {
            RedirectTransitionEdges(target, summary, cloneBlockMap, redirect, visited);
        }
    }
}

bool DoInstrumentationLowering(BPatch_function* function, FuncSummary* summary,
                               const litecfi::Parser& parser,
                               PatchMgr::Ptr patcher) {
  if (!summary || !summary->lowerInstrumentation())
      return false;
  fprintf(stderr, "Enter DoInstrumentationLowering\n");
  for (auto b: summary->unsafe_blocks) {
      fprintf(stderr, "\tunsafe block [%lx, %lx)\n", b->start(), b->end());
  }
  PatchFunction *f = PatchAPI::convert(function);
  assert(parser.parser->markPatchFunctionEntryInstrumented(f));

  std::map<PatchBlock*, PatchBlock*> cloneBlockMap; 
  CloneFunctionCFG(f, patcher, cloneBlockMap);

  fprintf(stderr, "After cloning\n");
  for (auto b : f->blocks()) {
      fprintf(stderr, "%p %p\n", b, b->block());
      for (auto e : b->targets()) {
          fprintf(stderr, "\t(%p,%p) ", e, e->trg());
      }
      fprintf(stderr, "\n");
  }

  
  std::set<PatchEdge*> visited;
  std::set<PatchEdge*> redirect;
  RedirectTransitionEdges(f->entry(), summary, cloneBlockMap, redirect, visited);

  fprintf(stderr, "%d trasition edges\n", redirect.size());
  fprintf(stderr, "After redirecting edges\n");
  for (auto b : f->blocks()) {
      fprintf(stderr, "%p %p\n", b, b->block());
      for (auto e : b->targets()) {
          fprintf(stderr, "\t(%p,%p) ", e, e->trg());
      }
      fprintf(stderr, "\n");
  }

  // Insert stack push operations
  for (auto e : redirect) {
      assert(summary->SPHeight.find(e->src()->start()) != summary->SPHeight.end());
      int height = summary->SPHeight[e->src()->start()];
      fprintf(stderr, "SP height %d\n", height);
      Snippet::Ptr stack_push;
      if (summary->shouldUseRegisterFrame()) {
          stack_push = RegisterPushSnippet::create(new RegisterPushSnippet(summary));
      } else {
          stack_push = StackPushSnippet::create(new StackPushSnippet(summary, false, height));
      }

      Point * p = patcher->findPoint(PatchAPI::Location::EdgeInstance(f, e), Point::EdgeDuring);
      assert(p);
      p->pushBack(stack_push);
  }

  // Insert stack pop operations
  Snippet::Ptr stack_pop;
  if (summary->shouldUseRegisterFrame()) {
      stack_pop = RegisterPopSnippet::create(new RegisterPopSnippet(summary));
  } else {
      stack_pop = StackPopSnippet::create(new StackPopSnippet(summary, false));
  }
  for (auto b: f->exitBlocks()) {
      PatchBlock* cloneB = cloneBlockMap[b];
      Point* p = patcher->findPoint(PatchAPI::Location::BlockInstance(f, cloneB), Point::BlockExit);
      assert(p);
      if (IsNonreturningCall(p)) continue;
      fprintf(stderr, "insert stack pop operation to cloned exit %p\n", cloneB);
      p->pushBack(stack_pop);
      assert(parser.parser->markPatchBlockInstrumented(cloneB));
  }

  return true;
}

void AddInlineHint(BPatch_function* function, const litecfi::Parser& parser) {
  Address entry = (Address)(function->getBaseAddr());
  parser.parser->addInliningEntry(entry);
}

bool Skippable(BPatch_function* function, FuncSummary* summary) {
  if (summary != nullptr && summary->safe) {
    StdOut(Color::RED, FLAGS_vv)
        << "      Skipping function : " << function->getName() << Endl;
    return true;
  }
  if ((Address)(function->getBaseAddr()) != 0x486790) return true;

  if (skip_addrs.find((Address)(function->getBaseAddr())) != skip_addrs.end()) {
    StdOut(Color::RED, FLAGS_vv)
        << "      Skipping function (user's skip list): " << function->getName()
        << Endl;

    return true;
  }
  return false;
}

bool MoveInstrumentation(BPatch_point * &p, FuncSummary* s) {
  if (p->getPointType() == BPatch_locEntry) {
    BPatch_function* f = p->getFunction();
    BPatch_flowGraph* cfg = f->getCFG();
    
    // Should have only one function entry block.
    std::vector<BPatch_basicBlock*> eb;
    cfg->getEntryBasicBlock(eb);
    if (eb.size() != 1)
      return false;

    // If the function entry block has intra-procedural
    // incoming edges, then we have to instrument at function entry.
    // Otherwise, the stack push op will be executed in a loop
    BPatch_basicBlock* func_entry = eb[0];
    std::vector<BPatch_edge*> edges;
    func_entry->getIncomingEdges(edges);
    if (edges.size() > 0)
      return false;
    MoveInstData* mid = s->getMoveInstDataAtEntry(func_entry->getStartAddress());
    if (mid == nullptr) 
      return false;
    p = func_entry->findPoint(mid->newInstAddress);
  } else if (p->getPointType() == BPatch_locBasicBlockEntry) {
    BPatch_basicBlock* b = p->getBlock();
    MoveInstData* mid = s->getMoveInstDataAtEntry(b->getStartAddress());
    if (mid == nullptr) 
      return false;
    p = b->findPoint(mid->newInstAddress);
  } else if (p->getPointType() == BPatch_locExit || p->getPointType() == BPatch_locBasicBlockExit) {
    BPatch_basicBlock* b = p->getBlock();
    MoveInstData* mid = s->getMoveInstDataAtExit(b->getStartAddress());
    if (mid == nullptr) 
      return false;
    p = b->findPoint(mid->newInstAddress);
  } else {
    // Cannot move instrumentation if instrumenting at an edge
    return false;
  }
  return true;
}

void InstrumentFunction(BPatch_function* function,
                        const litecfi::Parser& parser, PatchMgr::Ptr patcher,
                        const std::map<uint64_t, FuncSummary*>& analyses,
                        InstrumentationResult* res) {
  std::string fn_name = Dyninst::PatchAPI::convert(function)->name();
  StdOut(Color::YELLOW, FLAGS_vv) << "     Function : " << fn_name << Endl;

  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
  BPatch_nullExpr nopSnippet;
  vector<BPatch_point*> points;
  FuncSummary* summary = nullptr;

  if (FLAGS_shadow_stack == "light") {
    auto it = analyses.find(static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(function->getBaseAddr())));
    if (it != analyses.end())
      summary = (*it).second;

    // Check if this function is safe to skip and do so if it is.
    if (Skippable(function, summary)) {
      res->safe_fns.push_back(fn_name);
      return;
    }

    // Add inlining hint so that writeFile may inline small leaf functions.
    AddInlineHint(function, parser);

    // If possible check and lower the instrumentation to within non frequently
    // executed unsafe control flow paths.
    if (DoInstrumentationLowering(function, summary, parser, patcher)) {
      return;
    }

    // For leaf functions we may be able to carry out stack operations using
    // unused registers.
    if (DoStackOpsUsingRegisters(function, summary, parser, patcher)) {
      res->reg_stack_fns.push_back(fn_name);
      return;
    }

    // Apply fast path optimization if applicable.
    BPatch_basicBlock* condNotTakenEntry = NULL;
    vector<BPatch_basicBlock*> condNotTakenExits;
    if (CheckFastPathFunction(condNotTakenEntry, condNotTakenExits, function)) {
      res->lowered_fns.push_back(fn_name);
      StdOut(Color::RED, FLAGS_vv)
          << "      Optimized fast path instrumentation for function at 0x"
          << std::hex << (uint64_t)function->getBaseAddr() << Endl;
      /* If the function has the following shape:
       * entry:
       *    code that does not writes memory
       *    jz A   (or other conditional jump)
       *    some complicated code
       * A: ret
       *
       * Then we do not need to instrument the fast path: entry -> ret.
       * We can instrument the entry and exit of the "some complicated code",
       * which is the slow path.
       */

      // Instrument slow paths entry with stack push operation
      // and nop snippet, which enables instrumentation frame spec.
      //
      // Also attempt to move instrumentation to utilize existing push & pop
      BPatch_point* push_point = condNotTakenEntry->findEntryPoint() ;
      bool moveInst = MoveInstrumentation(push_point, summary);      
      Snippet::Ptr stack_push =
          StackPushSnippet::create(new StackPushSnippet(summary, moveInst));
      PatchAPI::convert(push_point, BPatch_callBefore)->pushBack(stack_push);
      points.push_back(push_point);
      binary_edit->insertSnippet(nopSnippet, points, BPatch_callBefore,
                                 BPatch_lastSnippet, &is_empty);

      // Instrument slow paths exits with stack pop operations
      // and nop snippet, which enables instrumentation frame spec.
      points.clear();
      std::vector<BPatch_point*> insnPoints;
      for (auto b : condNotTakenExits) {
        BPatch_point* pop_point = b->findExitPoint();
        bool moveInst = MoveInstrumentation(pop_point, summary);
        Snippet::Ptr stack_pop =
            StackPopSnippet::create(new StackPopSnippet(summary, moveInst));
        if (pop_point->getPointType() == BPatch_locInstruction) {
          PatchAPI::convert(pop_point, BPatch_callBefore)->pushBack(stack_pop);
          insnPoints.push_back(pop_point);
        } else {
          PatchAPI::convert(pop_point, BPatch_callAfter)->pushBack(stack_pop);
          points.push_back(pop_point);
        }
      }
      binary_edit->insertSnippet(nopSnippet, insnPoints, BPatch_callBefore,
                                 BPatch_lastSnippet, &is_empty);
      binary_edit->insertSnippet(nopSnippet, points, BPatch_callAfter,
                                 BPatch_lastSnippet, &is_empty);
      return;
    } else {
      // Attempt to move instrumentation to utilize 
      // existing push & pop
      std::vector<BPatch_point*> entryPoints;
      function->getEntryPoints(entryPoints);
      for (auto& p : entryPoints) {
        bool moveInst = MoveInstrumentation(p, summary);      
        Snippet::Ptr stack_push =
            StackPushSnippet::create(new StackPushSnippet(summary, moveInst));
        PatchAPI::convert(p, BPatch_callBefore)->pushBack(stack_push);
      }
      binary_edit->insertSnippet(nopSnippet, entryPoints, BPatch_callBefore,
                                 BPatch_lastSnippet, &is_empty);

      std::vector<BPatch_point*> exitPoints;
      function->getExitPoints(exitPoints);

      std::vector<BPatch_point*> beforePoints;
      std::vector<BPatch_point*> afterPoints;
      for (auto& p : exitPoints) {
        if (IsNonreturningCall(PatchAPI::convert(p, BPatch_callAfter))) continue;
        bool moveInst = MoveInstrumentation(p, summary);
        Snippet::Ptr stack_pop =
            StackPopSnippet::create(new StackPopSnippet(summary, moveInst));
        if (p->getPointType() == BPatch_locInstruction) {
          PatchAPI::convert(p, BPatch_callBefore)->pushBack(stack_pop);
          beforePoints.push_back(p);
        } else {
          PatchAPI::convert(p, BPatch_callAfter)->pushBack(stack_pop);
          afterPoints.push_back(p);
        }
      }
      binary_edit->insertSnippet(nopSnippet, beforePoints, BPatch_callBefore,
                                 BPatch_lastSnippet, &is_empty);
      binary_edit->insertSnippet(nopSnippet, afterPoints, BPatch_callAfter,
                                 BPatch_lastSnippet, &is_empty);
      return;
    }
  }

  // Either we are in 'full' instrumentation mode or in 'light' mode but none of
  // the optimizations worked out. Carry out regular entry-exit shadow stack
  // instrumentation.
  Snippet::Ptr stack_push =
      StackPushSnippet::create(new StackPushSnippet(summary, false));
  InsertSnippet(function, Point::FuncEntry, stack_push, patcher);

  Snippet::Ptr stack_pop =
      StackPopSnippet::create(new StackPopSnippet(summary, false));
  InsertSnippet(function, Point::FuncExit, stack_pop, patcher);

  function->getEntryPoints(points);
  binary_edit->insertSnippet(nopSnippet, points, BPatch_callBefore,
                             BPatch_lastSnippet, &is_empty);

  points.clear();
  function->getExitPoints(points);
  binary_edit->insertSnippet(nopSnippet, points, BPatch_callAfter,
                             BPatch_lastSnippet, &is_empty);
}

BPatch_function* FindFunctionByName(BPatch_image* image, std::string name) {
  BPatch_Vector<BPatch_function*> funcs;
  if (image->findFunction(name.c_str(), funcs,
                          /* showError */ true,
                          /* regex_case_sensitive */ true,
                          /* incUninstrumentable */ true) == nullptr ||
      !funcs.size() || funcs[0] == nullptr) {
    return nullptr;
  }
  return funcs[0];
}

void InstrumentInitFunction(BPatch_function* function,
                            const litecfi::Parser& parser) {
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
  BPatch_Vector<BPatch_snippet*> args;

  auto stack_init_fn = FindFunctionByName(parser.image, kShadowStackInitFn);

  BPatch_funcCallExpr stack_init(*stack_init_fn, args);
  std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);
  BPatchSnippetHandle* handle = nullptr;
  handle = binary_edit->insertSnippet(stack_init, *entries, BPatch_callBefore,
                                      BPatch_lastSnippet, &is_init);
  DCHECK(handle != nullptr)
      << "Failed to instrument init function for stack initialization.";
}

static void GetIFUNCs(BPatch_module* module,
                      std::set<Dyninst::Address>& addrs) {
  SymtabAPI::Module* sym_mod = SymtabAPI::convert(module);
  std::vector<SymtabAPI::Symbol*> ifuncs;

  // Dyninst represents IFUNC as ST_INDIRECT.
  sym_mod->getAllSymbolsByType(ifuncs, SymtabAPI::Symbol::ST_INDIRECT);
  for (auto sit = ifuncs.begin(); sit != ifuncs.end(); ++sit) {
    addrs.insert((Address)(*sit)->getOffset());
  }
}

void InstrumentModule(BPatch_module* module, const litecfi::Parser& parser,
                      PatchMgr::Ptr patcher,
                      const std::map<uint64_t, FuncSummary*>& analyses,
                      InstrumentationResult* res) {
  std::vector<BPatch_function*>* functions = module->getProcedures();

  std::set<Dyninst::Address> ifuncAddrs;
  GetIFUNCs(module, ifuncAddrs);

  for (auto it = functions->begin(); it != functions->end(); it++) {
    BPatch_function* function = *it;

    ParseAPI::Function* f = ParseAPI::convert(function);
    if (f->retstatus() == ParseAPI::NORETURN)
      continue;

    // We should only instrument functions in .text.
    ParseAPI::CodeRegion* codereg = f->region();
    ParseAPI::SymtabCodeRegion* symRegion =
        dynamic_cast<ParseAPI::SymtabCodeRegion*>(codereg);
    assert(symRegion);
    SymtabAPI::Region* symR = symRegion->symRegion();
    if (symR->getRegionName() != ".text")
      continue;

    InstrumentFunction(function, parser, patcher, analyses, res);
  }
}

void InstrumentCodeObject(BPatch_object* object, const litecfi::Parser& parser,
                          PatchMgr::Ptr patcher, InstrumentationResult* res) {
  if (!IsSharedLibrary(object)) {
    StdOut(Color::GREEN, FLAGS_vv) << "\n  >> Instrumenting main application "
                                   << object->pathName() << Endl;
  } else {
    StdOut(Color::GREEN, FLAGS_vv)
        << "\n    Instrumenting " << object->pathName() << Endl;
  }

  if (FLAGS_threat_model == "trust_system" && IsSystemCode(object)) {
    return;
  }

  std::vector<BPatch_module*> modules;
  object->modules(modules);

  std::map<uint64_t, FuncSummary*> analyses;
  // Do the static analysis on this code and obtain skippable functions.
  if (FLAGS_shadow_stack == "light") {
    CodeObject* co = Dyninst::ParseAPI::convert(object);
    co->parse();

    PassManager* pm = new PassManager;
    pm->AddPass(new CallGraphAnalysis())
        ->AddPass(new LargeFunctionFilter())
        ->AddPass(new IntraProceduralMemoryAnalysis())
        ->AddPass(new InterProceduralMemoryAnalysis())
        /*
        ->AddPass(new CFGAnalysis())
        ->AddPass(new CFGStatistics())
        ->AddPass(new LowerInstrumentation())
        ->AddPass(new LinkParentsOfCFG())
        ->AddPass(new CoalesceIngressInstrumentation())
        ->AddPass(new CoalesceEgressInstrumentation())
        ->AddPass(new ValidateCFG())
        ->AddPass(new LoweringStatistics())
        */
        ->AddPass(new DeadRegisterAnalysis())
        ->AddPass(new BlockDeadRegisterAnalysis());
    std::set<FuncSummary*> summaries = pm->Run(co);

    for (auto f : summaries) {
      analyses[f->func->addr()] = f;
    }
  }

  for (auto it = modules.begin(); it != modules.end(); it++) {
    char modname[2048];
    BPatch_module* module = *it;
    module->getName(modname, 2048);

    InstrumentModule(module, parser, patcher, analyses, res);
  }
}

void SetupInstrumentationSpec() {
  // Suppose we instrument a call to stack init at entry of A;
  // If A does not use r11, we dont need to save r11 (_start does not).
  is_init.trampGuard = false;
  is_init.redZone = false;
  is_init.saveRegs.push_back(x86_64::rax);
  is_init.saveRegs.push_back(x86_64::rdx);

  is_empty.trampGuard = false;
  is_empty.redZone = false;
}

void Instrument(std::string binary, const litecfi::Parser& parser) {
  StdOut(Color::BLUE, FLAGS_vv) << "\n\nInstrumentation Pass" << Endl;
  StdOut(Color::BLUE, FLAGS_vv) << "====================" << Endl;

  cfgMaker = parser.parser->getCFGMaker();

  StdOut(Color::BLUE) << "+ Instrumenting the binary..." << Endl;

  if (FLAGS_skip_list != "") {
    std::ifstream infile(FLAGS_skip_list, std::fstream::in);
    Address addr;
    while (infile >> std::hex >> addr)
      skip_addrs.insert(addr);
  }

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  PatchMgr::Ptr patcher = Dyninst::PatchAPI::convert(parser.app);
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

  SetupInstrumentationSpec();

  InstrumentationResult* res = new InstrumentationResult;

  for (auto it = objects.begin(); it != objects.end(); it++) {
    BPatch_object* object = *it;

    // Skip other shared libraries for now.
    if (!FLAGS_libs && IsSharedLibrary(object)) {
      continue;
    }

    InstrumentCodeObject(object, parser, patcher, res);
  }

  if (FLAGS_output.empty()) {
    binary_edit->writeFile((binary + "_cfi").c_str());
  } else {
    binary_edit->writeFile(FLAGS_output.c_str());
  }

  StdOut(Color::RED) << "Safe functions : " << res->safe_fns.size() << "\n  ";
  for (auto it : res->safe_fns) {
    StdOut(Color::BLUE) << it << " ";
  }
  StdOut(Color::BLUE) << Endl << Endl;

  StdOut(Color::RED) << "Register stack functions : "
                     << res->reg_stack_fns.size() << "\n  ";
  for (auto it : res->reg_stack_fns) {
    StdOut(Color::BLUE) << it << " ";
  }
  StdOut(Color::BLUE) << Endl << Endl;

  StdOut(Color::RED) << "Lowering stack functions : " << res->lowered_fns.size()
                     << "\n  ";
  for (auto it : res->lowered_fns) {
    StdOut(Color::BLUE) << it << " ";
  }
  StdOut(Color::BLUE) << Endl;
}
