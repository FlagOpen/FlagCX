#ifndef FLAGCX_TUNER_UTIL_H_
#define FLAGCX_TUNER_UTIL_H_

#include "tuner.h" // struct flagcxEnvConfig
#include <vector>

// This is a demonstration function that provide a way to load all config list for a specific GPU.
flagcxResult_t loadConfigList(std::vector<struct flagcxEnvConfig> &cfgList);

#endif // end include guard