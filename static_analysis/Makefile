CFLAGS = -O0 -std=gnu++0x -g
# CFLAGS = -ggdb -O0 -std=gnu++0x 
DYNINST_INSTALL=/home/budkahaw/Installations/dyninst
INC = -I$(DYNINST_INSTALL)/include 
LIB = -L$(DYNINST_INSTALL)/lib 
DEP = -lparseAPI -linstructionAPI -lsymtabAPI -lelf -ldynDwarf -ldw -lelf -ldynElf -lcommon -lpatchAPI -ldyninstAPI
all: ConstantPropagation JumpTable Liveness StackAnalysis

CG: CG.cpp
	g++ $(CFLAGS) $(INC) $(LIB) $^ -o $@ $(DEP)

PatchNOP: PatchNOP.cpp
	g++ $(CFLAGS) $(INC) $(LIB) $^ -o $@ $(DEP)

patch_app: PatchNOP
	./PatchNOP hmmer

do_analysis: CG
	./CG hmmer

clean:
	rm -f CG PatchNOP mutant *.so.6 *.so *.cfg *.so.*
