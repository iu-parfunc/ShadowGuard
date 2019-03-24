
#include <map>
#include <string>

#include "call_graph.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "instrument.h"
#include "parse.h"
#include "register_usage.h"
#include "utils.h"

DEFINE_bool(vv, false, "Log verbose output.");

DEFINE_bool(skip, false, "Skip codegen.");

DEFINE_bool(stats, false,
            "Instrument to collect internal stack usage "
            "statistics at runtime.");

DEFINE_bool(libs, false, "Protect shared libraries as well.");

DEFINE_string(
    shadow_stack, "avx_v3",
    "\n Shadow stack implementation mechanism for backward-edge protection.\n"
    "\n Valid values are\n"
    "   * avx_v2 : Uses avx2 register file as backing store (v2"
    "implementation)\n"
    "   * avx_v3 : Uses avx2 register file as backing store (v3 "
    "implementation)\n"
    "   * avx512 : Uses avx512 register file as backing store\n"
    "   * mem : Uses a memory region as backing store\n"
    "   * xor : Uses a xor check based technique to validate the return "
    "chain.\n"
    "   * dispatch : Makes shadow stack with only a dispatch to a jump table.\n"
    "   * empty : Makes an empty shadow stack.\n"
    "   * reloc : Only perform relocation.\n"
    "   * savegpr: Relocation + saving & restoring GPRs\n"
    " Less context sensitive and precise than other techniques.\n");

DEFINE_string(shadow_stack_protection, "sfi",
              "\n Applicable only when `shadow-stack` is set to mem."
              " Specifies protection mechanism for the memory region used as"
              " the backing store for the shadow stack.\n"
              "\n Valid values are\n"
              "   * sfi: Use Software Fault Isolation by sanitizing every"
              " memory write of the application\n"
              "   * mpx : Use mpx bound checking\n"
              "   * none : Use no protection\n");

DEFINE_string(
    threat_model, "trust_system",
    "\n The threat model for instrumentation.\n"
    "\n Valid values are\n"
    "   * trust_system : Trust the loader and system libraries (libc) to be "
    "free from stack overflow"
    " So, only instrument system code for context switch, no CFI checks\n"
    "   * trust_none : Instrument all code for CFI checks\n");

DEFINE_string(cache, "./libs/",
              "\n Path to the cache of hardened shared libraries."
              " Once a shared library dependency is encountered the tool will"
              " check in the cache and reuse that if available.\n");

DEFINE_string(
    install, "./bin/",
    "\n Installation path of the hardened binary and its dependencies.\n");

DEFINE_int32(reserved_from, 8,
             "\n The register number starting to be reserved for CFI\n");

DEFINE_int32(qwords_per_reg, 4,
        "\n Number of qwords per vector register used for CFI\n");

static bool ValidateShadowStackFlag(const char* flagname,
                                    const std::string& value) {
  if (value == "avx512" || value == "mem" || value == "dispatch" ||
      value == "reloc" || value == "empty" || value == "savegpr" ||
      value == "avx_v2" || value == "avx_v3") {
    return true;
  }

  return false;
}

static bool ValidateShadowStackProtectionFlag(const char* flagname,
                                              const std::string& value) {
  if (value == "sfi" || value == "mpx" || value == "none") {
    return true;
  }
  return false;
}

DEFINE_validator(shadow_stack, &ValidateShadowStackFlag);

DEFINE_validator(shadow_stack_protection, &ValidateShadowStackProtectionFlag);

void PrintVector(const std::vector<bool>& vec) {
  for (unsigned int i = 0; i < vec.size(); i++) {
    printf("%d", vec[i]);
  }
  printf("\n");
}

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);

  std::string usage("Usage : ./cfi <flags> binary");
  gflags::SetUsageMessage(usage);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binary(argv[1]);

  Parser parser = InitParser(binary);

  std::map<std::string, Code*>* cache = AnalyseRegisterUsage(binary, parser);

  Instrument(binary, cache, parser);

  return 0;
}
