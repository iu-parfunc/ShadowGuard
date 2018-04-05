
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#define NUM_THREADS 5

// extern void dummy();

void bar() {
  printf("Bar..\n");
  // dummy();
}

int foo() {
  asm(".long 0x90909090");
  asm(".long 0x90909090");
  printf("Foo..\n");
  return 45;
}

void* worker(void* a) {
  while(1) {
    foo();
  }
}

int main() {
  // printf("Inside child..\n");
  pthread_t threads[NUM_THREADS];
  int rc;
  long t;
  for(t=0; t<NUM_THREADS; t++){
    printf("In main: creating thread %ld\n", t);
    rc = pthread_create(&threads[t], NULL, worker, (void *)t);
    if (rc){
      printf("ERROR; return code from pthread_create() is %d\n", rc);
      return -1;
    }
  }

  void* status;
  for(t=0; t<NUM_THREADS; t++){
    pthread_join(threads[t], &status);
  }

  return 0;

}
