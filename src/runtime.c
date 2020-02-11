#define _GNU_SOURCE
#include <asm/prctl.h>
#include <bits/pthreadtypes.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#undef pthread_create

#define CONSTRUCTOR(priority) __attribute__((constructor(priority)))

#ifdef __cplusplus
extern "C" {
#endif

static const long long __stack_sz = 8 * 1024 * 1024;  // 8 MB

// Sets up the thread shadow stack.
//
// Format of the stack is
//
//           |   .   |
//           |   .   |
//           ---------
//           |  RA1  | First stack entry
//           ---------
//           |  0x0  | Guard Word [16 bytes](To catch underflows)
//           ---------
//           |  0x0  | Sratch space for register frame
//           ---------
// gs:0x0 -> |  SP   | Stack Pointer
//           ---------
CONSTRUCTOR(0) static void __shadow_guard_init_stack() {
  unsigned long addr = (unsigned long)malloc(__stack_sz);
  if (syscall(SYS_arch_prctl, ARCH_SET_GS, addr) < 0)
    abort();
  addr += 8;
  *((unsigned long *)addr) = 0;
  addr += 8;
  *((unsigned long *)addr) = 0;
  addr += 8;
  *((unsigned long *)addr) = 0;
  addr += 8;

  asm volatile("mov %0, %%gs:0\n\t" : : "a"(addr) :);
}

typedef void *(*pthread_fn_type)(void *);

typedef struct __pthread_fn_info {
  void *original_arg;
  pthread_fn_type original_fn;
} pthread_fn_info;

static void *__pthread_fn_wrapper(void *arg) {
  __shadow_guard_init_stack();

  pthread_fn_info *info = (void *)arg;

  return info->original_fn(info->original_arg);
}

int pthread_create(pthread_t *thread, pthread_attr_t *attr, pthread_fn_type fn,
                   void *arg) {
  typedef int (*pthread_create_type)(pthread_t *, pthread_attr_t *,
                                     pthread_fn_type fn, void *);
  static pthread_create_type real_create = NULL;
  if (!real_create)
    real_create = (pthread_create_type)dlsym(RTLD_NEXT, "pthread_create");

  pthread_fn_info *info = (pthread_fn_info *)malloc(sizeof(pthread_fn_info));
  info->original_arg = arg;
  info->original_fn = (pthread_fn_type)fn;

  return real_create(thread, attr, __pthread_fn_wrapper, info);
}

#ifdef __cplusplus
}  // extern "C"
#endif
