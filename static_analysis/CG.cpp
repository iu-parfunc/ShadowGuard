
#include <stdio.h>
#include <map>
#include <list>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <ctime>
#include <stdlib.h>

#include "CodeObject.h"
#include "CFG.h"

#include "Instruction.h"
#include "InstructionDecoder.h"
#include "slicing.h"
#include "SymEval.h"
#include "DynAST.h"
using namespace std;
using namespace Dyninst;
using namespace ParseAPI;
using namespace InstructionAPI;
using namespace DataflowAPI;

class CGEdge;

enum Color {
  WHITE,
  GRAY,
  BLACK
};

class CGFunction {
  public:

    CGFunction() {
      state = WHITE;
    }

    bool operator <(const CGFunction& rhs) const{
      return addr < rhs.addr;
    }

    Color state;
    int depth;
    std::string name;
    Address addr;
    std::list<CGFunction*> children;

    // More detailed meta data. May only be calculated on demand
    std::list<CGEdge> callEdges;
    std::list<CGFunction*> parents;
};

class CGEdge {
  public:

    CGEdge(const CGEdge &e){
      is_indirect = e.is_indirect;
      src         = e.src;
      target      = e.target;
    }

    CGEdge() {}

    bool is_indirect;
    CGFunction* src;
    CGFunction* target;
};

class CG {
  public:

    CGFunction* main;
    std::list<CGFunction*> roots;
    std::map<Address, CGFunction*> node_by_addr;
    std::map<std::string, CGFunction*> node_by_name;
    std::map<CGFunction*, bool> nodes;

};

class SCC {
  public:
    std::list<CGFunction*> nodes;
};



// std::map<std::string, CGFunction*> fns;
// std::map<Address, CGFunction*> cgf_by_addr;

class ConstVisitor: public ASTVisitor {
  public:
    bool resolved;
    Address target;
    ConstVisitor() : resolved(true), target(0){}
    virtual AST::Ptr visit(DataflowAPI::ConstantAST * ast) {
      target = ast->val().val;
      return AST::Ptr();
    };

    // If the PC expression contains a variable 
    // or an operation, then the control flow target cannot
    // be resolved through constant propagation
    virtual AST::Ptr visit(DataflowAPI::VariableAST *) {
      resolved = false;
      return AST::Ptr();
    };
    virtual AST::Ptr visit(DataflowAPI::RoseAST * ast) {
      resolved = false;

      // Recursively visit all children
      unsigned totalChildren = ast->numChildren();
      for (unsigned i = 0 ; i < totalChildren; ++i) {
	ast->child(i)->accept(this);
      }
      return AST::Ptr();
    };

};


class ConstantPred : public Slicer::Predicates {

  public:
    virtual bool endAtPoint(Assignment::Ptr ap) {
      return ap->insn()->writesMemory();
    }
    virtual bool addPredecessor(AbsRegion reg) {
      if (reg.absloc().type() == Absloc::Register) {
	MachRegister r = reg.absloc().reg();
	return !r.isPC();
      } 
      return true;
    }
};

// Assume block b in function f contains unknown control flow.
// This function tries to use constant propagation 
// to resolve the unknown control flow
// and returns the jump target if it succeeds.
Address AnalyzeControlFlowTarget(Function *f, Block *b) {

  // Decode the last instruction in this block
  // as this is the instruction that leads to unknown control flow 
  const unsigned char * buf = 
    (const unsigned char*) b->obj()->cs()->getPtrToInstruction(b->last());
  InstructionDecoder dec(buf, 
      InstructionDecoder::maxInstructionLength, 
      b->obj()->cs()->getArch());
  Instruction::Ptr insn = dec.decode();

  // Convert the instruction to SSA-like assignments
  AssignmentConverter ac(true, false);
  vector<Assignment::Ptr> assignments;
  ac.convert(insn, b->last(), f, b, assignments);

  Assignment::Ptr pcAssign;
  for (auto ait = assignments.begin(); ait != assignments.end(); ++ait) {
    const AbsRegion &out = (*ait)->out();
    if (out.absloc().type() == Absloc::Register && out.absloc().reg().isPC()) {
      pcAssign = *ait;
      break;
    }
  }

  if (!pcAssign) {
    // printf("Do not find an assignment that changes PC\n");
    return 0;
  } else {
    // printf("PC assignment %s\n", pcAssign->format().c_str());
  }
  Slicer s(pcAssign, b, f);
  ConstantPred mp;
  GraphPtr slice = s.backwardSlice(mp);

  // printf("There are %d nodes in slice\n", slice->size());
  // We expand the slice to get an AST of the PC
  Result_t symRet;
  SymEval::expand(slice, symRet);
  /*
     for (auto rit = symRet.begin(); rit != symRet.end(); ++rit) {
     printf("Assign %s expands to AST %s\n", rit->first->format().c_str(), rit->second->format().c_str());
     }
   */
  AST::Ptr pcExp = symRet[pcAssign];

  // We analyze the AST to see if it can actually be resolved by constant propagation
  ConstVisitor cv;
  if (!pcExp) {
    // printf("Cannot get PC expression for block [%lx, %lx) %s\n", b->start(), b->end(), insn->format().c_str());
    return 0;
  } else {
    // printf("[%lx, %lx), instruction %s, pc expression %s\n", b->start(), b->end(), insn->format().c_str(), pcExp->format().c_str());
  }
  pcExp->accept(&cv);

  // If the unknown control flow is resolved,
  // return the control flow target.
  // Otherwise, return 0
  if (cv.resolved)
    return cv.target;
  else
    return 0;
}

void resetGraph(CGFunction* src) {
  if (src == nullptr) {
    return;
  }

  for (auto it = src->callEdges.begin(); it != src->callEdges.end(); it++) {
    if (it->target->state != WHITE) {
      it->target->state = WHITE;
      resetGraph(it->target);
    }
  }
}

void setGraphRoots(CG* cg) {
  for (auto it : cg->nodes) {
    if (it.first->parents.size() == 0) {
      // printf("Root name : %s\n", it.first->name.c_str());
      // printf("Root children count : %d\n", it.first->children.size());
      // doDFS(it.first, 0);
      // printf("Root max depth : %d\n\n", maxDepth);
      // maxDepth = 0;
      cg->roots.push_back(it.first);

      if (!it.first->name.compare("main")) {
	cg->main = it.first;
      }
    }
  }
}

// Returns a cloned complement of a graph
CG* complementGraph(CG* cg, std::map<std::string, CGFunction*>& mapping) {

  CG* newcg = new CG();
  for (auto it = cg->nodes.begin(); it != cg->nodes.end(); it++) {
    CGFunction* f = (*it).first;

    CGFunction* newfn = nullptr;
    auto it2 = mapping.find(f->name);
    if (it2 != mapping.end()) {
      newfn = (*it2).second;
    } else {
      newfn = new CGFunction();
      newfn->name = f->name;
      newfn->addr = f->addr;

      mapping.insert(std::pair<std::string, CGFunction*>(f->name, newfn));
      newcg->nodes.insert(std::pair<CGFunction*, bool>(newfn, true));
      newcg->node_by_name.insert(std::pair<std::string, CGFunction*>(newfn->name, 
        newfn));
      newcg->node_by_addr.insert(std::pair<Address, CGFunction*>(newfn->addr, 
        newfn));

    }

    for (auto child : f->children) {
      CGFunction* newchld = nullptr;
      auto it3 = mapping.find(child->name);
      if (it3 != mapping.end()) {
	newchld = (*it3).second;
      } else {
	newchld = new CGFunction();
	newchld->name = child->name;
	newchld->addr = child->addr;

        mapping.insert(std::pair<std::string, CGFunction*>(f->name, newchld));
        newcg->nodes.insert(std::pair<CGFunction*, bool>(newchld, true));
	newcg->node_by_name.insert(std::pair<std::string, CGFunction*>(newchld->name, 
	  newchld));
	newcg->node_by_addr.insert(std::pair<Address, CGFunction*>(newchld->addr, 
	  newchld));
      }

      CGEdge cge;
      cge.src = newchld;
      cge.target = newfn;

      newchld->children.push_back(newfn);
      newfn->parents.push_back(newchld);
      newchld->callEdges.push_back(cge);
    }
  }

  setGraphRoots(newcg);

  return newcg;
}

void topologicalSort(CGFunction* src, std::list<CGFunction*>& ls) {
  if (src == nullptr) {
    return;
  }

  if (src->state == WHITE) {
    src->state = GRAY;

    for (auto it = src->callEdges.begin(); it != src->callEdges.end(); it++) {
      if (it->target->state == WHITE) {
	topologicalSort(it->target, ls);
      }
    }

    src->state = BLACK;
    ls.push_front(src);
  }
}

bool populateSCC(CGFunction* src, SCC &scc) {
  if (src == nullptr) {
    return false;
  }

  if (src->state == WHITE) {
    src->state = GRAY;
    scc.nodes.push_back(src);

    for (auto it = src->callEdges.begin(); it != src->callEdges.end(); it++) {
      if (it->target->state == WHITE) {
	populateSCC(it->target, scc);
      }
    }
    src->state = BLACK;
    return true;
  }

  return false;
}

std::list<SCC> doSCC(CG* cg) {
  std::list<SCC> sccs;
  if (cg->main == nullptr) {
    return sccs;
  }

  std::list<CGFunction*> sorted;
  topologicalSort(cg->main, sorted);

  std::map<std::string, CGFunction*> mappings;
  complementGraph(cg, mappings);

  for (auto f : sorted) {
    auto it = mappings.find(f->name);
    if (it != mappings.end()) {
      SCC scc;
      bool modified = populateSCC((*it).second, scc); 
      if (modified) {
	sccs.push_back(scc);
      }
    } else {
      // This is probably non main root graph. Skip
      continue;
    }
  }

  return sccs;
}

std::queue<CGFunction*> q;



/*
   void doBFS(CGFunction* src) {
   if (!src->visited) {
   src->visited = true;
   }

   q.push(src);

   while(!q.empty()) {
   CGFunction* f = q.front();
   printf("%s ", f->name.c_str()); 
   q.pop();
   for (auto it = f->callEdges.begin(); it != f->callEdges.end(); it++) {
   if (!(it->target->visited)) {
   it->target->visited = true;
   q.push(it->target);
   }
   }
   }
   }
 */

int maxDepth = 0;

void doDFS(CGFunction* n, int depth) {
  if (n->state == WHITE) {
    n->state = GRAY;
    n->depth = depth;

    if (maxDepth < depth) {
      maxDepth = depth;
      // printf("Current max depth at %s is %d\n", n->name.c_str(), maxDepth);
    }

    depth++;

    for (auto it = n->callEdges.begin(); it != n->callEdges.end(); it++) {
      if (it->target->state == WHITE) {
	doDFS(it->target, depth);
      }
    }

    n->state = BLACK;
  }
}

CG* getCallGraph(char* binary) {
  SymtabCodeSource *sts;
  CodeObject *co;

  // Create a new binary code object from the filename argument
  sts = new SymtabCodeSource(binary);
  co = new CodeObject( sts );
  co->parse();

  std::map<Address, Function*> fn_by_addr;
  for (auto fit = co->funcs().begin(); fit != co->funcs().end(); ++fit) {
    Function *f = *fit;
    fn_by_addr.insert(std::pair<Address, Function*>(f->addr(), f));
  }

  CG* cg = new CG();
  for (auto fit = co->funcs().begin(); fit != co->funcs().end(); ++fit) {
    Function *f = *fit;
    // if (co->cs()->linkage().find(f->addr()) != co->cs()->linkage().end()) continue;
    
    // printf("%s\n", f->name().c_str());

    CGFunction* src; 
    auto it = cg->node_by_addr.find(f->addr());
    if (it != cg->node_by_addr.end()) {
      src = it->second;
    } else {
      src = new CGFunction();
      src->name = f->name();
      src->addr = f->addr();
      cg->node_by_addr.insert(std::pair<Address, CGFunction*>(f->addr(), src));
      cg->node_by_name.insert(std::pair<std::string, CGFunction*>(f->name(), src));
      cg->nodes.insert(std::pair<CGFunction*, bool>(src, true));
    }

    // Capture direct calls and direct tail calls
    const Function::edgelist & calls = f->callEdges();
    for (Function::edgelist::const_iterator cit = calls.begin(); cit != calls.end(); cit++) {
      if (!(*cit)->sinkEdge()) {
	Block* calledB = (*cit)->trg();
	Function* calledF = calledB->obj()->findFuncByEntry(calledB->region(), calledB->start());

	CGEdge cge;
	cge.src = src;
	cge.is_indirect = false;

	CGFunction* target;
	auto it = cg->node_by_addr.find(calledF->addr());
	if (it != cg->node_by_addr.end()) {
	  target = it->second;
	} else {
	  target = new CGFunction();
	  target->name = calledF->name();
	  target->addr = calledF->addr();
	  cg->node_by_addr.insert(std::pair<Address, CGFunction*>(calledF->addr(), target));
	  cg->node_by_name.insert(std::pair<std::string, CGFunction*>(calledF->name(), target));
	  cg->nodes.insert(std::pair<CGFunction*, bool>(target, true));
	}
	cge.target = target; 
	target->parents.push_back(src);

	src->callEdges.push_back(cge);
	src->children.push_back(target);
      }
    }

    // Try to resolve indirect calls and indirect tail calls
    for (auto bit = f->blocks().begin(); bit != f->blocks().end(); ++bit) {
      Block *b = *bit;

      bool unresolved = false;
      for (auto eit = b->targets().begin(); eit != b->targets().end(); ++eit) {
	ParseAPI::Edge *e = *eit;
	if (e->sinkEdge() && e->type() == CALL) unresolved = true;
	if (e->sinkEdge() && e->type() == INDIRECT && e->interproc()) unresolved = true;
      }

      if (unresolved) {
	Address target = AnalyzeControlFlowTarget(f, b); 
	if (target > 0) {
	  // printf("Unknown control flow resolved in block [%lx, %lx), transfer to %lx\n", b->start(), b->end(), target);
	  auto it = fn_by_addr.find(target);
	  if (it != fn_by_addr.end()) {
	    Function* fn = it->second;

	    CGEdge cge;
	    cge.src = src;
	    cge.is_indirect = true; 

	    CGFunction* trg; 
	    auto it = cg->node_by_addr.find(fn->addr());
	    if (it != cg->node_by_addr.end()) {
	      trg = it->second;
	    } else {
	      trg = new CGFunction();
	      trg->name = fn->name();
	      trg->addr = fn->addr();
	      cg->node_by_addr.insert(std::pair<Address, CGFunction*>(fn->addr(), trg));
	      cg->node_by_name.insert(std::pair<std::string, CGFunction*>(f->name(), trg));
	      cg->nodes.insert(std::pair<CGFunction*, bool>(trg, true));
	    }
	    cge.target = trg; 
	    trg->parents.push_back(src);

	    src->callEdges.push_back(cge);
	    src->children.push_back(trg);
	  }
	}    
      }
    }
  }

  setGraphRoots(cg);

  return cg;
}

int main(int argc, char * argv[]) {

  CG* cg = getCallGraph(argv[1]);

  printf("Call graph root : %s\n", cg->main->name.c_str());

  /*
     CGFunction* main = nullptr;
     auto it = cg->node_by_name.find("main");
     if (it != cg->node_by_name.end()) {
     main = it->second;
     }

     if (main != nullptr) {
     printf("Main found..\n");
     }

     std::list<SCC> sccs = doSCC(main);
     for (auto scc : sccs) {
     printf("SCC Node count : %ld\n", scc.nodes.size());
     }
   */

  // doBFS(main);
  // doDFS(main, 0);

  std::list<SCC> sccs = doSCC(cg);
  int n_sccs = 0;
  int cycles = 0; 
  int node_sccs = 0;
  for (SCC scc : sccs) {
    if (scc.nodes.size() > 1) {
      cycles++;
      node_sccs += scc.nodes.size();
    }
    n_sccs++;
  }

  printf("Number of SCCs   : %d\n", sccs.size());
  printf("Number of cycles : %d\n", cycles);
  printf("Number of nodes  : %d\n", cg->nodes.size());
  printf("Number of nodes in cycles : %d\n", node_sccs);

}
