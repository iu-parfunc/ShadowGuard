benchmark = /home/buddhika/Builds/benchmark/build
all:
	g++ -std=c++11 -isystem $(benchmark)/../include -L$(benchmark)/src shadow_stack.cc -o shadow_stack -lbenchmark -lbenchmark_main -lpthread

clean:
	rm -f shadow_stack
