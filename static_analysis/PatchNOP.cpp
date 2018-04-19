// standard C libs
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

// Dependencies
#include <asmjit/asmjit.h>

// STL classes
#include <string>
#include <vector>

// DyninstAPI
#include "BPatch.h"
#include "BPatch_basicBlock.h"
#include "BPatch_flowGraph.h"
#include "BPatch_function.h"
#include "BPatch_point.h"
#include "BPatch_snippet.h"

// PatchAPI
#include "PatchMgr.h"
#include "Point.h"
#include "Snippet.h"

using namespace std;
using namespace Dyninst;
using namespace Dyninst::InstructionAPI;
using namespace Dyninst::PatchAPI;

using namespace asmjit;

#define BUFFER_STRING_LEN 1024

// Error handler that just prints the error and lets AsmJit ignore it.
class PrintErrorHandler : public asmjit::ErrorHandler {
  public:
    // Return `true` to set last error to `err`, return `false` to do nothing.
    bool handleError(asmjit::Error err, const char* message, asmjit::CodeEmitter* origin) override {
      fprintf(stderr, "ERROR: %s\n", message);
      return false;
    }
};

// main Dyninst driver structure
BPatch *bpatch = NULL;

// top-level Dyninst mutatee structures
BPatch_addressSpace *mainApp = NULL;
BPatch_image        *mainImg = NULL;
PatchMgr::Ptr        mainMgr;

// analysis shared library
BPatch_object *libObj  = NULL;

// global parameters
char *binary = NULL;
bool instShared = false;
unsigned long iidx = 0;

// instructions to instrument
const bool FILTER_ENABLED = false;
const bool FILTER_INVERSE = false;  // false = keep ONLY instructions in filter
                                    // true  = keep all EXCEPT named instructions
const unsigned long FILTER_INSNS[] = { };
const unsigned long FILTER_INSNS_SIZE = 0;

BPatch_module* getInitFiniModule() {

    // detects the module that should be instrumented with init/cleanup
    BPatch_Vector<BPatch_module *> *modules = mainImg->getModules();
    BPatch_Vector<BPatch_module *>::iterator m;
    BPatch_Vector<BPatch_function *> initFiniFuncs;
    bool hasInit, hasFini, isShared;
    for (m = modules->begin(); m != modules->end(); m++) {
        hasInit = hasFini = isShared = false;
        if ((*m)->isSharedLib()) {
            isShared = true;
        }
        initFiniFuncs.clear();
        (*m)->findFunction("_init", initFiniFuncs, false);
        if (initFiniFuncs.size() > 0) {
            hasInit = true;
        }
        initFiniFuncs.clear();
        (*m)->findFunction("_fini", initFiniFuncs, false);
        if (initFiniFuncs.size() > 0) {
            hasFini = true;
        }
        if (!isShared && hasInit && hasFini) {
            return *m;
        }
    }
    return NULL;
}

BPatch_function* getMutateeFunction(const char *name) {

    // assumes there is a single function in the mutatee address space
    // with the given name; finds and returns it
    BPatch_Vector<BPatch_function *> funcs;
    mainImg->findFunction(name, funcs, true, true, true);
    assert(funcs.size() == 1);
    return funcs.at(0);
}

Snippet::Ptr buildInstrumentation(void *addr) {

    // null instrumentation (for testing)
    BPatch_snippet *nullExpr = new BPatch_nullExpr();
    return PatchAPI::convert(nullExpr);
}

class StackPopSnippet : public Snippet {
  public:
    virtual bool generate(Point* pt, Buffer& buf) {
      printf("  generating code for stack pop..\n");
      JitRuntime rt;
      PrintErrorHandler eh;

      CodeHolder code;
      code.init(rt.getCodeInfo());
      code.setErrorHandler(&eh);

      X86Assembler a(&code);

      Label nmatch = a.newLabel();
      Label abort  = a.newLabel();
      Label ret    = a.newLabel();

      uint64_t stack_ptr = 0x100000000; 

      a.mov(asmjit::x86::rcx, imm(stack_ptr));
      a.mov(asmjit::x86::rcx, asmjit::x86::qword_ptr(asmjit::x86::rcx));
      a.mov(asmjit::x86::rdx, asmjit::x86::qword_ptr(asmjit::x86::rsp));
      a.bind(nmatch);
      a.cmp(asmjit::x86::qword_ptr(asmjit::x86::rcx), imm(0));
      a.jz(abort);
      a.sub(asmjit::x86::rcx, 8);
      a.cmp(asmjit::x86::rdx, asmjit::x86::ptr(asmjit::x86::rcx, 8));
      a.jnz(nmatch);
      a.mov(asmjit::x86::rdx, imm(stack_ptr));
      a.mov(asmjit::x86::qword_ptr(asmjit::x86::rdx), asmjit::x86::rcx);
      a.jmp(ret);
      a.bind(abort);
      a.hlt();
      a.bind(ret);

      int size = code.getCodeSize();
      char* temp_buf = (char*) malloc(size);

      size = code.relocate(temp_buf);
      buf.copy(temp_buf, size);
    }
};

class StackPushSnippet : public Snippet {
  public:
    virtual bool generate(Point* pt, Buffer& buf) {
      printf("  generating code for stack push..\n");
      JitRuntime rt;
      PrintErrorHandler eh;

      CodeHolder code;
      code.init(rt.getCodeInfo());
      code.setErrorHandler(&eh);

      X86Assembler a(&code);

      Label nmatch = a.newLabel();
      Label abort  = a.newLabel();
      Label ret    = a.newLabel();

      uint64_t stack_ptr = 0x100000000; 

      a.mov(asmjit::x86::rcx, imm(stack_ptr));
      a.mov(asmjit::x86::rdx, asmjit::x86::rcx);
      a.mov(asmjit::x86::rcx, asmjit::x86::qword_ptr(asmjit::x86::rcx));
      a.add(asmjit::x86::rcx, 8);
      a.mov(asmjit::x86::qword_ptr(asmjit::x86::rdx), asmjit::x86::rcx);
      a.mov(asmjit::x86::rdx, asmjit::x86::qword_ptr(asmjit::x86::rsp));
      a.mov(asmjit::x86::qword_ptr(asmjit::x86::rcx), asmjit::x86::rdx);

      int size = code.getCodeSize();
      char* temp_buf = (char*) malloc(size);

      size = code.relocate(temp_buf);
      buf.copy(temp_buf, size);
    }
};


void handleInstruction(void *addr, Instruction::Ptr iptr, PatchBlock *block, PatchFunction *func) {

    bool handle = true;

    // filter unwanted instructions if the filter is enabled
    if (FILTER_ENABLED) {
        handle = (FILTER_INVERSE ? true : false);
        for (unsigned i=0; i<FILTER_INSNS_SIZE; i++) {
            if ((unsigned long)addr == FILTER_INSNS[i]) {
                handle = (FILTER_INVERSE ? false : true);
            }
        }
    }
    if (!handle) {
        return;
    }


    entryID id = iptr->getOperation().getID();

    // print instruction info
    printf("  instruction at %lx: %s\n",
            (unsigned long)addr, iptr->format((Address)addr).c_str());

    if (id == e_ret_near) {
       
      Snippet::Ptr snippet = StackPopSnippet::create(new StackPopSnippet);

      printf("  processing ret instrution..\n");

      // grab instrumentation point
      Point *prePoint  = mainMgr->findPoint(
                          Location::InstructionInstance(func, block, (Address)addr),
                          Point::PreInsn, true);

      // build and insert instrumentation
      prePoint->pushBack(snippet);
    } else if (id == e_call) {
      Snippet::Ptr snippet = StackPushSnippet::create(new StackPushSnippet);

      printf("  processing call instrution..\n");

      // grab instrumentation point
      Point *prePoint  = mainMgr->findPoint(
                          Location::InstructionInstance(func, block, (Address)addr),
                          Point::PreInsn, true);

      // build and insert instrumentation
      prePoint->pushBack(snippet);
    }
}

void handleBasicBlock(BPatch_basicBlock *block, PatchFunction *func)
{
    static size_t MAX_RAW_INSN_SIZE = 16;

    Instruction::Ptr iptr;
    void *addr;
    unsigned char bytes[MAX_RAW_INSN_SIZE];
    size_t nbytes, i;

    // get all floating-point instructions
    PatchBlock::Insns insns;
    PatchAPI::convert(block)->getInsns(insns);

    // handle each point separately
    PatchBlock::Insns::iterator j;
    for (j = insns.begin(); j != insns.end(); j++) {

        // get instruction bytes
        addr = (void*)((*j).first);
        iptr = (*j).second;
        nbytes = iptr->size();
        assert(nbytes <= MAX_RAW_INSN_SIZE);
        for (i=0; i<nbytes; i++) {
            bytes[i] = iptr->rawByte(i);
        }
        bytes[nbytes] = '\0';

        handleInstruction(addr, iptr, PatchAPI::convert(block), func);
    }
}

void handleFunction(BPatch_function *function, const char *name)
{
    //printf("function %s:\n", name);

    // handle all basic blocks
    std::set<BPatch_basicBlock*> blocks;
    std::set<BPatch_basicBlock*>::iterator b;
    
    /*
    BPatch_flowGraph *cfg = function->getCFG();
    cfg->getAllBasicBlocks(blocks);
    for (b = blocks.begin(); b != blocks.end(); b++) {
        handleBasicBlock(*b, PatchAPI::convert(function));
    }
    */

    if (strcmp(name, "f") == 0 || strcmp(name, "g") == 0) {
      std::vector<Point*> pts;
      mainMgr->findPoints(Scope(PatchAPI::convert(function)), 
                        Point::FuncEntry,
                        back_inserter(pts));

      BPatch_snippet *nullExpr = new BPatch_nullExpr();
      Snippet::Ptr snippet =  PatchAPI::convert(nullExpr);

      for(vector<Point*>::iterator iter = pts.begin(); iter != pts.end(); ++iter) {
        Point* pt = *iter;
        pt->pushBack(snippet);
      }
      return;
    }

    std::vector<Point*> pts;
    mainMgr->findPoints(Scope(PatchAPI::convert(function)), 
                        Point::FuncEntry,
                        back_inserter(pts));

    Snippet::Ptr snippet = StackPushSnippet::create(new StackPushSnippet);

    printf("  processing function entry for %s..\n", function->getName().c_str());

    for(vector<Point*>::iterator iter = pts.begin(); iter != pts.end(); ++iter) {
      Point* pt = *iter;
      pt->pushBack(snippet);
    }

    pts.clear();

    printf("  processing function exit for %s..\n", function->getName().c_str());

    mainMgr->findPoints(Scope(PatchAPI::convert(function)), 
                        Point::FuncExit,
                        back_inserter(pts));

    snippet = StackPopSnippet::create(new StackPopSnippet);

    for(vector<Point*>::iterator iter = pts.begin(); iter != pts.end(); ++iter) {
      Point* pt = *iter;
      pt->pushBack(snippet);
    }

/*
    std::set<BPatch_basicBlock*> blocks;
    std::set<BPatch_basicBlock*>::iterator b;

    cfg->getAllBasicBlocks(blocks);
    for (b = blocks.begin(); b != blocks.end(); b++) {
      handleBasicBlock(*b, PatchAPI::convert(function));
    }

*/

    //printf("\n");
}

void handleModule(BPatch_module *mod, const char *name)
{
	char funcname[BUFFER_STRING_LEN];

	// get list of all functions
	std::vector<BPatch_function *>* functions;
	functions = mod->getProcedures();

	// for each function ...
	for (unsigned i = 0; i < functions->size(); i++) {
		BPatch_function *function = functions->at(i);
		function->getName(funcname, BUFFER_STRING_LEN);

        // CRITERIA FOR INSTRUMENTATION:
        // don't handle:
        //   - memset() or call_gmon_start() or frame_dummy()
        //   - functions that begin with an underscore
		if ( (strcmp(funcname,"memset")!=0)
                && (strcmp(funcname,"call_gmon_start")!=0)
                && (strcmp(funcname,"frame_dummy")!=0)
                && funcname[0] != '_') {

            handleFunction(function, funcname);
		}
	}

}

void handleApplication(BPatch_addressSpace *app)
{
    char modname[BUFFER_STRING_LEN];

	// get a reference to the application image
    mainApp = app;
    mainImg = mainApp->getImage();
    mainMgr = PatchAPI::convert(mainApp);

    // get list of all modules
    std::vector<BPatch_module *>* modules;
    std::vector<BPatch_module *>::iterator m;
    modules = mainImg->getModules();

    mainApp->beginInsertionSet();

    BPatch_function* function = getMutateeFunction("foo");
    handleFunction(function, "foo");

    function = getMutateeFunction("bar");
    handleFunction(function, "bar");

    function = getMutateeFunction("f");
    handleFunction(function, "f");

    function = getMutateeFunction("g");
    handleFunction(function, "g");

/*
    // for each module ...
    for (m = modules->begin(); m != modules->end(); m++) {
        (*m)->getName(modname, BUFFER_STRING_LEN);

        // for the purposes of this test,
        // don't handle our own library or libm
        if (strcmp(modname, "libdyntest.so") == 0 ||
            strcmp(modname, "libm.so.6") == 0 ||
            strcmp(modname, "libc.so.6") == 0) {
            //printf(" skipping %s [%s]\n", name, modname);
            continue;
        }

        // don't handle shared libs unless requested
        if ((*m)->isSharedLib() && !instShared) {
            continue;
        }

        handleModule(*m, modname);
    }
    */

    mainApp->finalizeInsertionSet(false); 
}

int main(int argc, char *argv[])
{
    // ABBREVIATED: parse command-line parameters
    binary = argv[1];
    instShared = false;
 
    // initalize DynInst library
    bpatch = new BPatch;

    printf("Patching %s\n", binary);

    // open binary (and possibly dependencies)
    BPatch_addressSpace *app;
    if (instShared) {
        app = bpatch->openBinary(binary, true);
    } else {
        app = bpatch->openBinary(binary, false);
    }
	if (app == NULL) {
		printf("ERROR: Unable to open application.\n");
        exit(EXIT_FAILURE);
    }

    // perform test
    handleApplication(app);

    ((BPatch_binaryEdit*)app)->writeFile("mutant");
    printf("Done.\n");

    return(EXIT_SUCCESS);
}
