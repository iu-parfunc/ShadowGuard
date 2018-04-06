#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/wait.h>

void error_and_die(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
  int r;

  const char *memname = "sample";
  const size_t region_size = sysconf(_SC_PAGE_SIZE);

  int fd = shm_open(memname, O_CREAT | O_TRUNC | O_RDWR, 0666);
  if (fd == -1)
    error_and_die("shm_open");

  r = ftruncate(fd, region_size);
  if (r != 0)
    error_and_die("ftruncate");

  void *ptr = mmap(0, region_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (ptr == MAP_FAILED)
    error_and_die("mmap");
  close(fd);

  pid_t pid = fork();

  if (pid == 0) {
    u_long *d = (u_long *) ptr;
    printf("[child] Setting read only permissions to the child..\n");
    mprotect((void*) ptr, region_size, PROT_READ);
    printf("[child] Child going to write..\n");
    *d = 0xdbeebee;
    printf("[child] Child done writing..\n");
    exit(0);
  }
  else {
    int status;
    waitpid(pid, &status, 0);
    printf("[parent] child write failed with value not written %#lx\n",
        *(u_long *) ptr);

    printf("[parent] Setting read and write permissions to the parent..\n");
    mprotect((void*) ptr, region_size, PROT_READ | PROT_WRITE);

    u_long *d = (u_long *) ptr;
    *d = 0xdbeebee;
    printf("[parent] parent wrote %#lx\n",
        *(u_long *) ptr);
  }

  r = munmap(ptr, region_size);
  if (r != 0)
    error_and_die("munmap");

  r =
    shm_unlink(memname);
  if (r != 0)
    error_and_die("shm_unlink");

  return 0;
}
