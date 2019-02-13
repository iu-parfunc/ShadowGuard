
#include <memory>
#include <string>

#include "BPatch.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "CodeObject.h"
#include "cache.h"
#include "call_graph.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "parse.h"
#include "register_usage.h"
#include "utils.h"

DECLARE_bool(vv);

void PrintRegisterUsageSummary(std::set<std::string>& used,
                               RegisterUsageInfo& info) {
  if (FLAGS_vv) {
    StdOut(Color::BLUE) << "\n\n[Register Analysis] Results : " << Endl << Endl;
    StdOut(Color::GREEN) << "  Used registers : " << Endl;
    PrintSequence<std::set<std::string>, std::string>(used, 4, ", ");

    StdOut(Color::GREEN) << "\n  Unused register masks (1's for unused)"
                         << Endl;

    StdOut(Color::GREEN) << "    AVX2 :   ";
    PrintSequence<std::vector<bool>, bool>(info.unused_avx2_mask);

    StdOut(Color::GREEN) << "    AVX512 : ";
    PrintSequence<std::vector<bool>, bool>(info.unused_avx512_mask);

    StdOut(Color::GREEN) << "    MMX :    ";
    PrintSequence<std::vector<bool>, bool>(info.unused_mmx_mask);
    StdOut() << Endl << Endl;
  }

  StdOut(Color::BLUE) << "[Register Analysis] Register analysis complete."
                      << Endl << Endl;
  StdOut(Color::GREEN) << "  Unused Register Count: " << Endl;
  StdOut(Color::GREEN) << "    AVX2   : " << info.n_unused_avx2_regs << Endl;
  StdOut(Color::GREEN) << "    AVX512 : " << info.n_unused_avx512_regs << Endl;
  StdOut(Color::GREEN) << "    MMX    : " << info.n_unused_mmx_regs << Endl;
}

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

void PopulateUnusedAvx2Mask(const std::set<std::string>& used,
                            RegisterUsageInfo* const info) {
  // Used register mask. Only 16 registers.
  bool used_mask[16] = {false};
  for (const std::string& reg : used) {
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

    info->unused_avx2_mask.push_back(!used_mask[i]);
  }
  info->n_unused_avx2_regs = unused_count;
}

void PopulateUnusedAvx512Mask(const std::set<std::string>& used,
                              RegisterUsageInfo* const info) {
  // Used register mask. 32 AVX512 registers
  bool used_mask[32] = {false};
  for (const std::string& reg : used) {
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

    info->unused_avx512_mask.push_back(!used_mask[i]);
  }
  info->n_unused_avx512_regs = unused_count;
}

void PopulateUnusedMmxMask(const std::set<std::string>& used,
                           RegisterUsageInfo* const info) {
  // First check if FPU register stack is used anywhere. If it has been then we
  // cannot use MMX register mode since they overlap with the FPU stack
  // registers.
  for (const std::string& reg : used) {
    if (!reg.compare(0, 2, "FP") ||
        !reg.compare(0, 2, "ST")) {  // FPU register used

      for (int i = 0; i < 8; i++) {
        info->unused_mmx_mask.push_back(false);
      }
      info->n_unused_mmx_regs = 0;
      return;
    }
  }

  bool used_mask[8] = {false};  // Used register mask
  for (const std::string& reg : used) {
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
    info->unused_mmx_mask.push_back(!used_mask[i]);
  }
  info->n_unused_mmx_regs = unused_count;
}

void PopulateUnusedGprMask(const std::set<std::string>& used,
                           RegisterUsageInfo* const info) {
  // TODO(chamibuddhika) Complete this
}

void PopulateUsedRegistersInFunction(
    Dyninst::ParseAPI::Function* const function,
    std::set<std::string>* const used) {
  if (FLAGS_vv) {
    StdOut(Color::YELLOW) << "     Function : " << function->name() << Endl;
  }

  std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;

  std::set<std::string> regs;

  for (auto b : function->blocks()) {
    b->getInsns(insns);

    for (auto const& ins : insns) {
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

      for (auto const& written_register : written) {
        std::string normalized_name =
            NormalizeRegisterName(written_register->format());
        regs.insert(normalized_name);
        used->insert(normalized_name);
      }
    }
  }

  if (FLAGS_vv) {
    PrintSequence<std::set<std::string>, std::string>(regs, 8, ", ");
  }
}

void PopulateUsedRegisters(Dyninst::ParseAPI::CodeObject* const code_object,
                           std::set<std::string>* const used) {
  code_object->parse();

  for (auto function : code_object->funcs()) {
    PopulateUsedRegistersInFunction(function, used);
  }
}

std::set<std::string> GetRegisterUsageForSharedFunction(
    std::string function, SharedLibrary* const lib,
    LazyCallGraph<RegisterUsageInfo>* const call_graph) {
  // First check in the cache
  auto it = lib->register_usage.find(function);
  if (it != lib->register_usage.end()) {
    return it->second;
  }

  // Failing that, analyze the call graph
  std::set<std::string>* used = new std::set<std::string>();
  call_graph->VisitCallGraph(
      function, [=](LazyFunction<RegisterUsageInfo>* const lazy) {
        std::set<std::string> registers;
        if (lazy->data) {
          registers = lazy->data->used;
          std::copy(registers.begin(), registers.end(),
                    std::inserter(*used, used->end()));
          return;
        }

        PopulateUsedRegistersInFunction(lazy->function, &registers);

        RegisterUsageInfo* info = new RegisterUsageInfo();
        info->used = registers;
        PopulateUnusedAvx2Mask(registers, info);
        PopulateUnusedAvx512Mask(registers, info);
        PopulateUnusedMmxMask(registers, info);

        lazy->data = info;

        std::copy(registers.begin(), registers.end(),
                  std::inserter(*used, used->end()));
        return;
      });

  // RAII the collection
  std::unique_ptr<std::set<std::string>> ptr(used);

  // Update the cache
  lib->register_usage.insert(
      std::pair<std::string, std::set<std::string>>(function, *(ptr.get())));

  return *(ptr.get());
}

std::set<std::string> GetMatchingSharedLibraryFunctions(
    const BPatch_object* const object, const std::set<std::string>& plt_stubs) {
  Dyninst::ParseAPI::CodeObject* code_object =
      Dyninst::ParseAPI::convert(object);

  std::set<std::string> called;
  for (auto function : code_object->funcs()) {
    if (plt_stubs.find(function->name()) != plt_stubs.end()) {
      called.insert(function->name());
    }
  }
  return called;
}

std::set<std::string> GetCalledPltStubs(
    const std::vector<BPatch_object*>& objects) {
  std::set<std::string> plt_stub_funcs;
  for (auto object : objects) {
    if (!IsSharedLibrary(object)) {
      // This is the code object corresponding to the program text
      Dyninst::ParseAPI::CodeObject* code_object =
          Dyninst::ParseAPI::convert(object);

      std::map<Dyninst::Address, std::string> linkage =
          code_object->cs()->linkage();

      // Iterate on functions and find externally linked PLT stub functions
      for (auto function : code_object->funcs()) {
        auto plt_it = linkage.find(function->addr());
        if (plt_it != linkage.end() && plt_it->second != "") {
          plt_stub_funcs.insert(plt_it->second);
        }
      }
    }
  }
  return plt_stub_funcs;
}

void AnalyzeSharedLibraryRegisterUsage(
    const std::vector<BPatch_object*>& objects,
    std::map<std::string, SharedLibrary*>* const cache,
    LazyCallGraph<RegisterUsageInfo>* const call_graph,
    std::set<std::string>* const used) {
  StdOut(Color::GREEN, FLAGS_vv)
      << "\n\n  >> Analysing shared library register usage ..." << Endl;
  // Find shared library plt stubs called from the main application
  std::set<std::string> plt_stubs = GetCalledPltStubs(objects);
  StdOut(Color::GREEN, FLAGS_vv) << "\n    Called shared library functions\n";
  PrintSequence<std::set<std::string>, std::string>(plt_stubs, 6, ", ");

  // Now analyze register usage of shared library functions corresponding to the
  // discovered plt stubs
  for (auto object : objects) {
    if (IsSharedLibrary(object)) {
      // Get shared library functions from this library matching any of the plt
      // stubs found
      std::set<std::string> matching =
          GetMatchingSharedLibraryFunctions(object, plt_stubs);

      if (matching.size() == 0) {
        // No matching functions found in this shared library. Move on to the
        // next.
        continue;
      }

      // Check if we have results already cached for this shared library. Later
      // we need to also check if any of the functions that we are currently
      // looking for have been cached as well
      SharedLibrary* lib = nullptr;
      auto it = cache->find(object->pathName());
      if (it != cache->end()) {
        lib = it->second;
      }

      if (!lib) {
        StdOut(Color::GREEN, FLAGS_vv)
            << "\n    Analysing " << object->pathName() << Endl;
        lib = new SharedLibrary();
        lib->path = object->pathName();
        cache->insert(
            std::pair<std::string, SharedLibrary*>(object->pathName(), lib));
      }

      for (auto function : matching) {
        std::set<std::string> registers =
            GetRegisterUsageForSharedFunction(function, lib, call_graph);
        std::copy(registers.begin(), registers.end(),
                  std::inserter(*used, used->end()));
      }
    }
  }
}

void AnalyzeMainApplicationRegisterUsage(
    const std::vector<BPatch_object*>& objects,
    std::set<std::string>* const used) {
  StdOut(Color::GREEN, FLAGS_vv)
      << "\n  >> Analyzing main application register usage ..." << Endl;
  for (auto object : objects) {
    if (!IsSharedLibrary(object)) {
      // This is the program text.
      std::set<std::string> registers;
      PopulateUsedRegisters(Dyninst::ParseAPI::convert(object), &registers);
      std::copy(registers.begin(), registers.end(),
                std::inserter(*used, used->end()));
      if (FLAGS_vv) {
        StdOut(Color::GREEN)
            << "\n    [Main Application]  Register Usage :" << Endl;
        PrintSequence<std::set<std::string>, std::string>(registers, 6, ", ");
      }
      return;
    }
  }
}

RegisterUsageInfo GetRegisterUsageInfo(
    std::string binary, LazyCallGraph<RegisterUsageInfo>* const call_graph,
    const Parser& parser) {
  StdOut(Color::BLUE, FLAGS_vv) << "Register Analysis Pass" << Endl;
  StdOut(Color::BLUE, FLAGS_vv) << "======================" << Endl;

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  // Get register audit cache deserialized from the disk
  StdOut(Color::BLUE, FLAGS_vv) << "\n+ Loading cached "
                                   "shared library register usage data...\n";
  std::map<std::string, SharedLibrary*>* cache = GetRegisterAuditCache();
  StdOut(Color::NONE, FLAGS_vv) << Endl;

  StdOut(Color::BLUE) << "+ Running register analysis ...\n";

  // Used registers in the application and its linked shared libraries
  std::set<std::string> used;

  // Find used registers in the main application
  AnalyzeMainApplicationRegisterUsage(objects, &used);

  // Add used registers in shared library functions called from main
  AnalyzeSharedLibraryRegisterUsage(objects, cache, call_graph, &used);

  // Update on disk register analysis cache
  FlushRegisterAuditCache(cache);

  // Calculate usage summary data
  RegisterUsageInfo info;
  PopulateUnusedAvx2Mask(used, &info);
  PopulateUnusedAvx512Mask(used, &info);
  PopulateUnusedMmxMask(used, &info);

  PrintRegisterUsageSummary(used, info);
  return info;
}
