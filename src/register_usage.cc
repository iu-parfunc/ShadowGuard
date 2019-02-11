
#include <cstddef>
#include <fstream>
#include <string>

#include <errno.h>

#if defined(__GLIBCXX__) || defined(__GLIBCPP__)
#include <ext/stdio_filebuf.h>
#else
#error We require libstdc++ at the moment. Compile with GCC or specify\
 libstdc++ at compile time (e.g: -stdlib=libstdc++ in Clang).
#endif

#include <sys/file.h>

#include "BPatch.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "CodeObject.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "parse.h"
#include "register_usage.h"
#include "utils.h"

DECLARE_bool(vv);

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

void PopulateUsedRegisters(Dyninst::ParseAPI::CodeObject* code_object,
                           std::set<std::string>& used) {
  code_object->parse();

  for (auto function : code_object->funcs()) {
    if (FLAGS_vv) {
      StdOut(Color::YELLOW) << " Function : " << function->name() << Endl;
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
          used.insert(normalized_name);
        }

        for (auto const& written_register : written) {
          std::string normalized_name =
              NormalizeRegisterName(written_register->format());
          regs.insert(normalized_name);
          used.insert(normalized_name);
        }
      }
    }

    if (FLAGS_vv) {
      PrintSequence<std::set<std::string>, std::string>(regs, 4, ", ");
    }
  }
}

const std::string kAuditCacheFile = ".audit_cache";

bool PopulateCacheFromDisk(
    std::map<std::string, std::set<std::string>>& cache) {
  std::ifstream cache_file(kAuditCacheFile);

  if (cache_file.fail()) {
    return false;
  }

  std::string line;
  while (std::getline(cache_file, line)) {
    std::vector<std::string> tokens = Split(line, ',');
    DCHECK(tokens.size() == 2);

    std::string library = tokens[0];
    if (FLAGS_vv) {
      StdOut(Color::GREEN) << "Loading cached analysis results for library : "
                           << library << Endl;
      StdOut(Color::NONE) << "  " << tokens[1] << Endl << Endl;
    }

    std::vector<std::string> registers = Split(tokens[1], ':');
    DCHECK(registers.size() > 0);

    std::set<std::string> register_set(registers.begin(), registers.end());
    cache[library] = register_set;
  }

  return true;
}

void FlushCacheToDisk(
    const std::map<std::string, std::set<std::string>>& cache) {
  std::ofstream cache_file(kAuditCacheFile);

  if (!cache_file.is_open()) {
    char error[2048];
    fprintf(stderr,
            "Failed to create/ open the register audit cache file with : %s\n",
            strerror_r(errno, error, 2048));
    return;
  }

  // Exclusively lock the file for writing
  int fd =
      static_cast<__gnu_cxx::stdio_filebuf<char>* const>(cache_file.rdbuf())
          ->fd();

  if (flock(fd, LOCK_EX)) {
    char error[2048];
    fprintf(stderr, "Failed to lock the register audit cache file with : %s\n",
            strerror_r(errno, error, 2048));
    return;
  }

  for (auto const& it : cache) {
    std::string registers_concat = "";
    std::set<std::string> registers = it.second;

    for (auto const& reg : registers) {
      registers_concat += (reg + ":");
    }

    // Remove the trailing ':'
    registers_concat.pop_back();
    cache_file << it.first << "," << registers_concat << std::endl;
  }

  if (flock(fd, LOCK_UN)) {
    char error[2048];
    fprintf(stderr,
            "Failed to unlock the register audit cache file with : %s\n",
            strerror_r(errno, error, 2048));
  }

  cache_file.close();
  return;
}

std::vector<std::string> GetCalledSharedLibraryFunctions(
    std::vector<BPatch_object*> objects) {
  std::vector<std::string> plt_stub_funcs;
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
        if (plt_it != code_object->cs()->linkage().end() &&
            plt_it->second != "") {
          plt_stub_funcs.push_back(plt_it->second);
        }
      }
    }
  }
  return plt_stub_funcs;
}

RegisterUsageInfo GetUnusedRegisterInfo(std::string binary,
                                        const Parser& parser) {
  StdOut(Color::BLUE, FLAGS_vv) << "Register Analysis Pass" << Endl;
  StdOut(Color::BLUE, FLAGS_vv) << "======================" << Endl;

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  // Used registers in the application and its linked shared libraries
  std::set<std::string> used;
  // Register audit cache deserialized from the disk
  std::map<std::string, std::set<std::string>> cache;

  StdOut(Color::BLUE, FLAGS_vv)
      << "\n[Register Analysis] Loading analysis cache.\n\n";

  bool is_cache_present = PopulateCacheFromDisk(cache);

  StdOut(Color::NONE, FLAGS_vv) << Endl;
  StdOut(Color::BLUE) << "[Register Analysis] Running register analysis ...\n";

  std::vector<std::string> plt_funcs = GetCalledSharedLibraryFunctions(objects);

  StdOut(Color::GREEN, FLAGS_vv) << "\nCalled shared library functions\n";
  PrintSequence<std::vector<std::string>, std::string>(plt_funcs, 2, ", ");

  for (auto object : objects) {
    if (IsSharedLibrary(object)) {
      StdOut(Color::GREEN, FLAGS_vv)
          << "\nAnalysing shared library : " << object->pathName() << Endl;
    } else {
      StdOut(Color::GREEN, FLAGS_vv)
          << "\nAnalysing program text : " << object->pathName() << Endl;
    }

    std::set<std::string> registers;
    if (is_cache_present && IsSharedLibrary(object)) {
      auto it = cache.find(object->pathName());
      if (it != cache.end()) {
        StdOut(Color::YELLOW, FLAGS_vv)
            << " Using cached analysis results." << Endl;
        registers = it->second;
      }
    }

    if (!registers.size()) {
      // Couldn't find the library info in the cache or this object is the
      // program text. Parse the object and get register usage information.
      PopulateUsedRegisters(Dyninst::ParseAPI::convert(object), registers);

      // Update the cache if this is a shared library
      if (IsSharedLibrary(object)) {
        cache[object->pathName()] = registers;
      } else {
        if (FLAGS_vv) {
          StdOut(Color::GREEN) << "\nApplication Register Usage :" << Endl;
          PrintSequence<std::set<std::string>, std::string>(registers, 4, ", ");
        }
      }
    }

    for (auto const& reg : registers) {
      used.insert(reg);
    }
  }

  FlushCacheToDisk(cache);

  RegisterUsageInfo info;
  PopulateUnusedAvx2Mask(used, &info);
  PopulateUnusedAvx512Mask(used, &info);
  PopulateUnusedMmxMask(used, &info);

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

  return info;
}
