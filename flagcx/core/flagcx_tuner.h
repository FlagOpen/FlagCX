#ifndef FLAGCX_TUNER_H_
#define FLAGCX_TUNER_H_

#include "tuner.h"

bool operator<(const struct flagcxCommTag& lhs, const struct flagcxCommTag& rhs);
extern flagcxTuner_t internalTuner;
#endif // end of FLAGCX_TUNER_H_