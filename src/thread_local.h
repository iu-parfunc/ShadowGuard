
#ifndef LITECFI_THREAD_LOCAL_H_
#define LITECFI_THREAD_LOCAL_H_

#include <stdint.h>

void litecfi_mem_initialize();

void litecfi_overflow_stack_push();

void litecfi_overflow_stack_pop();

// Vararg functions to save up to eight registers
#define ONE_REG_ARG_FN(fn) void fn##_1(int reg1);

#define TWO_REG_ARG_FN(fn) void fn##_2(int reg1, int reg2);

#define THREE_REG_ARG_FN(fn) void fn##_3(int reg1, int reg2, int reg3);

#define FOUR_REG_ARG_FN(fn) void fn##_4(int reg1, int reg2, int reg3, int reg4);

#define FIVE_REG_ARG_FN(fn) \
  void fn##_5(int reg1, int reg2, int reg3, int reg4, int reg5);

#define SIX_REG_ARG_FN(fn) \
  void fn##_6(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6);

#define SEVEN_REG_ARG_FN(fn)                                              \
  void fn##_7(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int reg7);

#define EIGHT_REG_ARG_FN(fn)                                              \
  void fn##_8(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int reg7, int reg8);

// Vararg functions having an additional offset parameter
#define ONE_REG_ARG_N_OFFSET_FN(fn) void fn##_1(int reg1, int offset);

#define TWO_REG_ARG_N_OFFSET_FN(fn) void fn##_2(int reg1, int reg2, int offset);

#define THREE_REG_ARG_N_OFFSET_FN(fn) \
  void fn##_3(int reg1, int reg2, int reg3, int offset);

#define FOUR_REG_ARG_N_OFFSET_FN(fn) \
  void fn##_4(int reg1, int reg2, int reg3, int reg4, int offset);

#define FIVE_REG_ARG_N_OFFSET_FN(fn) \
  void fn##_5(int reg1, int reg2, int reg3, int reg4, int reg5, int offset);

#define SIX_REG_ARG_N_OFFSET_FN(fn)                                       \
  void fn##_6(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int offset);

#define SEVEN_REG_ARG_N_OFFSET_FN(fn)                                     \
  void fn##_7(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int reg7, int offset);

#define EIGHT_REG_ARG_N_OFFSET_FN(fn)                                     \
  void fn##_8(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int reg7, int reg8, int offset);

ONE_REG_ARG_FN(litecfi_register_spill)
TWO_REG_ARG_FN(litecfi_register_spill)
THREE_REG_ARG_FN(litecfi_register_spill)
FOUR_REG_ARG_FN(litecfi_register_spill)
FIVE_REG_ARG_FN(litecfi_register_spill)
SIX_REG_ARG_FN(litecfi_register_spill)
SEVEN_REG_ARG_FN(litecfi_register_spill)
EIGHT_REG_ARG_FN(litecfi_register_spill)
void litecfi_register_spill(unsigned);

ONE_REG_ARG_FN(litecfi_register_restore)
TWO_REG_ARG_FN(litecfi_register_restore)
THREE_REG_ARG_FN(litecfi_register_restore)
FOUR_REG_ARG_FN(litecfi_register_restore)
FIVE_REG_ARG_FN(litecfi_register_restore)
SIX_REG_ARG_FN(litecfi_register_restore)
SEVEN_REG_ARG_FN(litecfi_register_restore)
EIGHT_REG_ARG_FN(litecfi_register_restore)
void litecfi_register_restore(unsigned);

/*
ONE_REG_ARG_N_OFFSET_FN(litecfi_register_peek)
TWO_REG_ARG_N_OFFSET_FN(litecfi_register_peek)
THREE_REG_ARG_N_OFFSET_FN(litecfi_register_peek)
FOUR_REG_ARG_N_OFFSET_FN(litecfi_register_peek)
FIVE_REG_ARG_N_OFFSET_FN(litecfi_register_peek)
SIX_REG_ARG_N_OFFSET_FN(litecfi_register_peek)
SEVEN_REG_ARG_N_OFFSET_FN(litecfi_register_peek)
EIGHT_REG_ARG_N_OFFSET_FN(litecfi_register_peek)
*/

ONE_REG_ARG_FN(litecfi_ctx_save)
TWO_REG_ARG_FN(litecfi_ctx_save)
THREE_REG_ARG_FN(litecfi_ctx_save)
FOUR_REG_ARG_FN(litecfi_ctx_save)
FIVE_REG_ARG_FN(litecfi_ctx_save)
SIX_REG_ARG_FN(litecfi_ctx_save)
SEVEN_REG_ARG_FN(litecfi_ctx_save)
EIGHT_REG_ARG_FN(litecfi_ctx_save)
void litecfi_ctx_save(unsigned);

ONE_REG_ARG_FN(litecfi_ctx_restore)
TWO_REG_ARG_FN(litecfi_ctx_restore)
THREE_REG_ARG_FN(litecfi_ctx_restore)
FOUR_REG_ARG_FN(litecfi_ctx_restore)
FIVE_REG_ARG_FN(litecfi_ctx_restore)
SIX_REG_ARG_FN(litecfi_ctx_restore)
SEVEN_REG_ARG_FN(litecfi_ctx_restore)
EIGHT_REG_ARG_FN(litecfi_ctx_restore)
void litecfi_ctx_restore(unsigned);

/*
ONE_REG_ARG_FN(litecfi_ctx_peek)
TWO_REG_ARG_FN(litecfi_ctx_peek)
THREE_REG_ARG_FN(litecfi_ctx_peek)
FOUR_REG_ARG_FN(litecfi_ctx_peek)
FIVE_REG_ARG_FN(litecfi_ctx_peek)
SIX_REG_ARG_FN(litecfi_ctx_peek)
SEVEN_REG_ARG_FN(litecfi_ctx_peek)
EIGHT_REG_ARG_FN(litecfi_ctx_peek)
*/
#endif  // LITECFI_THREAD_LOCAL_H_
