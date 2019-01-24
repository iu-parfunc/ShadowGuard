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

std::set<std::string> all_regs;
std::set<std::string> callee_saved;
std::set<std::string> all_except_callee_saved;

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

inline bool isInteger(const std::string & s)
{
  if(s.empty() || ((!isdigit(s[0])) && (s[0] != '-') && (s[0] != '+'))) return false ;

  char * p ;
  strtol(s.c_str(), &p, 10) ;

  return (*p == 0) ;
}

void populateAllRegs(std::set<std::string>& all_regs) {
  all_regs.insert("RAX");
  all_regs.insert("RCX");
  all_regs.insert("RDX");
  all_regs.insert("RBX");
  all_regs.insert("RSP");
  all_regs.insert("RBP");
  all_regs.insert("RSI");
  all_regs.insert("RDI");
  all_regs.insert("R8");
  all_regs.insert("R9");
  all_regs.insert("R10");
  all_regs.insert("R11");
  all_regs.insert("R12");
  all_regs.insert("R13");
  all_regs.insert("R14");
  all_regs.insert("R15");
  all_regs.insert("XMM0");
  all_regs.insert("XMM1");
  all_regs.insert("XMM2");
  all_regs.insert("XMM3");
  all_regs.insert("XMM4");
  all_regs.insert("XMM5");
  all_regs.insert("XMM6");
  all_regs.insert("XMM7");
  all_regs.insert("XMM8");
  all_regs.insert("XMM9");
  all_regs.insert("XMM10");
  all_regs.insert("XMM11");
  all_regs.insert("XMM12");
  all_regs.insert("XMM13");
  all_regs.insert("XMM14");
  all_regs.insert("XMM15");
}

void populateCalleeSaved(std::set<std::string>& callee_saved) {
  callee_saved.insert("R12");
  callee_saved.insert("R13");
  callee_saved.insert("R14");
  callee_saved.insert("R15");
  callee_saved.insert("RBX");
  callee_saved.insert("RSP");
  callee_saved.insert("RBP");
}

std::string normalize(std::string reg) {
  if (!reg.compare(0, 1, "E")) {
    return "R" + reg.substr(1);
  }

  if (!(reg.compare("AX") && reg.compare("AH") && reg.compare("AL"))) {
    return "RAX";
  } else if (!(reg.compare("BX") && reg.compare("BH") && reg.compare("BL"))) {
    return "RBX";
  } else if (!(reg.compare("CX") && reg.compare("CH") && reg.compare("CL"))) {
    return "RCX";
  } else if (!(reg.compare("DX") && reg.compare("DH") && reg.compare("DL"))) {
    return "RDX";
  } else if (!reg.compare("SI")) {
    return "RSI";
  } else if (!reg.compare("DI")) {
    return "RDI";
  } else if (!reg.compare("BP")) {
    return "RBP";
  } else if (!reg.compare("SP")) {
    return "RSP";
  } else if (!reg.compare(0, 1, "R") && isInteger(reg.substr(1, 1)) && (std::isalpha(reg.substr(reg.size()-1).at(0)))) {
    if (std::isdigit(reg.substr(2, 1).at(0))) {
      return "R" + reg.substr(1, 2);
    } 

    return "R" + reg.substr(1, 1);
  }

  return reg;
}

void handleInstruction(void *addr, Instruction::Ptr instr, std::set<std::string>& used) {

  std::set<std::string> read;
  std::set<std::string> written;

  InsnCategory c = instr->getCategory();

  std::set<RegisterAST::Ptr> regsRead;
  std::set<RegisterAST::Ptr> regsWritten;

  instr->getReadSet(regsRead);
  instr->getWriteSet(regsWritten);

  for (auto it = regsRead.begin(); it != regsRead.end(); it++) {
    for (auto it = regsRead.begin(); it != regsRead.end(); it++) {
      used.insert(normalize((*it)->format()));
    }

    for (auto it = regsWritten.begin(); it != regsWritten.end(); it++) {
      used.insert(normalize((*it)->format()));
    }
  }
}

void handleBasicBlock(BPatch_basicBlock *block, PatchFunction *func, std::set<std::string>& used)
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

    handleInstruction(addr, iptr, used);
  }
}

void handleFunction(BPatch_function *function, const char *name, std::set<std::string>& used)
{
  printf("%s:\n", name);

  // handle all basic blocks
  std::set<BPatch_basicBlock*> blocks;
  std::set<BPatch_basicBlock*>::iterator b;

  BPatch_flowGraph *cfg = function->getCFG();
  cfg->getAllBasicBlocks(blocks);
  for (b = blocks.begin(); b != blocks.end(); b++) {
    handleBasicBlock(*b, PatchAPI::convert(function), used);
  }
}

long n_funcs   = 0;

std::map<std::string, std::set<std::string>> unused_regs;

void handleModule(BPatch_module *mod, const char *name)
{
  char funcname[BUFFER_STRING_LEN];

  // get list of all functions
  std::vector<BPatch_function *>* functions;
  functions = mod->getProcedures();

  std::set<std::string> used;
  std::set<std::string> unused;

  // for each function ...
  for (unsigned i = 0; i < functions->size(); i++) {
    n_funcs++;
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


      handleFunction(function, funcname, used);

      std::set_difference(all_regs.begin(), all_regs.end(), used.begin(), used.end(),
        std::inserter(unused , unused.end()));

      unused_regs.insert(std::pair<std::string, std::set<std::string>>(
        string(funcname), unused));

      used.clear();
      unused.clear();
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

}

int main(int argc, char *argv[])
{
  // ABBREVIATED: parse command-line parameters
  binary = argv[1];
  instShared = true;

  populateAllRegs(all_regs);
  populateCalleeSaved(callee_saved);

  // initalize DynInst library
  bpatch = new BPatch;

  printf("AUDITING %s\n\n", binary);

  printf("Processing functions..\n\n");

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

  FILE* fp = fopen("regs.out", "w");

  std::map<std::string, long> counts;
  for (auto it : unused_regs) {
    fprintf(fp, "%s : ", it.first.c_str());

    for (auto it2 : it.second) {
      fprintf(fp, "%s ", it2.c_str());

      auto it3 = counts.find(it2);
      if (it3 != counts.end()) {
	long count = it3->second;
	counts[it2] = ++count;
      } else {
	counts[it2] = 1; 
      }
    }

    fprintf(fp, "\n");
  }

  std::vector<std::string> common_regs;
  long funcs = unused_regs.size();
  for (auto it : counts) {
    if (it.second == funcs) {
      common_regs.push_back(it.first);
    }
  }

  fclose(fp);

  printf("\nTotal number of functions : %ld\n", n_funcs);
  printf("Common unused registers to all functions : ");
  
  for (auto it : common_regs) {
    printf("%s ", it.c_str());
  }

  printf("\n");

  return(EXIT_SUCCESS);
}
