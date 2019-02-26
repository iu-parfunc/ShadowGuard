#ifndef LITECFI_CACHE_H_
#define LITECFI_CACHE_H_

#include <map>
#include <set>
#include <string>

#include "register_usage.h"

std::map<std::string, Code*>* GetRegisterAnalysisCache();

void FlushRegisterAnalysisCache(
    const std::map<std::string, Code*>* const cache);

#endif  // LITECFI_CACHE_H_
