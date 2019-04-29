
#include <fstream>
#include <memory>
#include <set>
#include <string>

#include "BPatch.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "CodeObject.h"
#include "cache.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "parse.h"
#include "register_usage.h"
#include "utils.h"

DECLARE_bool(vv);
DECLARE_string(instrument_list);
DECLARE_string(skip_list);

// Functions to instrument
std::set<std::string> kInstrumentFunctions;

// Functions to skip
std::set<std::string> kSkipFunctions;

void PopulateFunctions(std::set<std::string>& functions, std::string file) {
  std::ifstream instrument(file);
  std::string line;
  while (std::getline(instrument, line)) {
    if (line != "" || line != "\n") {
      functions.insert(line);
    }
  }
}

bool RegisterUsageInfo::ShouldSkip() {
  if (FLAGS_instrument_list != "none") {
    if (kInstrumentFunctions.size() == 0) {
      PopulateFunctions(kInstrumentFunctions, FLAGS_instrument_list);
    }

    auto it = kInstrumentFunctions.find(name_);
    if (it != kInstrumentFunctions.end()) {
      return false;
    }
    return true;
  } else if (FLAGS_skip_list != "none") {
    if (kSkipFunctions.size() == 0) {
      PopulateFunctions(kSkipFunctions, FLAGS_skip_list);
    }

    auto it = kSkipFunctions.find(name_);
    if (it != kSkipFunctions.end()) {
      return true;
    }
    return false;
  }

  return !writesMemory_ && !writesSP_ && !containsCall_;
}

// -------------- RegisterUsageInfo ------------------

std::string NormalizeRegisterName(std::string reg) {
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
  } else if (!reg.compare(0, 1, "R") && isdigit(reg.substr(1, 1).at(0)) &&
             (std::isalpha(reg.substr(reg.size() - 1).at(0)))) {
    if (std::isdigit(reg.substr(2, 1).at(0))) {
      return "R" + reg.substr(1, 2);
    }

    return "R" + reg.substr(1, 1);
  }

  return reg;
}

int ExtractNumericPostFix(std::string reg) {
  std::string post_fix =
      reg.substr(reg.size() - 2);  // Numeric Postfix can be one or two digits

  if (std::isdigit(post_fix[0])) {
    return std::stoi(post_fix, nullptr);
  } else if (std::isdigit(post_fix[1])) {
    return std::stoi(post_fix.substr(1), nullptr);
  }

  return -1;
}

const std::vector<bool>& RegisterUsageInfo::GetUnusedAvx2Mask() {
  if (unused_avx2_mask_.size() > 0) {
    return const_cast<std::vector<bool>&>(unused_avx2_mask_);
  }

  // Used register mask. Only 16 registers.
  bool used_mask[16] = {false};
  for (const std::string& reg : used_) {
    if (!reg.compare(0, 3, "YMM") || !reg.compare(0, 3, "XMM")) {
      // Extract integer post fix
      int register_index = ExtractNumericPostFix(reg);
      DCHECK(register_index >= 0 && register_index < 16);
      used_mask[register_index] = true;
    }
  }

  int unused_count = 0;
  for (int i = 0; i < 16; i++) {
    if (!used_mask[i]) {
      unused_count++;
    }

    unused_avx2_mask_.push_back(!used_mask[i]);
  }
  n_unused_avx2_regs_ = unused_count;

  return const_cast<const std::vector<bool>&>(unused_avx2_mask_);
}

const std::vector<bool>& RegisterUsageInfo::GetUnusedAvx512Mask() {
  if (unused_avx512_mask_.size() > 0) {
    return const_cast<std::vector<bool>&>(unused_avx512_mask_);
  }

  // Used register mask. 32 AVX512 registers
  bool used_mask[32] = {false};
  for (const std::string& reg : used_) {
    if (!reg.compare(0, 3, "ZMM") || !reg.compare(0, 3, "YMM") ||
        !reg.compare(0, 3, "XMM")) {
      // Extract integer post fix
      int register_index = ExtractNumericPostFix(reg);
      DCHECK(register_index >= 0 && register_index < 32);
      used_mask[register_index] = true;
    }
  }

  int unused_count = 0;
  for (int i = 0; i < 32; i++) {
    if (!used_mask[i]) {
      unused_count++;
    }

    unused_avx512_mask_.push_back(!used_mask[i]);
  }
  n_unused_avx512_regs_ = unused_count;
  return const_cast<const std::vector<bool>&>(unused_avx512_mask_);
}

const std::vector<bool>& RegisterUsageInfo::GetUnusedMmxMask() {
  if (unused_mmx_mask_.size() > 0) {
    return const_cast<const std::vector<bool>&>(unused_mmx_mask_);
  }

  // First check if FPU register stack is used anywhere. If it has been then we
  // cannot use MMX register mode since they overlap with the FPU stack
  // registers.
  for (const std::string& reg : used_) {
    if (!reg.compare(0, 2, "FP") ||
        !reg.compare(0, 2, "ST")) {  // FPU register used

      for (int i = 0; i < 8; i++) {
        unused_mmx_mask_.push_back(false);
      }
      n_unused_mmx_regs_ = 0;
      return const_cast<std::vector<bool>&>(unused_mmx_mask_);
    }
  }

  bool used_mask[8] = {false};  // Used register mask
  for (const std::string& reg : used_) {
    if (!reg.compare(0, 1, "M")) {  // Register is MMX
      int register_index = ExtractNumericPostFix(reg);
      DCHECK(register_index >= 0 && register_index < 16);
      used_mask[register_index] = true;
    }
  }

  int unused_count = 0;
  for (int i = 0; i < 8; i++) {
    if (!used_mask[i]) {
      unused_count++;
    }
    unused_mmx_mask_.push_back(!used_mask[i]);
  }
  n_unused_mmx_regs_ = unused_count;

  return const_cast<std::vector<bool>&>(unused_mmx_mask_);
}

// ------------  End RegisterUsageInfo -----------------

void PopulateFunctionRegisterUsage(Dyninst::ParseAPI::Function* const function,
                                   std::set<std::string>* const used,
                                   bool& write_mem, bool& write_sp,
                                   bool& contains_call) {
  if (FLAGS_vv) {
    StdOut(Color::YELLOW) << "     Function : " << function->name() << Endl;
  }

  std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;

  std::set<std::string> regs;

  for (auto b : function->blocks()) {
    b->getInsns(insns);

    for (auto const& ins : insns) {
      write_mem |= ins.second.writesMemory();
      contains_call |=
          (ins.second.getCategory() == Dyninst::InstructionAPI::c_CallInsn);
      std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> read;
      std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> written;
      ins.second.getReadSet(read);
      ins.second.getWriteSet(written);

      for (auto const& read_register : read) {
        std::string normalized_name =
            NormalizeRegisterName(read_register->format());
        regs.insert(normalized_name);
        used->insert(normalized_name);
      }

      bool isRet =
          (ins.second.getCategory() == Dyninst::InstructionAPI::c_ReturnInsn);

      for (auto const& written_register : written) {
        std::string normalized_name =
            NormalizeRegisterName(written_register->format());
        if (!isRet && normalized_name == "RSP")
          write_sp = true;
        regs.insert(normalized_name);
        used->insert(normalized_name);
      }
    }
  }

  if (FLAGS_vv) {
    PrintSequence<std::set<std::string>, std::string>(regs, 8, ", ");
  }
}

void AnalyseFunctionRegisterUsage(Dyninst::ParseAPI::Function* const function,
                                  Code* const lib) {
  // First check in the cache
  auto it = lib->register_usage.find(function->name());
  if (it != lib->register_usage.end()) {
    it->second->name_ = function->name();
    if (FLAGS_vv) {
      StdOut(Color::YELLOW)
          << "     Function (cached) : " << function->name() << Endl;
      PrintSequence<std::set<std::string>, std::string>(it->second->used_, 8,
                                                        ", ");
    }
    return;
  }

  std::set<std::string> registers;
  bool writesMem = false;
  bool writesSP = false;
  bool containsCall = false;
  PopulateFunctionRegisterUsage(function, &registers, writesMem, writesSP,
                                containsCall);

  RegisterUsageInfo* info = new RegisterUsageInfo();
  info->used_ = registers;
  info->writesMemory_ = writesMem;
  info->writesSP_ = writesSP;
  info->containsCall_ = containsCall;
  info->name_ = function->name();

  // Update the cache
  lib->register_usage.insert(
      std::pair<std::string, RegisterUsageInfo*>(function->name(), info));
}

void PopulateRegisterUsage(const std::vector<BPatch_object*>& objects,
                           std::map<std::string, Code*>* const cache) {
  for (auto object : objects) {
    if (!IsSharedLibrary(object)) {
      StdOut(Color::GREEN, FLAGS_vv)
          << "\n  >> Analyzing main application register usage ..." << Endl;
    }
    StdOut(Color::GREEN, FLAGS_vv)
        << "\n    Analysing " << object->pathName() << Endl;

    // Check if we have results already cached for this code object
    Code* lib = nullptr;
    auto it = cache->find(object->pathName());
    if (it != cache->end()) {
      lib = it->second;
    }

    if (!lib) {
      lib = new Code();
      lib->path = object->pathName();
      cache->insert(std::pair<std::string, Code*>(object->pathName(), lib));
    }

    Dyninst::ParseAPI::CodeObject* code_object =
        Dyninst::ParseAPI::convert(object);

    for (auto function : code_object->funcs()) {
      AnalyseFunctionRegisterUsage(function, lib);
    }
  }
}

std::map<std::string, Code*>* AnalyseRegisterUsage(std::string binary,
                                                   const Parser& parser) {
  StdOut(Color::BLUE, FLAGS_vv) << "Register Analysis Pass" << Endl;
  StdOut(Color::BLUE, FLAGS_vv) << "======================" << Endl;

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  // Get register audit cache deserialized from the disk
  StdOut(Color::BLUE, FLAGS_vv) << "\n+ Loading cached "
                                   "shared library register usage data...\n";
  std::map<std::string, Code*>* cache = GetRegisterAnalysisCache();
  StdOut(Color::NONE, FLAGS_vv) << Endl;

  StdOut(Color::BLUE) << "+ Running register analysis ...\n";

  // Find used registers in the main application
  // AnalyzeMainApplicationRegisterUsage(objects, call_graph);

  // Add used registers in shared library functions called from main
  PopulateRegisterUsage(objects, cache);

  // Update on disk register analysis cache
  FlushRegisterAnalysisCache(cache);

  return cache;
}
