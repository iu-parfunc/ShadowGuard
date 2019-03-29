
#ifndef LITECFI_PARSE_H_
#define LITECFI_PARSE_H_

#include <string>

#include "BPatch.h"
#include "BPatch_object.h"

struct Parser {
  BPatch* parser;
  BPatch_addressSpace* app;
  BPatch_image* image;
};

Parser InitParser(std::string binary, bool libs, bool sanitize);

bool IsSharedLibrary(BPatch_object* object);
bool IsSystemCode(BPatch_object* object);

#endif  // LITECFI_PARSE_H_
