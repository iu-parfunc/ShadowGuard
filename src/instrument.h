
#ifndef LITECFI_INSTRUMENT_H_
#define LITECFI_INSTRUMENT_H_

#include <string>

#include "register_usage.h"

void Instrument(std::string binary, const RegisterUsageInfo& info);

#endif  // LITECFI_INSTRUMENT_H_
