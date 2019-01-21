
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "instrument.h"
#include "register_usage.h"

DEFINE_string(mode, "back-edge",
              "Level of CFI protection. Valid values are "
              "back-edge, forward-edge, full.");

void PrintVector(const std::vector<bool>& vec) {
  for (int i = 0; i < vec.size(); i++) {
    printf("%d", vec[i]);
  }
  printf("\n");
}

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binary(argv[1]);

  RegisterUsageInfo info = GetUnusedRegisterInfo(binary);
  PrintVector(info.unused_avx_mask);
  PrintVector(info.unused_mmx_mask);

  Instrument(binary, info);

  return 0;
}
