
#include "parse.h"

Parser InitParser(std::string binary) {
  BPatch* parser = new BPatch;
  // Open binary and its linked shared libraries for parsing
  BPatch_addressSpace* app = parser->openBinary(binary.c_str(), true);
  BPatch_image* image = app->getImage();

  return {parser, app, image};
}

bool IsSharedLibrary(BPatch_object* object) {
  // TODO(chamibudhika) isSharedLib() should return false for program text
  // code object modules IMO. Check with Dyninst team about this.
  //
  //  std::vector<BPatch_module*> modules;
  //  object->modules(modules);
  //  DCHECK(modules.size() > 0);
  //
  //  return modules[0]->isSharedLib();

  // For now check if .so extension occurs somewhere in the object path
  std::string name = std::string(object->pathName());
  if (name.find(".so") != std::string::npos) {
    return true;
  }
  return false;
}
