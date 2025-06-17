#pragma once
#include <cuda_runtime.h>
#include "flagcx.h"

#ifdef __cplusplus
extern "C" {
#endif

flagcxResult_t launchAsyncKernel(flagcxStream_t stream, void *h_flag);

struct flagcxStream {
  cudaStream_t base;
};
#ifdef __cplusplus
}
#endif
