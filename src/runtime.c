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

#include <stdatomic.h>

/***********************************************
 *   Begin of a simple spinlock implementation 
 *   Code is copied from HPCToolkit 
 *   (https://github.com/HPCToolkit/hpctoolkit)
 **********************************************/

typedef struct spinlock_s {
        atomic_long thelock;
} spinlock_t;

#define SPINLOCK_UNLOCKED_VALUE (-1L)
#define SPINLOCK_LOCKED_VALUE (1L)
#define INITIALIZE_SPINLOCK(x) { .thelock = ATOMIC_VAR_INIT(x) }

#define SPINLOCK_UNLOCKED INITIALIZE_SPINLOCK(SPINLOCK_UNLOCKED_VALUE)
#define SPINLOCK_LOCKED INITIALIZE_SPINLOCK(SPINLOCK_LOCKED_VALUE)

static spinlock_t thread_lock = SPINLOCK_UNLOCKED;

static inline
void 
spinlock_unlock(spinlock_t *l)
{
  atomic_store_explicit(&l->thelock, SPINLOCK_UNLOCKED_VALUE, memory_order_release);
}

static inline
void 
spinlock_lock(spinlock_t *l)
{
  /* test-and-set lock */
  long old_lockval = SPINLOCK_UNLOCKED_VALUE;
  while (!atomic_compare_exchange_weak_explicit(&l->thelock, &old_lockval, SPINLOCK_LOCKED_VALUE,
              memory_order_acquire, memory_order_relaxed)) {
    old_lockval = SPINLOCK_UNLOCKED_VALUE;
  }
}

/***********************************************
 *   End of a simple spinlock implementation 
 **********************************************/

#undef pthread_create

#define CONSTRUCTOR(priority) __attribute__((constructor(priority)))

#ifdef __cplusplus
extern "C" {
#endif

static const long long __stack_sz = 8 * 1024 * 1024;  // 8 MB

// The default stack size of a thread is 8MB.
// However, since we do not get the actual stack top,
// we limit the stack size to 6MB for safety
static const int thread_stack_size = 6 * 1024 * 1024; 
static void* global_stack_lower_bound = NULL;


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
//           |  0x0  | Global stack bottom
//           ---------
//           |  0x0  | Local stack bottom
//           ---------
//           |  0x0  | Local stack top
//           ---------
//           |  0x0  | Sratch space for register frame
//           ---------
// gs:0x0 -> |  SP   | Stack Pointer
//           ---------
//
//  Currently, we allow write to address range
//  [0, global stack bottom) and [local stack bottom, local stack top)
//
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
  
  char* stack_end = (char*) (&info);
  char* stack_bottom = stack_end + thread_stack_size;
  asm volatile("mov %0, %%gs:16\n\t" : : "a"(stack_end) :);
  asm volatile("mov %0, %%gs:24\n\t" : : "a"(stack_bottom) :);

  // The global stack bottom is a shared variable among
  // all threads, so we need a lock to update it
  spinlock_lock(&thread_lock);
  if ((uint64_t)stack_bottom < (uint64_t)global_stack_lower_bound)
      global_stack_lower_bound = (void*)stack_bottom;
  asm volatile("mov %0, %%gs:32\n\t" : : "a"(global_stack_lower_bound) :);
  spinlock_unlock(&thread_lock);

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

typedef int (*libc_start_main_fn_type) (int *(main) (int, char * *, char * *), 
        int argc, 
        char * * ubp_av, 
        void (*init) (void), 
        void (*fini) (void), 
        void (*rtld_fini) (void), 
        void (* stack_end));

int __libc_start_main(int *(main) (int, char * *, char * *), int argc, char * * ubp_av, void (*init) (void), void (*fini) (void), void (*rtld_fini) (void), void (* stack_end)) {
  static libc_start_main_fn_type real_libc_start_main = NULL;
  char* stack_bottom = (char*) stack_end + thread_stack_size;
  if (!real_libc_start_main)
    real_libc_start_main = (libc_start_main_fn_type) dlsym(RTLD_NEXT, "__libc_start_main");
  global_stack_lower_bound = (void*) stack_bottom;

  asm volatile("mov %0, %%gs:16\n\t" : : "a"(stack_end) :);
  asm volatile("mov %0, %%gs:24\n\t" : : "a"(stack_bottom) :);
  asm volatile("mov %0, %%gs:32\n\t" : : "a"(global_stack_lower_bound) :);

  return real_libc_start_main(main, argc, ubp_av, init, fini, rtld_fini, stack_end);
}

#ifdef __cplusplus
}  // extern "C"
#endif
