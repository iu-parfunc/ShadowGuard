
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
//__thread __attribute__ ((tls_model("initial-exec"))) uint64_t  overflow_stack_space[1024];
//__thread __attribute__ ((tls_model("initial-exec"))) uint64_t* overflow_stack = NULL;

uint64_t  overflow_stack_space[1024];
uint64_t* overflow_stack = &overflow_stack_space[0];

#define NUM_REG   16
#define REG_SIZE  (256/64)
#define CONTEXT_SIZE  (NUM_REG)*(REG_SIZE)
//__thread __attribute__ ((tls_model("initial-exec"))) uint64_t cfi_context[CONTEXT_SIZE];
//__thread __attribute__ ((tls_model("initial-exec"))) uint64_t user_context[CONTEXT_SIZE];

uint64_t cfi_context[CONTEXT_SIZE];
uint64_t user_context[CONTEXT_SIZE];


/*
static void setup_memory(uint64_t** mem_ptr, long size) {
  // Add space for two guard pages at the beginning and the end of the stack
  int page_size = getpagesize();
  size += page_size * 2;

  *mem_ptr = (uint64_t*)mmap(0, size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  // Protect guard pages
  mprotect(*mem_ptr, page_size, PROT_NONE);
  mprotect(*mem_ptr + size - page_size, page_size, PROT_NONE);

  // Skip past the guard page at the beginning
  *mem_ptr += page_size;
}
*/
void litecfi_mem_initialize() {
    return;
  //long stack_size = pow(2, 16);  // Stack size = 2^16
  //setup_memory(&overflow_stack, stack_size);
}
void litecfi_overflow_stack_push() {
  if (overflow_stack == NULL) {
      overflow_stack = (uint64_t*)overflow_stack_space;
  }
  asm("movq (%%r10), %%r10;\n\t"
      "movq %%r10, (%0);\n\t"
      "addq $8, %0;\n\t"
      : "+a"(overflow_stack)
      :
      :);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
void litecfi_overflow_stack_pop() {
  uint64_t ret_addr;
  asm("subq $8, %0;\n\t"
      "movq (%0), %1;\n\t"
      : "+d"(overflow_stack), "=b"(ret_addr)
      :);
  asm goto(
      "cmp %0, (%%r10); \n\t"
      "jz %l1; \n\t"
      "int $3; \n\t"
      :
      : "b"(ret_addr)
      : "cc"
      : ok);
ok:
  return;
  /*
  asm("subq $8, %0;\n\t"
      "movq (%0), %%rax;\n\t"
      : "+d"(overflow_stack)
      :
      : "rax");
      */
}
#pragma GCC diagnostic pop

#define OUT_OF_BOUNDS()           \
  default:                        \
    printf("Invalid register\n"); \
    assert(0);

// clang-format off
#define PUSH_AVX2(cfi_context, index, ymm_spill)  \
  case index: {                                   \
    asm(ymm_spill                                 \
        :                                         \
        : "r"(&cfi_context[index*REG_SIZE])       \
        :);                                       \
    break;                                        \
  }

#define POP_AVX2(cfi_context, index, ymm_restore)     \
  case index: {                                       \
    asm(ymm_restore                                   \
        :                                             \
        : "r"(&cfi_context[index*REG_SIZE])           \
        :);                                           \
    break;                                            \
  }

#define PEEK_AVX2(cfi_context, index, offset, ymm_peek) \
  case index: {                                         \
    asm("imul $256, %0;\n\t"                            \
        "neg %0;\n\t"                                   \
        "movq %1, %%rdx;\n\t"                           \
        "leaq (%%rdx, %%rbx, 1), %%rdx;\n\t"            \
        ymm_peek                                        \
        :                                               \
        : "b"(offset), "a"(cfi_context)                 \
        : "rdx");                                       \
    break;                                              \
  }
// clang-format on

#define REGISTER_PUSH(index, sp)                    \
  switch (index) {                                  \
    PUSH_AVX2(sp, 0, "vmovdqu %%ymm0, (%0);\n\t")   \
    PUSH_AVX2(sp, 1, "vmovdqu %%ymm1, (%0);\n\t")   \
    PUSH_AVX2(sp, 2, "vmovdqu %%ymm2, (%0);\n\t")   \
    PUSH_AVX2(sp, 3, "vmovdqu %%ymm3, (%0);\n\t")   \
    PUSH_AVX2(sp, 4, "vmovdqu %%ymm4, (%0);\n\t")   \
    PUSH_AVX2(sp, 5, "vmovdqu %%ymm5, (%0);\n\t")   \
    PUSH_AVX2(sp, 6, "vmovdqu %%ymm6, (%0);\n\t")   \
    PUSH_AVX2(sp, 7, "vmovdqu %%ymm7, (%0);\n\t")   \
    PUSH_AVX2(sp, 8, "vmovdqu %%ymm8, (%0);\n\t")   \
    PUSH_AVX2(sp, 9, "vmovdqu %%ymm9, (%0);\n\t")   \
    PUSH_AVX2(sp, 10, "vmovdqu %%ymm10, (%0);\n\t") \
    PUSH_AVX2(sp, 11, "vmovdqu %%ymm11, (%0);\n\t") \
    PUSH_AVX2(sp, 12, "vmovdqu %%ymm12, (%0);\n\t") \
    PUSH_AVX2(sp, 13, "vmovdqu %%ymm13, (%0);\n\t") \
    PUSH_AVX2(sp, 14, "vmovdqu %%ymm14, (%0);\n\t") \
    PUSH_AVX2(sp, 15, "vmovdqu %%ymm15, (%0);\n\t") \
    OUT_OF_BOUNDS()                                 \
  }

#define REGISTER_POP(index, sp)                    \
  switch (index) {                                 \
    POP_AVX2(sp, 0, "vmovdqu (%0), %%ymm0;\n\t")   \
    POP_AVX2(sp, 1, "vmovdqu (%0), %%ymm1;\n\t")   \
    POP_AVX2(sp, 2, "vmovdqu (%0), %%ymm2;\n\t")   \
    POP_AVX2(sp, 3, "vmovdqu (%0), %%ymm3;\n\t")   \
    POP_AVX2(sp, 4, "vmovdqu (%0), %%ymm4;\n\t")   \
    POP_AVX2(sp, 5, "vmovdqu (%0), %%ymm5;\n\t")   \
    POP_AVX2(sp, 6, "vmovdqu (%0), %%ymm6;\n\t")   \
    POP_AVX2(sp, 7, "vmovdqu (%0), %%ymm7;\n\t")   \
    POP_AVX2(sp, 8, "vmovdqu (%0), %%ymm8;\n\t")   \
    POP_AVX2(sp, 9, "vmovdqu (%0), %%ymm9;\n\t")   \
    POP_AVX2(sp, 10, "vmovdqu (%0), %%ymm10;\n\t") \
    POP_AVX2(sp, 11, "vmovdqu (%0), %%ymm11;\n\t") \
    POP_AVX2(sp, 12, "vmovdqu (%0), %%ymm12;\n\t") \
    POP_AVX2(sp, 13, "vmovdqu (%0), %%ymm13;\n\t") \
    POP_AVX2(sp, 14, "vmovdqu (%0), %%ymm14;\n\t") \
    POP_AVX2(sp, 15, "vmovdqu (%0), %%ymm15;\n\t") \
    OUT_OF_BOUNDS()                                \
  }

#define REGISTER_PEEK(index, sp, offset)                       \
  switch (index) {                                             \
    PEEK_AVX2(sp, 0, offset, "vmovdqu (%%rdx), %%ymm0;\n\t")   \
    PEEK_AVX2(sp, 1, offset, "vmovdqu (%%rdx), %%ymm1;\n\t")   \
    PEEK_AVX2(sp, 2, offset, "vmovdqu (%%rdx), %%ymm2;\n\t")   \
    PEEK_AVX2(sp, 3, offset, "vmovdqu (%%rdx), %%ymm3;\n\t")   \
    PEEK_AVX2(sp, 4, offset, "vmovdqu (%%rdx), %%ymm4;\n\t")   \
    PEEK_AVX2(sp, 5, offset, "vmovdqu (%%rdx), %%ymm5;\n\t")   \
    PEEK_AVX2(sp, 6, offset, "vmovdqu (%%rdx), %%ymm6;\n\t")   \
    PEEK_AVX2(sp, 7, offset, "vmovdqu (%%rdx), %%ymm7;\n\t")   \
    PEEK_AVX2(sp, 8, offset, "vmovdqu (%%rdx), %%ymm8;\n\t")   \
    PEEK_AVX2(sp, 9, offset, "vmovdqu (%%rdx), %%ymm9;\n\t")   \
    PEEK_AVX2(sp, 10, offset, "vmovdqu (%%rdx), %%ymm10;\n\t") \
    PEEK_AVX2(sp, 11, offset, "vmovdqu (%%rdx), %%ymm11;\n\t") \
    PEEK_AVX2(sp, 12, offset, "vmovdqu (%%rdx), %%ymm12;\n\t") \
    PEEK_AVX2(sp, 13, offset, "vmovdqu (%%rdx), %%ymm13;\n\t") \
    PEEK_AVX2(sp, 14, offset, "vmovdqu (%%rdx), %%ymm14;\n\t") \
    PEEK_AVX2(sp, 15, offset, "vmovdqu (%%rdx), %%ymm15;\n\t") \
    OUT_OF_BOUNDS()                                            \
  }

#define ONE_REG_PUSH_FN(fn, sp) \
  void fn##_1(int reg1) { REGISTER_PUSH(reg1, sp) }

#define TWO_REG_PUSH_FN(fn, sp)     \
  void fn##_2(int reg1, int reg2) { \
    REGISTER_PUSH(reg1, sp)         \
    REGISTER_PUSH(reg2, sp)         \
  }

#define THREE_REG_PUSH_FN(fn, sp)             \
  void fn##_3(int reg1, int reg2, int reg3) { \
    REGISTER_PUSH(reg1, sp)                   \
    REGISTER_PUSH(reg2, sp)                   \
    REGISTER_PUSH(reg3, sp)                   \
  }

#define FOUR_REG_PUSH_FN(fn, sp)                        \
  void fn##_4(int reg1, int reg2, int reg3, int reg4) { \
    REGISTER_PUSH(reg1, sp)                             \
    REGISTER_PUSH(reg2, sp)                             \
    REGISTER_PUSH(reg3, sp)                             \
    REGISTER_PUSH(reg4, sp)                             \
  }

#define FIVE_REG_PUSH_FN(fn, sp)                                  \
  void fn##_5(int reg1, int reg2, int reg3, int reg4, int reg5) { \
    REGISTER_PUSH(reg1, sp)                                       \
    REGISTER_PUSH(reg2, sp)                                       \
    REGISTER_PUSH(reg3, sp)                                       \
    REGISTER_PUSH(reg4, sp)                                       \
    REGISTER_PUSH(reg5, sp)                                       \
  }

#define SIX_REG_PUSH_FN(fn, sp)                                             \
  void fn##_6(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6) { \
    REGISTER_PUSH(reg1, sp)                                                 \
    REGISTER_PUSH(reg2, sp)                                                 \
    REGISTER_PUSH(reg3, sp)                                                 \
    REGISTER_PUSH(reg4, sp)                                                 \
    REGISTER_PUSH(reg5, sp)                                                 \
    REGISTER_PUSH(reg6, sp)                                                 \
  }

#define SEVEN_REG_PUSH_FN(fn, sp)                                         \
  void fn##_7(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int reg7) {                                                 \
    REGISTER_PUSH(reg1, sp)                                               \
    REGISTER_PUSH(reg2, sp)                                               \
    REGISTER_PUSH(reg3, sp)                                               \
    REGISTER_PUSH(reg4, sp)                                               \
    REGISTER_PUSH(reg5, sp)                                               \
    REGISTER_PUSH(reg6, sp)                                               \
    REGISTER_PUSH(reg7, sp)                                               \
  }

#define EIGHT_REG_PUSH_FN(fn, sp)                                         \
  void fn##_8(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int reg7, int reg8) {                                       \
    REGISTER_PUSH(reg1, sp)                                               \
    REGISTER_PUSH(reg2, sp)                                               \
    REGISTER_PUSH(reg3, sp)                                               \
    REGISTER_PUSH(reg4, sp)                                               \
    REGISTER_PUSH(reg5, sp)                                               \
    REGISTER_PUSH(reg6, sp)                                               \
    REGISTER_PUSH(reg7, sp)                                               \
    REGISTER_PUSH(reg8, sp)                                               \
  }

#define ONE_REG_POP_FN(fn, sp) \
  void fn##_1(int reg1) { REGISTER_POP(reg1, sp) }

#define TWO_REG_POP_FN(fn, sp)      \
  void fn##_2(int reg1, int reg2) { \
    REGISTER_POP(reg1, sp)          \
    REGISTER_POP(reg2, sp)          \
  }

#define THREE_REG_POP_FN(fn, sp)              \
  void fn##_3(int reg1, int reg2, int reg3) { \
    REGISTER_POP(reg1, sp)                    \
    REGISTER_POP(reg2, sp)                    \
    REGISTER_POP(reg3, sp)                    \
  }

#define FOUR_REG_POP_FN(fn, sp)                         \
  void fn##_4(int reg1, int reg2, int reg3, int reg4) { \
    REGISTER_POP(reg1, sp)                              \
    REGISTER_POP(reg2, sp)                              \
    REGISTER_POP(reg3, sp)                              \
    REGISTER_POP(reg4, sp)                              \
  }

#define FIVE_REG_POP_FN(fn, sp)                                   \
  void fn##_5(int reg1, int reg2, int reg3, int reg4, int reg5) { \
    REGISTER_POP(reg1, sp)                                        \
    REGISTER_POP(reg2, sp)                                        \
    REGISTER_POP(reg3, sp)                                        \
    REGISTER_POP(reg4, sp)                                        \
    REGISTER_POP(reg5, sp)                                        \
  }

#define SIX_REG_POP_FN(fn, sp)                                              \
  void fn##_6(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6) { \
    REGISTER_POP(reg1, sp)                                                  \
    REGISTER_POP(reg2, sp)                                                  \
    REGISTER_POP(reg3, sp)                                                  \
    REGISTER_POP(reg4, sp)                                                  \
    REGISTER_POP(reg5, sp)                                                  \
    REGISTER_POP(reg6, sp)                                                  \
  }

#define SEVEN_REG_POP_FN(fn, sp)                                          \
  void fn##_7(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int reg7) {                                                 \
    REGISTER_POP(reg1, sp)                                                \
    REGISTER_POP(reg2, sp)                                                \
    REGISTER_POP(reg3, sp)                                                \
    REGISTER_POP(reg4, sp)                                                \
    REGISTER_POP(reg5, sp)                                                \
    REGISTER_POP(reg6, sp)                                                \
    REGISTER_POP(reg7, sp)                                                \
  }

#define EIGHT_REG_POP_FN(fn, sp)                                          \
  void fn##_8(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int reg7, int reg8) {                                       \
    REGISTER_POP(reg1, sp)                                                \
    REGISTER_POP(reg2, sp)                                                \
    REGISTER_POP(reg3, sp)                                                \
    REGISTER_POP(reg4, sp)                                                \
    REGISTER_POP(reg5, sp)                                                \
    REGISTER_POP(reg6, sp)                                                \
    REGISTER_POP(reg7, sp)                                                \
    REGISTER_POP(reg8, sp)                                                \
  }

#define ONE_REG_PEEK_FN(fn, sp) \
  void fn##_1(int reg1) { REGISTER_PEEK(reg1, sp, 1) }

#define TWO_REG_PEEK_FN(fn, sp)     \
  void fn##_2(int reg1, int reg2) { \
    uint64_t offset = 1;            \
    REGISTER_PEEK(reg1, sp, offset) \
    offset = 2;                     \
    REGISTER_PEEK(reg2, sp, offset) \
  }

#define THREE_REG_PEEK_FN(fn, sp)             \
  void fn##_3(int reg1, int reg2, int reg3) { \
    uint64_t offset = 1;                      \
    REGISTER_PEEK(reg1, sp, offset)           \
    offset = 2;                               \
    REGISTER_PEEK(reg2, sp, offset)           \
    offset = 3;                               \
    REGISTER_PEEK(reg3, sp, offset)           \
  }

#define FOUR_REG_PEEK_FN(fn, sp)                        \
  void fn##_4(int reg1, int reg2, int reg3, int reg4) { \
    uint64_t offset = 1;                                \
    REGISTER_PEEK(reg1, sp, offset)                     \
    offset = 2;                                         \
    REGISTER_PEEK(reg2, sp, offset)                     \
    offset = 3;                                         \
    REGISTER_PEEK(reg3, sp, offset)                     \
    offset = 4;                                         \
    REGISTER_PEEK(reg4, sp, offset)                     \
  }

#define FIVE_REG_PEEK_FN(fn, sp)                                  \
  void fn##_5(int reg1, int reg2, int reg3, int reg4, int reg5) { \
    uint64_t offset = 1;                                          \
    REGISTER_PEEK(reg1, sp, offset)                               \
    offset = 2;                                                   \
    REGISTER_PEEK(reg2, sp, offset)                               \
    offset = 3;                                                   \
    REGISTER_PEEK(reg3, sp, offset)                               \
    offset = 4;                                                   \
    REGISTER_PEEK(reg4, sp, offset)                               \
    offset = 5;                                                   \
    REGISTER_PEEK(reg5, sp, offset)                               \
  }

#define SIX_REG_PEEK_FN(fn, sp)                                             \
  void fn##_6(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6) { \
    uint64_t offset = 1;                                                    \
    REGISTER_PEEK(reg1, sp, offset)                                         \
    offset = 2;                                                             \
    REGISTER_PEEK(reg2, sp, offset)                                         \
    offset = 3;                                                             \
    REGISTER_PEEK(reg3, sp, offset)                                         \
    offset = 4;                                                             \
    REGISTER_PEEK(reg4, sp, offset)                                         \
    offset = 5;                                                             \
    REGISTER_PEEK(reg5, sp, offset)                                         \
    offset = 6;                                                             \
    REGISTER_PEEK(reg6, sp, offset)                                         \
  }

#define SEVEN_REG_PEEK_FN(fn, sp)                                         \
  void fn##_7(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int reg7) {                                                 \
    uint64_t offset = 1;                                                  \
    REGISTER_PEEK(reg1, sp, offset)                                       \
    offset = 2;                                                           \
    REGISTER_PEEK(reg2, sp, offset)                                       \
    offset = 3;                                                           \
    REGISTER_PEEK(reg3, sp, offset)                                       \
    offset = 4;                                                           \
    REGISTER_PEEK(reg4, sp, offset)                                       \
    offset = 5;                                                           \
    REGISTER_PEEK(reg5, sp, offset)                                       \
    offset = 6;                                                           \
    REGISTER_PEEK(reg6, sp, offset)                                       \
    offset = 7;                                                           \
    REGISTER_PEEK(reg7, sp, offset)                                       \
  }

#define EIGHT_REG_PEEK_FN(fn, sp)                                         \
  void fn##_8(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6, \
              int reg7, int reg8) {                                       \
    uint64_t offset = 1;                                                  \
    REGISTER_PEEK(reg1, sp, offset)                                       \
    offset = 2;                                                           \
    REGISTER_PEEK(reg2, sp, offset)                                       \
    offset = 3;                                                           \
    REGISTER_PEEK(reg3, sp, offset)                                       \
    offset = 4;                                                           \
    REGISTER_PEEK(reg4, sp, offset)                                       \
    offset = 5;                                                           \
    REGISTER_PEEK(reg5, sp, offset)                                       \
    offset = 6;                                                           \
    REGISTER_PEEK(reg6, sp, offset)                                       \
    offset = 7;                                                           \
    REGISTER_PEEK(reg7, sp, offset)                                       \
    offset = 8;                                                           \
    REGISTER_PEEK(reg8, sp, offset)                                       \
  }

// Register spill functions
ONE_REG_PUSH_FN(litecfi_register_spill, cfi_context);
TWO_REG_PUSH_FN(litecfi_register_spill, cfi_context);
THREE_REG_PUSH_FN(litecfi_register_spill, cfi_context);
FOUR_REG_PUSH_FN(litecfi_register_spill, cfi_context);
FIVE_REG_PUSH_FN(litecfi_register_spill, cfi_context);
SIX_REG_PUSH_FN(litecfi_register_spill, cfi_context);
SEVEN_REG_PUSH_FN(litecfi_register_spill, cfi_context);
EIGHT_REG_PUSH_FN(litecfi_register_spill, cfi_context);

// Register restore functions
ONE_REG_POP_FN(litecfi_register_restore, cfi_context);
TWO_REG_POP_FN(litecfi_register_restore, cfi_context);
THREE_REG_POP_FN(litecfi_register_restore, cfi_context);
FOUR_REG_POP_FN(litecfi_register_restore, cfi_context);
FIVE_REG_POP_FN(litecfi_register_restore, cfi_context);
SIX_REG_POP_FN(litecfi_register_restore, cfi_context);
SEVEN_REG_POP_FN(litecfi_register_restore, cfi_context);
EIGHT_REG_POP_FN(litecfi_register_restore, cfi_context);

// Register peek functions
/*
ONE_REG_PEEK_FN(litecfi_register_peek, cfi_context);
TWO_REG_PEEK_FN(litecfi_register_peek, cfi_context);
THREE_REG_PEEK_FN(litecfi_register_peek, cfi_context);
FOUR_REG_PEEK_FN(litecfi_register_peek, cfi_context);
FIVE_REG_PEEK_FN(litecfi_register_peek, cfi_context);
SIX_REG_PEEK_FN(litecfi_register_peek, cfi_context);
SEVEN_REG_PEEK_FN(litecfi_register_peek, cfi_context);
EIGHT_REG_PEEK_FN(litecfi_register_peek, cfi_context);
*/

// Register context save functions
ONE_REG_PUSH_FN(litecfi_ctx_save, user_context);
TWO_REG_PUSH_FN(litecfi_ctx_save, user_context);
THREE_REG_PUSH_FN(litecfi_ctx_save, user_context);
FOUR_REG_PUSH_FN(litecfi_ctx_save, user_context);
FIVE_REG_PUSH_FN(litecfi_ctx_save, user_context);
SIX_REG_PUSH_FN(litecfi_ctx_save, user_context);
SEVEN_REG_PUSH_FN(litecfi_ctx_save, user_context);
EIGHT_REG_PUSH_FN(litecfi_ctx_save, user_context);

// Register context restore functions
ONE_REG_POP_FN(litecfi_ctx_restore, user_context);
TWO_REG_POP_FN(litecfi_ctx_restore, user_context);
THREE_REG_POP_FN(litecfi_ctx_restore, user_context);
FOUR_REG_POP_FN(litecfi_ctx_restore, user_context);
FIVE_REG_POP_FN(litecfi_ctx_restore, user_context);
SIX_REG_POP_FN(litecfi_ctx_restore, user_context);
SEVEN_REG_POP_FN(litecfi_ctx_restore, user_context);
EIGHT_REG_POP_FN(litecfi_ctx_restore, user_context);

// Register context peek functions
/*
ONE_REG_PEEK_FN(litecfi_ctx_peek, user_context);
TWO_REG_PEEK_FN(litecfi_ctx_peek, user_context);
THREE_REG_PEEK_FN(litecfi_ctx_peek, user_context);
FOUR_REG_PEEK_FN(litecfi_ctx_peek, user_context);
FIVE_REG_PEEK_FN(litecfi_ctx_peek, user_context);
SIX_REG_PEEK_FN(litecfi_ctx_peek, user_context);
SEVEN_REG_PEEK_FN(litecfi_ctx_peek, user_context);
EIGHT_REG_PEEK_FN(litecfi_ctx_peek, user_context);
*/

