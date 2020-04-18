
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
#include "PatchModifier.h"
#include "Point.h"
#include "Snippet.h"

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

#include "Symtab.h"
#include "Region.h"

using namespace Dyninst;
using namespace Dyninst::PatchAPI;

DECLARE_bool(libs);
DECLARE_bool(vv);

DECLARE_string(output);
DECLARE_string(shadow_stack);
DECLARE_string(threat_model);
DECLARE_string(stats);
DECLARE_string(skip_list);
DECLARE_string(sfi);

DECLARE_bool(disable_lowering);
DECLARE_bool(disable_reg_frame);
DECLARE_bool(disable_reg_save_opt);
DECLARE_bool(disable_inline);
DECLARE_bool(disable_sfe);

std::set<Address> exception_free_func;
std::set<Address> init_funcs;

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
static int total_func = 0;
static int func_with_indirect_or_plt_call = 0;
static int func_with_indirect_call = 0;
static int func_with_plt_call = 0;
static int memory_writes = 0;
static int stack_writes = 0;
static int heap_writes = 0;
static int global_writes = 0;
static int arg_writes = 0;
static int heap_or_arg_writes = 0;
static int total_dead_reg_site = 0;
static int no_dead_reg_site = 0;
static int lowering_dead_reg_site = 0;
static int lowering_no_dead_reg_entry_site = 0;
static int lowering_no_dead_reg_exit_site = 0;
static bool isLibc = false;
static int uninstrumentable = 0;

struct InstrumentationResult {
  std::vector<std::string> safe_fns;
  std::vector<std::string> lowered_fns;
  std::vector<std::string> reg_stack_fns;
};

class StackOpSnippet : public Dyninst::PatchAPI::Snippet {
 public:
  explicit StackOpSnippet(FuncSummary* summary, bool u, int h, bool u2)
      : summary_(summary), useOriginalCode(u), height(h),
        useOriginalCodeFixed(u2) {}

  bool generate(Dyninst::PatchAPI::Point* pt, Dyninst::Buffer& buf) override {
    AssemblerHolder ah;
    jit_fn_(pt, summary_, ah, useOriginalCode, height, useOriginalCodeFixed);

    size_t size = ah.GetCode()->codeSize();
    char* temp_buf = (char*)malloc(size);

    assert(ah.GetCode()->relocateToBase((uint64_t)temp_buf) == asmjit::kErrorOk);

    size = ah.GetCode()->codeSize();
    assert(ah.GetCode()->copyFlattenedData(temp_buf, size,
                                    asmjit::CodeHolder::kCopyWithPadding) == asmjit::kErrorOk);

    buf.copy(temp_buf, size);
    return true;
  }

 protected:
  std::string (*jit_fn_)(Dyninst::PatchAPI::Point* pt, FuncSummary* summary,
                         AssemblerHolder&, bool, int, bool);

 private:
  FuncSummary* summary_;
  bool useOriginalCode;
  int height;
  bool useOriginalCodeFixed;
};

class StackPushSnippet : public StackOpSnippet {
 public:
  explicit StackPushSnippet(FuncSummary* summary, bool u, int h = 0,
                            bool u2 = false)
      : StackOpSnippet(summary, u, h, u2) {
    jit_fn_ = JitStackPush;
  }
};

class StackPopSnippet : public StackOpSnippet {
 public:
  explicit StackPopSnippet(FuncSummary* summary, bool u)
      : StackOpSnippet(summary, u, 0, false) {
    jit_fn_ = JitStackPop;
  }
};

class RegisterPushSnippet : public StackOpSnippet {
 public:
  explicit RegisterPushSnippet(FuncSummary* summary, int height = 0)
      : StackOpSnippet(summary, false, height, false) {
    jit_fn_ = JitRegisterPush;
  }
};

class RegisterPopSnippet : public StackOpSnippet {
 public:
  explicit RegisterPopSnippet(FuncSummary* summary)
      : StackOpSnippet(summary, false, 0, false) {
    jit_fn_ = JitRegisterPop;
  }
};


class SFISnippet : public Dyninst::PatchAPI::Snippet {
 public:
  explicit SFISnippet(FuncSummary* summary)
      : summary_(summary) {
        jit_fn_ = JitSFI;
      }

  bool generate(Dyninst::PatchAPI::Point* pt, Dyninst::Buffer& buf) override {
    AssemblerHolder ah;
    jit_fn_(pt, summary_, ah);

    size_t size = ah.GetCode()->codeSize();
    char* temp_buf = (char*)malloc(size);

    assert(ah.GetCode()->relocateToBase((uint64_t)temp_buf) == asmjit::kErrorOk);

    size = ah.GetCode()->codeSize();
    assert(ah.GetCode()->copyFlattenedData(temp_buf, size,
                                    asmjit::CodeHolder::kCopyWithPadding) == asmjit::kErrorOk);

    buf.copy(temp_buf, size);
    return true;
  }

 protected:
  std::string (*jit_fn_)(Dyninst::PatchAPI::Point* pt, FuncSummary* summary,
                         AssemblerHolder&);

 private:
  FuncSummary* summary_;
};

class InitSnippet : public Dyninst::PatchAPI::Snippet {
 public:
  explicit InitSnippet(Address shadow_region_start)
      : start_(shadow_region_start) {
        jit_fn_ = JitInit;
      }

  bool generate(Dyninst::PatchAPI::Point* pt, Dyninst::Buffer& buf) override {
    AssemblerHolder ah;
    jit_fn_(buf.curAddr(), start_, ah);

    size_t size = ah.GetCode()->codeSize();
    char* temp_buf = (char*)malloc(size);

    assert(ah.GetCode()->relocateToBase((uint64_t)temp_buf) == asmjit::kErrorOk);

    size = ah.GetCode()->codeSize();
    assert(ah.GetCode()->copyFlattenedData(temp_buf, size,
                                    asmjit::CodeHolder::kCopyWithPadding) == asmjit::kErrorOk);

    buf.copy(temp_buf, size);
    free(temp_buf);
    return true;
  }

 protected:
  std::string (*jit_fn_)(Address, Address, AssemblerHolder&);

 private:
  Address start_;
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
  if (FLAGS_disable_lowering) return false;
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
  if (FLAGS_disable_reg_frame) return false;
  if (summary != nullptr && summary->shouldUseRegisterFrame()) {
    StdOut(Color::RED, FLAGS_vv)
        << "      [Register Stack] Function at 0x"
        << std::hex << (uint64_t)function->getBaseAddr() << Endl;
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

bool skipPatchEdges(PatchEdge* e) {
  if (e->sinkEdge() || e->interproc())
    return true;
  if (e->type() == ParseAPI::CATCH)
    return true;
  return false;
}

void CloneFunctionCFG(PatchFunction* f, PatchMgr::Ptr patcher,
                      std::map<PatchBlock*, PatchBlock*>& cloneBlockMap) {
  // Clone all blocks
  std::vector<PatchBlock*> newBlocks;
  for (auto b : f->blocks()) {
    PatchBlock* cloneB = cfgMaker->cloneBlock(b, b->object());
    cloneBlockMap[b] = cloneB;
    newBlocks.push_back(cloneB);
  }
  for (auto b : newBlocks) {
    PatchModifier::addBlockToFunction(f, b);
  }

  // The edges of the cloned blocks are still from/to original blocks
  // Now we redirect all edges
  for (auto b : newBlocks) {
    for (auto e : b->targets()) {
      if (skipPatchEdges(e))
        continue;      
      PatchBlock* newTarget = cloneBlockMap[e->trg()];
      assert(PatchModifier::redirect(e, newTarget));
    }
  }
}

void RedirectTransitionEdges(PatchBlock* cur, FuncSummary* summary,
                             std::set<PatchEdge*>& redirect,
                             std::set<PatchEdge*>& visited) {
  for (auto e : cur->targets()) {
    if (skipPatchEdges(e))
      continue;
    if (visited.find(e) != visited.end())
      continue;
    visited.insert(e);
    PatchBlock* target = e->trg();
    bool targetHasIndJump = false;
    for (auto te : target->targets()) {
      if (skipPatchEdges(te))
        continue;
      if (te->type() == ParseAPI::INDIRECT) {
        targetHasIndJump = true;
        break;
      }
    }
    if (summary->unsafe_blocks.find(target->block()) !=
        summary->unsafe_blocks.end() ||
        targetHasIndJump) {
      redirect.insert(e);
    } else {
      RedirectTransitionEdges(target, summary, redirect, visited);
    }
  }
}

void GetReachableBlocks(PatchBlock* b, std::set<PatchBlock*>& visited) {
  if (visited.find(b) != visited.end())
    return;
  visited.insert(b);
  for (auto e : b->targets()) {
    if (e->interproc())
      continue;
    if (e->sinkEdge())
      continue;
    if (e->type() == ParseAPI::RET || e->type() == ParseAPI::CATCH)
      continue;
    GetReachableBlocks(e->trg(), visited);
  }
}

void CoalesceEdgeInstrumentation(PatchFunction* f,
                                 std::set<PatchEdge*>& redirect,
                                 std::set<PatchBlock*>& instBlocks) {

  std::set<PatchBlock*> reachableBlocks;
  GetReachableBlocks(f->entry(), reachableBlocks);
  for (auto b : reachableBlocks) {
    if (!b->isClone())
      continue;
    int instEdges = 0;
    int notInstEdges = 0;
    bool hasCatchEdge = false;
    for (auto e : b->sources()) {
      if (redirect.find(e) != redirect.end()) {
        instEdges += 1;
        continue;
      }
      if (e->type() == ParseAPI::CATCH) {
        hasCatchEdge = true;
        continue;
      }
      if (reachableBlocks.find(e->src()) != reachableBlocks.end()) {
        notInstEdges += 1;
      }
    }
    if (!hasCatchEdge && instEdges > 0 && notInstEdges == 0) {
      instBlocks.insert(b);
      for (auto e : b->sources())
        redirect.erase(e);
    }
  }
}

bool DoInstrumentationLowering(BPatch_function* function, FuncSummary* summary,
                               const litecfi::Parser& parser,
                               PatchMgr::Ptr patcher) {
  if (FLAGS_disable_lowering) return false;
  if (isLibc) return false;
  if (!summary || !summary->lowerInstrumentation()) {
    return false;
  }
  if (summary->has_indirect_cf) return false;
  PatchFunction* f = PatchAPI::convert(function);
  std::set<PatchEdge*> visited;
  std::set<PatchEdge*> redirect;
  RedirectTransitionEdges(f->entry(), summary, redirect, visited);

  for (auto e : redirect)
    if (summary->blockEndSPHeight.find(e->src()->start()) ==
        summary->blockEndSPHeight.end())
      return false;

  bool useRegisterFrame = summary->shouldUseRegisterFrame();
  if (FLAGS_disable_reg_frame) useRegisterFrame = false;
  if (!useRegisterFrame && summary->redZoneAccess.size() > 0) {
    for (auto e : redirect) {
      MoveInstData* mid =
          summary->getMoveInstDataFixedAtEntry(e->trg()->start());
      if (mid == nullptr)
        return false;
      if (mid->saveCount < 2)
        return false;
    }
  }

  assert(parser.parser->markPatchFunctionEntryInstrumented(f));

  std::map<PatchBlock*, PatchBlock*> cloneBlockMap;
  CloneFunctionCFG(f, patcher, cloneBlockMap);
  for (auto e : redirect) {
    assert(PatchModifier::redirect(e, cloneBlockMap[e->trg()]));
  }

  std::set<PatchBlock*> instBlocks;
  CoalesceEdgeInstrumentation(f, redirect, instBlocks);

  // Insert stack push operations at edges
  for (auto e : redirect) {
    // We get the stack height at block exit
    assert(summary->blockEndSPHeight.find(e->src()->start()) !=
           summary->blockEndSPHeight.end());
    int height = summary->blockEndSPHeight[e->src()->start()];
    MoveInstData* mid = summary->getMoveInstDataFixedAtEntry(e->trg()->start());
    if (FLAGS_disable_reg_save_opt) mid = nullptr;
    Snippet::Ptr stack_push;
    if (useRegisterFrame) {
      stack_push =
          RegisterPushSnippet::create(new RegisterPushSnippet(summary, height));
    } else if (mid == nullptr) {
      lowering_dead_reg_site += 1;
      lowering_no_dead_reg_entry_site += 1;
      stack_push = StackPushSnippet::create(
          new StackPushSnippet(summary, false, height));
    } else {
      lowering_dead_reg_site += 1;
      stack_push = StackPushSnippet::create(
          new StackPushSnippet(summary, false, height, true));
    }

    Point* p = patcher->findPoint(PatchAPI::Location::EdgeInstance(f, e),
                                  Point::EdgeDuring);
    assert(p);
    p->pushBack(stack_push);
  }

  // Insert stack push operations at blocks whose
  // incoming edge instrumentations are coalesced
  for (auto b : instBlocks) {
    assert(summary->blockEntrySPHeight.find(b->start()) !=
           summary->blockEntrySPHeight.end());
    int height = summary->blockEntrySPHeight[b->start()];
    MoveInstData* mid = summary->getMoveInstDataAtEntry(b->start());
    if (FLAGS_disable_reg_save_opt) mid = nullptr;
    Snippet::Ptr stack_push;
    Point* p = patcher->findPoint(PatchAPI::Location::BlockInstance(f, b),
                                  Point::BlockEntry);
    if (useRegisterFrame) {
      stack_push =
          RegisterPushSnippet::create(new RegisterPushSnippet(summary, height));
    } else if (mid == nullptr) {
      lowering_dead_reg_site += 1;
      lowering_no_dead_reg_entry_site += 1;
      stack_push = StackPushSnippet::create(
          new StackPushSnippet(summary, false, height));
    } else {
      lowering_dead_reg_site += 1;
      p = patcher->findPoint(
          PatchAPI::Location::InstructionInstance(f, b, mid->newInstAddress),
          Point::PreInsn);
      assert(p);
      stack_push =
          StackPushSnippet::create(new StackPushSnippet(summary, true, height));
    }
    assert(p);
    p->pushBack(stack_push);
  }

  // Insert stack pop operations
  for (auto b : f->exitBlocks()) {
    PatchBlock* cloneB = cloneBlockMap[b];
    Point* p = patcher->findPoint(PatchAPI::Location::BlockInstance(f, cloneB),
                                  Point::BlockExit);
    assert(p);
    if (IsNonreturningCall(p))
      continue;

    MoveInstData* mid = summary->getMoveInstDataAtExit(b->start());
    if (FLAGS_disable_reg_save_opt) mid = nullptr;

    Snippet::Ptr stack_pop;
    if (useRegisterFrame) {
      stack_pop = RegisterPopSnippet::create(new RegisterPopSnippet(summary));
    } else if (mid == nullptr) {
      lowering_dead_reg_site += 1;
      lowering_no_dead_reg_exit_site += 1;
      stack_pop = StackPopSnippet::create(new StackPopSnippet(summary, false));
    } else {
      lowering_dead_reg_site += 1;
      p = patcher->findPoint(PatchAPI::Location::InstructionInstance(
                                 f, cloneB, mid->newInstAddress),
                             Point::PreInsn);
      assert(p);
      stack_pop = StackPopSnippet::create(new StackPopSnippet(summary, true));
    }
    p->pushBack(stack_pop);
    assert(parser.parser->markPatchBlockInstrumented(cloneB));
  }

  f->setContainsClonedBlocks(true);
  return true;
}

void AddInlineHint(BPatch_function* function, const litecfi::Parser& parser, FuncSummary * summary) {
  if (FLAGS_disable_inline) return;
  // Do no attempt to inline functions that we do not need to instrument
  if (summary != nullptr && summary->safe) return;
  Address entry = (Address)(function->getBaseAddr());
  parser.parser->addInliningEntry(entry);
}

bool Skippable(BPatch_function* function, FuncSummary* summary) {
  if (FLAGS_disable_sfe) return false;
  if (summary != nullptr && summary->safe) {
    StdOut(Color::RED, FLAGS_vv)
        << "      Skipping function : " << function->getName() << Endl;
    return true;
  }

  Address addr = (Address)(function->getBaseAddr());

  if (skip_addrs.find(addr) != skip_addrs.end()) {
    StdOut(Color::RED, FLAGS_vv)
        << "      Skipping function (user's skip list): " << function->getName()
        << Endl;

    return true;
  }
  return false;
}

bool MoveInstrumentation(BPatch_point*& p, FuncSummary* s) {
  if (FLAGS_disable_reg_save_opt) return false;
  if (s == nullptr) return false;
  ++total_dead_reg_site;
  if (p->getPointType() == BPatch_locEntry) {
    BPatch_function* f = p->getFunction();
    BPatch_flowGraph* cfg = f->getCFG();

    // Should have only one function entry block.
    std::vector<BPatch_basicBlock*> eb;
    cfg->getEntryBasicBlock(eb);
    if (eb.size() != 1) {
      ++no_dead_reg_site;
      return false;
    }

    // If the function entry block has intra-procedural
    // incoming edges, then we have to instrument at function entry.
    // Otherwise, the stack push op will be executed in a loop
    BPatch_basicBlock* func_entry = eb[0];
    std::vector<BPatch_edge*> edges;
    func_entry->getIncomingEdges(edges);
    if (edges.size() > 0) {
      ++no_dead_reg_site;
      return false;
    }
    MoveInstData* mid =
        s->getMoveInstDataAtEntry(func_entry->getStartAddress());
    if (mid == nullptr) {
      ++no_dead_reg_site;
      return false;
    }
    p = func_entry->findPoint(mid->newInstAddress);
  } else if (p->getPointType() == BPatch_locBasicBlockEntry) {
    BPatch_basicBlock* b = p->getBlock();
    MoveInstData* mid = s->getMoveInstDataAtEntry(b->getStartAddress());
    if (mid == nullptr) {
      ++no_dead_reg_site;
      return false;
    }
    p = b->findPoint(mid->newInstAddress);
  } else if (p->getPointType() == BPatch_locExit ||
             p->getPointType() == BPatch_locBasicBlockExit) {
    BPatch_basicBlock* b = p->getBlock();
    MoveInstData* mid = s->getMoveInstDataAtExit(b->getStartAddress());
    if (mid == nullptr) {
      ++no_dead_reg_site;
      return false;
    }
    p = b->findPoint(mid->newInstAddress);
  } else {
    // Cannot move instrumentation if instrumenting at an edge
    ++no_dead_reg_site;
    return false;
  }
  return true;
}

void MarkExceptionSafeCalls(BPatch_function* function) {
  PatchFunction* f = PatchAPI::convert(function);
  for (auto b : f->blocks()) {
    Address callee = 0;
    for (auto e : b->targets()) {
      if (e->type() == ParseAPI::CALL && !e->sinkEdge()) {
        callee = e->trg()->start();
        break;
      }
      if (e->interproc() && (e->type() == ParseAPI::DIRECT ||
                             e->type() == ParseAPI::COND_TAKEN)) {
        callee = e->trg()->start();
        break;
      }
    }
    if (exception_free_func.find(callee) != exception_free_func.end()) {
      b->markExceptionSafe();
    }
  }
}

void CountMemoryWrites(FuncSummary* s) {
  if (s == nullptr) return;
  for (auto const &it : s->all_writes) {
    auto const& w = it.second;
    memory_writes += 1;
    int type_count = 0;
    type_count += (int)(w->stack);
    type_count += (int)(w->global);
    type_count += (int)(w->heap);
    type_count += (int)(w->arg);
    type_count += (int)(w->heap_or_arg);
    if (type_count >= 2) {
        fprintf(stderr, "write at %lx classified more than one type\n", w->addr);
        fprintf(stderr, "\tstack: %d, global: %d, heap: %d, arg: %d, heap_or_arg: %d\n", w->stack, w->global, w->heap, w->arg, w->heap_or_arg);
        assert(0);
    }
    if (w->stack) stack_writes += 1;
    if (w->global) global_writes += 1;
    if (w->heap) {
        heap_writes += 1;
    }
    if (w->arg) arg_writes += 1;
    if (w->heap_or_arg) heap_or_arg_writes += 1;
  }
}

bool InstrumentFunction(BPatch_function* function,
                        const litecfi::Parser& parser, PatchMgr::Ptr patcher,
                        const std::map<uint64_t, FuncSummary*>& analyses,
                        InstrumentationResult* res) {
  total_func++;
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
    if (summary != nullptr) {
      if (summary->has_unknown_cf || !summary->plt_calls.empty())
        func_with_indirect_or_plt_call++;
      if (summary->has_unknown_cf)
        func_with_indirect_call++;
      if (!summary->plt_calls.empty())
        func_with_plt_call++;
    }

    CountMemoryWrites(summary);

    // Check if this function is safe to skip and do so if it is.
    if (Skippable(function, summary)) {
      res->safe_fns.push_back(fn_name);
      return false;
    }

    MarkExceptionSafeCalls(function);

    // Add inlining hint so that writeFile may inline small leaf functions.
    AddInlineHint(function, parser, summary);

    // If possible check and lower the instrumentation to within non frequently
    // executed unsafe control flow paths.
    if (DoInstrumentationLowering(function, summary, parser, patcher)) {
      res->lowered_fns.push_back(fn_name);
      StdOut(Color::RED, FLAGS_vv)
          << "      Optimized instrumentation lowering for function at 0x"
          << std::hex << (uint64_t)function->getBaseAddr() << Endl;

      return true;
    }

    // For leaf functions we may be able to carry out stack operations using
    // unused registers.
    if (DoStackOpsUsingRegisters(function, summary, parser, patcher)) {
      res->reg_stack_fns.push_back(fn_name);
      return true;
    }

    // Apply fast path optimization if applicable.
    BPatch_basicBlock* condNotTakenEntry = NULL;
    vector<BPatch_basicBlock*> condNotTakenExits;
    if (summary != nullptr && CheckFastPathFunction(condNotTakenEntry, condNotTakenExits, function)) {
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
      BPatch_point* push_point = condNotTakenEntry->findEntryPoint();
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
      return true;
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
        if (IsNonreturningCall(PatchAPI::convert(p, BPatch_callAfter)))
          continue;
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
      return true;
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
  return true;
}

void InstrumentFunctionMemoryWrite(BPatch_function* function,
                        const litecfi::Parser& parser, PatchMgr::Ptr patcher,
                        const std::map<uint64_t, FuncSummary*>& analyses,
                        InstrumentationResult* res) {
  if (FLAGS_sfi == "none") return;
  std::string fn_name = Dyninst::PatchAPI::convert(function)->name();
  StdOut(Color::YELLOW, FLAGS_vv) << "     SFI : " << fn_name << Endl;

  //BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
  //BPatch_nullExpr nopSnippet;
  FuncSummary* summary = nullptr;
  if (FLAGS_shadow_stack == "light") {
    auto it = analyses.find(static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(function->getBaseAddr())));
    if (it != analyses.end())
      summary = (*it).second;
  }

  BPatch_Set<BPatch_opCode> axs;
  axs.insert(BPatch_opStore);

  vector<BPatch_point*>* points = function->findPoint(axs);
  for (auto point : *points) {
      PatchAPI::Point* p = PatchAPI::convert(point, BPatch_callBefore);
      entryID e = p->insn().getOperation().getID();
      // Do not perform SFI on call or push instructions
      if (e == e_call || e == e_push) continue;
      if (summary != nullptr) {
        // When we do static analysis, only instrument unknown memory write
        if (summary->stack_heights.find(p->addr()) != summary->stack_heights.end()) continue;
      }
      Snippet::Ptr sfi = SFISnippet::create(new SFISnippet(summary));
      p->pushBack(sfi);
  }
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

void InstrumentModule(BPatch_module* module, const litecfi::Parser& parser,
                      PatchMgr::Ptr patcher,
                      const std::map<uint64_t, FuncSummary*>& analyses,
                      InstrumentationResult* res) {
  std::vector<BPatch_function*>*  functions = module->getProcedures(true);

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
    if (init_funcs.find(f->addr()) != init_funcs.end())
      continue;
    if (!function->isInstrumentable()) {
      StdOut(Color::YELLOW, FLAGS_vv)
        << "Uninstrumentable funciton at 0x"
        << std::hex << (uint64_t)function->getBaseAddr() << Endl;
      uninstrumentable += 1;
      continue;
    }
    bool needSFI = InstrumentFunction(function, parser, patcher, analyses, res);
    if (needSFI)
        InstrumentFunctionMemoryWrite(function, parser, patcher, analyses, res);
  }
}

void IdentifyInitFunctions(Dyninst::SymtabAPI::Symtab* sym) {
  init_funcs.clear();
  Dyninst::SymtabAPI::Region *reg = nullptr;
  sym->findRegion(reg, ".init_array");
  if (reg == nullptr) return;
  Address* ptrs = (Address*)(reg->getPtrToRawData());
  for (size_t i = 0; i < reg->getMemSize() / sizeof(void*); ++i)
    init_funcs.insert(ptrs[i]);
  reg = nullptr;
}

void SetupInitCode(BPatch_object* object, const litecfi::Parser& parser, PatchMgr::Ptr patcher) {
  /* 1. Increase .bss section size  */
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
  std::vector<BPatch_function *> funcs;
  static std::string loader = "/lib64/ld-linux-x86-64.so.2";  
  static std::string exec = "";
  Address shadow_region_start;
  if (FLAGS_libs) {
    shadow_region_start = binary_edit->allocateStaticMemoryRegion(8 * 1024 * 1024, loader);
    object->findFunction("_dl_start", funcs);
  } else {
    shadow_region_start = binary_edit->allocateStaticMemoryRegion(8 * 1024 * 1024, exec);
    object->findFunction("_start", funcs);
  }


  /* 3. Find _start in either the executable when not instrumenting shared library
   *    or in the ld.so when instrumenting shared libraries                         */
  assert(funcs.size() == 1);

  /* 4. Find entry point and insert the init snippet */
  BPatch_function* function = funcs[0];
  vector<BPatch_point*> points;
  function->getEntryPoints(points);

  assert(points.size() == 1);
  PatchAPI::Point* p = PatchAPI::convert(points[0], BPatch_callBefore);
  Snippet::Ptr init = InitSnippet::create(new InitSnippet(shadow_region_start));
  p->pushBack(init);

  BPatch_nullExpr nopSnippet;
  binary_edit->insertSnippet(nopSnippet, points, BPatch_callBefore,
                             BPatch_lastSnippet, &is_empty);

}

bool IsProgramEntry(BPatch_object *object) {
  std::string name = std::string(object->pathName());
  if (FLAGS_libs)
    return name.find("ld-linux-x86-64.so") != std::string::npos;
  else
    return !IsSharedLibrary(object);
}

void InstrumentCodeObject(BPatch_object* object, const litecfi::Parser& parser,
                          PatchMgr::Ptr patcher, InstrumentationResult* res) {
  if (!IsSharedLibrary(object)) {
    StdOut(Color::GREEN, FLAGS_vv) << "\n  >> Instrumenting main application "
                                   << object->pathName() << Endl;
  } else {
    StdOut(Color::GREEN, FLAGS_vv)
        << "\n    Instrumenting " << object->pathName() << Endl;
    IdentifyInitFunctions(Dyninst::SymtabAPI::convert(object));
  }

  isLibc = (object->pathName().find("libc.so") != std::string::npos);

  if (IsProgramEntry(object))
    SetupInitCode(object, parser, patcher);

  std::vector<BPatch_module*> modules;
  object->modules(modules);

  std::map<uint64_t, FuncSummary*> analyses;
  // Do the static analysis on this code and obtain skippable functions.
  if (FLAGS_shadow_stack == "light") {
    CodeObject* co = Dyninst::ParseAPI::convert(object);
    co->parse();
    co->adjustJumpTableRange();

    PassManager* pm = new PassManager;
    pm->AddPass(new CallGraphAnalysis())
        ->AddPass(new LargeFunctionFilter())
        ->AddPass(new StackHeightAnalysis())
        ->AddPass(new CFGAnalysis())
//        ->AddPass(new HeapWriteAnalysis())
        ->AddPass(new InterProceduralMemoryAnalysis())
        ->AddPass(new FunctionExceptionAnalysis())
        ->AddPass(new UnsafeCallBlockAnalysis())
        ->AddPass(new SafePathsCounting())
        ->AddPass(new DeadRegisterAnalysis())
        ->AddPass(new UnusedRegisterAnalysis())
        ->AddPass(new BlockDeadRegisterAnalysis());
    std::set<FuncSummary*> summaries = pm->Run(co);

    for (auto f : summaries) {
      analyses[f->func->addr()] = f;
    }
  }

  if (FLAGS_threat_model == "trust_system" && IsSystemCode(object)) {
    return;
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

  StdOut(Color::RED) << "Safe functions : " << std::dec << res->safe_fns.size() << "(" << res->safe_fns.size() * 100.0 / total_func << "%)"
                     << "\n";
  StdOut(Color::RED) << "Uninstrumentable functions : " << std::dec << uninstrumentable << "\n";
  /*
  for (auto it : res->safe_fns) {
    StdOut(Color::BLUE) << it << " ";
  }
  */
  StdOut(Color::BLUE) << Endl << Endl;

  StdOut(Color::RED) << "Register stack functions : "
                     << res->reg_stack_fns.size() << "(" << res->reg_stack_fns.size() * 100.0 / total_func << "%)"<< "\n  ";
  /*
  for (auto it : res->reg_stack_fns) {
    StdOut(Color::BLUE) << it << " ";
  }
  */
  StdOut(Color::BLUE) << Endl << Endl;
  StdOut(Color::RED) << "Lowering stack functions : " << res->lowered_fns.size() << "(" << res->lowered_fns.size() * 100.0 / total_func << "%)"
                     << "\n  ";
  /*
  for (auto it : res->lowered_fns) {
    StdOut(Color::BLUE) << it << " ";
  }
  */
  StdOut(Color::BLUE) << Endl;
  StdOut(Color::RED) << "Functions with indirect call or plt calls : "
                     << func_with_indirect_or_plt_call
                     << Endl;
  StdOut(Color::RED) << "Functions with indirect call: "
                     << func_with_indirect_call 
                     << Endl;
  StdOut(Color::RED) << "Functions with plt calls : " << func_with_plt_call
                     << Endl;
  StdOut(Color::RED) << "Total functions : " << total_func << Endl;

  StdOut(Color::RED) << "Total memory writes : " << memory_writes << Endl;
  StdOut(Color::RED) << "\tStack writes : " << stack_writes << "(" << stack_writes * 100.0 / memory_writes << "%)" << Endl;
  StdOut(Color::RED) << "\tGlobal writes : " << global_writes <<  "(" << global_writes * 100.0 / memory_writes << "%)" << Endl;
  StdOut(Color::RED) << "\tHeap writes : " << heap_writes << "(" << heap_writes * 100.0 / memory_writes << "%)" << Endl;
  StdOut(Color::RED) << "\tArg writes : " << arg_writes << "(" << arg_writes * 100.0 / memory_writes << "%)" <<  Endl;
  StdOut(Color::RED) << "\tHeap_or_arg writes : " << heap_or_arg_writes << "(" << heap_or_arg_writes * 100.0 / memory_writes << "%)" <<  Endl;

  int unknown = memory_writes;
  unknown -= stack_writes;
  unknown -= global_writes;
  unknown -= heap_writes;
  unknown -= arg_writes;
  unknown -= heap_or_arg_writes;
  StdOut(Color::RED) << "\tUnknown writes : " << unknown << "(" << unknown * 100.0 / memory_writes << "%)" <<  Endl;
  StdOut(Color::RED) << "Dead register optimization : " << no_dead_reg_site << "/" << total_dead_reg_site << Endl;
  StdOut(Color::RED) << "Lowering dead register optimization : " << lowering_no_dead_reg_entry_site << "/" << lowering_no_dead_reg_exit_site << "/" << lowering_dead_reg_site << Endl;

}
