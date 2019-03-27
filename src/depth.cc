
#include <stdint.h>
#include <stdio.h>

static __thread uint64_t __lib_depth = 0;
static __thread uint64_t __lib_max_depth = 0;
static __thread uint64_t __lib_overflows = 0;

void _litecfi_inc_depth(int32_t stack_size) {
  __lib_depth++;
  if (__lib_depth > __lib_max_depth) {
    __lib_max_depth = __lib_depth;
  }

  if (__lib_depth > (unsigned int)stack_size) {
    __lib_overflows++;
  }
}

void _litecfi_sub_depth(int32_t stack_size) { __lib_depth--; }

void _litecfi_print_stats(int32_t stack_size) {
  printf("[Statistics] Stack size : %d\n", stack_size);
  printf("[Statistics] Maximum call stack depth : %lu\n", __lib_max_depth);
  printf("[Statistics] Number of overflows : %lu\n", __lib_overflows);
}
