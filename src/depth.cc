
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Use a macro to quickly tweak global variable vs TLS variables */
#define TLS_VAR __thread 
//#define TLS_VAR

#define STACK_SIZE 1024

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
  if (__lib_depth == 0) {
    fprintf(stderr, "shadow stack goes below 0 frame.\n");
    fprintf(stderr, "Something strange is happening.\n");
  }
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
