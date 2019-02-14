
#include "BPatch.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "CodeObject.h"
#include "parse.h"

void PrintEdgeInfo(Dyninst::ParseAPI::Edge* edge) {
  printf("Target : %lx\n", edge->trg()->start());
  std::vector<Dyninst::ParseAPI::Function*> functions;
  edge->trg()->getFuncs(functions);

  printf("Functions \n");
  for (auto function : functions) {
    printf("%s ", function->name().c_str());
  }
  printf("\n");

  printf("Edge type : ");
  if (edge->interproc()) {
    switch (edge->type()) {
      case Dyninst::ParseAPI::EdgeTypeEnum::CALL:
        printf("CALL\n");
        break;
      case Dyninst::ParseAPI::EdgeTypeEnum::INDIRECT:
        printf("INDIRECT\n");
        break;
      case Dyninst::ParseAPI::EdgeTypeEnum::DIRECT:
        printf("DIRECT\n");
        break;
      default:
        printf("Some other\n");
        break;
    }
  }

  printf("\n\n");
}

int main(int argc, char* argv[]) {
  Parser parser = InitParser(argv[1]);

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  for (auto object : objects) {
    printf("Object : %s\n", object->pathName().c_str());
    if (IsSharedLibrary(object)) continue;

    Dyninst::ParseAPI::CodeObject* code_object =
        Dyninst::ParseAPI::convert(object);

    for (auto function : code_object->funcs()) {
      printf("Processing function : %s\n", function->name().c_str());
      for (auto const& edge : function->callEdges()) {
        PrintEdgeInfo(edge);
      }
    }
  }
}
