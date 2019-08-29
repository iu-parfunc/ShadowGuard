
#include <map>
#include <string>

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

DEFINE_string(instrument_list, "none",
              "File containing the list of functions to "
              "be instrumented.");

DEFINE_string(skip_list, "none",
              "File containing the list of functions to "
              "be skipped.");

DEFINE_string(
    shadow_stack, "mem",
    "\n Shadow stack implementation mechanism for backward-edge protection.\n"
    "\n Valid values are\n"
    "   * mem : Uses a memory region as backing store\n");

DEFINE_string(shadow_stack_protection, "none",
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

static bool ValidateShadowStackFlag(const char* flagname,
                                    const std::string& value) {
  if (value == "mem") {
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

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);

  std::string usage("Usage : ./cfi <flags> binary");
  gflags::SetUsageMessage(usage);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binary(argv[1]);

  Parser* parser;
  if (FLAGS_shadow_stack_protection == "sfi") {
    parser = InitParser(binary, /* libs */ false, /* sanitize */ true);
  } else {
    parser = InitParser(binary, /* libs */ true, /* sanitize */ false);
  }

  std::map<std::string, Code*>* cache =
      AnalyseRegisterUsage(binary, const_cast<Parser&>(*parser));

  Instrument(binary, cache, const_cast<Parser&>(*parser));

  return 0;
}
