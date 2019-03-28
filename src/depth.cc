
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static __thread uint64_t __lib_depth = 0;
static __thread uint64_t __lib_max_depth = 0;
static __thread uint64_t __lib_overflows = 0;

static __thread uint64_t __lib_stack[128] = {0};
static __thread uint64_t __lib_stack_snapshot[128] = {0};

void _litecfi_inc_depth(int32_t stack_size, int32_t capture_at) {
  uint64_t return_addr;

  asm("movq %%r10, %0;\n\t" : "=a"(return_addr) : :);

  __lib_stack[__lib_depth] = return_addr;

  if (__lib_depth == (uint64_t)capture_at) {
    memcpy(__lib_stack_snapshot, __lib_stack, (size_t)capture_at * 8);
  }

  __lib_depth++;

  if (__lib_depth > __lib_max_depth) {
    __lib_max_depth = __lib_depth;
  }

  if (__lib_depth > (unsigned int)stack_size) {
    __lib_overflows++;
  }
}

void _litecfi_sub_depth(int32_t stack_size, int32_t capture_at) {
  __lib_depth--;
}

void _litecfi_print_stats(int32_t stack_size, int32_t capture_at) {
  printf("[Statistics] Stack size : %d\n", stack_size);
  printf("[Statistics] Maximum call stack depth : %lu\n", __lib_max_depth);
  printf("[Statistics] Number of overflows : %lu\n\n", __lib_overflows);
  printf("[Statistics] Stack trace at depth %d : \n", capture_at);

  for (int i = 0; i < capture_at; i++) {
    printf("   %p\n", __lib_stack_snapshot[i]);
  }
}
