

all: monitor

ARGS=-Ideps/elph/include/

DEP=deps/elph/libelph.a

CC = g++ -std=c++11  $(ARGS)

monitor: preload.c tracee.c
	$(CC) -g -fPIC -c -Wall -o preload.o preload.c 
	$(CC) -shared -Wl,-soname,libmonitor.so -Wl,-init,boostrap -o libmonitor.so preload.o -L/home/budkahaw/Installations/dyninst/lib $(DEP)
	$(CC) -g -Wall -o tracee tracee.c -pthread # -L. -lmonitor

temp: pmain.c main.c
	gcc -g -Wall -o main main.c -pthread
	gcc -g -fPIC -c -Wall -o pmain.o pmain.c 
	gcc -shared -Wl,-soname,libp.so -Wl,-init,boostrap -o libp.so pmain.o -pthread

clean:
	rm -rf *.o tracee main *.so
