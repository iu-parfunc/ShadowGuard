
/*
  Authors: Buddhika Chamith
  year : 2018

  Sets up the secure execution enviornment where the application will be 
  executed. Application will be run as a child process under the monitor
  process. Quite a bit is happening here. The main goals of the the secure
  execution enviornment are
  
  1. The monitor process may modify the application code at runtime.
  2. During code modifications application threads should not see any
     permission change in code pages (i.e: see them as writeable). If that is
     the case an attacker can overwrite executable code and all bets will be
     off.
  3. Code modifications should not halt any application thread since it hinders
     performance. 

  Implmentation Details
  ---------------------

  In order to achieve 1 & 2 we use ptrace API to poke in to the child process 
  memory. Ptrace uses internal kernel memory page mappings to overwrite code 
  pages without modifying application visible memory page permissions [1]. 

  We LD_PRELOAD our library and in the library constructor we self spawn 
  the application as a child to the current application instance. Then we 
  ptrace attach to the child from our parent instance. Parent instance acts as
  the monitor and never leaves the library constructor (i.e: once the child is
  done it simply exits without continuing on to the application code). We could
  have made monitor a separate application under which the user application is 
  run. But current scheme makes it more seamless. Just LD_PRELOAD our library
  and you are set.

  Achieving 3 is bit tricky. Ptrace allows attaching to separate threads in a 
  multi-threaded process. Only attached thread will be stopped in signal stops
  and waited on by the tracer. Our goal is to be able to periodically make 
  modifications to the application code without stopping any application
  threads. So what we do is to inject an extra thread to which we can attach to
  from our monitor. And we periodically send SIGTOP from that thread so that
  monitor will be notified and then will be take necessary actions to inject 
  code as required. We inject our library as LD_PRELOAD in our child as well. 
  But in addition we inject extra environment variable IN_CHILD so that child can 
  determine it is in fact the child process instance (aka secure execution 
  environment). Inside the forked child process we check for this and if that's
  the case we spawn the extra thread inside the library constructor and 
  then continue on to executing the application code.

  [1] https://stackoverflow.com/questions/49442087/how-does-ptrace-poketext-works-when-modifying-program-text  

*/

#include <string>

#include <signal.h>
#include <sys/ptrace.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cstdint>
#include <dirent.h>
#include <pthread.h>
#include <errno.h>

#include "elf64.h"
#include "sym_tab.h"
#include "elf64_shr.h"

void dummy() {

}

void* findFunction(char* program, int pid, char* name) {
  // printf("Program : %s\n", program);
  ELF *bin = elf64_read((char*) program);
  unsigned int nb_sym = bin->symtab_num;
  Elf64_Sym **tab = bin->symtab;
  Elf64_Half strtab_idx = get_section_idx(bin, ".strtab");
  Elf64_Off strtab_offset = bin->shr[strtab_idx]->sh_offset;

  for (unsigned int i=0; i < nb_sym; i++) {
    if ((tab[i]->st_info & 0x0F) == STT_FUNC) {
      char* s_name = get_sym_name(bin->file, tab[i], strtab_offset);

      // printf("Name : %s Program : %s\n", s_name, program);

      if (!strcmp(s_name, name)) {
	char* addr = (char*) tab[i]->st_value;

	free(s_name);
	fclose(bin->file);
	free_elf64(bin);

	return addr;
      }

      free(s_name);
    }
  }

  fclose(bin->file);
  free_elf64(bin);
}

void print_word(long res) {
  char *datap = (char *)&res;

  if (res == -1)
    printf("Error..\n");
  //check errno for errors
  else {
    printf("%x %x %x %x\n", datap[0], datap[1], datap[2], datap[3]);
  }
}

pid_t *gettids(const pid_t pid, size_t *const countptr)
{
    char           dirbuf[128];
    DIR           *dir;
    struct dirent *ent;

    pid_t         *data = NULL, *temp;
    size_t         size = 0;
    size_t         used = 0;

    int            tid;
    char           dummy;

    if ((int)pid < 2) {
        errno = EINVAL;
        return NULL;
    }

    if (snprintf(dirbuf, sizeof dirbuf, "/proc/%d/task/", (int)pid) >= (int)sizeof dirbuf) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    dir = opendir(dirbuf);
    if (!dir)
        return NULL;

    while (1) {
        errno = 0;
        ent = readdir(dir);
        if (!ent)
            break;

        if (sscanf(ent->d_name, "%d%c", &tid, &dummy) != 1)
            continue;

        if (tid < 2)
            continue;

        if (used >= size) {
            size = (used | 127) + 129;
            temp = (pid_t*) realloc(data, size * sizeof data[0]);
            if (!temp) {
                free(data);
                closedir(dir);
                errno = ENOMEM;
                return NULL;
            }
            data = temp;
        }

        data[used++] = (pid_t)tid;
    }
    if (errno) {
        free(data);
        closedir(dir);
        errno = EIO;
        return NULL;
    }
    if (closedir(dir)) {
        free(data);
        errno = EIO;
        return NULL;
    }

    if (used < 1) {
        free(data);
        errno = ENOENT;
        return NULL;
    }

    size = used + 1;
    temp = (pid_t*) realloc(data, size * sizeof data[0]);
    if (!temp) {
        free(data);
        errno = ENOMEM;
        return NULL;
    }
    data = temp;

    data[used] = (pid_t)0;

    if (countptr)
        *countptr = used;

    errno = 0;
    return data;
}

void* attachee_thread(void* t) {
  while(1) {
    sleep(1);
    printf("Running attachee..\n");
    kill(getpid(), SIGSTOP);
  }
}

__attribute__((constructor))
  void boostrap() {

    char* in_child = getenv("IN_CHILD");
    pthread_t t;
    if (in_child) {
      printf("[INFO] In secure execution enviornment..\n");

      int rc = pthread_create(&t, NULL, attachee_thread, (void *)NULL);
      if (rc){
        printf("[FATAL] Failed to setup secure enviornment..\n");
	exit(-1);
      }
      return;
    }

    char* c_program_path = (char*) malloc(sizeof(char) * MAXPATHLEN);
    ssize_t len = 0;
    if (c_program_path != NULL) {
      if ((len = readlink("/proc/self/exe", c_program_path,
	      sizeof(char) * MAXPATHLEN)) == -1) {
	free(c_program_path);
	return;
      }
    }
    c_program_path[len] = '\0';

    int pid, status;
    pid = fork();

    char* foo = NULL; //  = (char*)findFunction(c_program_path, pid, "_Z3foov");
    char* bar = NULL; // = (char*)findFunction(c_program_path, pid, "_Z3barv");

    if (pid == -1) {
      perror(0);
      exit(1);
    }

    if(pid == 0) {
      printf("Inside the child  %llu\n", getpid());
      char* lib_paths     = getenv("LD_PRELOAD");

      if (!lib_paths) {
	printf("[FATAL] Works only in LD_PRELOAD mode..\n");
	exit(0);
      }

      int in_child_str_len   = 8;
      int ld_preload_str_len = 10;
      int buf_size, written;

      buf_size         = strlen(lib_paths) + ld_preload_str_len + 2;
      char* ld_preload = (char*) malloc(buf_size);
      written          = snprintf(ld_preload , buf_size, "LD_PRELOAD=%s", lib_paths);  

      if (written < 0 || written > buf_size) {
	printf("[FATAL] Error in setting up child process enviornment..\n");
        exit(1);
      }

      buf_size         = in_child_str_len + 3;
      char* in_child   = (char*) malloc(buf_size);
      written          = snprintf(in_child, buf_size, "IN_CHILD=1");  

      char* const envp[3] = {ld_preload , in_child, NULL};

      printf("%s\n%s\n", envp[0], envp[1]);

      ptrace(PTRACE_TRACEME, 0, 0, 0);
      printf("Spawning child..\n");
      execve(c_program_path, 0, envp);
    } else {
      char* foo = (char*)findFunction(c_program_path, pid, "_Z3foov"); 
      char* bar = (char*)findFunction(c_program_path, pid, "_Z3barv");

      printf("[INFO] Parent waiting for child..\n");
      printf("[INFO] Parent PID : %llu\n", getpid());

      bool reattached = false;
      while (waitpid(pid, &status, 0) && !WIFEXITED(status)) {
	if (!reattached) {
          size_t tids = 0;
          pid_t* tid  = gettids(pid, &tids); 

          printf("[INFO] Child process pid : %ld\n", pid);
          printf("[INFO] Number of threads : %ld\n", tids);

	  if (tids == 1) {
            printf("[INFO] Not yet reattached..\n");
	    ptrace(PTRACE_CONT, pid, 0, 0);
            continue;
	  }

	  if (tids == 2) {
            for (int k = 0; k < (int)tids; k++) {
              const pid_t t = tid[k];
              printf("[INFO] Thread id : %ld\n", t);

	      if (t != pid) { // Attach to the thread that we spawned
                if (ptrace(PTRACE_ATTACH, t, (void *)0L, (void *)0L)) {
                  fprintf(stderr, "Cannot attach to TID %d: %s.\n", (int)t, strerror(errno));
		  exit(0);
	        }
	      }
            }

            printf("[INFO] Reattached..\n");
            reattached = true;
	    ptrace(PTRACE_DETACH, pid, 0, 0); // PTRACE_DETACH is a restarting operation.
	                                      // No need to do a PTRACE_CONT

	    // ptrace(PTRACE_CONT, pid, 0, 0);
	    continue;
	  }
	}

	printf("Parent doing stuff..\n");

	printf("Foo address : %p\n", foo);
	printf("Bar address : %p\n", bar);
	printf("Nop padding start : %p\n", foo +4);

	int relative = bar- (foo + 4) - 5;

	printf("Relative : %p\n", relative);

	unsigned long call = 0x9090909090909090;

	*reinterpret_cast<uint8_t*>(&call) = 0xe8;
	*reinterpret_cast<int32_t*>(&(reinterpret_cast<uint8_t*>(&call)[1])) =
	  relative;
	long original = *((long*)(foo + 4)) | 0xFFFFFF0000000000; 
	long patch    = original | call;

	printf("Call : %08x\n", call); 
	printf("Patch : %08x\n", patch); 

	long res = ptrace(PTRACE_PEEKTEXT, pid, foo + 4, NULL);
	print_word(res);

	res = ptrace(PTRACE_POKETEXT, pid, foo + 4, call);
	if (res != 0) {
	  printf("Some thing went real wrong!!\n");
	}

	res = ptrace(PTRACE_PEEKTEXT, pid, foo + 4, NULL);
	print_word(res);

	res = ptrace(PTRACE_PEEKTEXT, pid, foo + 8, NULL);
	print_word(res);

	ptrace(PTRACE_CONT, pid, 0, 0);
      }

      exit(0);
    }
  }
