#include <asm/prctl.h>
#include <benchmark/benchmark.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static void init_shadow_stack() {
  unsigned long addr = (unsigned long)malloc(1024);
  if (syscall(SYS_arch_prctl, ARCH_SET_GS, addr) < 0) abort();
  addr += 8;
  asm volatile("mov %0, %%gs:0;" : : "a"(addr) :);
}

static void BM_ShadowStackPush(benchmark::State& state) {
  init_shadow_stack();
  for (auto _ : state) {
    asm volatile(
        "push %%rax\n\t"
        "push %%rcx\n\t"
        "mov 16(%%rsp), %%rcx\n\t"
        "mov %%gs:0, %%rax\n\t"
        "addq $0, %%gs:0\n\t"
        "mov %%rcx, (%%rax)\n\t"
        "pop %%rcx\n\t"
        "pop %%rax\n\t"
        :
        :
        : "memory");
  }
}
BENCHMARK(BM_ShadowStackPush);

static void BM_ShadowStackPushConditional(benchmark::State& state) {
  init_shadow_stack();
  asm("xorq %%rdi, %%rdi\n\t" : : :);
  asm("xorq %%rax, %%rax\n\t" : : :);
  asm("xorq %%rcx, %%rcx\n\t" : : :);
  for (auto _ : state) {
    asm volatile(
        "cmp %%rsp, %%rdi\n\t"
        "jl %=f\n\t"
        "int3\n\t"
        "push %%rax\n\t"
        "push %%rcx\n\t"
        "mov 16(%%rsp), %%rcx\n\t"
        "mov %%gs:0, %%rax\n\t"
        "addq $0, %%gs:0\n\t"
        "mov %%rcx, (%%rax)\n\t"
        "pop %%rcx\n\t"
        "pop %%rax\n\t"
        "%=:\n\t"
        :
        :
        : "memory");
  }
}
BENCHMARK(BM_ShadowStackPushConditional);

static void BM_ShadowStackPushNoCtxSave(benchmark::State& state) {
  init_shadow_stack();
  for (auto _ : state) {
    asm volatile(
        "mov 16(%%rsp), %%rcx\n\t"
        "mov %%gs:0, %%rax\n\t"
        "addq $0, %%gs:0\n\t"
        "mov %%rcx, (%%rax)\n\t"
        :
        :
        : "memory");
  }
}
BENCHMARK(BM_ShadowStackPushNoCtxSave);

static void BM_ShadowStackPopNoCtxSave(benchmark::State& state) {
  init_shadow_stack();
  for (auto _ : state) {
    asm volatile(
        "mov %%gs:0, %%rax\n\t"
        "mov -8(%%rax), %%rcx\n\t"
        "subq $0, %%gs:0\n\t"
        "cmp 16(%%rsp), %%rcx\n\t"
        :
        :
        : "memory");
  }
}
BENCHMARK(BM_ShadowStackPopNoCtxSave);

static void BM_ShadowStackPop(benchmark::State& state) {
  init_shadow_stack();
  for (auto _ : state) {
    asm volatile(
        "push %%rax\n\t"
        "push %%rcx\n\t"
        "mov %%gs:0, %%rax\n\t"
        "mov -8(%%rax), %%rcx\n\t"
        "subq $0, %%gs:0\n\t"
        "cmp 16(%%rsp), %%rcx\n\t"
        "pop %%rcx\n\t"
        "pop %%rax\n\t"
        :
        :
        : "memory");
  }
}
BENCHMARK(BM_ShadowStackPop);
