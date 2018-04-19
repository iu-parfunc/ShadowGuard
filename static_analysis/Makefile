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

patch_app: PatchNOP
	./PatchNOP hmmer

do_analysis: CG
	./CG hmmer

app: app.cpp
	gcc $(CFLAGS) -o app app.cpp

test: PatchNOP app
	./PatchNOP app

clean:
	rm -f CG PatchNOP mutant *.so.6 *.so *.cfg *.so.* app
