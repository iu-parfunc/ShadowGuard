
#include "assert.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Use a macro to quickly tweak global variable vs TLS variables */
#define TLS_VAR __thread
//#define TLS_VAR

#define STACK_SIZE 1512

static TLS_VAR uint64_t __lib_depth = 0;
static TLS_VAR uint64_t __lib_max_depth = 0;
static TLS_VAR uint64_t __lib_overflows = 0;

static TLS_VAR uint64_t __lib_stack[STACK_SIZE] = {0};
static TLS_VAR uint64_t __lib_stack_snapshot[STACK_SIZE] = {0};

void _litecfi_inc_depth(int32_t stack_size, int32_t capture_at) {
  uint64_t* return_addr;

  asm("movq %%r10, %0;\n\t" : "=a"(return_addr) : :);

  if (__lib_depth < STACK_SIZE)
    __lib_stack[__lib_depth] = *return_addr;

  // printf("%p\n", __lib_stack[__lib_depth]);

  if (__lib_stack[__lib_depth] == 0) {
    printf("(nil) at stack depth : %lu\n", __lib_depth);
  }

  __lib_depth++;

  if (__lib_depth > __lib_max_depth) {
    __lib_max_depth = __lib_depth;
    memset(__lib_stack_snapshot, 0, STACK_SIZE * 8);
    memcpy(__lib_stack_snapshot, __lib_stack, (size_t)__lib_max_depth * 8);
  }

  if (__lib_depth > (unsigned int)stack_size) {
    __lib_overflows++;
  }
}

void _litecfi_sub_depth(int32_t stack_size, int32_t capture_at) {
  uint64_t* return_addr;

  asm("movq %%r10, %0;\n\t" : "=a"(return_addr) : :);

  if (__lib_depth == 0) {
    fprintf(stderr, "shadow stack goes below 0 frame.\n");
    fprintf(stderr, "Something strange is happening.\n");
  }
  __lib_depth--;
  /*
  if (__lib_depth < STACK_SIZE && __lib_depth > 1) {
    if (__lib_stack[__lib_depth] != *return_addr) {
      fprintf(stderr, "return address does not match: RA on stack %p, RA in
  __lib_stack %p\n", *return_addr, __lib_stack[__lib_depth]);
    }
  }
  */
}

void _litecfi_print_stats(int32_t stack_size, int32_t capture_at) {
  printf("[Statistics] Stack size : %d\n", stack_size);
  printf("[Statistics] Maximum call stack depth : %lu\n", __lib_max_depth);
  printf("[Statistics] Number of overflows : %lu\n\n", __lib_overflows);
  printf("[Statistics] Stack trace at depth %lu : \n", __lib_max_depth);

  assert(__lib_max_depth < STACK_SIZE);

  // Print one element past the stack snapshot (which should be null) for
  // visual verification that we got all the elements printed out
  for (unsigned long i = 0; i <= __lib_max_depth; i++) {
    printf("   %p\n", (void*)__lib_stack_snapshot[i]);
  }
}
