
#include <map>
#include <string>

#include "gflags/gflags.h"
#include "instrument.h"
#include "parse.h"
#include "utils.h"

DEFINE_bool(vv, false, "Log verbose output.");

DEFINE_bool(optimize_regs, false,
            "Optimize register usage in shadow stack instrumentation.");

DEFINE_bool(validate_frame, false,
            "Validate stack frame in addition to the "
            "return address");

DEFINE_bool(disable_lowering, false, "Disable instrumentation lowering");
DEFINE_bool(disable_reg_frame, false, "Disable register frame");
DEFINE_bool(disable_reg_save_opt, false, "Disable register save optimization");
DEFINE_bool(disable_inline, false, "Disable function inlining");
DEFINE_bool(disable_sfe, false, "Disable safe function elision");

DEFINE_bool(libs, false, "Protect shared libraries as well.");

DEFINE_string(
    shadow_stack, "light",
    "\n Shadow stack implementation mechanism for backward-edge protection.\n"
    "\n Valid values are\n"
    "   * light : Uses static analysis to skip run-time checks on functions "
    "             deemed safe\n"
    "   * full :  Add run-time checks at every function\n");

DEFINE_string(output, "", "\n Output binary.\n");

DEFINE_string(stats, "",
              "\n File to log statistics related static analyses. Only used "
              "with 'light' shadow stack option\n");

DEFINE_string(
    threat_model, "trust_system",
    "\n The threat model for instrumentation.\n"
    "\n Valid values are\n"
    "   * trust_system : Trust the loader and system libraries (libc) to be "
    "free from stack overflow"
    " So, only instrument system code for context switch, no CFI checks\n"
    "   * trust_none : Instrument all code for CFI checks\n");

DEFINE_string(
    skip_list, "", "\nA list of function entry addresses to skip instrumentation.\n");

DEFINE_string(
    dry_run, "", 
    "\n Do not do actual shadow stack. Insert partial instrumentation to understand performance impact\n"
    "By default, dry run mode is off.\n"
    "Valid values are\n"
    "   * empty : No instrumentation. Used to measure Dyninst internal overhead\n "
    "   * only-save :  Only save GPR needed for shadow stack\n");


static bool ValidateShadowStackFlag(const char* flagname,
                                    const std::string& value) {
  if (value == "light" || value == "full") {
    return true;
  }

  return false;
}

DEFINE_validator(shadow_stack, &ValidateShadowStackFlag);

int main(int argc, char* argv[]) {
  std::string usage("Usage : ./cfi <flags> binary");
  gflags::SetUsageMessage(usage);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binary(argv[1]);

  litecfi::Parser* parser =
      InitParser(binary, /* libs */ true, /* sanitize */ false);

  Instrument(binary, const_cast<litecfi::Parser&>(*parser));

  return 0;
}
