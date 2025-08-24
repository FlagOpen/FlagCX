#ifndef FLAGCX_LAUNCH_KERNEL_H_
typedef struct flagcxStream *flagcxStream_t;
typedef void (*flagcxLaunchFunc_t)(flagcxStream_t, void *);
#define FLAGCX_LAUNCH_KERNEL_H_
#pragma once
#include "adaptor.h"
#include "debug.h"
#include "flagcx.h"
#include "param.h"
#include "topo.h"
#include "utils.h"
#include <dlfcn.h>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

flagcxResult_t loadKernelSymbol(const char *path, const char *name,
                                flagcxLaunchFunc_t *fn);

#ifdef __cplusplus
}
#endif

struct flagcxFuncArgs {
  flagcxStream_t stream;
  bool void *dValue; // hPtr or dPtr
};

extern flagcxLaunchFunc_t deviceAsyncStore;
extern flagcxLaunchFunc_t deviceAsyncLoad;

void cpuAsyncStore(void *_args);
void cpuAsyncLoad(void *_args);

/*
Reference CUDA implementation for async kernel launch (for future adaptor
implementations)
1.1 deviceAsyncStore, set value to true
__global__ void deviceAsyncStore(bool* __restrict__ value) {
  *value = 1;
  // __threadfence_system();  // Ensure that the write is visible to the CPU.
}

1.2 host launcher for deviceAsyncStore
extern "C" flagcxResult_t launchDeviceAsyncStore(flagcxStream_t stream, void
*args) { bool* value = static_cast<bool*>(args); deviceAsyncStore<<<1, 1, 0,
*(cudaStream_t*)stream>>>(value); return flagcxSuccess;
}

2.1 deviceAsyncLoad, wait until value becomes true
__global__ void deviceAsyncLoad(const volatile bool* __restrict__ value) {
  while (!(*value)) { // no-op; }
}

2.2 host launcher for deviceAsyncLoad
extern "C" flagcxResult_t launchDeviceAsyncLoad(flagcxStream_t stream, void
*args) { bool* value = static_cast<bool*>(args); deviceAsyncLoad<<<1, 1, 0,
*(cudaStream_t*)stream>>>(value); return flagcxSuccess;
}
*/

#endif
