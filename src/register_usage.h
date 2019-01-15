
#include <set>

#include "CodeObject.h"

enum class Register : uint16_t {

  // General purpose registers
  RAX = 0x00,
  RCX = 0x01,
  RDX = 0x02,
  RBX = 0x03,
  RSP = 0x04,
  RBP = 0x05,
  RSI = 0x06,
  RDI = 0x07,
  R8 = 0x08,
  R9 = 0x09,
  R10 = 0x0A,
  R11 = 0x0B,
  R12 = 0x0C,
  R13 = 0x0D,
  R14 = 0x0E,
  R15 = 0x0F,

  // AVX registers
  XMM0 = 0x10,
  XMM1 = 0x11,
  XMM2 = 0x12,
  XMM3 = 0x13,
  XMM4 = 0x14,
  XMM5 = 0x15,
  XMM6 = 0x16,
  XMM7 = 0x17,
  XMM8 = 0x18,
  XMM9 = 0x19,
  XMM10 = 0x1A,
  XMM11 = 0x1B,
  XMM12 = 0x1C,
  XMM13 = 0x1D,
  XMM14 = 0x1E,
  XMM15 = 0x1F,

  // AVX2 registers
  YMM0 = 0x20,
  YMM1 = 0x21,
  YMM2 = 0x22,
  YMM3 = 0x23,
  YMM4 = 0x24,
  YMM5 = 0x25,
  YMM6 = 0x26,
  YMM7 = 0x27,
  YMM8 = 0x28,
  YMM9 = 0x29,
  YMM10 = 0x2A,
  YMM11 = 0x2B,
  YMM12 = 0x2C,
  YMM13 = 0x2D,
  YMM14 = 0x2E,
  YMM15 = 0x2F,

  // AVX512 registers
  ZMM0 = 0x30,
  ZMM1 = 0x31,
  ZMM2 = 0x32,
  ZMM3 = 0x33,
  ZMM4 = 0x34,
  ZMM5 = 0x35,
  ZMM6 = 0x36,
  ZMM7 = 0x37,
  ZMM8 = 0x38,
  ZMM9 = 0x39,
  ZMM10 = 0x3A,
  ZMM11 = 0x3B,
  ZMM12 = 0x3C,
  ZMM13 = 0x3D,
  ZMM14 = 0x3E,
  ZMM15 = 0x3F,

};

struct RegisterUsageInfo {
  std::vector<bool> unused_avx_mask;  // Mask of unused AVX and AVX2 registers
  std::vector<bool> unused_mmx_mask;  // Mask of unused MMX registers
  std::vector<bool> unused_gpr;  // Mask of unused general purpose registers
};

// Find unused registers across all functions in the application. This
// includes linked shared library functions.
RegisterUsageInfo FindUnusedRegisters(Dyninst::ParseAPI::CodeObject co);
