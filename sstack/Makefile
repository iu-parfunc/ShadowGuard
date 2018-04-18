
DEPS = /home/budkahaw/Installations/asmjit/lib
INCLUDES = /home/budkahaw/Installations/asmjit/include

all: bench.cpp
	g++ -std=c++11 -g -o bench bench.cpp -I$(INCLUDES) -L$(DEPS) -lasmjit

clean:
	rm bench
