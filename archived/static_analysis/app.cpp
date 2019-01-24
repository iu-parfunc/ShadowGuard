
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>

#include "cycle.h"

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

int bar() {
  return 42;
}

int foo() {
  bar();
}

int g() {
  return 42;
}

int f() {
  g();
}

int main() {

  uint64_t stack_ptr = 0x100000000; 

  uint64_t* addr = (uint64_t*) mmap((void*) stack_ptr, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE| MAP_ANONYMOUS|MAP_FIXED, -1, 0); 

  addr[0] = (uint64_t) (addr + 1);
  addr[1] = 0;
  addr[2] = 0;

  if (addr == MAP_FAILED)
    handle_error("mmap");

  printf("Address : %p\n", addr);

  assert((uint64_t) addr == stack_ptr);

  long iters = 1000000000;
  ticks start = getticks();
  for (long i=0; i < iters; i++) {
    foo();
  }
  ticks end = getticks();

  ticks scycles = end - start;

  start = getticks();
  for (long i=0; i < iters; i++) {
    f();
  }

  end = getticks();

  ticks ncycles = end - start;

  printf("Regular cycles : %llu\n", ncycles / iters);
  printf("Shadow stack cycles : %llu\n", scycles / iters);
}


