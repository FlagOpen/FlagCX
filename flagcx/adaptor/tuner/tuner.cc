#include "tuner/tuner_util.h"

#ifdef USE_NVIDIA_ADAPTOR
std::vector<EnvVar> vars = ncclTunerVars;
#elif USE_METAX_ADAPTOR
std::vector<EnvVar> vars = mcclTunerVars;
#else
std::vector<EnvVar> vars = {};
#endif