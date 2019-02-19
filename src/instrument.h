
#ifndef LITECFI_INSTRUMENT_H_
#define LITECFI_INSTRUMENT_H_

#include <string>

#include "parse.h"
#include "register_usage.h"

void Instrument(std::string binary,
                LazyCallGraph<RegisterUsageInfo>* const call_graph,
                const Parser& parser);

#endif  // LITECFI_INSTRUMENT_H_
