CFLAGS = -O0 -std=gnu++0x -g
# CFLAGS = -ggdb -O0 -std=gnu++0x 
DYNINST_INSTALL=/home/budkahaw/Installations/dyninst
ASMJIT_INSTALL=/home/budkahaw/Installations/asmjit
INC = -I$(DYNINST_INSTALL)/include  -I$(ASMJIT_INSTALL)/include
LIB = -L$(DYNINST_INSTALL)/lib  -L$(ASMJIT_INSTALL)/lib
DEP = -lparseAPI -linstructionAPI -lsymtabAPI -lelf -ldynDwarf -ldw -lelf -ldynElf -lcommon -lpatchAPI -ldyninstAPI -lasmjit
all: ConstantPropagation JumpTable Liveness StackAnalysis

CG: CG.cpp
	g++ $(CFLAGS) $(INC) $(LIB) $^ -o $@ $(DEP)

PatchNOP: PatchNOP.cpp 
	g++ $(CFLAGS) $(INC) $(LIB) $^ -o $@ $(DEP)


StackInstrumentation: StackInstrumentation.cpp 
	g++ $(CFLAGS) $(INC) $(LIB) $^ -o $@ $(DEP)

RegisterInstrumentation: RegisterInstrumentation.cpp 
	g++ $(CFLAGS) $(INC) $(LIB) $^ -o $@ $(DEP)

stack: StackInstrumentation 
	./StackInstrumentation app

register: RegisterInstrumentation 
	./RegisterInstrumentation app

do_analysis: CG
	./CG hmmer

app: app.cpp
	gcc $(CFLAGS) -o app app.cpp

test: app stack register 

clean:
	rm -f CG PatchNOP mutant *.so.6 *.so *.cfg *.so.* app
